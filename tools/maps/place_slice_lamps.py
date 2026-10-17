"""Places a street lamp in each room of the playable slice's map (assets/maps/tranche.json).

    python tools/maps/place_slice_lamps.py

A street lamp is a tile, like a brazier: `lampadaire` (not walkable), written `l` in the rows, its
cell a point `lampadaire` where the slice stands the lamp's model and its light (demo3d.cpp). Each
room gets one in the first free corner cell of a fixed list: a floor cell (".") with no decor
object within 1 m and no monster within 0.7 m (rooms without one get none). Running it again first
removes the lamps it placed, then places them again: the same map. The rest of the file (its
objects, its formatting) is kept as it is.
"""

import json
import math
import sys
from pathlib import Path

MAP = Path(__file__).resolve().parents[2] / "assets" / "maps" / "tranche.json"
ROOM = 10
# Corners first, then the cells next to them (local coordinates in a room).
CANDIDATES = [(1, 1), (9, 1), (1, 9), (9, 9), (1, 2), (2, 1), (8, 1), (9, 2), (1, 8), (2, 9), (9, 8), (8, 9)]


def main():
    text = MAP.read_text(encoding="utf-8")
    data = json.loads(text)
    if data.get("version") != 2:
        sys.exit(f"{MAP}: expected a version 2 map")
    rows = [row.replace("l", ".") for row in data["rows"]]  # the lamps of a previous run go
    objects = data.get("objects", [])
    decor = [(o["x"], o["z"]) for o in objects if o["type"] != "monstre"]
    monsters = [(o["x"], o["z"]) for o in objects if o["type"] == "monstre"]
    height, width = len(rows), len(rows[0])
    placed = 0
    for rj in range(height // ROOM):
        for ri in range(width // ROOM):
            for li, lj in CANDIDATES:
                i, j = ri * ROOM + li, rj * ROOM + lj
                if rows[j][i] != ".":
                    continue
                cx, cz = i + 0.5, j + 0.5
                if any(math.hypot(x - cx, z - cz) < 1.0 for x, z in decor) or \
                        any(math.hypot(x - cx, z - cz) < 0.7 for x, z in monsters):
                    continue
                rows[j] = rows[j][:i] + "l" + rows[j][i + 1:]
                placed += 1
                break

    # The tile and its legend entry, written in the file's own style, once.
    if '"lampadaire": {"walkable": false}' not in text:
        old = '    "brasero": {"walkable": false},\n'
        if text.count(old) != 1:
            sys.exit("the tiles of the map are not as expected (\"brasero\" line)")
        text = text.replace(old, old + '    "lampadaire": {"walkable": false},\n')
    if '"l": {"tiles": ["sol", "lampadaire"], "point": "lampadaire"}' not in text:
        old = '    "b": {"tiles": ["sol", "brasero"], "point": "brasero"}'
        if text.count(old) != 1:
            sys.exit("the legend of the map is not as expected (\"b\" line)")
        text = text.replace(old, old + ',\n    "l": {"tiles": ["sol", "lampadaire"], "point": "lampadaire"}')
    for old_row, new_row in zip(data["rows"], rows):
        if old_row != new_row:
            line_old = "    " + json.dumps(old_row)
            if text.count(line_old) < 1:
                sys.exit("a row of the map is not written as expected")
    # Rows are replaced in order (several rows can be identical).
    head, sep, tail = text.partition('  "rows": [\n')
    body, sep2, rest = tail.partition("\n  ]")
    lines = body.split(",\n")
    if len(lines) != len(rows):
        sys.exit("the rows of the map are not one per line")
    body = ",\n".join("    " + json.dumps(row, ensure_ascii=False) for row in rows)
    text = head + sep + body + sep2 + rest
    description = '"description": "La carte de la tranche jouable : 100 x 100 cases, des salles de 10 x 10 aux murs percés de portes, un brasero au milieu de chaque salle."'
    if description in text:
        text = text.replace(description, description[:-2] + ', un lampadaire dans un coin."')
    MAP.write_text(text, encoding="utf-8", newline="\n")
    json.loads(text)  # still valid
    print(f"{MAP}: {placed} street lamps")


if __name__ == "__main__":
    main()
