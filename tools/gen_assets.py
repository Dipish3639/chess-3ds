#!/usr/bin/env python3
"""Generate the chess piece sprites, home-menu icon and banner for Chess 3DS.

Run from the repository root:  python3 tools/gen_assets.py
Requires Pillow and the DejaVu Sans font (has the Unicode chess glyphs).
"""
import math
import os
import struct
import wave

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
SIZE = 30  # sprite size in pixels (one board square on the bottom screen)

# piece letter -> (outline glyph, solid glyph); only the solid one is drawn
GLYPHS = {
    "k": ("♔", "♚"),
    "q": ("♕", "♛"),
    "r": ("♖", "♜"),
    "b": ("♗", "♝"),
    "n": ("♘", "♞"),
    "p": ("♙", "♟"),
}


def render_piece(letter, white, size=SIZE):
    scale = 4  # supersample, then downscale for smooth edges
    big = size * scale
    font = ImageFont.truetype(FONT, int(big * 0.92))
    solid_ch = GLYPHS[letter][1]
    glyph = Image.new("L", (big, big), 0)
    d = ImageDraw.Draw(glyph)
    bx0, by0, bx1, by1 = d.textbbox((0, 0), solid_ch, font=font)
    x = (big - (bx1 - bx0)) / 2 - bx0
    y = (big - (by1 - by0)) / 2 - by0
    d.text((x, y), solid_ch, font=font, fill=255)
    glyph = glyph.point(lambda v: 255 if v > 127 else 0)

    # Silhouette = glyph with its interior holes filled in.
    outside = glyph.copy()
    ImageDraw.floodfill(outside, (0, 0), 128)
    silhouette = outside.point(lambda v: 0 if v == 128 else 255)
    holes = Image.composite(Image.new("L", glyph.size, 0), silhouette, glyph)

    border = silhouette.filter(ImageFilter.MaxFilter(scale * 2 + 1))
    if white:
        body, line = (250, 247, 236, 255), (25, 25, 25, 255)
    else:
        body, line = (34, 34, 38, 255), (225, 225, 220, 255)
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    img.paste(Image.new("RGBA", img.size, line), (0, 0), border)
    img.paste(Image.new("RGBA", img.size, body), (0, 0), silhouette)
    img.paste(Image.new("RGBA", img.size, line), (0, 0), holes)
    return img.resize((size, size), Image.LANCZOS)


def board_image(w, h, sq):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 255))
    d = ImageDraw.Draw(img)
    for r in range(h // sq + 1):
        for c in range(w // sq + 1):
            col = (238, 216, 178, 255) if (r + c) % 2 == 0 else (181, 136, 99, 255)
            d.rectangle([c * sq, r * sq, c * sq + sq - 1, r * sq + sq - 1], fill=col)
    return img


def main():
    gfx = os.path.join(ROOT, "gfx")
    assets = os.path.join(ROOT, "assets")
    os.makedirs(gfx, exist_ok=True)
    os.makedirs(assets, exist_ok=True)

    # Sprite order must match source/gfx.h: white KQRBNP, black KQRBNP
    names = []
    for white in (True, False):
        for letter in "kqrbnp":
            name = ("w" if white else "b") + letter + ".png"
            render_piece(letter, white).save(os.path.join(gfx, name))
            names.append(name)
    with open(os.path.join(gfx, "sprites.t3s"), "w") as f:
        f.write("--atlas -f rgba8888 -z auto\n")
        for n in names:
            f.write(n + "\n")

    # Home-menu icon (48x48)
    icon = board_image(48, 48, 12)
    icon.alpha_composite(render_piece("n", True, 40), (4, 4))
    icon.save(os.path.join(ROOT, "icon.png"))

    # Banner (256x128) shown on the home menu top screen
    banner = Image.new("RGBA", (256, 128), (0, 0, 0, 0))
    b = board_image(256, 128, 16)
    mask = Image.new("L", (256, 128), 0)
    ImageDraw.Draw(mask).rounded_rectangle([8, 8, 247, 119], radius=14, fill=255)
    banner.paste(b, (0, 0), mask)
    ImageDraw.Draw(banner).rounded_rectangle([8, 8, 247, 119], radius=14,
                                              outline=(60, 40, 25, 255), width=3)
    banner.alpha_composite(render_piece("k", True, 72), (40, 28))
    banner.alpha_composite(render_piece("q", False, 72), (144, 28))
    banner.save(os.path.join(assets, "banner.png"))

    # Short two-note jingle for the banner (16-bit mono, 22050 Hz)
    rate = 22050
    frames = bytearray()
    for freq, dur in ((523.25, 0.18), (783.99, 0.35)):
        n = int(rate * dur)
        for i in range(n):
            env = min(1.0, i / 300) * (1 - i / n)
            v = int(9000 * env * math.sin(2 * math.pi * freq * i / rate))
            frames += struct.pack("<h", v)
    with wave.open(os.path.join(assets, "banner.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))


if __name__ == "__main__":
    main()
