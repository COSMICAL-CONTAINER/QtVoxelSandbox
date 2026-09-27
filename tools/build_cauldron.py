#!/usr/bin/env python3
"""Generate the cauldron (Cauldron) wall tile (16x16, original procedural pixel art).

tile 206 six-face cauldron wall; inner water surface reuses the static water tile 19.
Visual: dark cast-iron body - grey-iron base, bright rim band, two hoop bands, rivet dots.
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16

IRON_BASE = np.array([74.0, 76.0, 82.0])
IRON_DARK = np.array([48.0, 50.0, 56.0])
IRON_LITE = np.array([104.0, 108.0, 116.0])
RIVET_DK = np.array([36.0, 38.0, 44.0])

def draw_cauldron():
    c = np.zeros((TS, TS, 4), np.float64)
    c[..., 0:3] = IRON_BASE
    c[..., 3] = 255.0
    c[0:TS, 0, 0:3] = IRON_LITE          # bright rim band
    c[0:TS, 1, 0:3] = IRON_DARK          # rim seam
    c[0:TS, 6, 0:3] = IRON_DARK          # upper hoop
    c[0:TS, 7, 0:3] = IRON_LITE          # hoop highlight
    c[0:TS, 11, 0:3] = IRON_DARK         # lower hoop
    c[0:TS, 12, 0:3] = IRON_LITE         # hoop highlight
    for x in (3, 8, 13):                 # rivet dots on the hoops
        c[x, 6, 0:3] = RIVET_DK
        c[x, 11, 0:3] = RIVET_DK
    c[0, 0:TS, 0:3] = IRON_DARK          # side edge shading
    c[TS - 1, 0:TS, 0:3] = IRON_DARK
    c[0:TS, 14:TS, 0:3] = IRON_DARK      # foot shadow at the bottom
    c[0:TS, TS - 1, 0:3] = RIVET_DK
    return c

def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)

if __name__ == "__main__":
    save(draw_cauldron(), "default_cauldron")
