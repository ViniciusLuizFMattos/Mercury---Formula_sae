// Entradas reais do painel (modo MODO_DEMO 0). NÃO testado com sensores: só compila.
// Pinos livres na CYD ESP32-2432S028 (conector externo): IO35 (só entrada), IO22, IO27.
//   IO27 = RPM (saída do módulo optoacoplador, ver rpm_cdi.h)
//   IO22 = sensor Hall da roda
//   IO35 = boia de combustível (divisor de tensão, ADC)
//   IO16 = neutro (opcional, mesmo pino do LED verde da placa; precisa de divisor de 12 V -> 3,3 V)
#pragma once
#include <Arduino.h>
#include "rpm_cdi.h"

constexpr int PIN_RODA = 22;
constexpr int PIN_COMB = 35;
constexpr int PIN_NEUTRO = 16;
#define USAR_NEUTRO 0

// ---- roda (ímãs na roda dianteira) ----
constexpr float CIRCUNFERENCIA_M = 1.40f;  // MEDIR: perímetro do pneu com o carro carregado
constexpr float IMAS_NA_RODA = 2.0f;       // quantos ímãs passam pelo sensor a cada volta
constexpr uint32_t FILTRO_RODA_US = 3000;  // ignora pulsos mais próximos que 3 ms
constexpr uint32_t SEM_PULSO_RODA_US = 2000000;
volatile uint32_t g_rodaUltimoUs = 0;
volatile uint32_t g_rodaPeriodoUs = 0;

void IRAM_ATTR onPulsoRoda() {
  uint32_t agora = micros();
  uint32_t dt = agora - g_rodaUltimoUs;
  if (dt < FILTRO_RODA_US) return;
  g_rodaPeriodoUs = dt;
  g_rodaUltimoUs = agora;
}

inline float lerKmh() {
  noInterrupts();
  uint32_t p = g_rodaPeriodoUs;
  uint32_t u = g_rodaUltimoUs;
  interrupts();
  if (p == 0 || (uint32_t)(micros() - u) > SEM_PULSO_RODA_US) return 0.0f;
  float hz = 1.0e6f / static_cast<float>(p);
  return hz / IMAS_NA_RODA * CIRCUNFERENCIA_M * 3.6f;
}

// ---- boia de combustível ----
// CALIBRAR: ler a tensão (mV) no pino com o tanque vazio e cheio. Valores abaixo são só marcadores.
constexpr float COMB_MV_VAZIO = 300.0f;
constexpr float COMB_MV_CHEIO = 2400.0f;
float g_combFiltrado = -1;

inline float lerCombustivel() {
  float mv = analogReadMilliVolts(PIN_COMB);
  g_combMv = mv;  // vai para o log do SD (ajuda a calibrar vazio e cheio)
  float pct = (mv - COMB_MV_VAZIO) / (COMB_MV_CHEIO - COMB_MV_VAZIO) * 100.0f;
  pct = constrain(pct, 0.0f, 100.0f);
  // a boia balança: filtro bem lento (~3 s)
  g_combFiltrado = g_combFiltrado < 0 ? pct : g_combFiltrado + (pct - g_combFiltrado) * 0.02f;
  return g_combFiltrado;
}

inline void iniciaEntradas() {
  iniciaRpm();
  pinMode(PIN_RODA, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_RODA), onPulsoRoda, FALLING);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_COMB, ADC_11db);
#if USAR_NEUTRO
  pinMode(PIN_NEUTRO, INPUT_PULLUP);
#endif
}

inline bool lerNeutro() {
#if USAR_NEUTRO
  return digitalRead(PIN_NEUTRO) == LOW;
#else
  return false;
#endif
}
