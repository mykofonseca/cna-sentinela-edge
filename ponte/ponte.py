"""Ponte HiveMQ <-> Ubidots do Centro Nuclear Atlântico.

Sentido 1: assina cna/# no HiveMQ, converte texto em número (o Ubidots só
guarda valores numéricos) e publica no broker MQTT do Ubidots, com o texto
original no context.
Sentido 2: assina as variáveis de comando dos dispositivos no Ubidots
(cmd-ack, cmd-rearmar, cmd-armar, cmd-desarmar). Quando um botão do painel
grava 1, publica o comando em cna/zona/{id}/comando e devolve a variável a 0.
"""

import json
import os
import signal
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

import certifi
import paho.mqtt.client as mqtt
from dotenv import load_dotenv

AQUI = Path(__file__).resolve().parent
load_dotenv(AQUI / ".env")
load_dotenv(AQUI.parent / ".env")

HIVE_HOST = os.getenv("HIVEMQ_HOST", "localhost")
HIVE_PORTA = int(os.getenv("HIVEMQ_PORTA", "8883"))
HIVE_USUARIO = os.getenv("HIVEMQ_USUARIO")
HIVE_SENHA = os.getenv("HIVEMQ_SENHA")
USAR_TLS = os.getenv("MQTT_TLS", "1") != "0"

UBI_TOKEN = os.getenv("UBIDOTS_TOKEN", "").strip()
UBI_HOST = os.getenv("UBIDOTS_HOST", "industrial.api.ubidots.com")
INTERVALO_TEMP = float(os.getenv("UBIDOTS_INTERVALO_TEMP", "10"))
ZONAS = (1, 2)

# Tabelas de conversão texto -> número usadas nos widgets do painel.
ESTADOS = {"NORMAL": 0, "ATENCAO": 1, "ALERTA": 2, "CRITICO": 3}
EVENTOS = {
    "NENHUM": 0, "PIR": 1, "PORTAO": 2, "PIR_PORTAO": 3, "PERSISTENCIA": 4,
    "TEMP_ALTA": 5, "TEMP_CRITICA": 6, "TAMPER": 7, "ACK": 8, "REARME": 9,
    "REARME_NEGADO": 10, "ARMADO": 11, "DESARMADO": 12, "NORMALIZADO": 13,
}
ETAPAS_DRONE = {
    "OFFLINE": -1, "DISPONIVEL": 0, "DECOLANDO": 1, "EM_ROTA": 2, "NA_ZONA": 3,
    "NADA_ENCONTRADO": 4, "ANOMALIA_CONFIRMADA": 5, "RETORNANDO": 6,
}
RESULTADOS = {"NADA_ENCONTRADO": 0, "ANOMALIA_CONFIRMADA": 1}
BINARIOS = {
    "conexao": {"OFFLINE": 0, "ONLINE": 1},
    "modo": {"DESARMADO": 0, "ARMADO": 1},
    "horario": {"DENTRO": 0, "FORA": 1},
    "pir": {"0": 0, "1": 1},
}
COMANDOS = {
    "cmd-ack": "ACK",
    "cmd-rearmar": "REARMAR",
    "cmd-armar": "ARMAR",
    "cmd-desarmar": "DESARMAR",
}
# Valores 1 que chegam logo após assinar são resto de sessão anterior: só zera.
JANELA_INICIAL_S = 3.0


def log(origem, mensagem):
    print(f"{datetime.now():%Y-%m-%d %H:%M:%S} [{origem}] {mensagem}", flush=True)


def numero_zona(texto):
    texto = texto.strip().upper()
    if texto.startswith("Z") and texto[1:].isdigit():
        return int(texto[1:])
    return None


class Ponte:
    def __init__(self):
        self.ultima_temp = {}
        self.assinado_em = 0.0
        self.ultimo_cmd = {}

        self.hive = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"cna-ponte-{os.getpid()}")
        if HIVE_USUARIO:
            self.hive.username_pw_set(HIVE_USUARIO, HIVE_SENHA)
        if USAR_TLS:
            self.hive.tls_set(ca_certs=certifi.where())
        self.hive.reconnect_delay_set(1, 30)
        self.hive.on_connect = self.hive_conectou
        self.hive.on_disconnect = lambda *a: log("hivemq", "desconectado, tentando reconectar")
        self.hive.on_message = self.hive_mensagem

        self.ubi = None
        if UBI_TOKEN:
            self.ubi = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"cna-ponte-ubi-{os.getpid()}")
            self.ubi.username_pw_set(UBI_TOKEN, "")  # Ubidots: token no usuário, senha vazia
            self.ubi.tls_set(ca_certs=certifi.where())
            self.ubi.reconnect_delay_set(1, 30)
            self.ubi.on_connect = self.ubi_conectou
            self.ubi.on_disconnect = lambda *a: log("ubidots", "desconectado, tentando reconectar")
            self.ubi.on_message = self.ubi_mensagem

    # ---------- HiveMQ -> Ubidots ----------

    def hive_conectou(self, cliente, userdata, flags, rc, props=None):
        if rc.is_failure:
            log("hivemq", f"falha ao conectar: {rc}")
            return
        log("hivemq", f"conectado em {HIVE_HOST}:{HIVE_PORTA}, assinando cna/#")
        cliente.subscribe("cna/#", qos=1)

    def enviar_ubidots(self, dispositivo, variavel, valor, texto=None):
        dado = {"value": valor}
        if texto is not None:
            dado["context"] = {"texto": texto}
        corpo = json.dumps({variavel: dado})
        if self.ubi is None:
            log("ubidots", f"(sem token) {dispositivo} <- {corpo}")
            return
        self.ubi.publish(f"/v1.6/devices/{dispositivo}", corpo, qos=1)
        log("ubidots", f"{dispositivo}.{variavel} = {valor}" + (f" ({texto})" if texto else ""))

    def hive_mensagem(self, cliente, userdata, msg):
        texto = msg.payload.decode(errors="replace").strip()
        partes = msg.topic.split("/")
        try:
            if partes[1] == "zona" and len(partes) == 4:
                self.traduzir_zona(int(partes[2]), partes[3], texto)
            elif partes[1] == "drone" and len(partes) == 3:
                self.traduzir_drone(partes[2], texto)
        except (ValueError, IndexError, KeyError) as erro:
            log("ponte", f"mensagem ignorada em {msg.topic}: {texto!r} ({erro})")

    def traduzir_zona(self, zona, campo, texto):
        dispositivo = f"cna-z{zona}"
        maiusculo = texto.upper()
        if campo == "temperatura":
            valor = float(texto)
            agora = time.monotonic()
            anterior = self.ultima_temp.get(zona)
            if anterior and agora - anterior < INTERVALO_TEMP:
                return
            self.ultima_temp[zona] = agora
            self.enviar_ubidots(dispositivo, "temperatura", valor)
        elif campo == "estado":
            self.enviar_ubidots(dispositivo, "estado", ESTADOS[maiusculo], maiusculo)
        elif campo == "evento":
            self.enviar_ubidots(dispositivo, "evento", EVENTOS.get(maiusculo, 99), maiusculo)
        elif campo in BINARIOS:
            self.enviar_ubidots(dispositivo, campo, BINARIOS[campo][maiusculo], maiusculo)
        # comando e teste são do sentido contrário: não voltam para o painel.

    def traduzir_drone(self, campo, texto):
        maiusculo = texto.upper()
        if campo == "status":
            self.enviar_ubidots("cna-drone", "etapa", ETAPAS_DRONE[maiusculo], maiusculo)
            if maiusculo in RESULTADOS:
                self.enviar_ubidots("cna-drone", "resultado", RESULTADOS[maiusculo], maiusculo)
        elif campo == "missao":
            zona = numero_zona(texto)
            if zona is not None:
                self.enviar_ubidots("cna-drone", "missao", zona, maiusculo)
        elif campo == "zona":
            zona = numero_zona(texto)
            if zona is not None:
                self.enviar_ubidots("cna-drone", "zona", zona, maiusculo)
        elif campo == "conexao":
            self.enviar_ubidots("cna-drone", "conexao", BINARIOS["conexao"][maiusculo], maiusculo)

    # ---------- Ubidots -> HiveMQ ----------

    def ubi_conectou(self, cliente, userdata, flags, rc, props=None):
        if rc.is_failure:
            log("ubidots", f"falha ao conectar (confira o token): {rc}")
            return
        topicos = [
            (f"/v1.6/devices/cna-z{z}/{var}/lv", 1) for z in ZONAS for var in COMANDOS
        ]
        cliente.subscribe(topicos)
        self.assinado_em = time.monotonic()
        log("ubidots", f"conectado em {UBI_HOST}, aguardando comandos do painel")

    def ubi_mensagem(self, cliente, userdata, msg):
        # /v1.6/devices/cna-z1/cmd-ack/lv -> "1.0"
        partes = msg.topic.split("/")
        dispositivo, variavel = partes[3], partes[4]
        try:
            valor = float(msg.payload.decode())
        except ValueError:
            return
        if valor < 0.5:
            return

        zona = int(dispositivo.replace("cna-z", ""))
        comando = COMANDOS[variavel]
        # Devolve o botão a 0 para o próximo clique ser detectado.
        cliente.publish(f"/v1.6/devices/{dispositivo}", json.dumps({variavel: 0}), qos=1)

        if time.monotonic() - self.assinado_em < JANELA_INICIAL_S:
            log("ubidots", f"{dispositivo}.{variavel} estava em 1 ao conectar, apenas zerado")
            return
        chave = (zona, comando)
        agora = time.monotonic()
        if agora - self.ultimo_cmd.get(chave, 0) < 2.0:  # clique duplo no painel
            return
        self.ultimo_cmd[chave] = agora

        topico = f"cna/zona/{zona}/comando"
        self.hive.publish(topico, comando, qos=1, retain=False)
        log("ponte", f"comando {comando} enviado para {topico}")

    # ---------- execução ----------

    def rodar(self):
        log("ponte", f"iniciando; Ubidots {'ativo' if self.ubi else 'desligado (sem token)'}")
        self.hive.connect_async(HIVE_HOST, HIVE_PORTA, keepalive=30)
        self.hive.loop_start()
        if self.ubi:
            self.ubi.connect_async(UBI_HOST, 8883, keepalive=30)
            self.ubi.loop_start()

        parar = threading.Event()
        signal.signal(signal.SIGINT, lambda *_: parar.set())
        signal.signal(signal.SIGTERM, lambda *_: parar.set())
        while not parar.is_set():
            parar.wait(0.5)

        for cliente in (self.hive, self.ubi):
            if cliente:
                cliente.disconnect()
                cliente.loop_stop()
        log("ponte", "encerrada")


if __name__ == "__main__":
    if USAR_TLS and not HIVE_USUARIO:
        sys.exit("Configure o .env a partir do .env.example (HIVEMQ_* e UBIDOTS_TOKEN).")
    Ponte().rodar()
