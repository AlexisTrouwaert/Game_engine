"""The identities of the assets, from the command line (milestone 9, part 4).

    python tools/assets/assets.py check   FOLDER...          duplicated identifiers, orphan .meta files, assets without .meta
    python tools/assets/assets.py meta    FOLDER...          gives every asset without a .meta one (a new identifier)
    python tools/assets/assets.py move    FOLDER OLD NEW     moves (renames) an asset with its .meta, then refreshes the paths
    python tools/assets/assets.py find    FOLDER... ID       the path of an identifier
    python tools/assets/assets.py refresh FOLDER...          rewrites the "path" written next to each identifier in the JSON
                                                             files of the folders (prefabs, levels) to the asset's current path

FOLDER: an assets folder (moteur/assets, ARPG/assets...); an asset's key is its path under its
folder, with '/' separators. The rules are the engine's (src/moteur/asset_registry.cpp): every file
is an asset but the .meta files, glTF buffers (.bin), the data tables (data/), documentation (.md,
.txt) and hidden files and folders. A .meta file is `{"id": "<uuid>", "import": {...}, "version": 1}`,
written as the engine writes it (keys sorted, two spaces, a final newline).

What refers to an asset does so by identifier: moving it breaks nothing. The path written next to
the identifier is for people; `move` refreshes it, and the preparation (cook) warns where it is stale.
Exit code 0 when everything is fine, 1 for problems found, 2 for a wrong command line.
"""

import json
import os
import re
import shutil
import sys
import uuid

UUID = re.compile(r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$")


def is_identified(key):
    lower = key.lower()
    if lower.endswith((".meta", ".bin", ".md", ".txt")) or lower.startswith("data/"):
        return False
    return not any(part.startswith(".") for part in key.split("/"))


def meta_text(identifier, import_settings=None):
    return json.dumps({"id": identifier, "import": import_settings or {}, "version": 1}, indent=2, sort_keys=True) + "\n"


def scan(folder):
    """(assets: key -> identifier or None, metas: keys of the .meta files without their extension)"""
    assets, metas = {}, set()
    for root, dirs, files in os.walk(folder):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for name in files:
            key = os.path.relpath(os.path.join(root, name), folder).replace("\\", "/")
            if key.endswith(".meta"):
                metas.add(key[:-5])
            elif is_identified(key):
                assets[key] = None
    for key in assets:
        if key in metas:
            try:
                with open(os.path.join(folder, key + ".meta"), encoding="utf-8") as file:
                    identifier = json.load(file).get("id", "")
                assets[key] = identifier if UUID.match(identifier) else None
            except (OSError, ValueError):
                assets[key] = None
    return assets, metas


def command_check(folders):
    problems = 0
    owners = {}
    for folder in folders:
        assets, metas = scan(folder)
        for key, identifier in sorted(assets.items()):
            if identifier is None:
                print(f"no .meta (or unreadable): {folder}/{key}")
                problems += 1
            elif identifier.lower() in owners:
                print(f"identifier {identifier} given twice: {owners[identifier.lower()]} and {folder}/{key}")
                problems += 1
            else:
                owners[identifier.lower()] = f"{folder}/{key}"
        for meta in sorted(metas - set(assets)):
            if is_identified(meta):
                print(f"orphan .meta (its asset moved without it?): {folder}/{meta}.meta")
                problems += 1
    print(f"{len(owners)} asset(s) identified, {problems} problem(s)")
    return 1 if problems else 0


def command_meta(folders):
    made = 0
    for folder in folders:
        assets, _ = scan(folder)
        for key, identifier in sorted(assets.items()):
            path = os.path.join(folder, key + ".meta")
            if identifier is None and not os.path.exists(path):
                with open(path, "w", encoding="utf-8", newline="\n") as file:
                    file.write(meta_text(str(uuid.uuid4())))
                made += 1
                print(f"made: {key}.meta")
    print(f"{made} .meta file(s) made")
    return 0


def json_files(folders):
    for folder in folders:
        for root, dirs, files in os.walk(folder):
            dirs[:] = [d for d in dirs if not d.startswith(".")]
            for name in files:
                if name.endswith(".json"):
                    yield os.path.join(root, name)


def refresh_paths(folders):
    """Rewrites {"id": ..., "path": ...} pairs whose path is not the asset's current one."""
    where = {}
    for folder in folders:
        assets, _ = scan(folder)
        for key, identifier in assets.items():
            if identifier:
                where[identifier.lower()] = key
    changed_files = 0

    def fix(value):
        changed = False
        if isinstance(value, dict):
            identifier = value.get("id")
            if isinstance(identifier, str) and identifier.lower() in where and isinstance(value.get("path"), str):
                current = where[identifier.lower()]
                if value["path"] != current:
                    value["path"] = current
                    changed = True
            for child in value.values():
                changed = fix(child) or changed
        elif isinstance(value, list):
            for child in value:
                changed = fix(child) or changed
        return changed

    for path in json_files(folders):
        try:
            with open(path, encoding="utf-8") as file:
                document = json.load(file)
        except (OSError, ValueError):
            continue
        if fix(document):
            with open(path, "w", encoding="utf-8", newline="\n") as file:
                json.dump(document, file, indent=2, ensure_ascii=False)
                file.write("\n")
            changed_files += 1
            print(f"paths refreshed: {path}")
    return changed_files


def command_move(folder, old, new):
    old, new = old.replace("\\", "/"), new.replace("\\", "/")
    source, target = os.path.join(folder, old), os.path.join(folder, new)
    if not os.path.isfile(source):
        print(f"no asset {old} in {folder}")
        return 1
    if os.path.exists(target):
        print(f"{new} exists already")
        return 1
    if not os.path.isfile(source + ".meta"):
        print(f"{old} has no .meta: give it one first (meta), or its identity is lost")
        return 1
    os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
    shutil.move(source, target)
    shutil.move(source + ".meta", target + ".meta")
    print(f"moved: {old} -> {new} (same identity)")
    refresh_paths([folder])
    return 0


def command_find(folders, identifier):
    for folder in folders:
        assets, _ = scan(folder)
        for key, found in assets.items():
            if found and found.lower() == identifier.lower():
                print(f"{folder}/{key}")
                return 0
    print(f"no asset {identifier}")
    return 1


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 2
    command, rest = args[0], args[1:]
    if command == "check":
        return command_check(rest)
    if command == "meta":
        return command_meta(rest)
    if command == "refresh":
        print(f"{refresh_paths(rest)} file(s) changed")
        return 0
    if command == "move" and len(rest) == 3:
        return command_move(*rest)
    if command == "find" and len(rest) >= 2:
        return command_find(rest[:-1], rest[-1])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
