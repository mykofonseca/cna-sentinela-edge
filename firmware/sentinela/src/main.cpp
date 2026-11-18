// Nó Sentinela de Borda - Centro Nuclear Atlântico
// Loop local (núcleo 1): sensores, máquina de estados, LED, buzzer e LCD.
// A rede roda em outra tarefa, então a resposta local não depende do broker.
#include <Arduino.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <time.h>

#include "config.h"
#include "maquina_estados.h"
#include "rede.h"

#define CANAL_R 0
#define CANAL_G 1
#define CANAL_B 2
#define CANAL_BUZZER 4  // timer separado dos canais do LED

LiquidCrystal_I2C lcd(LCD_ENDERECO, 16, 2);
DHT dht(PINO_DHT, DHT22);
MaquinaEstados maquina;

struct Debounce {
  uint8_t pino;
  bool estavel;
  bool leitura;
  uint32_t mudouEm;
};

Debounce dbPir{PINO_PIR, LOW, LOW, 0};
Debounce dbPortao{PINO_PORTAO, LOW, LOW, 0};
Debounce dbTamper{PINO_TAMPER, HIGH, HIGH, 0};

float temperatura = NAN;
int8_t testeForaHorario = -1;  // -1 usa o relógio, 0 força dentro, 1 força fora

uint32_t ultimaLeituraDht = 0;
uint32_t ultimaTemperaturaPub = 0;
uint32_t ultimoEstadoPub = 0;
uint32_t ultimoLcd = 0;
uint32_t conexoesVistas = 0;
uint16_t freqAtual = 0;
bool pirPublicado = false;
bool horarioPublicado = false;
bool foraPublicado = false;
Estado estadoPublicado = NORMAL;

bool lerDebounce(Debounce& d, uint32_t agora) {
  bool l = digitalRead(d.pino);
  if (l != d.leitura) {
    d.leitura = l;
    d.mudouEm = agora;
  } else if (l != d.estavel && agora - d.mudouEm >= DEBOUNCE_MS) {
    d.estavel = l;
  }
  return d.estavel;
}

bool foraDoHorario() {
  if (testeForaHorario >= 0) return testeForaHorario == 1;
  struct tm t;
  // Sem NTP ainda não há hora confiável: trata como fora do horário (mais restritivo).
  if (!getLocalTime(&t, 0)) return true;
  return t.tm_hour < HORA_INICIO || t.tm_hour >= HORA_FIM;
}

void corLed(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CANAL_R, r);
  ledcWrite(CANAL_G, g);
  ledcWrite(CANAL_B, b);
}

void atualizarLed(Estado e, bool silenciado, uint32_t agora) {
  switch (e) {
    case NORMAL:  corLed(0, 255, 0); break;
    case ATENCAO: corLed(255, 200, 0); break;
    case ALERTA:  corLed(255, 70, 0); break;
    case CRITICO:
      // Pisca até o ACK; depois fica vermelho fixo.
      if (silenciado || (agora % 500) < 250) corLed(255, 0, 0);
      else corLed(0, 0, 0);
      break;
  }
}

// Padrões: ATENÇÃO bipe curto a cada 3 s, ALERTA intermitente, CRÍTICO sirene.
uint16_t frequenciaBuzzer(Estado e, bool silenciado, uint32_t agora) {
  if (silenciado) return 0;
  switch (e) {
    case ATENCAO: return (agora % 3000) < 120 ? 1000 : 0;
    case ALERTA:  return (agora % 600) < 300 ? 1500 : 0;
    case CRITICO: return (agora % 500) < 250 ? 2000 : 2600;
    default:      return 0;
  }
}

void atualizarBuzzer(uint32_t agora) {
  uint16_t f = frequenciaBuzzer(maquina.estado(), maquina.silenciado(), agora);
  if (f != freqAtual) {
    ledcWriteTone(CANAL_BUZZER, f);
    freqAtual = f;
  }
}

// Linha 1: "Z1 ALERTA   ASWM" (A/D armado, S silenciado, W Wi-Fi, M MQTT)
// Linha 2: causa + temperatura, ex. "PIR+PORTAO 31.5C"
void atualizarLcd(uint32_t agora) {
  static char l1Antiga[17] = "";
  static char l2Antiga[17] = "";
  if (agora - ultimoLcd < INTERVALO_LCD_MS) return;
  ultimoLcd = agora;

  char l1[17], l2[17], temp[7];
  snprintf(l1, sizeof(l1), "Z%d %-8s %c%c%c%c", ZONA_ID, nomeEstado(maquina.estado()),
           maquina.armado() ? 'A' : 'D', maquina.silenciado() ? 'S' : ' ',
           redeWifiConectado() ? 'W' : '-', redeMqttConectado() ? 'M' : '-');
  if (isnan(temperatura)) strcpy(temp, "--.-C");
  else snprintf(temp, sizeof(temp), "%4.1fC", temperatura);
  snprintf(l2, sizeof(l2), "%-10s%6s", rotuloEvento(maquina.causa()), temp);

  if (strcmp(l1, l1Antiga) != 0) {
    lcd.setCursor(0, 0);
    lcd.print(l1);
    strcpy(l1Antiga, l1);
  }
  if (strcmp(l2, l2Antiga) != 0) {
    lcd.setCursor(0, 1);
    lcd.print(l2);
    strcpy(l2Antiga, l2);
  }
}

void publicarEstado(uint32_t agora) {
  redePublicar("estado", nomeEstado(maquina.estado()), true);
  estadoPublicado = maquina.estado();
  ultimoEstadoPub = agora;
}

void tratarEntrada(const MensagemEntrada& m, const Entradas& e, uint32_t agora) {
  if (m.tipo == ENTRADA_TESTE) {
    // Só para a demonstração: força ou libera a regra de horário.
    if (strcmp(m.valor, "FORA_HORARIO") == 0) testeForaHorario = 1;
    else if (strcmp(m.valor, "DENTRO_HORARIO") == 0) testeForaHorario = 0;
    else if (strcmp(m.valor, "HORARIO_REAL") == 0) testeForaHorario = -1;
    Serial.printf("[TESTE] %s\n", m.valor);
    return;
  }
  Comando c = interpretarComando(m.valor);
  Serial.printf("[CMD] %s%s\n", m.valor, c == CMD_INVALIDO ? " (ignorado)" : "");
  if (c == CMD_INVALIDO) return;
  maquina.comando(c, e, agora);
  if (c == CMD_ARMAR || c == CMD_DESARMAR) {
    redePublicar("modo", maquina.armado() ? "ARMADO" : "DESARMADO", true);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_PIR, INPUT);
  pinMode(PINO_PORTAO, INPUT_PULLUP);
  pinMode(PINO_TAMPER, INPUT_PULLUP);

  ledcSetup(CANAL_R, 5000, 8);
  ledcSetup(CANAL_G, 5000, 8);
  ledcSetup(CANAL_B, 5000, 8);
  ledcAttachPin(PINO_LED_R, CANAL_R);
  ledcAttachPin(PINO_LED_G, CANAL_G);
  ledcAttachPin(PINO_LED_B, CANAL_B);
  ledcSetup(CANAL_BUZZER, 2000, 8);
  ledcAttachPin(PINO_BUZZER, CANAL_BUZZER);
  ledcWriteTone(CANAL_BUZZER, 0);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.printf("CNA Sentinela Z%d", ZONA_ID);
  lcd.setCursor(0, 1);
  lcd.print("iniciando...");

  dht.begin();
  redeIniciar();
  Serial.printf("[NO] zona %d, horario permitido %02d:00-%02d:00\n", ZONA_ID, HORA_INICIO, HORA_FIM);
}

void loop() {
  uint32_t agora = millis();

  Entradas e;
  e.pir = lerDebounce(dbPir, agora) == HIGH;
  e.portaoAberto = lerDebounce(dbPortao, agora) == HIGH;
  e.tamperAtivo = lerDebounce(dbTamper, agora) == LOW;
  if (agora - ultimaLeituraDht >= INTERVALO_DHT_MS) {
    ultimaLeituraDht = agora;
    float t = dht.readTemperature();
    if (!isnan(t)) temperatura = t;
  }
  e.temperatura = temperatura;
  e.foraHorario = foraDoHorario();

  MensagemEntrada m;
  while (redeProximaEntrada(m)) tratarEntrada(m, e, agora);

  maquina.atualizar(e, agora);

  Evento ev;
  while (maquina.proximoEvento(ev)) {
    Serial.printf("[EVENTO] %s -> estado %s\n", nomeEvento(ev), nomeEstado(maquina.estado()));
    redePublicar("evento", nomeEvento(ev));
  }
  if (maquina.consumirMissao()) {
    char zona[4];
    snprintf(zona, sizeof(zona), "Z%d", ZONA_ID);
    redePublicar("cna/drone/missao", zona);
    Serial.printf("[MISSAO] drone solicitado para %s\n", zona);
  }

  bool conectado = redeMqttConectado();
  bool reconectou = redeConexoes() != conexoesVistas;
  if (reconectou) {
    // Depois de (re)conectar, republica o que é retido para o painel ficar coerente.
    conexoesVistas = redeConexoes();
    redePublicar("modo", maquina.armado() ? "ARMADO" : "DESARMADO", true);
    horarioPublicado = false;
  }
  if (maquina.estado() != estadoPublicado || reconectou ||
      (conectado && agora - ultimoEstadoPub >= INTERVALO_ESTADO_MS)) {
    publicarEstado(agora);
  }
  if (conectado && e.pir != pirPublicado) {
    redePublicar("pir", e.pir ? "1" : "0");
    pirPublicado = e.pir;
  }
  if (conectado && (!horarioPublicado || e.foraHorario != foraPublicado)) {
    redePublicar("horario", e.foraHorario ? "FORA" : "DENTRO", true);
    foraPublicado = e.foraHorario;
    horarioPublicado = true;
  }
  if (conectado && !isnan(temperatura) && agora - ultimaTemperaturaPub >= INTERVALO_TEMP_MS) {
    char t[8];
    snprintf(t, sizeof(t), "%.1f", temperatura);
    redePublicar("temperatura", t);
    ultimaTemperaturaPub = agora;
  }

  atualizarLed(maquina.estado(), maquina.silenciado(), agora);
  atualizarBuzzer(agora);
  atualizarLcd(agora);
}
