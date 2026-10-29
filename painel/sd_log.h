// Gravação dos sensores no cartão microSD da CYD (leitor na própria placa, barramento VSPI).
// Uma tarefa no núcleo 0 faz todo o acesso ao cartão: o loop só joga amostras numa fila, então a tela não trava
// quando o cartão demora para responder.
// Arquivos: /LOG/LOG_0001.csv, LOG_0002.csv... (número seguinte ao maior que já existe). Sem relógio: o tempo é
// contado em ms desde o início da gravação.
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

constexpr int SD_SCK = 18, SD_MISO = 19, SD_MOSI = 23, SD_CS = 5;  // leitor microSD da CYD ESP32-2432S028

constexpr uint32_t REC_PERIODO_MS = 40;      // no máximo 25 amostras por segundo (o loop roda a ~20 a 30 por segundo)
constexpr uint32_t REC_FLUSH_MS = 1000;      // grava de fato no cartão a cada 1 s (corte de energia perde no máximo 1 s)
constexpr float REC_RPM_PARADO = 100.0f;     // abaixo disso, motor considerado parado
constexpr float REC_ESPERA_PARA_S = 300.0f;  // motor parado por 5 min: para a gravação (esquecida ligada)

enum EstadoSd : uint8_t { SD_AUSENTE, SD_PRONTO, SD_GRAVANDO, SD_ERRO };

struct Amostra {
  uint32_t t_ms;
  float rpm, kmh, comb, comb_mv;
  char marcha;
  uint8_t neutro;
};

// preenchido por lerCombustivel() (entradas.h). Fica 0 no modo demo.
float g_combMv = 0;

volatile EstadoSd g_sdEstado = SD_AUSENTE;
volatile uint32_t g_recInicioMs = 0;
volatile uint32_t g_recLinhas = 0;
volatile uint32_t g_recPerdidas = 0;
volatile int g_recComando = 0;  // 1 = inicia, 2 = para (tratado pela tarefa)
char g_recNome[16] = "";

static SPIClass g_spiSd(VSPI);
static QueueHandle_t g_filaRec = nullptr;
static File g_arq;

static bool montaSd() {
  g_spiSd.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, g_spiSd, 20000000)) return false;
  return SD.cardType() != CARD_NONE;
}

static int proximoNumeroLog() {
  int maior = 0;
  File dir = SD.open("/LOG");
  if (!dir) return 1;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    int n = 0;
    if (sscanf(f.name(), "LOG_%d.csv", &n) == 1 && n > maior) maior = n;
    f.close();
  }
  dir.close();
  return maior + 1;
}

static bool abreArquivo() {
  if (g_sdEstado == SD_AUSENTE || g_sdEstado == SD_ERRO) {
    SD.end();
    if (!montaSd()) {
      g_sdEstado = SD_AUSENTE;
      return false;
    }
  }
  if (!SD.exists("/LOG")) SD.mkdir("/LOG");
  char caminho[32];
  int n = proximoNumeroLog();
  snprintf(g_recNome, sizeof(g_recNome), "LOG_%04d", n);
  snprintf(caminho, sizeof(caminho), "/LOG/%s.csv", g_recNome);
  g_arq = SD.open(caminho, FILE_WRITE);
  if (!g_arq) {
    g_sdEstado = SD_ERRO;
    return false;
  }
  g_arq.print("t_ms,rpm,kmh,comb_pct,comb_mv,marcha,neutro\n");
  g_recLinhas = 0;
  g_recInicioMs = millis();
  g_sdEstado = SD_GRAVANDO;
  return true;
}

static void fechaArquivo() {
  if (g_arq) {
    g_arq.flush();
    g_arq.close();
  }
  if (g_sdEstado == SD_GRAVANDO) g_sdEstado = SD_PRONTO;
}

static void tarefaSd(void *) {
  g_sdEstado = montaSd() ? SD_PRONTO : SD_AUSENTE;
  Serial.printf("SD %s\n", g_sdEstado == SD_PRONTO ? "ok" : "ausente");
  char buf[512];
  size_t n = 0;
  uint32_t ultFlush = millis();
  for (;;) {
    int cmd = g_recComando;
    g_recComando = 0;
    if (cmd == 1 && g_sdEstado != SD_GRAVANDO) {
      if (abreArquivo()) ultFlush = millis();
      xQueueReset(g_filaRec);
      n = 0;
    } else if (cmd == 2 && g_sdEstado == SD_GRAVANDO) {
      if (n) g_arq.write((const uint8_t *)buf, n);
      n = 0;
      fechaArquivo();
    }

    Amostra a;
    while (xQueueReceive(g_filaRec, &a, 0) == pdTRUE) {
      if (g_sdEstado != SD_GRAVANDO) continue;
      n += snprintf(buf + n, sizeof(buf) - n, "%lu,%.0f,%.1f,%.1f,%.0f,%c,%u\n", (unsigned long)(a.t_ms - g_recInicioMs), a.rpm,
                    a.kmh, a.comb, a.comb_mv, a.marcha, (unsigned)a.neutro);
      g_recLinhas++;
      if (n > sizeof(buf) - 80) {
        if (g_arq.write((const uint8_t *)buf, n) != n) g_sdEstado = SD_ERRO;
        n = 0;
      }
    }
    if (g_sdEstado == SD_GRAVANDO) {
      if (n) {
        if (g_arq.write((const uint8_t *)buf, n) != n) g_sdEstado = SD_ERRO;
        n = 0;
      }
      if (millis() - ultFlush >= REC_FLUSH_MS) {
        g_arq.flush();
        ultFlush = millis();
      }
    }
    if (g_sdEstado == SD_ERRO && g_arq) {  // cartão tirado ou cheio no meio da gravação
      g_arq.close();
      n = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

inline void iniciaGravador() {
  g_filaRec = xQueueCreate(256, sizeof(Amostra));
  xTaskCreatePinnedToCore(tarefaSd, "sd", 8192, nullptr, 1, nullptr, 0);
}

// chamado a cada volta do loop: manda uma amostra a cada REC_PERIODO_MS enquanto grava
inline void gravaAmostra(uint32_t agoraMs, float rpm, float kmh, float comb, char marcha, bool neutro) {
  static uint32_t ult = 0;
  if (g_sdEstado != SD_GRAVANDO || agoraMs - ult < REC_PERIODO_MS) return;
  ult = agoraMs;
  Amostra a = {agoraMs, rpm, kmh, comb, g_combMv, marcha, (uint8_t)neutro};
  if (xQueueSend(g_filaRec, &a, 0) != pdTRUE) g_recPerdidas++;
}

// Decide quando gravar. Nunca liga sozinha: só começa quando o piloto toca na caixa REC (ou aperta o BOOT) e para no
// próximo toque. Única parada automática, por segurança: motor parado por 5 min.
class ControleRec {
 public:
  void passo(float rpm, float dt, bool toque) {
    if (rpm < REC_RPM_PARADO) tParado_ += dt; else tParado_ = 0;
    bool gravando = g_sdEstado == SD_GRAVANDO;
    if (toque) {
      g_recComando = gravando ? 2 : 1;
      return;
    }
    if (gravando && tParado_ >= REC_ESPERA_PARA_S) g_recComando = 2;
  }

 private:
  float tParado_ = 0;
};