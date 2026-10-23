"""Generates the engine's locale tables from the Unicode CLDR (milestone 9 bis, part 5).

    python tools/text/fetch_text_data.py      # once: the CLDR files
    python tools/text/make_locale_tables.py

Reads tools/text/cldr/ and writes:

- src/moteur/private/moteur/locale_tables.inc: the plural rules of every CLDR language, cardinal
  and ordinal, compiled to C++ functions (the CLDR rule syntax: operands n i v w f t e, `%`, `=`,
  `!=`, ranges, `and`, `or`), the languages mapped to them; the number symbols and patterns of the
  languages fetch_text_data.py lists (Latin digits: decimal and group separators, minus and percent
  signs, grouping sizes, minimum grouping digits, percent pattern). Included by locale.cpp only.
- tests/data/locale/plural_samples.txt: the samples CLDR gives with each rule ("@integer 2~4,
  22~24 ..."), expanded: tests/test_localization.cpp checks every one against the compiled rules.
"""

import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
CLDR = os.path.join(ROOT, "tools", "text", "cldr")
OUTPUT = os.path.join(ROOT, "src", "moteur", "private", "moteur", "locale_tables.inc")
SAMPLES = os.path.join(ROOT, "tests", "data", "locale", "plural_samples.txt")
CATEGORIES = ["zero", "one", "two", "few", "many", "other"]  # the order of moteur::PluralCategory


# --- Rule compiler -------------------------------------------------------------------------------

TOKEN = re.compile(r"\s*(\.\.|!=|=|%|,|[a-z]+|\d+)")


def tokenize(text):
    tokens = []
    pos = 0
    text = text.strip()
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            raise SystemExit(f"cannot read the rule '{text}' at {pos}")
        tokens.append(m.group(1))
        pos = m.end()
    return tokens


def compile_condition(text):
    """A CLDR condition -> a C++ boolean expression over `o` (moteur::PluralOperands)."""
    tokens = tokenize(text)
    pos = 0

    def peek():
        return tokens[pos] if pos < len(tokens) else None

    def take(expected=None):
        nonlocal pos
        token = tokens[pos]
        if expected is not None and token != expected:
            raise SystemExit(f"expected {expected}, got {token} in '{text}'")
        pos += 1
        return token

    def relation():
        operand = take()
        if operand not in ("n", "i", "v", "w", "f", "t", "e", "c"):
            raise SystemExit(f"unknown operand {operand} in '{text}'")
        operand = "e" if operand == "c" else operand
        modulo = None
        if peek() == "%":
            take("%")
            modulo = take()
        op = take()
        if op not in ("=", "!="):
            raise SystemExit(f"unknown operator {op} in '{text}'")
        items = []
        while True:
            low = take()
            if peek() == "..":
                take("..")
                high = take()
                items.append((int(low), int(high)))
            else:
                items.append((int(low), int(low)))
            if peek() != ",":
                break
            take(",")
        # n is a double (its integer test matters for ranges); the others are integers.
        if operand == "n":
            value = f"std::fmod(o.n, {modulo}.0)" if modulo else "o.n"
            tests = []
            for low, high in items:
                if low == high:
                    tests.append(f"{value} == {low}.0")
                else:
                    tests.append(f"(o.n == std::floor(o.n) && {value} >= {low}.0 && {value} <= {high}.0)")
        else:
            value = f"(o.{operand} % {modulo})" if modulo else f"o.{operand}"
            tests = []
            for low, high in items:
                tests.append(f"{value} == {low}" if low == high else f"({value} >= {low} && {value} <= {high})")
        joined = " || ".join(tests)
        expression = f"({joined})"
        return expression if op == "=" else f"!{expression}"

    def and_condition():
        parts = [relation()]
        while peek() == "and":
            take("and")
            parts.append(relation())
        return " && ".join(parts)

    ors = [and_condition()]
    while peek() == "or":
        take("or")
        ors.append(and_condition())
    if pos != len(tokens):
        raise SystemExit(f"trailing tokens in '{text}'")
    return " || ".join(f"({o})" for o in ors) if len(ors) > 1 else ors[0]


def split_rule(rule):
    """'v = 0 and i % 10 = 1 @integer 1, 21 @decimal 0.1' -> (condition, {'integer': [...], 'decimal': [...]})."""
    samples = {}
    condition = rule
    if "@" in rule:
        condition, rest = rule.split("@", 1)
        for part in ("@" + rest).split("@")[1:]:
            kind, _, values = part.partition(" ")
            samples[kind] = [v.strip() for v in values.split(",") if v.strip() and v.strip() not in ("…", "...")]
    return condition.strip(), samples


def expand_samples(values, decimal):
    """'2~4' -> 2, 3, 4; '0.0~1.5' -> 0.0, 0.1 ... 1.5 (at most 12 per range); compact forms (1c6) skipped."""
    out = []
    for v in values:
        if "c" in v or "e" in v:
            continue
        if "~" in v:
            low, high = v.split("~")
            digits = len(low.split(".")[1]) if "." in low else 0
            step = 10 ** -digits
            a, b = float(low), float(high)
            count = int(round((b - a) / step)) + 1
            for k in range(min(count, 12)):
                out.append(f"{a + k * step:.{digits}f}")
            if count > 12:
                out.append(high)
        else:
            out.append(v)
    return out


def build_rules(kind):
    data = json.load(open(os.path.join(CLDR, "cldr-core", "supplemental", "plurals.json" if kind == "cardinal" else "ordinals.json"), encoding="utf-8"))
    table = data["supplemental"][f"plurals-type-{kind}"]
    functions = []      # C++ bodies, deduplicated
    by_body = {}
    languages = {}      # language -> function index
    samples = []        # (language, kind, category, sample)
    for language in sorted(table):
        rules = table[language]
        lines = []
        for category in CATEGORIES:
            key = f"pluralRule-count-{category}"
            if key not in rules:
                continue
            condition, examples = split_rule(rules[key])
            if category != "other":
                if not condition:
                    raise SystemExit(f"{language} {category}: empty condition")
                lines.append(f"    if ({compile_condition(condition)}) return PluralCategory::{category.capitalize()};")
            for kind_of, values in examples.items():
                for sample in expand_samples(values, kind_of == "decimal"):
                    samples.append((language, kind, category, sample))
        lines.append("    return PluralCategory::Other;")
        body = "\n".join(lines)
        if body not in by_body:
            by_body[body] = len(functions)
            functions.append(body)
        languages[language] = by_body[body]
    return functions, languages, samples


# --- Number formats ----------------------------------------------------------------------------

def c_string(text):
    out = '"'
    for ch in text:
        code = ord(ch)
        if ch in '"\\':
            out += "\\" + ch
        elif 32 <= code < 127:
            out += ch
        else:
            for byte in ch.encode("utf-8"):
                out += f"\\x{byte:02x}"
            out += '""'  # a hex escape must not swallow the next character
    return out + '"'


def grouping(pattern):
    """'#,##,##0.###' -> (primary 3, secondary 2); '#,##0' -> (3, 3); no comma -> (0, 0)."""
    integer = pattern.split(".")[0]
    integer = re.sub(r"[^#0,]", "", integer)
    if "," not in integer:
        return 0, 0
    groups = integer.split(",")
    primary = len(groups[-1])
    secondary = len(groups[-2]) if len(groups) > 2 else primary
    return primary, secondary


def affixes(pattern):
    """The text before and after the number in a pattern ('#,##0 %' -> ('', ' %'))."""
    pattern = pattern.split(";")[0]
    m = re.search(r"[#0,.]+", pattern)
    return pattern[:m.start()], pattern[m.end():]


def build_numbers():
    locales = []
    numbers_dir = os.path.join(CLDR, "cldr-numbers-full", "main")
    for locale in sorted(os.listdir(numbers_dir)):
        n = json.load(open(os.path.join(numbers_dir, locale, "numbers.json"), encoding="utf-8"))["main"][locale]["numbers"]
        symbols = n["symbols-numberSystem-latn"]
        decimal_pattern = n["decimalFormats-numberSystem-latn"]["standard"]
        percent_pattern = n["percentFormats-numberSystem-latn"]["standard"]
        primary, secondary = grouping(decimal_pattern)
        prefix, suffix = affixes(percent_pattern)
        prefix = prefix.replace("%", symbols["percentSign"])
        suffix = suffix.replace("%", symbols["percentSign"])
        locales.append((locale, symbols["decimal"], symbols["group"], symbols["minusSign"], prefix, suffix, primary,
                        secondary, int(n.get("minimumGroupingDigits", "1"))))
    return locales


def main():
    if not os.path.isfile(os.path.join(CLDR, "VERSION")):
        print("the CLDR is missing: run tools/text/fetch_text_data.py first", file=sys.stderr)
        return 1
    version = open(os.path.join(CLDR, "VERSION"), encoding="utf-8").read().strip()
    out = [f"// Generated by tools/text/make_locale_tables.py from the Unicode CLDR {version}: do not edit.",
           "// Included by locale.cpp only.", "",
           f'constexpr const char* kCldrVersion = "{version}";', ""]
    all_samples = []
    for kind in ("cardinal", "ordinal"):
        functions, languages, samples = build_rules(kind)
        all_samples += samples
        for index, body in enumerate(functions):
            out.append(f"PluralCategory {kind}_{index}([[maybe_unused]] const PluralOperands& o) {{")
            out.append(body)
            out.append("}")
            out.append("")
        out.append(f"using PluralFunction = PluralCategory (*)(const PluralOperands&);")
        out.append(f"constexpr PluralFunction k{kind.capitalize()}Functions[{len(functions)}] = {{")
        out.append("    " + ", ".join(f"{kind}_{i}" for i in range(len(functions))) + ",")
        out.append("};")
        out.append(f"constexpr LanguageRule k{kind.capitalize()}Languages[{len(languages)}] = {{")
        for language, index in sorted(languages.items()):
            out.append(f'    {{"{language}", {index}}},')
        out.append("};")
        out.append("")
    numbers = build_numbers()
    out.append(f"constexpr NumberSymbols kNumberSymbols[{len(numbers)}] = {{")
    for (locale, dec, group, minus, prefix, suffix, primary, secondary, min_group) in numbers:
        out.append(f'    {{"{locale}", {c_string(dec)}, {c_string(group)}, {c_string(minus)}, {c_string(prefix)}, '
                   f'{c_string(suffix)}, {primary}, {secondary}, {min_group}}},')
    out.append("};")
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    os.makedirs(os.path.dirname(SAMPLES), exist_ok=True)
    with open(SAMPLES, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# CLDR {version} plural samples: language kind category value (generated by make_locale_tables.py)\n")
        for language, kind, category, sample in all_samples:
            f.write(f"{language} {kind} {category} {sample}\n")
    print(f"{os.path.relpath(OUTPUT, ROOT)}: CLDR {version}, {len(numbers)} number formats; "
          f"{len(all_samples)} samples in {os.path.relpath(SAMPLES, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
