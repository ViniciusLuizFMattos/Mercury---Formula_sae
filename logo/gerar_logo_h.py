"""Converte o logo da equipe em array RGB565 (painel/logo_mercury.h) para o boot da tela."""
import os
from PIL import Image, ImageFilter

aqui = os.path.dirname(os.path.abspath(__file__))
origem = os.path.join(aqui, "logo_mercury_recorte.png")
LARGURA = 280
im = Image.open(origem).convert("RGB")
# "formula team" tem traços de ~1 px a 280 px de largura e fica fraco e serrilhado: engrossa só essa faixa
# antes de reduzir (a partir de x=1155 fica a cauda do "y" de Mercury, que não deve engrossar).
# 7 foi escolhido comparando 7/11/15: acima disso os buracos do "o" e do "a" fecham.
Y_TAGLINE, X_FIM_TAGLINE = 572, 1155
faixa = im.crop((0, Y_TAGLINE, X_FIM_TAGLINE, im.height)).filter(ImageFilter.MaxFilter(7))
im.paste(faixa, (0, Y_TAGLINE))
h = round(im.height * LARGURA / im.width)
im = im.resize((LARGURA, h), Image.LANCZOS)
px = list(im.getdata())
vals = [((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3) for r, g, b in px]
# pushImage(uint16_t*) do LovyanGFX espera RGB565 com os 2 bytes trocados (big-endian). Sem isso o laranja sai azulado.
vals = [((v & 0xFF) << 8) | (v >> 8) for v in vals]
linhas = []
for i in range(0, len(vals), 12):
    linhas.append("  " + ", ".join(f"0x{v:04X}" for v in vals[i:i + 12]) + ",")
saida = os.path.join(aqui, "..", "painel", "logo_mercury.h")
with open(saida, "w", encoding="utf-8") as f:
    f.write("// Gerado por gerar_logo_h.py a partir de simulador_tela/assets/logo_mercury_recorte.png. Nao editar.\n")
    f.write("#pragma once\n#include <stdint.h>\n")
    f.write(f"constexpr int LOGO_W = {LARGURA};\nconstexpr int LOGO_H = {h};\n")
    f.write("const uint16_t LOGO_MERCURY[] PROGMEM = {\n" + "\n".join(linhas) + "\n};\n")
print("logo", LARGURA, "x", h, "->", os.path.getsize(saida) // 1024, "KB de fonte")

