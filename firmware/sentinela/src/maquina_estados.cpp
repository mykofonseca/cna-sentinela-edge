#include "maquina_estados.h"

#include <math.h>
#include <string.h>

#include "config.h"

const char* nomeEstado(Estado e) {
  switch (e) {
    case NORMAL:  return "NORMAL";
    case ATENCAO: return "ATENCAO";
    case ALERTA:  return "ALERTA";
    case CRITICO: return "CRITICO";
  }
  return "?";
}

const char* nomeEvento(Evento e) {
  switch (e) {
    case EV_NENHUM:        return "NENHUM";
    case EV_PIR:           return "PIR";
    case EV_PORTAO:        return "PORTAO";
    case EV_PIR_PORTAO:    return "PIR_PORTAO";
    case EV_PERSISTENCIA:  return "PERSISTENCIA";
    case EV_TEMP_ALTA:     return "TEMP_ALTA";
    case EV_TEMP_CRITICA:  return "TEMP_CRITICA";
    case EV_TAMPER:        return "TAMPER";
    case EV_ACK:           return "ACK";
    case EV_REARME:        return "REARME";
    case EV_REARME_NEGADO: return "REARME_NEGADO";
    case EV_ARMADO:        return "ARMADO";
    case EV_DESARMADO:     return "DESARMADO";
    case EV_NORMALIZADO:   return "NORMALIZADO";
  }
  return "?";
}

const char* rotuloEvento(Evento e) {
  switch (e) {
    case EV_PIR:          return "PIR";
    case EV_PORTAO:       return "PORTAO";
    case EV_PIR_PORTAO:   return "PIR+PORTAO";
    case EV_PERSISTENCIA: return "PERSIST.";
    case EV_TEMP_ALTA:    return "TEMP ALTA";
    case EV_TEMP_CRITICA: return "TEMP CRIT";
    case EV_TAMPER:       return "TAMPER";
    default:              return "-";
  }
}

Comando interpretarComando(const char* t) {
  if (strcmp(t, "ACK") == 0) return CMD_ACK;
  if (strcmp(t, "REARMAR") == 0) return CMD_REARMAR;
  if (strcmp(t, "ARMAR") == 0) return CMD_ARMAR;
  if (strcmp(t, "DESARMAR") == 0) return CMD_DESARMAR;
  return CMD_INVALIDO;
}

// Histerese: só desce de nível quando cai TEMP_HISTERESE abaixo do limite.
void MaquinaEstados::atualizarNivelTemp(float t) {
  if (isnan(t)) return;
  if (t > TEMP_CRITICA) nivelTemp_ = 2;
  else if (nivelTemp_ == 2 && t > TEMP_CRITICA - TEMP_HISTERESE) nivelTemp_ = 2;
  else if (t > TEMP_ALTA) nivelTemp_ = 1;
  else if (nivelTemp_ >= 1 && t > TEMP_ALTA - TEMP_HISTERESE) nivelTemp_ = 1;
  else nivelTemp_ = 0;
}

// Estado que as entradas pedem agora, do mais grave para o mais leve.
MaquinaEstados::Alvo MaquinaEstados::avaliar(const Entradas& e) const {
  if (tamperTravado_ || e.tamperAtivo) return {CRITICO, EV_TAMPER};
  if (nivelTemp_ == 2) return {CRITICO, EV_TEMP_CRITICA};
  if (armado_) {
    if (e.pir && e.portaoAberto && e.foraHorario) return {ALERTA, EV_PIR_PORTAO};
    if (e.pir) return {ATENCAO, EV_PIR};
    if (e.portaoAberto && e.foraHorario) return {ATENCAO, EV_PORTAO};
  }
  if (nivelTemp_ == 1) return {ATENCAO, EV_TEMP_ALTA};
  return {NORMAL, EV_NENHUM};
}

void MaquinaEstados::emitir(Evento ev) {
  if (qtd_ == TAM_FILA) {  // fila cheia: descarta o mais antigo
    ini_ = (ini_ + 1) % TAM_FILA;
    qtd_--;
  }
  fila_[(ini_ + qtd_) % TAM_FILA] = ev;
  qtd_++;
}

bool MaquinaEstados::proximoEvento(Evento& ev) {
  if (qtd_ == 0) return false;
  ev = fila_[ini_];
  ini_ = (ini_ + 1) % TAM_FILA;
  qtd_--;
  return true;
}

bool MaquinaEstados::consumirMissao() {
  if (!missaoPendente_) return false;
  missaoPendente_ = false;
  return true;
}

void MaquinaEstados::subir(Estado novo, Evento ev, uint32_t agora) {
  estado_ = novo;
  causa_ = ev;
  silenciado_ = false;  // escalada volta a tocar mesmo depois de ACK
  semCondicao_ = false;
  if (novo == ATENCAO) {
    condAtencao_ = true;
    condAtencaoDesde_ = agora;
  }
  if (novo >= ALERTA && !missaoEnviada_) {
    missaoEnviada_ = true;
    missaoPendente_ = true;
  }
  emitir(ev);
}

void MaquinaEstados::atualizar(const Entradas& e, uint32_t agora) {
  atualizarNivelTemp(e.temperatura);
  if (e.tamperAtivo) tamperTravado_ = true;

  Alvo alvo = avaliar(e);

  // Tempo contínuo em que a condição de ATENÇÃO está presente.
  if (alvo.estado == ATENCAO) {
    if (!condAtencao_) {
      condAtencao_ = true;
      condAtencaoDesde_ = agora;
    }
  } else {
    condAtencao_ = false;
  }

  if (alvo.estado > estado_) {
    subir(alvo.estado, alvo.evento, agora);
    return;
  }

  // ALERTA e CRITICO ficam travados; só ATENÇÃO evolui sozinha.
  if (estado_ != ATENCAO) return;

  if (condAtencao_ && agora - condAtencaoDesde_ >= PERSISTENCIA_MS) {
    subir(ALERTA, EV_PERSISTENCIA, agora);
    return;
  }

  if (alvo.estado == NORMAL) {
    if (!semCondicao_) {
      semCondicao_ = true;
      semCondicaoDesde_ = agora;
    } else if (agora - semCondicaoDesde_ >= RETORNO_ATENCAO_MS) {
      estado_ = NORMAL;
      causa_ = EV_NENHUM;
      silenciado_ = false;
      semCondicao_ = false;
      emitir(EV_NORMALIZADO);
    }
  } else {
    semCondicao_ = false;
  }
}

void MaquinaEstados::comando(Comando c, const Entradas& e, uint32_t agora) {
  switch (c) {
    case CMD_ACK:
      if (estado_ != NORMAL && !silenciado_) {
        silenciado_ = true;
        emitir(EV_ACK);
      }
      break;

    case CMD_REARMAR: {
      if (estado_ < ALERTA) break;
      if (!silenciado_) {  // exige ACK antes do rearme
        emitir(EV_REARME_NEGADO);
        break;
      }
      // O tamper só é liberado se o botão já foi solto.
      bool tamperAnterior = tamperTravado_;
      tamperTravado_ = e.tamperAtivo;
      Alvo alvo = avaliar(e);
      if (alvo.estado >= ALERTA) {
        tamperTravado_ = tamperAnterior;
        emitir(EV_REARME_NEGADO);
        break;
      }
      estado_ = alvo.estado;
      causa_ = alvo.evento;
      silenciado_ = false;
      missaoEnviada_ = false;
      missaoPendente_ = false;
      semCondicao_ = false;
      condAtencao_ = (alvo.estado == ATENCAO);
      condAtencaoDesde_ = agora;
      emitir(EV_REARME);
      break;
    }

    case CMD_ARMAR:
      if (!armado_) {
        armado_ = true;
        emitir(EV_ARMADO);
      }
      break;

    case CMD_DESARMAR:
      if (armado_) {
        armado_ = false;
        emitir(EV_DESARMADO);
      }
      break;

    default:
      break;
  }
}
