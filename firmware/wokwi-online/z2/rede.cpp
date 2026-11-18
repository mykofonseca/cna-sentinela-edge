#include "rede.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>

#include "certificados.h"
#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Crie include/secrets.h a partir de include/secrets.example.h"
#endif

#define STR_(x) #x
#define STR(x) STR_(x)
#define PREFIXO "cna/zona/" STR(ZONA_ID) "/"

struct MensagemSaida {
  char topico[40];
  char valor[24];
  bool retain;
};

static WiFiClientSecure tls;
static PubSubClient mqtt(tls);
static QueueHandle_t filaSaida;
static QueueHandle_t filaEntrada;

static volatile bool wifiOk = false;
static volatile bool mqttOk = false;
static volatile uint32_t conexoes = 0;

static const uint32_t ESPERA_WIFI_MS = 15000;
static const uint32_t ESPERA_MQTT_MS = 5000;

bool redeWifiConectado() { return wifiOk; }
bool redeMqttConectado() { return mqttOk; }
uint32_t redeConexoes() { return conexoes; }

bool redePublicar(const char* subtopico, const char* valor, bool retain) {
  MensagemSaida m;
  if (strncmp(subtopico, "cna/", 4) == 0) {
    strlcpy(m.topico, subtopico, sizeof(m.topico));
  } else {
    snprintf(m.topico, sizeof(m.topico), PREFIXO "%s", subtopico);
  }
  strlcpy(m.valor, valor, sizeof(m.valor));
  m.retain = retain;
  // Timeout zero: se a fila encher durante uma queda longa, a mensagem é descartada.
  return xQueueSend(filaSaida, &m, 0) == pdTRUE;
}

bool redeProximaEntrada(MensagemEntrada& m) {
  return xQueueReceive(filaEntrada, &m, 0) == pdTRUE;
}

static void aoReceber(char* topico, byte* payload, unsigned int tam) {
  MensagemEntrada m;
  const char* sub = topico + strlen(PREFIXO);
  if (strcmp(sub, "comando") == 0) m.tipo = ENTRADA_COMANDO;
  else if (strcmp(sub, "teste") == 0) m.tipo = ENTRADA_TESTE;
  else return;

  // Normaliza: maiúsculas e sem espaços nas pontas.
  unsigned int j = 0;
  for (unsigned int i = 0; i < tam && j < sizeof(m.valor) - 1; i++) {
    char c = (char)payload[i];
    if (isspace((unsigned char)c)) continue;
    m.valor[j++] = (char)toupper((unsigned char)c);
  }
  m.valor[j] = '\0';
  xQueueSend(filaEntrada, &m, 0);
}

static void conectarMqtt() {
  char id[32];
  snprintf(id, sizeof(id), "cna-sentinela-z%d-%04X", ZONA_ID, (unsigned)(esp_random() & 0xFFFF));
  Serial.printf("[MQTT] conectando em %s:%d como %s\n", MQTT_HOST, MQTT_PORTA, id);

  bool ok = mqtt.connect(id, MQTT_USUARIO, MQTT_SENHA, PREFIXO "conexao", 1, true, "OFFLINE");
  if (!ok) {
    Serial.printf("[MQTT] falhou, rc=%d\n", mqtt.state());
    return;
  }
  mqtt.publish(PREFIXO "conexao", "ONLINE", true);
  mqtt.subscribe(PREFIXO "comando", 1);
  mqtt.subscribe(PREFIXO "teste", 1);
  mqttOk = true;
  conexoes = conexoes + 1;
  Serial.println("[MQTT] conectado");
}

static void tarefaRede(void*) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);  // canal 6 acelera a conexão no Wokwi
  uint32_t inicioWifi = millis();
  uint32_t ultimaTentativaMqtt = 0;
  bool primeiraTentativa = true;

  configTime(FUSO_SEGUNDOS, 0, NTP_SERVIDOR_1, NTP_SERVIDOR_2);

#ifdef MQTT_TLS_INSEGURO
  tls.setInsecure();
#else
  tls.setCACert(ISRG_ROOT_X1);
#endif
  tls.setHandshakeTimeout(10);
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setCallback(aoReceber);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(5);

  for (;;) {
    uint32_t agora = millis();

    if (WiFi.status() != WL_CONNECTED) {
      if (wifiOk) Serial.println("[WiFi] conexão perdida");
      wifiOk = false;
      mqttOk = false;
      if (agora - inicioWifi >= ESPERA_WIFI_MS) {
        Serial.println("[WiFi] tentando de novo");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);
        inicioWifi = agora;
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    if (!wifiOk) {
      wifiOk = true;
      Serial.printf("[WiFi] conectado, IP %s\n", WiFi.localIP().toString().c_str());
    }

    if (!mqtt.connected()) {
      mqttOk = false;
      if (primeiraTentativa || agora - ultimaTentativaMqtt >= ESPERA_MQTT_MS) {
        primeiraTentativa = false;
        ultimaTentativaMqtt = agora;
        conectarMqtt();
      }
    } else {
      mqtt.loop();
      MensagemSaida m;
      for (int i = 0; i < 6 && xQueueReceive(filaSaida, &m, 0) == pdTRUE; i++) {
        mqtt.publish(m.topico, m.valor, m.retain);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void redeIniciar() {
  filaSaida = xQueueCreate(16, sizeof(MensagemSaida));
  filaEntrada = xQueueCreate(8, sizeof(MensagemEntrada));
  xTaskCreatePinnedToCore(tarefaRede, "rede", 10240, nullptr, 1, nullptr, 0);
}
