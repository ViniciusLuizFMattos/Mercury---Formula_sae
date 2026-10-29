// Carro simulado e estimador de marcha. Porte direto de simulador_tela/simulador_tela.py.
// Marchas, velocidades e consumo são ILUSTRATIVOS (as relações reais saem de calibração rodando o carro).
#pragma once
#include <Arduino.h>
#include <math.h>

constexpr int N_MARCHAS = 5;
// km/h a 8000 rpm em cada marcha (índice 1..5). ILUSTRATIVO
constexpr float GEAR_VMAX[N_MARCHAS + 1] = {0, 35.0f, 55.0f, 75.0f, 95.0f, 115.0f};
constexpr float ACEL[N_MARCHAS + 1] = {0, 14.0f, 11.0f, 8.0f, 6.5f, 4.5f};  // km/h por segundo. ILUSTRATIVO
constexpr float RPM_TROCA = 7000.0f;
constexpr float RPM_LENTA = 1400.0f;

class Ciclo {
 public:
  enum Fase { PARADO, ACELERA, CRUZEIRO, FREIA, PARANDO };
  float rpm = RPM_LENTA, kmh = 0, comb = 100;
  int gear = 0;  // 0 = neutro

  void passo(float dt) {
    t_ += dt;
    bool andando = kmh > 1.0f;
    comb = fmaxf(0.0f, comb - (andando ? 1.2f : 0.3f) * dt);  // consumo ACELERADO para as barras se mexerem
    float alvo = rpm;
    switch (fase_) {
      case PARADO:
        gear = 0;
        kmh = 0;
        alvo = RPM_LENTA + 40.0f * sinf(t_ * 9.0f);
        if (t_ > 3.0f) {
          if (comb < 6) comb = 100;  // reabasteceu
          gear = 1;
          ir(ACELERA);
        }
        break;
      case ACELERA: {
        int g = gear;
        kmh += ACEL[g] * dt;
        alvo = kmh / GEAR_VMAX[g] * 8000.0f;
        if (g == 1 && kmh < 10) alvo = fmaxf(alvo, 3200.0f);  // embreagem patinando na saída
        if (alvo >= RPM_TROCA && g < N_MARCHAS) gear = g + 1;
        else if (g == N_MARCHAS && alvo >= 6500) ir(CRUZEIRO);
        break;
      }
      case CRUZEIRO:
        alvo = 6500.0f + 60.0f * sinf(t_ * 6.0f);
        if (t_ > 5.0f) ir(FREIA);
        break;
      case FREIA: {
        kmh = fmaxf(0.0f, kmh - 26.0f * dt);
        int g = gear;
        alvo = kmh / GEAR_VMAX[g] * 8000.0f;
        if (alvo < 2800 && g > 1) gear = g - 1;
        if (gear == 1 && kmh < 14) {
          gear = 0;
          ir(PARANDO);
        }
        break;
      }
      case PARANDO:
        kmh = fmaxf(0.0f, kmh - 8.0f * dt);
        alvo = RPM_LENTA;
        if (kmh <= 0.0f) ir(PARADO);
        break;
    }
    float k = dt * 14.0f;
    rpm += (alvo - rpm) * (k > 1.0f ? 1.0f : k);
  }

 private:
  Fase fase_ = PARADO;
  float t_ = 0;
  void ir(Fase f) {
    fase_ = f;
    t_ = 0;
  }
};

// Descobre a marcha SEM sensor de marcha: compara RPM / velocidade com a relação de cada marcha.
// Retorna 'N' (neutro), '-' (embreagem/entre marchas) ou '1'..'5'.
class EstimadorMarcha {
 public:
  char atual = '-';

  char passo(float rpm, float kmh, bool neutro, float dt) {
    if (neutro) {
      atual = 'N';
      cand_ = 0;
      tSem_ = 0;
      return 'N';
    }
    if (kmh < 5 || rpm < 1500) {
      atual = '-';
      cand_ = 0;
      return '-';
    }
    float r = rpm / kmh;
    int g = 1;
    float erro = 1e9f;
    for (int i = 1; i <= N_MARCHAS; i++) {
      float rel = 8000.0f / GEAR_VMAX[i];
      float e = fabsf(r - rel) / rel;
      if (e < erro) {
        erro = e;
        g = i;
      }
    }
    if (erro < 0.08f) {
      tSem_ = 0;
      if (atual == '0' + g) {
        cand_ = 0;
        tCand_ = 0;
      } else if (cand_ == g) {
        tCand_ += dt;
        if (tCand_ >= 0.12f) {
          atual = '0' + g;
          cand_ = 0;
          tCand_ = 0;
        }
      } else {
        cand_ = g;
        tCand_ = 0;
      }
    } else {
      tSem_ += dt;  // entre duas marchas (trocando): segura a última por um tempo
      if (tSem_ > 0.6f) atual = '-';
    }
    if (atual == 'N') atual = '-';
    return atual;
  }

 private:
  int cand_ = 0;
  float tCand_ = 0, tSem_ = 0;
};
