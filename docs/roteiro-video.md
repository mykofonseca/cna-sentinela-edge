# Roteiro do vídeo (até 3 min)

Antes de gravar: ponte e drone rodando em dois terminais, as duas simulações do Wokwi abertas (Z1 e Z2) com DHT22 em 26 °C, portão fechado e o painel do Ubidots aberto. O cliente web do HiveMQ fica logado em outra aba para mandar o comando de teste de horário.

Tela dividida durante quase todo o vídeo: Wokwi à esquerda, painel à direita, terminal do drone embaixo.

| Tempo | O que aparece na tela | Fala |
|---|---|---|
| 0:00–0:15 | Diagrama de arquitetura | "Este é o Nó Sentinela do Centro Nuclear Atlântico. Dois ESP32 no Wokwi publicam no HiveMQ Cloud por MQTT com TLS, uma ponte em Python leva os dados para o Ubidots e um drone simulado atende as missões." |
| 0:15–0:40 | **Cena 1.** Z1 e Z2 com LED verde, LCD "Z1 NORMAL" e "Z2 NORMAL". No painel, os dois indicadores em NORMAL e o gráfico de temperatura recebendo pontos. Mexer no slider do DHT22 de Z1 para 30 °C. | "Com tudo normal, cada nó manda a temperatura a cada 10 segundos e o estado a cada 30. Subindo a temperatura de Z1 para 30 graus, o ponto novo aparece no gráfico e o estado continua NORMAL, porque o limite de atenção é 35." |
| 0:40–0:55 | **Cena 2.** No cliente do HiveMQ, publicar `FORA_HORARIO` em `cna/zona/1/teste`. LCD de Z1 continua em NORMAL. | "Para não depender do relógio, mandamos um comando de teste que faz o nó considerar que estamos fora do horário permitido, que é das 7 às 19." |
| 0:55–1:10 | Mover a chave do portão de Z1. LED amarelo, bipe curto, LCD "Z1 ATENCAO / PORTAO". Painel muda para ATENÇÃO, evento PORTAO na tabela. | "Portão aberto fora do horário, sem presença: ATENÇÃO. A resposta local é imediata, não espera o broker." |
| 1:10–1:40 | Clicar no PIR de Z1. LED laranja, buzzer intermitente, LCD "Z1 ALERTA / PIR+PORTAO". Painel em ALERTA. Terminal do drone: missão recebida, DECOLANDO, EM_ROTA, NA_ZONA. | "Com presença e portão aberto o nó vai direto para ALERTA e publica uma única missão em cna/drone/missao. O drone decola, vai até a zona e inspeciona." |
| 1:40–1:55 | Painel do drone mostrando zona Z1 e resultado ANOMALIA_CONFIRMADA, depois RETORNANDO e DISPONIVEL. | "Como o PIR registrou movimento durante a missão, o drone confirma a anomalia e volta para a base." |
| 1:55–2:05 | Fechar o portão de Z1. No painel, clicar ACK e REARMAR de Z1. Z1 volta a verde. | "O alarme estava travado. ACK silencia o buzzer e REARMAR devolve a zona a NORMAL, já que as condições normalizaram." |
| 2:05–2:25 | **Cena 3.** Segurar o botão TAMPER de Z2. LED vermelho piscando, sirene, LCD "Z2 CRITICO / TAMPER". Painel em CRÍTICO. Terminal do drone: missão para Z2. | "Violação do equipamento em Z2 leva direto a CRÍTICO, em qualquer estado e mesmo com a zona desarmada. O drone recebe a missão." |
| 2:25–2:40 | Clicar REARMAR antes do ACK: evento REARME_NEGADO na tabela. Clicar ACK: buzzer para, LED fica vermelho fixo, LCD mostra "S". | "Rearmar sem reconhecer o alarme é recusado. Depois do ACK a sirene para, mas o estado continua CRÍTICO." |
| 2:40–2:55 | Soltar o botão, clicar REARMAR. Z2 volta a verde, painel em NORMAL. Drone termina com ANOMALIA_CONFIRMADA. | "Com o tamper liberado, o REARMAR é aceito e Z2 volta a NORMAL. Para tamper o drone sempre confirma a anomalia." |
| 2:55–3:00 | Painel completo | "Esse foi o Nó Sentinela de Borda. Obrigado." |

Ao terminar, publicar `HORARIO_REAL` em `cna/zona/1/teste` para voltar ao relógio do NTP.
