// Máquina de estados do nó: NORMAL -> ATENCAO -> ALERTA -> CRITICO.
// Não depende de Arduino; recebe as entradas já tratadas e o tempo atual.
#pragma once
#include <stdint.h>

enum Estado : uint8_t { NORMAL = 0, ATENCAO = 1, ALERTA = 2, CRITICO = 3 };

enum Evento : uint8_t {
  EV_NENHUM = 0,
  EV_PIR,
  EV_PORTAO,
  EV_PIR_PORTAO,
  EV_PERSISTENCIA,
  EV_TEMP_ALTA,
  EV_TEMP_CRITICA,
  EV_TAMPER,
  EV_ACK,
  EV_REARME,
  EV_REARME_NEGADO,
  EV_ARMADO,
  EV_DESARMADO,
  EV_NORMALIZADO
};

enum Comando : uint8_t { CMD_ACK, CMD_REARMAR, CMD_ARMAR, CMD_DESARMAR, CMD_INVALIDO };

struct Entradas {
  bool pir;
  bool portaoAberto;
  bool tamperAtivo;   // botão pressionado neste instante
  float temperatura;  // NAN enquanto não houver leitura válida
  bool foraHorario;
};

const char* nomeEstado(Estado e);
const char* nomeEvento(Evento e);   // texto publicado no MQTT
const char* rotuloEvento(Evento e); // texto curto para o LCD
Comando interpretarComando(const char* texto);

class MaquinaEstados {
 public:
  void atualizar(const Entradas& e, uint32_t agora);
  void comando(Comando c, const Entradas& e, uint32_t agora);

  Estado estado() const { return estado_; }
  Evento causa() const { return causa_; }
  bool armado() const { return armado_; }
  bool silenciado() const { return silenciado_; }
  uint8_t nivelTemperatura() const { return nivelTemp_; }

  // Eventos gerados desde a última leitura (para publicar em .../evento).
  bool proximoEvento(Evento& ev);
  // Verdadeiro uma única vez por ciclo ALERTA/CRITICO até o próximo REARMAR.
  bool consumirMissao();

 private:
  struct Alvo {
    Estado estado;
    Evento evento;
  };

  Alvo avaliar(const Entradas& e) const;
  void atualizarNivelTemp(float t);
  void subir(Estado novo, Evento ev, uint32_t agora);
  void emitir(Evento ev);

  Estado estado_ = NORMAL;
  Evento causa_ = EV_NENHUM;
  bool armado_ = true;
  bool silenciado_ = false;
  bool tamperTravado_ = false;
  uint8_t nivelTemp_ = 0;  // 0 normal, 1 alta, 2 crítica

  bool condAtencao_ = false;
  uint32_t condAtencaoDesde_ = 0;
  bool semCondicao_ = false;
  uint32_t semCondicaoDesde_ = 0;

  bool missaoEnviada_ = false;
  bool missaoPendente_ = false;

  static const uint8_t TAM_FILA = 8;
  Evento fila_[TAM_FILA];
  uint8_t ini_ = 0, qtd_ = 0;
};
