"""Capy Mobile's home-screen icon: the capybara (assets/capy.png) with a MOBILE band under it.

    python make_icon.py        (needs Pillow; writes sce_sys/icon0.png)

The word is drawn with the app's own 5x7 letters, read out of src/draw.h.
"""
import re
from pathlib import Path

from PIL import Image, ImageDraw

here = Path(__file__).resolve().parent
BAND, DARK, ACCENT = 132, (0x10, 0x19, 0x23), (0x55, 0xDD, 0xB0)

table = re.search(r"glyphs\[95\]\[7\] = \{(.*?)\n\};", (here / "src" / "draw.h").read_text(), re.S).group(1)
glyphs = [[int(n) for n in row.split(",")] for row in re.findall(r"\{([\d,]+)\}", table)]
assert len(glyphs) == 95

icon = Image.open(here / "assets" / "capy.png").convert("RGB").resize((512, 512), Image.LANCZOS)
draw = ImageDraw.Draw(icon)
draw.rectangle([0, 512 - BAND, 512, 512], fill=DARK)
draw.rectangle([0, 512 - BAND, 512, 512 - BAND + 5], fill=ACCENT)

word, scale = "MOBILE", 11
width = (len(word) * 6 - 1) * scale
x0, y0 = (512 - width) // 2, 512 - BAND + 5 + (BAND - 5 - 7 * scale) // 2
for i, letter in enumerate(word):
    for row, bits in enumerate(glyphs[ord(letter) - 32]):
        for col in range(5):
            if bits & (1 << (4 - col)):
                x, y = x0 + (i * 6 + col) * scale, y0 + row * scale
                draw.rectangle([x, y, x + scale - 1, y + scale - 1], fill=ACCENT)
icon.save(here / "sce_sys" / "icon0.png")
print("wrote", here / "sce_sys" / "icon0.png")
