"""Drone simulado do Centro Nuclear Atlântico.

Recebe missões em cna/drone/missao (payload "Z1", "Z2"), executa o voo em
etapas e publica cada etapa em cna/drone/status. Missões que chegam durante um
voo entram em fila (sem duplicar a mesma zona).
"""

import os
import queue
import re
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

HOST = os.getenv("HIVEMQ_HOST", "localhost")
PORTA = int(os.getenv("HIVEMQ_PORTA", "8883"))
USUARIO = os.getenv("HIVEMQ_USUARIO")
SENHA = os.getenv("HIVEMQ_SENHA")
USAR_TLS = os.getenv("MQTT_TLS", "1") != "0"
TEMPO_ETAPA = float(os.getenv("DRONE_TEMPO_ETAPA", "4"))

T_MISSAO = "cna/drone/missao"
T_STATUS = "cna/drone/status"
T_ZONA = "cna/drone/zona"
T_CONEXAO = "cna/drone/conexao"

# Eventos que disparam missão; guardamos o último de cada zona como "causa".
EVENTOS_GATILHO = {"PIR_PORTAO", "PERSISTENCIA", "TAMPER", "TEMP_CRITICA"}
# Causas que o drone sempre confirma (violação física ou foco de calor visível).
CAUSAS_CONFIRMADAS = {"TAMPER", "TEMP_CRITICA"}


def log(zona, mensagem):
    print(f"{datetime.now():%Y-%m-%d %H:%M:%S} [{zona or '--'}] {mensagem}", flush=True)


class Drone:
    def __init__(self):
        self.fila = queue.Queue()
        self.trava = threading.Lock()
        self.pendentes = set()
        self.em_voo = None
        self.zonas = {}

        self.cliente = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2, client_id=f"cna-drone-{os.getpid()}"
        )
        if USUARIO:
            self.cliente.username_pw_set(USUARIO, SENHA)
        if USAR_TLS:
            self.cliente.tls_set(ca_certs=certifi.where())
        self.cliente.will_set(T_CONEXAO, "OFFLINE", qos=1, retain=True)
        self.cliente.reconnect_delay_set(min_delay=1, max_delay=30)
        self.cliente.on_connect = self.ao_conectar
        self.cliente.on_disconnect = self.ao_desconectar
        self.cliente.on_message = self.ao_receber

    def zona(self, z):
        return self.zonas.setdefault(z, {"estado": None, "causa": None, "pir": "0", "pir_visto": False})

    def publicar(self, topico, valor, retain=True):
        self.cliente.publish(topico, valor, qos=1, retain=retain)

    def ao_conectar(self, cliente, userdata, flags, rc, props=None):
        if rc.is_failure:
            log(None, f"falha ao conectar no broker: {rc}")
            return
        log(None, f"conectado em {HOST}:{PORTA}")
        cliente.subscribe([
            (T_MISSAO, 1),
            ("cna/zona/+/estado", 1),
            ("cna/zona/+/evento", 1),
            ("cna/zona/+/pir", 0),
        ])
        self.publicar(T_CONEXAO, "ONLINE")
        if self.em_voo is None:
            self.publicar(T_STATUS, "DISPONIVEL")

    def ao_desconectar(self, cliente, userdata, flags, rc, props=None):
        log(None, f"desconectado do broker ({rc}), tentando reconectar")

    def ao_receber(self, cliente, userdata, msg):
        valor = msg.payload.decode(errors="replace").strip().upper()
        if msg.topic == T_MISSAO:
            self.receber_missao(valor)
            return

        partes = msg.topic.split("/")  # cna/zona/{id}/{campo}
        if len(partes) != 4:
            return
        z = f"Z{partes[2]}"
        campo = partes[3]
        with self.trava:
            info = self.zona(z)
            if campo == "estado":
                info["estado"] = valor
            elif campo == "evento":
                if valor in EVENTOS_GATILHO:
                    info["causa"] = valor
                elif valor in ("REARME", "NORMALIZADO"):
                    info["causa"] = None
            elif campo == "pir":
                info["pir"] = valor
                if valor == "1" and (self.em_voo == z or z in self.pendentes):
                    info["pir_visto"] = True

    def receber_missao(self, zona):
        if not re.fullmatch(r"Z\d+", zona):
            log(None, f"missão inválida ignorada: {zona!r}")
            return
        with self.trava:
            if zona == self.em_voo or zona in self.pendentes:
                log(zona, "missão repetida para zona já atendida ou na fila, ignorada")
                return
            # A causa é registrada na chegada: o operador pode rearmar o nó durante o voo.
            info = self.zona(zona)
            causa = info["causa"]
            info["pir_visto"] = info["pir"] == "1"
            self.pendentes.add(zona)
            self.fila.put((zona, causa))
            espera = self.fila.qsize()
            em_voo = self.em_voo
        if em_voo is None:
            log(zona, f"missão recebida (causa: {causa or 'desconhecida'})")
        else:
            log(zona, f"missão em fila; drone em voo para {em_voo} ({espera} na fila)")

    def resultado(self, zona, causa):
        info = self.zona(zona)
        if causa in CAUSAS_CONFIRMADAS:
            return "ANOMALIA_CONFIRMADA", f"causa {causa}"
        # ALERTA: confirma se houve movimento (PIR = 1) entre a chegada da missão e a inspeção.
        if info["pir_visto"] or info["pir"] == "1":
            return "ANOMALIA_CONFIRMADA", "movimento detectado na zona"
        return "NADA_ENCONTRADO", "sem movimento durante a inspeção"

    def etapa(self, zona, nome, espera=True):
        self.publicar(T_STATUS, nome)
        log(zona, nome)
        if espera:
            time.sleep(TEMPO_ETAPA)

    def voar(self):
        while True:
            zona, causa = self.fila.get()
            with self.trava:
                self.pendentes.discard(zona)
                self.em_voo = zona
            self.publicar(T_ZONA, zona)
            self.etapa(zona, "DECOLANDO")
            self.etapa(zona, "EM_ROTA")
            self.etapa(zona, "NA_ZONA")
            with self.trava:
                res, motivo = self.resultado(zona, causa)
            self.publicar(T_STATUS, res)
            log(zona, f"{res} ({motivo})")
            time.sleep(TEMPO_ETAPA)
            self.etapa(zona, "RETORNANDO")
            with self.trava:
                self.em_voo = None
            self.etapa(zona, "DISPONIVEL", espera=False)

    def rodar(self):
        threading.Thread(target=self.voar, daemon=True).start()
        log(None, f"drone iniciando, {TEMPO_ETAPA:.0f} s por etapa, TLS {'ligado' if USAR_TLS else 'desligado'}")
        self.cliente.connect_async(HOST, PORTA, keepalive=30)
        self.cliente.loop_start()

        parar = threading.Event()
        signal.signal(signal.SIGINT, lambda *_: parar.set())
        signal.signal(signal.SIGTERM, lambda *_: parar.set())
        while not parar.is_set():
            parar.wait(0.5)

        self.publicar(T_CONEXAO, "OFFLINE")
        self.publicar(T_STATUS, "OFFLINE")
        time.sleep(0.5)
        self.cliente.disconnect()
        self.cliente.loop_stop()
        log(None, "drone encerrado")


if __name__ == "__main__":
    if USAR_TLS and not USUARIO:
        sys.exit("Configure o .env a partir do .env.example (HIVEMQ_HOST, HIVEMQ_USUARIO, HIVEMQ_SENHA).")
    Drone().rodar()
