#!/usr/bin/env python3
"""Make the Mac app icon, pc/mac/icon.icns, from the picture the game shows at its start (demo/intro_img.c).

    python3 tools/make_mac_icon.py [pc/mac/icon.icns]

The cat's face is cropped square, scaled up and put in the rounded square macOS draws app icons in (the
artwork fills about 80% of the canvas, the rest is clear margin, as in Apple's icon grid). Needs Python 3 and
Pillow (Pillow writes the .icns itself, so this runs on any machine, not only a Mac).
"""
import re
import sys
from PIL import Image, ImageDraw, ImageFilter

SRC = "demo/intro_img.c"
OUT = sys.argv[1] if len(sys.argv) > 1 else "pc/mac/icon.icns"
W, H = 180, 240                    # the size of the picture in intro_img.c
FACE = (33, 43, 138, 148)          # the square that holds the cat's face and the chicken's head
CANVAS, ART, RADIUS = 1024, 824, 0.2237   # Apple's icon grid: the artwork is 824 of 1024, corners 22.37% of its width

words = [int(w, 16) for w in re.findall(r"0x([0-9a-fA-F]{4})", open(SRC).read())]
assert len(words) == W * H, "expected %d pixels in %s, found %d" % (W * H, SRC, len(words))
pic = Image.new("RGB", (W, H))
px = pic.load()
for i, w in enumerate(words):
    r, g, b = (w >> 11) & 31, (w >> 5) & 63, w & 31
    px[i % W, i // W] = (round(r * 255 / 31), round(g * 255 / 63), round(b * 255 / 31))

# scale up in two steps with a light sharpen, so the 5-6-5 dithering doesn't turn to mush
face = pic.crop(FACE)
face = face.resize((face.width * 3, face.height * 3), Image.LANCZOS)
face = face.resize((ART, ART), Image.LANCZOS).filter(ImageFilter.UnsharpMask(radius=2, percent=60, threshold=2))

# the rounded square, drawn 4x too big and shrunk so its edge is smooth
SS = 4
mask = Image.new("L", (ART * SS, ART * SS), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, ART * SS - 1, ART * SS - 1), radius=int(ART * SS * RADIUS), fill=255)
mask = mask.resize((ART, ART), Image.LANCZOS)

icon = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
off = (CANVAS - ART) // 2
icon.paste(face, (off, off), mask)

sizes = [(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)]   # (a photo this busy is about 1.4 MB at these sizes)
icon.save(OUT, format="ICNS", sizes=sizes)
print("wrote %s" % OUT)
