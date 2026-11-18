# Guia: montar os nós no Wokwi online

Cada zona é um projeto separado no wokwi.com. Os arquivos prontos estão em `firmware/wokwi-online/z1` e `firmware/wokwi-online/z2`; a única diferença entre as duas pastas é o `ZONA_ID` no `config.h`. Se o firmware em `firmware/sentinela` mudar, rode `python firmware/wokwi-online/montar.py` para atualizar as duas pastas antes de copiar de novo.

## 1. Criar o projeto da Zona 1

1. Entre em https://wokwi.com com a conta do grupo e clique em **New Project > ESP32** (modelo Arduino).
2. O projeto abre com duas abas: `sketch.ino` e `diagram.json`.
3. Apague o conteúdo de `sketch.ino` e cole o de `firmware/wokwi-online/z1/sketch.ino`.
4. Apague o conteúdo de `diagram.json` e cole o de `firmware/wokwi-online/z1/diagram.json`. O circuito aparece à direita com PIR, chave do portão, botão TAMPER, DHT22, LED RGB, buzzer e LCD.

## 2. Adicionar as outras abas

Na seta ao lado das abas, escolha **New file...** e crie cada arquivo abaixo com o mesmo nome, colando o conteúdo da pasta `z1`:

| Aba no Wokwi | Arquivo de origem |
|---|---|
| `config.h` | `z1/config.h` |
| `maquina_estados.h` | `z1/maquina_estados.h` |
| `maquina_estados.cpp` | `z1/maquina_estados.cpp` |
| `rede.h` | `z1/rede.h` |
| `rede.cpp` | `z1/rede.cpp` |
| `certificados.h` | `z1/certificados.h` |
| `libraries.txt` | `z1/libraries.txt` |
| `secrets.h` | conteúdo de `z1/secrets.example.h`, preenchido (passo 3) |

O `libraries.txt` também pode ser criado pela aba **Library Manager**, adicionando PubSubClient, DHT sensor library, Adafruit Unified Sensor e LiquidCrystal I2C.

## 3. Preencher o secrets.h

```cpp
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_SENHA    ""
#define MQTT_HOST     "SEU-CLUSTER.s1.eu.hivemq.cloud"
#define MQTT_PORTA    8883
#define MQTT_USUARIO  "SEU_USUARIO"
#define MQTT_SENHA    "SUA_SENHA"
```

O endereço do cluster está na página *Overview* do HiveMQ Cloud. Usuário e senha vêm de *Access Management > Credentials*.

**Atenção:** quem tem o link de um projeto do Wokwi vê todas as abas, inclusive o `secrets.h`. Por isso:

- crie no HiveMQ um usuário só para o Wokwi (por exemplo `wokwi-demo`) e use esse usuário nos dois projetos;
- se o link for entregue ao professor, deixe essa credencial ativa até a correção e depois apague o usuário no HiveMQ;
- nunca reaproveite a senha da conta do HiveMQ ou do Ubidots.

## 4. Salvar e pegar o link

1. Clique em **Save**, dê o nome "CNA Sentinela Z1" e salve.
2. O endereço muda para `https://wokwi.com/projects/<número>`. Esse é o link público.
3. Troque `LINK_WOKWI_Z1` por esse número no `README.md` e no relatório (seção 1). No relatório, depois de editar, atualize os campos (Ctrl+A, F9) e exporte o PDF de novo.

## 5. Zona 2

Repita os passos 1 a 4 com os arquivos de `firmware/wokwi-online/z2`, salve como "CNA Sentinela Z2" e troque `LINK_WOKWI_Z2`. Os dois projetos rodam ao mesmo tempo em abas diferentes do navegador.

## 6. Rodar e conferir

Clique no botão verde de play. No Serial Monitor deve aparecer algo assim:

```
[NO] zona 1, horario permitido 07:00-19:00
[WiFi] conectado, IP 10.10.0.2
[MQTT] conectando em SEU-CLUSTER.s1.eu.hivemq.cloud:8883 como cna-sentinela-z1-3F2A
[MQTT] conectado
```

O LCD mostra `Z1 NORMAL   A WM` na primeira linha. As letras à direita significam: `A` armado (ou `D` desarmado), `S` alarme silenciado por ACK, `W` Wi-Fi conectado e `M` broker conectado. Um `-` no lugar de `W` ou `M` indica que aquela conexão caiu. Mesmo assim LED, buzzer e LCD continuam respondendo aos sensores.

## 7. Como acionar cada sensor durante a simulação

| Sensor | Como acionar no Wokwi |
|---|---|
| PIR | clique no sensor e em **Simulate motion**; a saída fica ativa por 8 s |
| Portão | clique na chave deslizante; à esquerda = fechado, à direita = aberto |
| Tamper | mantenha o botão vermelho pressionado com o mouse |
| Temperatura | clique no DHT22 e mova o controle de temperatura (35 °C atenção, 45 °C crítico) |

Para simular "fora do horário" em qualquer hora, publique `FORA_HORARIO` no tópico `cna/zona/1/teste` pelo *Web Client* do HiveMQ Cloud. Ao terminar, publique `HORARIO_REAL`. Os comandos `ACK`, `REARMAR`, `ARMAR` e `DESARMAR` podem ser testados pelo mesmo Web Client no tópico `cna/zona/1/comando`, antes mesmo do painel do Ubidots estar pronto.

## 8. Problemas comuns

| Sintoma | Causa provável e solução |
|---|---|
| Erro `Crie include/secrets.h a partir de include/secrets.example.h` | a aba `secrets.h` não existe ou tem outro nome |
| Erro com `ledcSetup` ou `ledcAttachPin` | o `sketch.ino` é de uma versão antiga; copie de novo da pasta `wokwi-online` |
| `[MQTT] falhou, rc=-2` repetido | host errado ou falha no handshake TLS. Confira o `MQTT_HOST`; se continuar, descomente `#define MQTT_TLS_INSEGURO` no `secrets.h` só para a simulação |
| `rc=4` ou `rc=5` | usuário ou senha do HiveMQ incorretos, ou credencial sem permissão para `cna/#` |
| Fica em `[WiFi] tentando de novo` | o SSID precisa ser exatamente `Wokwi-GUEST`, com senha vazia |
| LCD apagado | confira se o `diagram.json` foi colado inteiro (o LCD usa I2C no endereço 0x27) |

## Alternativa: VS Code

Com a extensão Wokwi no VS Code não é preciso montar as abas. Compile com `pio run -e z1` dentro de `firmware/sentinela`, abra o `diagram.json` e rode *Wokwi: Start Simulator*. O `wokwi.toml` aponta para o build da Z1; para a Z2, troque `z1` por `z2` nos dois caminhos. A extensão só simula um projeto por vez, por isso o vídeo com as duas zonas fica mais fácil no wokwi.com.
