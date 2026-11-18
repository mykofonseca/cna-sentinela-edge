// Wi-Fi + MQTT/TLS em uma tarefa separada (núcleo 0).
// O loop principal só troca mensagens por filas e nunca espera a rede.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum TipoEntrada : uint8_t { ENTRADA_COMANDO, ENTRADA_TESTE };

struct MensagemEntrada {
  TipoEntrada tipo;
  char valor[24];
};

void redeIniciar();
bool redeWifiConectado();
bool redeMqttConectado();
// Incrementa a cada conexão bem-sucedida ao broker (para republicar o estado retido).
uint32_t redeConexoes();

// subtopico relativo à zona ("estado" vira cna/zona/1/estado) ou absoluto se começar com "cna/".
bool redePublicar(const char* subtopico, const char* valor, bool retain = false);
bool redeProximaEntrada(MensagemEntrada& m);
