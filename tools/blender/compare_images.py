"""Compares the engine's capture with Blender's render of the same scene (see compare_render.py).

    python tools/blender/compare_images.py engine.png blender.png engine.png.json comparison

writes comparison_side_by_side.png (engine on the left, Blender on the right) and
comparison_difference.png (absolute difference, x4), and prints, for each object of the row, its
mean color in both images and the difference. Needs Pillow (pip install pillow).

Blender's render has a transparent background: it is laid on the engine's background colour (read
in the engine's top left corner). The objects are found in Blender's alpha, as runs of columns,
left to right, in the order of the JSON file (models, then spheres).
"""

import json
import os
import sys

from PIL import Image, ImageChops, ImageDraw


def column_runs(mask, width, height):
    """Ranges [start, end) of the columns that hold at least one pixel of an object."""
    used = [any(mask[y * width + x] for y in range(height)) for x in range(width)]
    runs, start = [], None
    for x, on in enumerate(used + [False]):
        if on and start is None:
            start = x
        elif not on and start is not None:
            runs.append((start, x))
            start = None
    return runs


def mean_color(image, mask, width, x0, x1, height):
    total, count = [0, 0, 0], 0
    pixels = image.load()
    for y in range(height):
        for x in range(x0, x1):
            if mask[y * width + x]:
                r, g, b = pixels[x, y][:3]
                total[0] += r
                total[1] += g
                total[2] += b
                count += 1
    return tuple(round(c / count) for c in total) if count else (0, 0, 0)


def main():
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    engine_path, blender_path, scene_path, prefix = sys.argv[1:]
    engine = Image.open(engine_path).convert("RGB")
    blender = Image.open(blender_path).convert("RGBA")
    if engine.size != blender.size:
        raise SystemExit(f"sizes differ: engine {engine.size}, Blender {blender.size}")
    width, height = engine.size

    background = Image.new("RGBA", blender.size, engine.getpixel((2, 2)) + (255,))
    blender_on_background = Image.alpha_composite(background, blender).convert("RGB")

    side = Image.new("RGB", (width * 2, height))
    side.paste(engine, (0, 0))
    side.paste(blender_on_background, (width, 0))
    draw = ImageDraw.Draw(side)
    draw.text((10, 10), "moteur", fill=(255, 255, 255))
    draw.text((width + 10, 10), "Blender", fill=(255, 255, 255))
    side.save(prefix + "_side_by_side.png")
    difference = ImageChops.difference(engine, blender_on_background).point(lambda v: min(255, v * 4))
    difference.save(prefix + "_difference.png")

    with open(scene_path, encoding="utf-8") as f:
        description = json.load(f)
    names = [os.path.basename(m["path"]).split("_1k")[0] for m in description["models"]]
    names += [f"sphère {i + 1}" for i in range(len(description["spheres"]))]
    mask = [a > 128 for a in blender.getchannel("A").tobytes()]
    runs = column_runs(mask, width, height)
    if len(runs) != len(names):
        print(f"{len(runs)} objects found in Blender's alpha, {len(names)} expected: names may not match")
    print(f"{'objet':<22}{'moteur (sRGB)':<18}{'Blender (sRGB)':<18}écart")
    for index, (x0, x1) in enumerate(runs):
        name = names[index] if index < len(names) else f"objet {index + 1}"
        a = mean_color(engine, mask, width, x0, x1, height)
        b = mean_color(blender_on_background, mask, width, x0, x1, height)
        gap = tuple(p - q for p, q in zip(a, b))
        print(f"{name:<22}{str(a):<18}{str(b):<18}{gap}")
    print(f"written {prefix}_side_by_side.png and {prefix}_difference.png")


if __name__ == "__main__":
    main()
