"""Compares the engine's capture of a pose with Blender's render of it (see compare_pose.py).

    python tools/blender/compare_pose_images.py engine.png blender.png comparison

writes comparison_side_by_side.png (engine on the left, Blender on the right) and
comparison_overlay.png (grey where both have the character, red where only the engine has it,
blue where only Blender has it), and prints how well the two silhouettes overlap (intersection over
union), how many pixels only one of them covers, and the mean colour of each where both do.
Needs Pillow (pip install pillow).

The engine's silhouette is every pixel that differs from its background (a green the knight does
not have, read in its top left corner); Blender's is its alpha. A pose that matches gives an
overlap near 1: what is left is the edges (anti-aliasing) and the small differences of the two
samplings.
"""

import sys

from PIL import Image, ImageDraw


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    engine_path, blender_path, prefix = sys.argv[1:]
    engine = Image.open(engine_path).convert("RGB")
    blender = Image.open(blender_path).convert("RGBA")
    if engine.size != blender.size:
        raise SystemExit(f"sizes differ: engine {engine.size}, Blender {blender.size}")
    width, height = engine.size
    background = engine.getpixel((2, 2))

    engine_pixels = engine.load()
    blender_pixels = blender.load()
    overlay = Image.new("RGB", engine.size, (0, 0, 0))
    overlay_pixels = overlay.load()
    both = only_engine = only_blender = 0
    sums = [[0, 0, 0], [0, 0, 0]]
    for y in range(height):
        for x in range(width):
            e = engine_pixels[x, y]
            in_engine = max(abs(e[c] - background[c]) for c in range(3)) > 12
            in_blender = blender_pixels[x, y][3] > 128
            if in_engine and in_blender:
                both += 1
                overlay_pixels[x, y] = (150, 150, 150)
                for c in range(3):
                    sums[0][c] += e[c]
                    sums[1][c] += blender_pixels[x, y][c]
            elif in_engine:
                only_engine += 1
                overlay_pixels[x, y] = (255, 60, 40)
            elif in_blender:
                only_blender += 1
                overlay_pixels[x, y] = (40, 120, 255)

    on_background = Image.alpha_composite(Image.new("RGBA", blender.size, background + (255,)), blender).convert("RGB")
    side = Image.new("RGB", (width * 2, height))
    side.paste(engine, (0, 0))
    side.paste(on_background, (width, 0))
    draw = ImageDraw.Draw(side)
    draw.text((10, 10), "moteur", fill=(255, 255, 255))
    draw.text((width + 10, 10), "Blender", fill=(255, 255, 255))
    side.save(prefix + "_side_by_side.png")
    overlay.save(prefix + "_overlay.png")

    union = both + only_engine + only_blender
    print(f"silhouette: {both} pixels in both, {only_engine} in the engine only, {only_blender} in Blender only")
    print(f"overlap (intersection / union): {both / union:.4f}" if union else "no silhouette found")
    if both:
        means = [tuple(round(s / both) for s in total) for total in sums]
        print(f"mean colour where both (sRGB): engine {means[0]}, Blender {means[1]}")
    print(f"written {prefix}_side_by_side.png and {prefix}_overlay.png")


if __name__ == "__main__":
    main()
