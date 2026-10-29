# Mercury Painel

Painel do carro da equipe **Mercury** (Fórmula SAE UTFPR Londrina, Elétrica e Eletrônica), feito em C++/Arduino para a placa **CYD ESP32-2432S028** com tela de 2,8" (320x240, paisagem).

## O que o painel faz

- Mostra RPM (arco e LEDs de troca), velocidade, marcha estimada e nível de combustível.
- Grava os sensores em cartão microSD (CSV, cerca de 20 amostras por segundo) ao tocar na caixa **REC**, no canto superior esquerdo. O botão BOOT da placa também liga e desliga a gravação.
- Para de gravar sozinho se o motor ficar parado por 5 minutos.
- Exibe o logo da Mercury na tela de boot.

Os logs ficam em `/LOG/LOG_0001.csv`, `LOG_0002.csv` e assim por diante, com as colunas `t_ms, rpm, kmh, comb_pct, comb_mv, marcha, neutro`.

## Estrutura

| Pasta/arquivo | Conteúdo |
| --- | --- |
| `painel/` | Sketch Arduino (`painel.ino`) e os módulos `.h` (tela, toque, sensores, SD, simulação, logo) |
| `binarios_prontos/` | O programa já compilado (modo demo), para gravar sem compilar |
| `logo/` | Imagem do logo e o script que gera `painel/logo_mercury.h` |
| `gravar_binarios.ps1` | Grava `binarios_prontos/` na placa pelo USB |
| `LEIAME.txt` | Notas originais da equipe |

## Modos

- **`MODO_DEMO 1` (padrão):** o carro "anda sozinho", sem sensores. Serve para testar tela, toque e cartão.
- **`MODO_DEMO 0`:** lê os sensores reais. Defina `#define MODO_DEMO 0` no início de `painel.ino`, ou compile com `-DMODO_DEMO=0`.

> O modo com sensores ainda **não foi testado no carro**.

## Hardware

**Sensores (modo `MODO_DEMO 0`)**, em `painel/entradas.h` e `painel/rpm_cdi.h`:

| Pino | Função |
| --- | --- |
| IO27 | RPM, saída do optoacoplador PC817 que lê o CDI (3,3 V) |
| IO22 | Sensor Hall da roda dianteira (A3144) |
| IO35 | Boia de combustível, por divisor de tensão (entrada analógica) |
| IO16 | Neutro (opcional, desligado por padrão: `USAR_NEUTRO 0`) |

**Pinos da placa CYD usados:**

- Tela ST7789: SCLK 14, MOSI 13, MISO 12, DC 2, CS 15, backlight IO21
- Cartão SD: SCK 18, MISO 19, MOSI 23, CS 5
- Toque XPT2046 (lido por software, para não conflitar com o SD): CLK 25, MOSI 32, MISO 39, CS 33

## Como compilar

Requisitos:

- Arduino IDE ou `arduino-cli`
- Plataforma **esp32 (Espressif) 2.0.17**, placa "ESP32 Dev Module"
- Biblioteca **LovyanGFX 1.2.34**

No Arduino IDE, abra `painel/painel.ino` e clique em **Carregar**. Pela linha de comando:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 painel
```

Para o modo com sensores:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 --build-property "build.extra_flags=-DMODO_DEMO=0" painel
```

## Como gravar na placa

Esta placa só aceita gravação com o botão **BOOT** apertado: segure BOOT, espere o gravador conectar e solte quando começar a escrever. Depois, aperte reset ou reconecte o USB.

Para gravar sem compilar (Windows, PowerShell):

```powershell
.\gravar_binarios.ps1 -Porta COM8
```

Troque `COM8` pela porta da sua placa.

## Trocar o logo

Coloque a imagem em `logo/logo_mercury_recorte.png` e rode (precisa do Pillow):

```bash
python logo/gerar_logo_h.py
```

Isso refaz `painel/logo_mercury.h`.

## A calibrar

- `CIRCUNFERENCIA_M` e `IMAS_NA_RODA` (`entradas.h`)
- `COMB_MV_VAZIO` e `COMB_MV_CHEIO` (use a coluna `comb_mv` do log)
- `PULSOS_POR_ROTACAO` (`rpm_cdi.h`)
- Relações de marcha (`simulacao.h`, hoje ilustrativas)
- `RPM_MAX_ESCALA`, `FAIXA_AMARELA` e `FAIXA_VERMELHA` (`painel.ino`), valores provisórios a definir com a equipe do motor

## Autor

Vinicius Luiz Ferreira de Mattos, equipe Mercury Formula Team.
