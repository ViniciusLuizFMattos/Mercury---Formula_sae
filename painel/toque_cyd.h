// Toque da CYD ESP32-2432S028 (controlador XPT2046). Lido por software ("bit-bang") nos pinos próprios do toque,
// sem usar nenhum barramento SPI de hardware: o VSPI fica livre para o cartão SD (18/19/23/5) e o HSPI para a tela.
#pragma once
#include <Arduino.h>

constexpr int T_CLK = 25, T_MOSI = 32, T_MISO = 39, T_CS = 33;

// Mapeamento do toque para a tela 320x240 em paisagem (rotação 1). Valores típicos da CYD: conferir tocando nos cantos
// (o painel imprime os valores brutos na serial quando recebe um toque). Se x ou y sair invertido, troque min e max.
constexpr int TOQUE_X_MIN = 200, TOQUE_X_MAX = 3700;  // canal 0x91 -> eixo x da tela
constexpr int TOQUE_Y_MIN = 240, TOQUE_Y_MAX = 3800;  // canal 0xD1 -> eixo y da tela
constexpr int TOQUE_Z_MIN = 400;                      // pressão mínima para valer como toque

static inline void toqueRelogio() {
  digitalWrite(T_CLK, HIGH);
  delayMicroseconds(1);
  digitalWrite(T_CLK, LOW);
  delayMicroseconds(1);
}

// manda o comando e devolve a medida de 12 bits do XPT2046
static uint16_t toqueLe(uint8_t cmd) {
  digitalWrite(T_CS, LOW);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(T_MOSI, (cmd >> i) & 1);
    toqueRelogio();
  }
  uint16_t r = 0;
  for (int i = 0; i < 16; i++) {  // 1 bit nulo, 12 de dados, 3 de sobra
    digitalWrite(T_CLK, HIGH);
    delayMicroseconds(1);
    r = (r << 1) | (digitalRead(T_MISO) ? 1 : 0);
    digitalWrite(T_CLK, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(T_CS, HIGH);
  return (r >> 3) & 0x0FFF;
}

inline void iniciaToque() {
  pinMode(T_CLK, OUTPUT);
  pinMode(T_MOSI, OUTPUT);
  pinMode(T_CS, OUTPUT);
  pinMode(T_MISO, INPUT);
  digitalWrite(T_CS, HIGH);
  digitalWrite(T_CLK, LOW);
}

struct PontoToque {
  bool tocando = false;
  int x = 0, y = 0;       // coordenadas na tela 320x240
  int brutoA = 0, brutoB = 0;  // medidas brutas (canais 0x91 e 0xD1), para calibrar
};

inline PontoToque lerToque() {
  PontoToque p;
  int z = (int)toqueLe(0xB1) + 4095 - (int)toqueLe(0xC1);
  if (z < TOQUE_Z_MIN) return p;
  toqueLe(0x91);  // a primeira medida costuma vir ruidosa
  long a = 0, b = 0;
  for (int i = 0; i < 3; i++) {
    a += toqueLe(0x91);
    b += toqueLe(0xD1);
  }
  toqueLe(0x90);  // desliga o conversor
  p.brutoA = a / 3;
  p.brutoB = b / 3;
  p.x = constrain(map(p.brutoA, TOQUE_X_MIN, TOQUE_X_MAX, 0, 319), 0, 319);
  p.y = constrain(map(p.brutoB, TOQUE_Y_MIN, TOQUE_Y_MAX, 0, 239), 0, 239);
  p.tocando = true;
  return p;
}
