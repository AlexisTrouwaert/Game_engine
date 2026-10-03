"""Writes assets/maps/tranche.json, the map of the playable slice (milestone 6, part 11).

The same layout as the 3D demo's map built in code (demo_wall() and the braziers of demo3d.cpp):
rooms of 10 x 10 cells whose walls have doorways every 4 cells, a brazier in the middle of each
room. As a file, the slice reads it like any map (and the map editor of milestone 7 will open it).

    python tools/maps/make_slice_map.py [size]
"""

import json
import sys
from pathlib import Path

OUT = Path(__file__).resolve().parents[2] / "assets" / "maps" / "tranche.json"


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    rows = []
    for j in range(n):
        row = []
        for i in range(n):
            wall = (i % 10 == 0 and j % 4 != 0) or (j % 10 == 0 and i % 4 != 0)  # demo_wall()
            brazier = i % 10 == 5 and j % 10 == 5
            row.append("#" if wall else "b" if brazier else ".")
        rows.append("".join(row))
    r = list(rows[2])
    r[2] = "@"  # the hero starts in the first room, away from its brazier
    rows[2] = "".join(r)
    dump = lambda o: json.dumps(o, ensure_ascii=False)
    text = "{\n"
    text += '  "version": 1,\n'
    text += '  "description": ' + dump(f"La carte de la tranche jouable : {n} x {n} cases, des salles de 10 x 10 aux murs percés de portes, un brasero au milieu de chaque salle.") + ",\n"
    text += '  "tiles": {\n'
    text += '    "brasero": {"walkable": false},\n'
    text += '    "mur": {"walkable": false, "opaque": true},\n'
    text += '    "sol": {}\n'
    text += "  },\n"
    text += '  "legend": {\n'
    text += '    ".": ["sol"],\n'
    text += '    "#": ["sol", "mur"],\n'
    text += '    "b": {"tiles": ["sol", "brasero"], "point": "brasero"},\n'
    text += '    "@": {"tiles": ["sol"], "point": "depart"}\n'
    text += "  },\n"
    text += '  "rows": [\n' + ",\n".join("    " + dump(r) for r in rows) + "\n  ]\n}\n"
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
