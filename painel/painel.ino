/*
 * Painel Mercury — CYD ESP32-2432S028, 320x240 em paisagem.
 * Layout, cores e regras vêm de simulador_tela/simulador_tela.py.
 *
 * MODO_DEMO 1: o carro "anda sozinho" (sem sensores). MODO_DEMO 0: lê RPM, roda e boia (entradas.h, não testado).
 * Cada região (leds, arco, combustível, marcha) é um sprite redesenhado só quando o valor muda.
 */
#include <Arduino.h>

#ifndef MODO_DEMO
#define MODO_DEMO 1
#endif

#include "tela_cyd.h"
#include "logo_mercury.h"
#include "simulacao.h"
#include "sd_log.h"
#include "toque_cyd.h"
#if !MODO_DEMO
#include "entradas.h"
#endif


constexpr int PIN_BACKLIGHT = 21;
constexpr int CANAL_BL = 0;

static inline uint16_t c565(uint8_t r, uint8_t g, uint8_t b) { return lgfx::color565(r, g, b); }
const uint16_t BG = c565(8, 10, 14), PANEL = c565(22, 26, 34), TXT = c565(240, 244, 250), MUT = c565(140, 150, 165);
const uint16_t GRN = c565(46, 204, 113), YEL = c565(250, 204, 21), RED = c565(239, 68, 68), ORG = c565(249, 115, 22);
const uint16_t DIM = c565(38, 44, 56), BRANCO = c565(255, 255, 255);

constexpr float RPM_MAX_ESCALA = 8.0f;  // x1000. PALPITE: a equipe do motor define
constexpr float FAIXA_AMARELA = 5.5f, FAIXA_VERMELHA = 7.0f;  // PALPITE
constexpr int SEGMENTOS = 8;
constexpr float DURACAO_BOOT = 3.6f;

Tela tela;
lgfx::LGFX_Sprite sLeds(&tela), sGauge(&tela), sFuel(&tela), sRec(&tela);

constexpr int PIN_BOTAO_REC = 0;  // botão BOOT da placa: também liga e desliga a gravação (bancada)
constexpr int REC_ZONA_W = 90, REC_ZONA_H = 64;  // toque aqui (canto superior esquerdo) liga e desliga a gravação

// origem de cada sprite na tela (coordenadas absolutas do layout do simulador)
constexpr int LEDS_X = 70, LEDS_Y = 2, LEDS_W = 188, LEDS_H = 16;
// arco + velocidade + marcha ficam num sprite só (um único envio por quadro): a caixa de marcha (252..314, 18..68)
// se sobrepõe ao retângulo do arco, e sprites separados apagavam um ao outro.
constexpr int GAUGE_X = 100, GAUGE_Y = 18, GAUGE_W = 218, GAUGE_H = 186;
constexpr int FUEL_X = 0, FUEL_Y = 52, FUEL_W = 72, FUEL_H = 188;
// indicador de gravação no canto superior esquerdo (livre: os leds começam em x=70 e o combustível em y=52)
constexpr int REC_X = 2, REC_Y = 2, REC_W = 66, REC_H = 46;

// ------------------------------------------------------------------ utilidades
void texto(lgfx::LGFX_Sprite &s, const char *str, float x, float y, const lgfx::IFont *f, uint16_t cor, float tam = 1.0f,
           lgfx::textdatum_t d = lgfx::textdatum_t::middle_center) {
  s.setFont(f);
  s.setTextSize(tam);
  s.setTextDatum(d);
  s.setTextColor(cor);
  s.drawString(str, (int)x, (int)y);
}

// arco entre dois valores da escala (0..8 mil rpm). Ângulos do LovyanGFX: 0 = 3 horas, sentido horário.
void arcoV(lgfx::LGFX_Sprite &s, int cx, int cy, int ri, int ro, float v0, float v1, uint16_t cor) {
  float a0 = 150.0f + 30.0f * v0, a1 = 150.0f + 30.0f * v1;
  if (a1 - a0 < 0.5f) return;
  if (a0 >= 360) {
    a0 -= 360;
    a1 -= 360;
  }
  if (a1 > 360) {
    s.fillArc(cx, cy, ri, ro, a0, 360, cor);
    s.fillArc(cx, cy, ri, ro, 0, a1 - 360, cor);
  } else {
    s.fillArc(cx, cy, ri, ro, a0, a1, cor);
  }
}

int segmentosAcesos(float comb) {
  if (comb <= 0) return 0;
  int n = (int)lroundf(comb / 100.0f * SEGMENTOS);
  return constrain(n, 1, SEGMENTOS);
}

int ledsAcesos(float rpm) { return rpm > 4500 ? (int)lroundf(12 * fminf(rpm, 8000) / 8000) : 0; }

// ------------------------------------------------------------------ regiões
void desenhaLeds(int acesos) {
  sLeds.fillScreen(BG);
  for (int i = 0; i < 12; i++) {
    float x = 160 + (i - 5.5f) * 15 - LEDS_X, y = 10 - LEDS_Y;
    uint16_t c = i < 5 ? GRN : i < 9 ? YEL : RED;
    sLeds.fillCircle((int)lroundf(x), (int)y, 5, i < acesos ? c : DIM);
  }
  sLeds.pushSprite(LEDS_X, LEDS_Y);
}

void desenhaGear(char gear, float flash, int dir);

void desenhaGauge(float rpm, float kmh, char gear, float flash, int dir) {
  const int cx = 198 - GAUGE_X, cy = 142 - GAUGE_Y, ro = 88, ri = 72;
  sGauge.fillScreen(BG);
  arcoV(sGauge, cx, cy, ri, ro, 0, RPM_MAX_ESCALA, DIM);
  float v = fminf(rpm / 1000.0f, RPM_MAX_ESCALA);
  v = floorf(v * 12.0f) / 12.0f;  // passo de 1/12 de mil rpm (96 passos na escala)
  arcoV(sGauge, cx, cy, ri, ro, 0, fminf(v, FAIXA_AMARELA), GRN);
  if (v > FAIXA_AMARELA) arcoV(sGauge, cx, cy, ri, ro, FAIXA_AMARELA, fminf(v, FAIXA_VERMELHA), YEL);
  if (v > FAIXA_VERMELHA) arcoV(sGauge, cx, cy, ri, ro, FAIXA_VERMELHA, v, RED);
  for (int t = 0; t <= 8; t++) {
    float a = radians(210.0f - 30.0f * t);
    sGauge.drawWideLine(cx + (ri - 3) * cosf(a), cy - (ri - 3) * sinf(a), cx + (ri - 9) * cosf(a),
                        cy - (ri - 9) * sinf(a), 1.0f, MUT);
    if (t % 2 == 0) {
      char b[4];
      snprintf(b, sizeof(b), "%d", t);
      texto(sGauge, b, cx + (ri - 19) * cosf(a), cy - (ri - 19) * sinf(a), &lgfx::fonts::DejaVu12, MUT);
    }
  }
  char b[16];
  snprintf(b, sizeof(b), "%d", (int)lroundf(kmh));
  texto(sGauge, b, cx, cy - 12, &lgfx::fonts::FreeSansBold24pt7b, TXT, 1.15f);
  texto(sGauge, "km/h", cx, cy + 28, &lgfx::fonts::DejaVu12, MUT);
  snprintf(b, sizeof(b), "%d rpm", (int)lroundf(rpm / 10.0f) * 10);
  texto(sGauge, b, cx, 192 - GAUGE_Y, &lgfx::fonts::FreeSansBold12pt7b, MUT);
  desenhaGear(gear, flash, dir);
  sGauge.pushSprite(GAUGE_X, GAUGE_Y);
}

void desenhaFuel(float comb, bool fase) {
  int n = segmentosAcesos(comb);
  bool baixo = n <= 2;
  uint16_t cor = baixo ? RED : n <= 3 ? ORG : GRN;
  sFuel.fillScreen(BG);
  texto(sFuel, "FUEL", 8, 62 - FUEL_Y, &lgfx::fonts::DejaVu12, MUT, 1.0f, lgfx::textdatum_t::middle_left);
  const int x0 = 8, larg = 32, alt = 15, gap = 3, yBase = 216 - FUEL_Y;
  for (int i = 0; i < SEGMENTOS; i++) {
    int y = yBase - (i + 1) * alt - i * gap;
    uint16_t c = i < n ? cor : DIM;
    if (baixo && i == 0 && n > 0 && fase) c = DIM;  // o último segmento pisca na reserva
    sFuel.fillRoundRect(x0, y, larg, alt, 2, c);
  }
  int topo = yBase - SEGMENTOS * alt - (SEGMENTOS - 1) * gap;
  texto(sFuel, "F", x0 + larg + 6, topo + alt / 2, &lgfx::fonts::FreeSansBold9pt7b, MUT, 1.0f, lgfx::textdatum_t::middle_left);
  texto(sFuel, "E", x0 + larg + 6, yBase - alt / 2, &lgfx::fonts::FreeSansBold9pt7b, baixo ? RED : MUT, 1.0f,
        lgfx::textdatum_t::middle_left);
  if (baixo) texto(sFuel, "RESERVA", x0, 230 - FUEL_Y, &lgfx::fonts::DejaVu12, RED, 1.0f, lgfx::textdatum_t::middle_left);
  sFuel.pushSprite(FUEL_X, FUEL_Y);
}

void desenhaRec(EstadoSd est, bool pisca, uint32_t segundos, const char *nome) {
  sRec.fillScreen(BG);
  char b[16];
  uint16_t borda = DIM;
  switch (est) {
    case SD_GRAVANDO:
      borda = RED;
      sRec.fillCircle(12, 12, 5, pisca ? RED : DIM);
      texto(sRec, "REC", 23, 12, &lgfx::fonts::FreeSansBold9pt7b, RED, 1.0f, lgfx::textdatum_t::middle_left);
      snprintf(b, sizeof(b), "%02u:%02u", (unsigned)(segundos / 60), (unsigned)(segundos % 60));
      texto(sRec, b, 6, 28, &lgfx::fonts::FreeSansBold9pt7b, TXT, 1.0f, lgfx::textdatum_t::middle_left);
      texto(sRec, nome, 6, 40, &lgfx::fonts::DejaVu9, MUT, 1.0f, lgfx::textdatum_t::middle_left);
      break;
    case SD_PRONTO:
      borda = GRN;
      sRec.fillCircle(12, 12, 5, DIM);
      texto(sRec, "REC", 23, 12, &lgfx::fonts::FreeSansBold9pt7b, MUT, 1.0f, lgfx::textdatum_t::middle_left);
      texto(sRec, "SD ok", 6, 28, &lgfx::fonts::DejaVu12, GRN, 1.0f, lgfx::textdatum_t::middle_left);
      texto(sRec, "toque p/ REC", 6, 40, &lgfx::fonts::DejaVu9, MUT, 1.0f, lgfx::textdatum_t::middle_left);
      break;
    case SD_ERRO:
      borda = RED;
      sRec.fillCircle(12, 12, 5, RED);
      texto(sRec, "REC", 23, 12, &lgfx::fonts::FreeSansBold9pt7b, RED, 1.0f, lgfx::textdatum_t::middle_left);
      texto(sRec, "ERRO SD", 6, 28, &lgfx::fonts::DejaVu12, RED, 1.0f, lgfx::textdatum_t::middle_left);
      break;
    default:
      sRec.fillCircle(12, 12, 5, DIM);
      texto(sRec, "REC", 23, 12, &lgfx::fonts::FreeSansBold9pt7b, DIM, 1.0f, lgfx::textdatum_t::middle_left);
      texto(sRec, "SEM SD", 6, 28, &lgfx::fonts::DejaVu12, ORG, 1.0f, lgfx::textdatum_t::middle_left);
      break;
  }
  sRec.drawRoundRect(0, 0, REC_W, REC_H, 6, borda);  // contorno: é um botão de toque
  sRec.pushSprite(REC_X, REC_Y);
}
void desenhaGear(char gear, float flash, int dir) {
  bool num = gear >= '1' && gear <= '5';
  uint16_t fundo, borda, cor;
  if (gear == 'N') {
    fundo = c565(14, 52, 32); borda = GRN; cor = GRN;
  } else if (!num) {
    fundo = PANEL; borda = c565(90, 100, 115); cor = MUT;
  } else {
    fundo = PANEL; borda = YEL; cor = YEL;
  }
  int bw = 2;
  if (flash > 0 && num) {
    fundo = c565(60, 52, 12); borda = BRANCO; cor = BRANCO;
    bw = 3;  // borda um pouco mais grossa na troca, sem encostar no texto "GEAR"
  }
  const int bx = 252 - GAUGE_X, by = 18 - GAUGE_Y, bl = 62, ba = 50;
  sGauge.fillRoundRect(bx, by, bl, ba, 6, borda);
  sGauge.fillRoundRect(bx + bw, by + bw, bl - 2 * bw, ba - 2 * bw, 4, fundo);
  texto(sGauge, "GEAR", 283 - GAUGE_X, 26 - GAUGE_Y, &lgfx::fonts::DejaVu9, MUT);
  if (flash > 0 && num) {
    int ax = 304 - GAUGE_X, ay = 26 - GAUGE_Y;
    if (dir > 0) sGauge.fillTriangle(ax, ay - 4, ax - 4, ay + 3, ax + 4, ay + 3, ORG);
    else sGauge.fillTriangle(ax, ay + 4, ax - 4, ay - 3, ax + 4, ay - 3, ORG);
  }
  char b[2] = {gear, 0};
  // fonte de tamanho fixo: escalar a fonte GFX com fator fracionário deixava pixels faltando no glifo
  texto(sGauge, b, 283 - GAUGE_X, 50 - GAUGE_Y, &lgfx::fonts::DejaVu40, cor);
}

// ------------------------------------------------------------------ boot
void telaBoot() {
  tela.fillScreen(0x0000);
  ledcWrite(CANAL_BL, 0);
  tela.pushImage((320 - LOGO_W) / 2, 34, LOGO_W, LOGO_H, LOGO_MERCURY);
  uint32_t t0 = millis();
  int ultStatus = -1, ultLed = -2;
  tela.fillRoundRect(70, 200, 180, 8, 3, DIM);
  for (;;) {
    float t = (millis() - t0) / 1000.0f;
    if (t >= DURACAO_BOOT) break;
    ledcWrite(CANAL_BL, (uint32_t)(255 * constrain(t / 1.0f, 0.0f, 1.0f)));  // fade-in pelo backlight
    int led = -1;
    if (t > 1.0f) {
      int ciclo = (int)((t - 1.0f) / 0.10f) % 22;
      led = ciclo < 12 ? ciclo : 22 - ciclo - 1;
    }
    if (led != ultLed) {
      ultLed = led;
      desenhaLeds(led + 1);
    }
    float prog = constrain((t - 1.0f) / 2.0f, 0.0f, 1.0f);
    if (prog > 0) tela.fillRoundRect(70, 200, (int)(180 * prog) < 6 ? 6 : (int)(180 * prog), 8, 3, GRN);
    int status = t < 1.0f ? 0 : prog < 0.4f ? 1 : prog < 0.8f ? 2 : 3;
    if (status != ultStatus) {
      ultStatus = status;
      tela.fillRect(0, 214, 320, 20, 0x0000);
      const char *msg[] = {"", "Iniciando painel", "Lendo sensores", "Pronto"};
      tela.setFont(&lgfx::fonts::DejaVu12);
      tela.setTextDatum(lgfx::textdatum_t::middle_center);
      tela.setTextColor(MUT);
      tela.drawString(msg[status], 160, 224);
    }
    delay(20);
  }
  ledcWrite(CANAL_BL, 255);
}

// ------------------------------------------------------------------ principal
void setup() {
  Serial.begin(115200);
  ledcSetup(CANAL_BL, 5000, 8);
  ledcAttachPin(PIN_BACKLIGHT, CANAL_BL);
  ledcWrite(CANAL_BL, 0);

  tela.init();
  tela.setRotation(1);
  tela.setColorDepth(16);

  for (auto *s : {&sLeds, &sGauge, &sFuel, &sRec}) s->setColorDepth(16);
  bool ok = sLeds.createSprite(LEDS_W, LEDS_H) && sGauge.createSprite(GAUGE_W, GAUGE_H) &&
            sFuel.createSprite(FUEL_W, FUEL_H) && sRec.createSprite(REC_W, REC_H);
  Serial.printf("Sprites %s, heap livre %u KB\n", ok ? "ok" : "FALHOU", ESP.getFreeHeap() / 1024);

  pinMode(PIN_BOTAO_REC, INPUT_PULLUP);
  iniciaToque();
  iniciaGravador();  // monta o cartão em segundo plano durante o boot

  telaBoot();
  tela.fillScreen(BG);
#if !MODO_DEMO
  iniciaEntradas();
#endif
}

void loop() {
  static uint32_t ult = millis(), ultFps = millis(), quadros = 0;
  static Ciclo ciclo;
  static EstimadorMarcha estimador;
  static char gear = 'N';
  static float flash = 0, tFlash = -9;
  static int dir = 1;
  // últimos valores desenhados (só redesenha quando muda)
  static int sigLed = -1, sigGauge = -1, sigMarcha = -1, sigFuel = -1;
  static uint32_t sigRec = 0xFFFFFFFF;
  static ControleRec controleRec;
  static bool botaoAnt = false;
  static uint32_t ultBotaoMs = 0, ultLeituraToque = 0, ultDebugToque = 0, ultToqueMs = 0;
  static bool tocandoAnt = false;

  uint32_t agoraMs = millis();
  float dt = fminf(0.1f, (agoraMs - ult) / 1000.0f);
  ult = agoraMs;
  float agora = agoraMs / 1000.0f;

  float rpm, kmh, comb;
  bool neutro;
#if MODO_DEMO
  ciclo.passo(dt);
  rpm = ciclo.rpm; kmh = ciclo.kmh; comb = ciclo.comb;
  neutro = ciclo.gear == 0;
#else
  rpm = lerRpm(); kmh = lerKmh(); comb = lerCombustivel();
  neutro = lerNeutro();
#endif

  char novo = estimador.passo(rpm, kmh, neutro, dt);
  if (novo >= '1' && novo <= '5' && gear >= '1' && gear <= '5' && novo != gear) {
    tFlash = agora;
    dir = novo > gear ? 1 : -1;
  }
  gear = novo;
  flash = fmaxf(0.0f, 1.0f - (agora - tFlash) / 0.7f);

  int leds = ledsAcesos(rpm);
  if (leds != sigLed) { sigLed = leds; desenhaLeds(leds); }

  // assinatura do que está no sprite principal (arco, velocidade, rpm e marcha): só envia se algo mudou
  int estadoMarcha = (gear << 8) | ((int)(flash * 10) << 1) | (dir > 0);
  int sg = (int)(fminf(rpm, 8000.0f) / 1000.0f * 12.0f) * 1000000 + (int)lroundf(kmh) * 1000 +
           (int)lroundf(fminf(rpm, 9990.0f) / 10.0f) % 1000;
  if (sg != sigGauge || estadoMarcha != sigMarcha) {
    sigGauge = sg;
    sigMarcha = estadoMarcha;
    desenhaGauge(rpm, kmh, gear, flash, dir);
  }

  // gravação: só liga e desliga por toque na caixa REC ou pelo botão BOOT (borda de descida, com 300 ms de trava)
  bool botaoAgora = digitalRead(PIN_BOTAO_REC) == LOW;
  bool apertou = botaoAgora && !botaoAnt && agoraMs - ultBotaoMs > 300;
  if (apertou) ultBotaoMs = agoraMs;
  botaoAnt = botaoAgora;
  // toque na tela: encostar na caixa REC (canto superior esquerdo) liga e desliga a gravação
  if (agoraMs - ultLeituraToque >= 30) {
    ultLeituraToque = agoraMs;
    PontoToque tq = lerToque();
    if (tq.tocando) {
      if (agoraMs - ultDebugToque > 250) {
        ultDebugToque = agoraMs;
        Serial.printf("toque bruto %d,%d -> x %d y %d\n", tq.brutoA, tq.brutoB, tq.x, tq.y);
      }
      if (!tocandoAnt && tq.x < REC_ZONA_W && tq.y < REC_ZONA_H && agoraMs - ultToqueMs > 500) {
        ultToqueMs = agoraMs;
        apertou = true;
      }
    }
    tocandoAnt = tq.tocando;
  }
  controleRec.passo(rpm, dt, apertou);
  gravaAmostra(agoraMs, rpm, kmh, comb, gear, neutro);

  EstadoSd est = g_sdEstado;
  bool piscaRec = ((agoraMs / 500) % 2) == 0;
  uint32_t segRec = est == SD_GRAVANDO ? (agoraMs - g_recInicioMs) / 1000 : 0;
  uint32_t sr = ((uint32_t)est << 28) | ((segRec & 0xFFFFF) << 1) | (est == SD_GRAVANDO && piscaRec ? 1 : 0);
  if (sr != sigRec) { sigRec = sr; desenhaRec(est, piscaRec, segRec, g_recNome); }

  bool fase = ((agoraMs / 500) % 2) == 0;
  int sf = segmentosAcesos(comb) * 10 + (segmentosAcesos(comb) <= 2 && fase ? 1 : 0);
  if (sf != sigFuel) { sigFuel = sf; desenhaFuel(comb, fase); }

  quadros++;
  if (agoraMs - ultFps >= 5000) {
    Serial.printf("rpm %.0f  kmh %.1f  fuel %.0f%%  gear %c  | %.1f quadros/s | SD %d %s %lu linhas, %lu perdidas\n", rpm, kmh,
                  comb, gear, quadros * 1000.0f / (agoraMs - ultFps), (int)g_sdEstado, g_recNome, (unsigned long)g_recLinhas,
                  (unsigned long)g_recPerdidas);
    ultFps = agoraMs;
    quadros = 0;
  }
  delay(8);
}
