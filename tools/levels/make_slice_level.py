"""The playable slice's level (milestone 9, part 11): a gothic street of the ARPG's kit, built as a
level of version 3 (moteur_doc/FORMATS.md, 19) into assets/levels/tranche.level.json.

    python tools/levels/make_slice_level.py [OUTPUT]

The level is then opened, changed and saved with the level editor (editeur --level
levels/tranche.level.json); this script makes its first state, the same on every run (no random
numbers but a fixed generator), so that the slice's references can be made again from it.

The place, from south to north (y up, metres):
- a street 12 m wide (x -6 .. 6, z 0 .. 48) between two rows of houses (the facades of the kit:
  ground floor, upper floor, cornice, roofs), closed at the south by iron railings and a gate (low:
  the camera looks north over them), crossed by a gothic arch, with street lamps, crates, barrels, a barricade and an overturned cart;
- a square (x -21 .. 21, z 48 .. 76) with braziers;
- a parvis 0.84 m higher (x -12 .. 12, z 60 .. 76), reached by three stone stairs, fenced, before
  the cathedral (the kit's backdrop): the second height of the level;
- wet cobblestones painted everywhere (A, B in patches), 8 m cells;
- skeletons to fight (spawns: the "prefabs/tranche/apparition.prefab.json" instances, whose
  properties say who comes: the slice's characters table), the start point, the waypoints of the
  "traversee" measure, the ghost's place ("fantome").
"""
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUTPUT = os.path.join(ROOT, "assets", "levels", "tranche.level.json")

KIT = "models/props/facades_gothiques/"
STREET = "models/props/mobilier_rue/"
FENCE = "models/props/cloture_gothique/"
LAMP = "models/props/lampadaire_gothique.prefab.json"
PAVING = ["models/sols/sol_paves_humides_a_8m.prefab.json", "models/sols/sol_paves_humides_b_8m.prefab.json"]
SPAWN = "prefabs/tranche/apparition.prefab.json"
SOCLE = "prefabs/tranche/socle_terrasse.prefab.json"

RDC = 4.0    # ground floor height
ETAGE = 3.5  # upper floor height
MODULE = 3.0
PARVIS = 0.84  # the second height: the top of the kit's stair


class Rng:
    """A small fixed generator (the same level on every machine)."""

    def __init__(self, seed):
        self.state = seed

    def next(self):
        self.state = (self.state * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.state >> 8) / 16777216.0


def yaw(degrees):
    """A rotation about y as [w, x, y, z]."""
    half = math.radians(degrees) / 2
    return [round(math.cos(half), 6), 0.0, round(math.sin(half), 6), 0.0]


class Level:
    def __init__(self):
        self.instances = []
        self.points = []
        self.next_id = 1

    def add(self, prefab, position, degrees=0.0, scale=None, layer="default", properties=None):
        transform = {"position": [round(v, 4) for v in position], "rotation": yaw(degrees)}
        if scale is not None:
            transform["scale"] = [round(v, 4) for v in scale]
        instance = {"id": self.next_id, "prefab": prefab, "layer": layer, "transform": transform}
        if properties:
            instance["properties"] = properties
        self.instances.append(instance)
        self.next_id += 1

    def point(self, name, position):
        self.points.append({"name": name, "position": [round(v, 4) for v in position], "rotation": [1, 0, 0, 0]})


def facade(level, rng, start, direction, count, degrees, lit_windows=0.25):
    """`count` modules of houses side by side from `start` along `direction` (unit, x or z), their
    faces turned by `degrees` (0: facing +z): ground floor, upper floor, cornice, and the roof behind
    over three cells."""
    dx, dz = direction
    behind = {0: (0, -1), 180: (0, 1), 90: (-1, 0), -90: (1, 0)}[degrees]
    for i in range(count):
        cx = start[0] + dx * (MODULE * i + MODULE / 2)
        cz = start[1] + dz * (MODULE * i + MODULE / 2)
        roll = rng.next()
        if roll < 0.2:
            level.add(KIT + "mur_rdc_porte_3m.prefab.json", (cx, 0, cz), degrees, layer="facades")
            level.add(KIT + "porte_ogive.prefab.json", (cx, 0, cz), degrees, layer="facades")
        elif roll < 0.65:
            level.add(KIT + "mur_rdc_fenetre_3m.prefab.json", (cx, 0, cz), degrees, layer="facades")
            level.add(KIT + "fenetre_rdc.prefab.json", (cx, 0, cz), degrees, layer="facades")
        else:
            level.add(KIT + "mur_rdc_plein_3m.prefab.json", (cx, 0, cz), degrees, layer="facades")
        if rng.next() < 0.7:
            level.add(KIT + "mur_etage_fenetre_3m.prefab.json", (cx, RDC, cz), degrees, layer="facades")
            lit = rng.next() < lit_windows
            level.add(KIT + ("fenetre_etage_allumee" if lit else "fenetre_etage") + ".prefab.json", (cx, RDC, cz), degrees,
                      layer="facades")
            if rng.next() < 0.25:
                level.add(KIT + "balcon_fer_forge.prefab.json", (cx, RDC, cz), degrees, layer="facades")
        else:
            level.add(KIT + "mur_etage_plein_3m.prefab.json", (cx, RDC, cz), degrees, layer="facades")
        level.add(KIT + "corniche_3m.prefab.json", (cx, RDC + ETAGE, cz), degrees, layer="facades")
        # The roof: the inside of the houses is never seen.
        for depth in range(3):
            rx = cx + behind[0] * (MODULE * depth + MODULE / 2 + 0.3)
            rz = cz + behind[1] * (MODULE * depth + MODULE / 2 + 0.3)
            level.add(KIT + "toit_terrasse_3m.prefab.json", (rx, RDC + ETAGE, rz), 0, layer="toits")


def corner(level, position, degrees):
    level.add(KIT + "angle_rdc.prefab.json", (position[0], 0, position[1]), degrees, layer="facades")
    level.add(KIT + "angle_etage.prefab.json", (position[0], RDC, position[1]), degrees, layer="facades")
    level.add(KIT + "corniche_angle.prefab.json", (position[0], RDC + ETAGE, position[1]), degrees, layer="facades")


def fence(level, start, end, height=0.0):
    """Iron railings every 3 m between `start` and `end` (x, z), posts at their ends."""
    length = math.dist(start, end)
    count = max(1, round(length / MODULE))
    ux, uz = (end[0] - start[0]) / length, (end[1] - start[1]) / length
    degrees = 0 if abs(uz) < 1e-6 else 90
    for i in range(count + 1):
        px, pz = start[0] + ux * MODULE * i, start[1] + uz * MODULE * i
        level.add(FENCE + ("poteau_gargouille_a" if i % 2 == 0 else "poteau_gargouille_b") + ".prefab.json",
                  (px, height, pz), degrees, layer="decor")
        if i < count:
            level.add(FENCE + "grille_fer_forge_3m.prefab.json",
                      (px + ux * MODULE / 2, height, pz + uz * MODULE / 2), degrees, layer="decor")


def skeleton(level, rng, position, character=None):
    character = character or ("squelette_guerrier" if rng.next() < 0.45 else "squelette_sbire")
    angle = rng.next() * 2 * math.pi
    tint = 0.75 + 0.25 * rng.next()
    level.add(SPAWN, position, 0, layer="monstres", properties={
        "personnage": character,
        "couleur": [round(tint, 3), round(tint * 0.95, 3), round(tint * 0.85, 3)],
        "direction": [round(math.cos(angle), 4), round(math.sin(angle), 4)],
        "rythme": round(0.8 + 0.4 * rng.next(), 3),
        "vie": 1.0,
    })


def build():
    rng = Rng(20261018)
    level = Level()

    # The houses: the street's two rows, the square's sides.
    facade(level, rng, (-6.0, 0.0), (0, 1), 16, 90)     # west of the street, facing +x
    facade(level, rng, (6.0, 0.0), (0, 1), 16, -90)     # east of the street, facing -x
    facade(level, rng, (-21.0, 48.0), (1, 0), 5, 0)     # south of the square, west part
    facade(level, rng, (6.0, 48.0), (1, 0), 5, 0)       # south of the square, east part
    facade(level, rng, (-21.0, 48.0), (0, 1), 9, 90)    # west of the square
    facade(level, rng, (21.0, 48.0), (0, 1), 9, -90)    # east of the square
    corner(level, (-6.0, 48.0), 0)                       # where the street opens on the square
    corner(level, (6.0, 48.0), -90)
    # North of the square: the cathedral, and the houses beside it.
    level.add(STREET + "fond_cathedrale.prefab.json", (0, 0, 80.0), 180, layer="decor")
    facade(level, rng, (-21.0, 76.0), (1, 0), 2, 180)
    facade(level, rng, (15.0, 76.0), (1, 0), 2, 180)

    # The parvis: a block 0.84 m high, paved, fenced, three stairs to it.
    level.add(SOCLE, (0, 0, 68.0), 0, scale=(24.0, PARVIS - 0.01, 16.0), layer="parvis")  # under the paving
    for x in (-12.0, -4.0, 4.0):
        for z in (60.0, 68.0):
            level.add(PAVING[1 if (x, z) == (-4.0, 68.0) else 0], (x, PARVIS, z), 0, layer="parvis")
    for x in (-3.26, 0.0, 3.26):
        level.add(STREET + "escalier_pierre_3m.prefab.json", (x, 0, 60.0), 180, layer="parvis")
    fence(level, (-12.0, 60.3), (-6.0, 60.3), PARVIS)
    fence(level, (6.0, 60.3), (12.0, 60.3), PARVIS)
    fence(level, (-11.7, 60.0), (-11.7, 75.0), PARVIS)
    fence(level, (11.7, 60.0), (11.7, 75.0), PARVIS)

    # The south end: railings and a closed gate (low: the camera looks north over them).
    fence(level, (-6.0, 0.3), (-1.5, 0.3))
    fence(level, (1.5, 0.3), (6.0, 0.3))
    level.add(STREET + "portail_fer_forge.prefab.json", (0, 0, 0.3), 0, layer="decor")

    # Lamps, braziers, the arch, what clutters the street.
    for z in (6.0, 18.0, 30.0, 42.0):
        level.add(LAMP, (-4.7, 0, z), 90, layer="decor")
        level.add(LAMP, (4.7, 0, z + 6.0), -90, layer="decor")
    for x, z in ((-11.0, 62.0), (11.0, 62.0)):
        level.add(LAMP, (x, PARVIS, z), 0, layer="decor")
    for x, z, h in ((-15.0, 52.0, 0), (15.0, 52.0, 0), (-15.0, 70.0, 0), (15.0, 70.0, 0), (-8.0, 73.0, PARVIS),
                    (8.0, 73.0, PARVIS), (0.0, 10.0, 0)):
        level.add(STREET + "brasero.prefab.json", (x, h, z), 0, layer="decor", properties={"feu": True})
    level.add(STREET + "arche_gothique.prefab.json", (0, 0, 24.0), 0, layer="decor")
    level.add(STREET + "barricade_planches.prefab.json", (-2.5, 0, 36.0), 15, layer="decor")
    level.add(STREET + "charrette_renversee.prefab.json", (3.2, 0, 14.0), -30, layer="decor")
    clutter = [("caisse", -4.6, 9.0), ("caisse_grande", -4.4, 10.2), ("tonneau", -4.9, 11.3), ("sacs_grain", 4.6, 27.0),
               ("caisse", 4.8, 33.0), ("tonneau", 4.7, 34.0), ("tonneau", -16.5, 57.0), ("caisse_grande", 16.8, 64.0),
               ("sacs_grain", -17.0, 66.0), ("caisse", 17.5, 55.0)]
    for name, x, z in clutter:
        level.add(STREET + name + ".prefab.json", (x, 0, z), round(rng.next() * 60 - 30), layer="decor")
    for z in (4.0, 20.0, 40.0):
        level.add(STREET + "grille_egout.prefab.json", (0, 0.01, z), 0, layer="decor")
    for x, z, degrees in ((-6.0, 15.0, 90), (6.0, 39.0, -90), (-21.0, 60.0, 90)):
        level.add(STREET + "lierre_facade.prefab.json", (x, 0, z), degrees, layer="facades")

    # The skeletons: along the street, in the square, on the parvis (they come down its stairs).
    for z in (12.0, 16.0, 21.0, 28.0, 33.0, 38.0, 44.0):
        for x in (-3.0, 2.5):
            skeleton(level, rng, (x + rng.next() - 0.5, 0, z + rng.next() - 0.5))
    for i in range(18):
        skeleton(level, rng, (-18.0 + 36.0 * rng.next(), 0, 50.0 + 9.0 * rng.next()))
    for i in range(8):
        skeleton(level, rng, (-10.0 + 20.0 * rng.next(), PARVIS, 63.0 + 11.0 * rng.next()), "squelette_guerrier")

    # Points: the start, the measure's tour.
    level.point("start", (0.0, 0.0, 5.0))
    # The ghost (milestone 9 bis, part 2: the transparent pass) haunts the arch's west pillar: it goes
    # round it, in front of it and behind it as the camera sees them.
    level.point("fantome", (-2.3, 0.35, 24.0))
    for i, (x, y, z) in enumerate([(0, 0, 5), (-2, 0, 22), (2, 0, 30), (0, 0, 46), (-14, 0, 54), (14, 0, 56),
                                   (0, 0, 57), (0, PARVIS, 64), (-8, PARVIS, 70), (8, PARVIS, 70), (0, 0, 52), (0, 0, 8)]):
        level.point(f"tour_{i + 1:02d}", (x, y, z))

    # The ground: cobblestones over the whole place, B in patches.
    width, height, origin = 6, 11, (-24.0, -4.0)
    cells = []
    for z in range(height):
        for x in range(width):
            cells.append(2 if rng.next() < 0.3 else 1)

    return {
        "version": 3,
        "description": "The slice: a gothic street, a square, the parvis of the cathedral (milestone 9, part 11)",
        "chunk_size": 32.0,
        "next_id": level.next_id,
        "layers": [{"name": n, "visible": True, "locked": False} for n in
                   ("default", "facades", "toits", "decor", "parvis", "monstres")],
        "instances": level.instances,
        "points": level.points,
        "volumes": [],
        "ground": {"origin": list(origin), "cell_size": 8.0, "width": width, "height": height, "level": 0.0,
                   "prefabs": PAVING, "cells": cells},
        "navigation": {"agents": [{"radius": 0.35, "height": 1.8, "max_climb": 0.35, "min_ground_normal_y": 0.7071}],
                       "build": {"cell_size": 0.15, "cell_height": 0.1, "tile_size": 32.0}},
        "lighting": {"sun_yaw": 60.0, "sun_elevation": 42.0, "sun_color": [1.0, 0.86, 0.7], "sun_intensity": 2.2,
                     "sky_intensity": 0.7, "exposure": 1.0, "background": [0.04, 0.045, 0.06]},
    }


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else OUTPUT
    level = build()
    with open(output, "w", encoding="utf-8", newline="\n") as f:
        json.dump(level, f, indent=2, ensure_ascii=False)
        f.write("\n")
    spawns = sum(1 for i in level["instances"] if i["prefab"] == SPAWN)
    print(f"{output}: {len(level['instances'])} instances ({spawns} spawns), {len(level['points'])} points")


if __name__ == "__main__":
    main()
