"""Draws the slice's application icon (milestone 8, parts 10 and 11): a sword on a dark shield.

    python tools/package/make_icon.py [OUTPUT_DIR]

Writes tranche.png (1024 x 1024, the source), tranche.ico (Windows: 16 to 256 pixels) and
tranche.icns (macOS) into OUTPUT_DIR (default art/icons/). A placeholder of the project's own
making until the game has its artwork; the packages only need these three files (moteur_package
ICON), whatever draws them.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SIZE = 1024
SCALE = 4  # drawn 4 times larger, then reduced: smooth edges


def draw():
    s = SIZE * SCALE
    image = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    # Background: a rounded square, dark red at the top to almost black at the bottom.
    gradient = Image.new("RGBA", (s, s))
    pixels = gradient.load()
    for y in range(s):
        t = y / (s - 1)
        color = (int(92 - 70 * t), int(22 - 14 * t), int(26 - 16 * t), 255)
        for x in range(s):
            pixels[x, y] = color
    mask = Image.new("L", (s, s), 0)
    margin = int(s * 0.06)
    ImageDraw.Draw(mask).rounded_rectangle((margin, margin, s - margin, s - margin), radius=int(s * 0.2), fill=255)
    image.paste(gradient, (0, 0), mask)
    d = ImageDraw.Draw(image)
    # A thin gold border inside the square.
    inset = int(s * 0.1)
    d.rounded_rectangle((inset, inset, s - inset, s - inset), radius=int(s * 0.16), outline=(196, 152, 72, 255),
                        width=int(s * 0.012))

    cx = s / 2
    # The sword, point up: blade, fuller, guard, grip, pommel.
    blade_top, blade_bottom, half = s * 0.13, s * 0.62, s * 0.045
    shadow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    offset = s * 0.015
    sd.polygon([(cx + offset, blade_top + offset), (cx + half + offset, blade_top + half * 2.4 + offset),
                (cx + half + offset, blade_bottom + offset), (cx - half + offset, blade_bottom + offset),
                (cx - half + offset, blade_top + half * 2.4 + offset)], fill=(0, 0, 0, 150))
    sd.rounded_rectangle((cx - s * 0.2 + offset, s * 0.615 + offset, cx + s * 0.2 + offset, s * 0.66 + offset),
                         radius=int(s * 0.02), fill=(0, 0, 0, 150))
    image.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(s * 0.01)))
    d = ImageDraw.Draw(image)
    d.polygon([(cx, blade_top), (cx + half, blade_top + half * 2.4), (cx + half, blade_bottom),
               (cx - half, blade_bottom), (cx - half, blade_top + half * 2.4)], fill=(214, 220, 228, 255))
    d.polygon([(cx, blade_top), (cx + half, blade_top + half * 2.4), (cx + half, blade_bottom), (cx, blade_bottom)],
              fill=(168, 176, 188, 255))  # the shaded half
    d.line([(cx, blade_top + half * 3), (cx, blade_bottom - s * 0.02)], fill=(120, 128, 140, 255), width=int(s * 0.008))
    gold, dark_gold = (212, 166, 80, 255), (150, 108, 44, 255)
    d.rounded_rectangle((cx - s * 0.2, s * 0.615, cx + s * 0.2, s * 0.66), radius=int(s * 0.02), fill=gold)
    d.rectangle((cx - s * 0.2, s * 0.64, cx + s * 0.2, s * 0.66), fill=dark_gold)
    d.rounded_rectangle((cx - s * 0.028, s * 0.66, cx + s * 0.028, s * 0.77), radius=int(s * 0.012), fill=(88, 52, 34, 255))
    for i in range(4):  # the grip's binding
        y = s * (0.675 + i * 0.025)
        d.line([(cx - s * 0.028, y), (cx + s * 0.028, y + s * 0.012)], fill=(58, 34, 22, 255), width=int(s * 0.006))
    d.ellipse((cx - s * 0.05, s * 0.76, cx + s * 0.05, s * 0.86), fill=gold, outline=dark_gold, width=int(s * 0.008))
    return image.resize((SIZE, SIZE), Image.LANCZOS)


def main():
    out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "art", "icons")
    os.makedirs(out, exist_ok=True)
    icon = draw()
    icon.save(os.path.join(out, "tranche.png"))
    icon.save(os.path.join(out, "tranche.ico"), sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48),
                                                         (64, 64), (128, 128), (256, 256)])
    icon.save(os.path.join(out, "tranche.icns"))
    for name in ("tranche.png", "tranche.ico", "tranche.icns"):
        print(name, os.path.getsize(os.path.join(out, name)), "bytes")


if __name__ == "__main__":
    main()
