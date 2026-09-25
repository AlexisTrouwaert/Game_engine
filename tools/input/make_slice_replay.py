"""Writes tests/data/slice_replay.json: a scripted playthrough of the playable slice (milestone 4,
part 9), replayed tick by tick by `bac_a_sable --states --replay-input` for its reference capture.

Title: "Jouer" (menu_confirm). Game: the hero walks up and right (click held), strikes (skill 1),
pauses (Escape), resumes from the pause menu (menu_confirm on "Reprendre"), walks left, strikes
again. The game freezes by itself (--freeze-after, in game ticks) and captures.

Usage: python tools/input/make_slice_replay.py
"""

import json
import pathlib

ACTIONS = ["menu_confirm", "move_to", "skill_1", "pause", "menu_down"]
TICKS = 480
POINTER_UP_RIGHT = [900.0, 250.0]  # window pixels (1280 x 720): up and right of the hero
POINTER_DOWN_LEFT = [470.0, 440.0]  # towards the creatures of the first room


def main():
    frames = [{"p": [640.0, 360.0]} for _ in range(TICKS)]

    def press(tick, action):  # down and up within the tick
        frames[tick].setdefault("b", []).append([ACTIONS.index(action), 0, 1, 1])

    def hold(first, last, action, pointer):
        index = ACTIONS.index(action)
        for tick in range(first, last):
            frames[tick]["p"] = list(pointer)
            frames[tick].setdefault("b", []).append([index, 1, 1 if tick == first else 0, 0])
        frames[last]["p"] = list(pointer)
        frames[last].setdefault("b", []).append([index, 0, 0, 1])

    press(20, "menu_confirm")                  # title: "Jouer"; the loading takes a few ticks
    hold(60, 110, "move_to", POINTER_UP_RIGHT)
    press(210, "pause")
    press(240, "menu_confirm")                 # pause: "Reprendre" (the first item)
    hold(120, 200, "move_to", POINTER_DOWN_LEFT)
    hold(260, 330, "move_to", POINTER_DOWN_LEFT)
    for tick in list(range(125, 205, 8)) + list(range(262, 370, 8)):
        press(tick, "skill_1")                 # strikes whatever is within reach

    recording = {"version": 1, "actions": ACTIONS, "axes": [False] * len(ACTIONS), "frames": frames}
    out = pathlib.Path(__file__).resolve().parents[2] / "tests" / "data" / "slice_replay.json"
    out.write_text(json.dumps(recording), encoding="utf-8")
    print(f"{out}: {TICKS} ticks")


if __name__ == "__main__":
    main()
