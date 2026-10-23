"""Generates the engine's Unicode property tables and conformance test files (milestone 9 bis, part 4).

    python tools/text/fetch_text_data.py     # once: the Unicode Character Database
    python tools/text/make_unicode_tables.py

Reads the UCD in tools/text/ucd/ and writes:

- src/moteur/private/moteur/unicode_tables.inc: for every code point its Bidi_Class, Line_Break,
  General_Category, Grapheme_Cluster_Break, Indic_Conjunct_Break, East Asian width (F, W or H),
  Extended_Pictographic and Script, as a two-stage table (blocks of 128 code points); the paired
  brackets of the bidirectional algorithm; the script tags (ISO 15924). Included by unicode.cpp only.
- tests/data/unicode/*.txt.zst: the official conformance tests (BidiTest, BidiCharacterTest,
  LineBreakTest, GraphemeBreakTest), comments removed, compressed with zstd (read by
  tests/test_unicode.cpp).

The value lists below must stay in step with the enums of src/moteur/include/moteur/unicode.hpp (same
names, same order): the script stops if the data holds a value they do not list.
"""

import os
import re
import sys

try:
    from compression import zstd  # Python 3.14
except ImportError:  # pragma: no cover
    zstd = None

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
UCD = os.path.join(ROOT, "tools", "text", "ucd")
OUTPUT = os.path.join(ROOT, "src", "moteur", "private", "moteur", "unicode_tables.inc")
TESTS = os.path.join(ROOT, "tests", "data", "unicode")
MAX = 0x110000
BLOCK = 128

BIDI = ["L", "R", "AL", "EN", "ES", "ET", "AN", "CS", "NSM", "BN", "B", "S", "WS", "ON",
        "LRE", "LRO", "RLE", "RLO", "PDF", "LRI", "RLI", "FSI", "PDI"]
LINE = ["XX", "AI", "AK", "AL", "AP", "AS", "B2", "BA", "BB", "BK", "CB", "CJ", "CL", "CM", "CP", "CR", "EB",
        "EM", "EX", "GL", "H2", "H3", "HH", "HL", "HY", "ID", "IN", "IS", "JL", "JT", "JV", "LF", "NL", "NS", "NU",
        "OP", "PO", "PR", "QU", "RI", "SA", "SG", "SP", "SY", "VF", "VI", "WJ", "ZW", "ZWJ"]
CATEGORY = ["Cn", "Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl", "No", "Pc", "Pd", "Ps", "Pe",
            "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co"]
GRAPHEME = ["Other", "CR", "LF", "Control", "Extend", "ZWJ", "Regional_Indicator", "Prepend", "SpacingMark",
            "L", "V", "T", "LV", "LVT"]
CONJUNCT = ["None", "Linker", "Consonant", "Extend"]

BIDI_LONG = {"Left_To_Right": "L", "Right_To_Left": "R", "Arabic_Letter": "AL", "European_Number": "EN",
             "European_Separator": "ES", "European_Terminator": "ET", "Arabic_Number": "AN",
             "Common_Separator": "CS", "Nonspacing_Mark": "NSM", "Boundary_Neutral": "BN",
             "Paragraph_Separator": "B", "Segment_Separator": "S", "White_Space": "WS", "Other_Neutral": "ON",
             "Left_To_Right_Embedding": "LRE", "Left_To_Right_Override": "LRO", "Right_To_Left_Embedding": "RLE",
             "Right_To_Left_Override": "RLO", "Pop_Directional_Format": "PDF", "Left_To_Right_Isolate": "LRI",
             "Right_To_Left_Isolate": "RLI", "First_Strong_Isolate": "FSI", "Pop_Directional_Isolate": "PDI"}


def ranges(path, missing=True):
    """Yields (first, last, value fields) of a UCD file, @missing lines first (in file order)."""
    lines = open(os.path.join(UCD, path), encoding="utf-8").read().splitlines()
    if missing:
        for line in lines:
            m = re.match(r"#\s*@missing:\s*([0-9A-F]+)\.\.([0-9A-F]+)\s*;\s*(.*)$", line)
            if m:
                yield int(m.group(1), 16), int(m.group(2), 16), [f.strip() for f in m.group(3).split(";")]
    for line in lines:
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(";")]
        first, _, last = fields[0].partition("..")
        yield int(first, 16), int(last or first, 16), fields[1:]


def fill(table, path, convert, missing=True):
    for first, last, values in ranges(path, missing):
        value = convert(values)
        if value is None:
            continue
        for cp in range(first, last + 1):
            table[cp] = value


def index_of(names, kind):
    def convert(value):
        if value not in names:
            raise SystemExit(f"{kind}: value '{value}' is not in the list of make_unicode_tables.py (new in this UCD?)")
        return names.index(value)
    return convert


def script_tags():
    """Long script names (Scripts.txt) -> ISO 15924 tags, from PropertyValueAliases.txt."""
    tags = {}
    for line in open(os.path.join(UCD, "PropertyValueAliases.txt"), encoding="utf-8"):
        fields = [f.strip() for f in line.split("#", 1)[0].split(";")]
        if len(fields) >= 3 and fields[0] == "sc":
            tags[fields[2]] = fields[1]
            for alias in fields[3:]:
                tags[alias] = fields[1]
    return tags


def build_properties():
    bidi = [0] * MAX
    to_bidi = index_of(BIDI, "Bidi_Class")
    fill(bidi, "extracted/DerivedBidiClass.txt", lambda v: to_bidi(BIDI_LONG.get(v[0], v[0])))

    line = [0] * MAX
    fill(line, "LineBreak.txt", lambda v: index_of(LINE, "Line_Break")(v[0]))

    category = [0] * MAX
    fill(category, "extracted/DerivedGeneralCategory.txt", lambda v: index_of(CATEGORY, "General_Category")(v[0]),
         missing=False)

    grapheme = [0] * MAX
    fill(grapheme, "auxiliary/GraphemeBreakProperty.txt", lambda v: index_of(GRAPHEME, "Grapheme_Cluster_Break")(v[0]),
         missing=False)

    conjunct = [0] * MAX
    fill(conjunct, "DerivedCoreProperties.txt",
         lambda v: CONJUNCT.index(v[1]) if v[0] == "InCB" and len(v) > 1 else None, missing=False)

    east_asian = [0] * MAX
    fill(east_asian, "EastAsianWidth.txt", lambda v: 1 if v[0] in ("F", "W", "H") else 0)

    pictographic = [0] * MAX
    fill(pictographic, "emoji/emoji-data.txt", lambda v: 1 if v[0] == "Extended_Pictographic" else None, missing=False)

    tags = script_tags()
    script_list = ["Zzzz", "Zyyy", "Zinh"]  # Unknown, Common, Inherited: fixed indices 0, 1, 2
    script = [0] * MAX

    def to_script(values):
        tag = tags[values[0]]
        if tag not in script_list:
            script_list.append(tag)
        return script_list.index(tag)
    fill(script, "Scripts.txt", to_script, missing=False)

    records = []
    record_index = {}
    per_cp = [0] * MAX
    for cp in range(MAX):
        flags = conjunct[cp] | (east_asian[cp] << 2) | (pictographic[cp] << 3)
        record = (bidi[cp], line[cp], category[cp], grapheme[cp], flags, script[cp])
        index = record_index.get(record)
        if index is None:
            index = record_index[record] = len(records)
            records.append(record)
        per_cp[cp] = index
    if len(records) > 0xFFFF:
        raise SystemExit("too many distinct records for 16-bit indices")

    stage1 = []
    stage2 = []
    blocks = {}
    for start in range(0, MAX, BLOCK):
        block = tuple(per_cp[start:start + BLOCK])
        index = blocks.get(block)
        if index is None:
            index = blocks[block] = len(stage2) // BLOCK
            stage2.extend(block)
        stage1.append(index)
    return records, stage1, stage2, script_list


def brackets():
    pairs = []
    for first, _, values in ranges("BidiBrackets.txt", missing=False):
        pairs.append((first, int(values[0], 16), values[1]))
    return sorted(pairs)


def c_array(name, type_, values, per_line=16, fmt=str):
    lines = [f"constexpr {type_} {name}[{len(values)}] = {{"]
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(fmt(v) for v in values[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def write_tables():
    version = open(os.path.join(UCD, "VERSION"), encoding="utf-8").read().strip()
    records, stage1, stage2, scripts = build_properties()
    pairs = brackets()
    out = [
        "// Generated by tools/text/make_unicode_tables.py from the Unicode Character Database "
        f"{version}: do not edit.",
        "// Included by unicode.cpp only. Record fields: Bidi_Class, Line_Break, General_Category,",
        "// Grapheme_Cluster_Break, flags (bits 0-1 Indic_Conjunct_Break, bit 2 East Asian F/W/H,",
        "// bit 3 Extended_Pictographic), script index (into kScriptTags). The enums' orders are those of",
        "// moteur/unicode.hpp.",
        "",
        f'constexpr const char* kUnicodeVersion = "{version}";',
        f"constexpr std::uint32_t kBlockShift = {BLOCK.bit_length() - 1};",
        "",
        "struct UnicodeRecord {",
        "    std::uint8_t bidi, line, category, grapheme, flags, script;",
        "};",
        "",
        f"constexpr UnicodeRecord kRecords[{len(records)}] = {{",
    ]
    for r in records:
        out.append("    {" + ", ".join(str(v) for v in r) + "},")
    out.append("};")
    out.append("")
    out.append(c_array("kStage1", "std::uint16_t", stage1))
    out.append("")
    out.append(c_array("kStage2", "std::uint16_t", stage2))
    out.append("")
    tag_values = ['"' + t + '"' for t in scripts]
    out.append(c_array("kScriptTags", "const char*", tag_values, per_line=10))
    out.append("")
    out.append("struct BracketPair {")
    out.append("    char32_t code_point, pair;")
    out.append("    bool opening;")
    out.append("};")
    out.append(f"constexpr BracketPair kBrackets[{len(pairs)}] = {{")
    for cp, pair, kind in pairs:
        out.append(f"    {{0x{cp:04X}, 0x{pair:04X}, {'true' if kind == 'o' else 'false'}}},")
    out.append("};")
    text = "\n".join(out) + "\n"
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as file:
        file.write(text)
    size = len(records) * 6 + len(stage1) * 2 + len(stage2) * 2
    print(f"{os.path.relpath(OUTPUT, ROOT)}: UCD {version}, {len(records)} records, "
          f"{len(stage2) // BLOCK} blocks, {len(scripts)} scripts, {len(pairs)} brackets, ~{size // 1024} KiB of tables")


def write_tests():
    if zstd is None:
        raise SystemExit("Python 3.14 or later is needed (compression.zstd) to write the test files")
    os.makedirs(TESTS, exist_ok=True)
    for source in ("BidiTest.txt", "BidiCharacterTest.txt", "auxiliary/LineBreakTest.txt",
                   "auxiliary/GraphemeBreakTest.txt"):
        lines = []
        for line in open(os.path.join(UCD, source), encoding="utf-8"):
            if source.startswith("auxiliary/"):
                line = line.split("#", 1)[0]
            line = line.rstrip()
            if line and not line.startswith("#"):
                lines.append(line)
        data = ("\n".join(lines) + "\n").encode("utf-8")
        target = os.path.join(TESTS, os.path.basename(source) + ".zst")
        with open(target, "wb") as file:
            file.write(zstd.compress(data, level=19))
        print(f"{os.path.relpath(target, ROOT)}: {len(lines)} lines, {os.path.getsize(target) // 1024} KiB")


def main():
    if not os.path.isfile(os.path.join(UCD, "VERSION")):
        print("the UCD is missing: run tools/text/fetch_text_data.py first", file=sys.stderr)
        return 1
    write_tables()
    write_tests()
    return 0


if __name__ == "__main__":
    sys.exit(main())
