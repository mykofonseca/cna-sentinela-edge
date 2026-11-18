// Parâmetros do nó sentinela: pinos, limites e tempos.
#pragma once

#ifndef ZONA_ID
#define ZONA_ID 2
#endif

#ifndef HORA_INICIO
#define HORA_INICIO 7
#endif
#ifndef HORA_FIM
#define HORA_FIM 19
#endif

// Pinos (ESP32 DevKit)
#define PINO_PIR        27
#define PINO_PORTAO     26   // chave deslizante, INPUT_PULLUP: LOW = portão fechado
#define PINO_TAMPER     25   // botão, INPUT_PULLUP: LOW = equipamento violado
#define PINO_DHT        33
#define PINO_LED_R      16
#define PINO_LED_G      17
#define PINO_LED_B      4
#define PINO_BUZZER     13
#define LCD_ENDERECO    0x27 // SDA 21, SCL 22

// Limites de temperatura (°C)
#define TEMP_ALTA        35.0f
#define TEMP_CRITICA     45.0f
#define TEMP_HISTERESE   2.0f

// Tempos (ms)
#define DEBOUNCE_MS             50
#define PERSISTENCIA_MS         30000UL  // ATENÇÃO contínua por mais que isso vira ALERTA
#define RETORNO_ATENCAO_MS      5000UL   // condição ausente por esse tempo devolve ATENÇÃO a NORMAL
#define INTERVALO_DHT_MS        2000UL   // o DHT22 não aceita leituras mais rápidas
#define INTERVALO_TEMP_MS       10000UL
#define INTERVALO_ESTADO_MS     30000UL
#define INTERVALO_LCD_MS        250UL

// NTP (UTC-3, sem horário de verão)
#define FUSO_SEGUNDOS   (-3 * 3600)
#define NTP_SERVIDOR_1  "pool.ntp.org"
#define NTP_SERVIDOR_2  "a.st1.ntp.br"
