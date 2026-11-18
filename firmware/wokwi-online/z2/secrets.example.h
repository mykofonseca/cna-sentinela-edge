// Copie este arquivo para include/secrets.h e preencha com os dados do cluster.
// O secrets.h está no .gitignore e não deve ser versionado.
#pragma once

#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_SENHA    ""

#define MQTT_HOST     "SEU-CLUSTER.s1.eu.hivemq.cloud"
#define MQTT_PORTA    8883
#define MQTT_USUARIO  "SEU_USUARIO"
#define MQTT_SENHA    "SUA_SENHA"

// Descomente apenas se o handshake com certificado falhar no simulador.
// Com isso o nó aceita qualquer certificado (sem validação do servidor).
// #define MQTT_TLS_INSEGURO
