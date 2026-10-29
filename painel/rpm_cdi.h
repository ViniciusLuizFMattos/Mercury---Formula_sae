// Leitura do RPM a partir do sinal do CDI, já adaptado pelo módulo optoacoplador (saída em 3,3 V).
// Não foi compilado no ESP32. A matemática foi conferida com um porte em Python (ver teste_rpm_cdi.py).
#pragma once
#include <Arduino.h>

constexpr int PIN_RPM = 27;                  // GPIO ligado à saída do módulo. Confirmar qual pino da placa está livre
constexpr float PULSOS_POR_ROTACAO = 0.5f;   // 1 pulso a cada 2 rotações (4 tempos, 1 cilindro).
                                             // Se o CDI der 1 pulso por rotação, usar 1.0. MEDIR (ver o PDF)
constexpr uint32_t FILTRO_US = 2000;         // ignora pulsos mais próximos que 2 ms (ruído de ignição)
constexpr uint32_t SEM_PULSO_US = 1000000;   // sem pulso há 1 s: motor parado

volatile uint32_t g_ultimoUs = 0;
volatile uint32_t g_periodoUs = 0;

void IRAM_ATTR onPulsoRpm() {
  uint32_t agora = micros();
  uint32_t dt = agora - g_ultimoUs;
  if (dt < FILTRO_US) return;
  g_periodoUs = dt;
  g_ultimoUs = agora;
}

inline void iniciaRpm() {
  pinMode(PIN_RPM, INPUT_PULLUP);                       // a saída do módulo puxa para baixo quando há pulso
  attachInterrupt(digitalPinToInterrupt(PIN_RPM), onPulsoRpm, FALLING);
}

inline float lerRpm() {
  noInterrupts();
  uint32_t p = g_periodoUs;
  uint32_t u = g_ultimoUs;
  interrupts();
  if (p == 0 || (uint32_t)(micros() - u) > SEM_PULSO_US) return 0.0f;
  return 60.0e6f / (static_cast<float>(p) * PULSOS_POR_ROTACAO);
}
