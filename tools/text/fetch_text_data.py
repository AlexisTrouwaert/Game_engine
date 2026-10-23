"""Downloads the data of the text system (milestone 9 bis, part 4), not kept in Git.

    python tools/text/fetch_text_data.py

- The Unicode Character Database 18.0.0 (https://www.unicode.org/Public/18.0.0/ucd/, Unicode
  License v3), about 6 MB, unpacked into tools/text/ucd/. tools/text/make_unicode_tables.py
  reads it to generate src/moteur/private/moteur/unicode_tables.inc (kept in Git, so building the
  engine needs no download) and the conformance test files of tests/data/unicode/.
- Fallback fonts from Google's Noto family (SIL Open Font License 1.1), with their license, into
  assets/fonts/noto/: Noto Sans Arabic (0.8 MB) and Noto Sans SC (simplified Chinese, 17.8 MB).
  They are the sandbox's and the slice's fallback fonts; the engine itself only needs the file
  names a font stack lists (assets_moteur/ keeps Inter only).
- The Unicode CLDR 48.2.3 (https://github.com/unicode-org/cldr-json, Unicode License v3), about
  0.3 MB, into tools/text/cldr/ (milestone 9 bis, part 5): the plural rules (cardinal and ordinal)
  of every language, and the number symbols and patterns of the languages in CLDR_LOCALES.
  tools/text/make_locale_tables.py generates src/moteur/private/moteur/locale_tables.inc from them
  (kept in Git).

Each download is checked against the SHA-256 of the file this list was made with: a source that
changed its file is reported rather than used. Files already present are not downloaded again.
"""

import hashlib
import io
import os
import sys
import urllib.request
import zipfile

USER_AGENT = {"User-Agent": "moteur-engine-text-data/1.0"}
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

UCD_VERSION = "18.0.0"
UCD_URL = f"https://www.unicode.org/Public/{UCD_VERSION}/ucd/UCD.zip"
UCD_SHA256 = "7b3e555514060b92290d154f53655c5eb0fa62b16eb04c03434ff72d1a66a0d8"
UCD_DIR = os.path.join(ROOT, "tools", "text", "ucd")
# The files of UCD.zip the generator and the tests read.
UCD_FILES = [
    "UnicodeData.txt",
    "BidiBrackets.txt",
    "BidiMirroring.txt",
    "LineBreak.txt",
    "EastAsianWidth.txt",
    "Scripts.txt",
    "ScriptExtensions.txt",
    "PropertyValueAliases.txt",
    "DerivedCoreProperties.txt",
    "emoji/emoji-data.txt",
    "auxiliary/GraphemeBreakProperty.txt",
    "auxiliary/LineBreakTest.txt",
    "auxiliary/GraphemeBreakTest.txt",
    "extracted/DerivedBidiClass.txt",
    "extracted/DerivedGeneralCategory.txt",
    "extracted/DerivedJoiningType.txt",
    "BidiTest.txt",
    "BidiCharacterTest.txt",
]

# google/fonts at the commits that last changed these folders (2026-03).
NOTO_ARABIC = "https://raw.githubusercontent.com/google/fonts/8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5/ofl/notosansarabic/"
NOTO_SC = "https://raw.githubusercontent.com/google/fonts/a85815a42757630ce188fdad368c2dfc444d4773/ofl/notosanssc/"
FONTS = [
    (NOTO_ARABIC + "NotoSansArabic%5Bwdth,wght%5D.ttf",
     "63111b5b2e074dd48cc67692e0a2726d86ee94c1c37fe8598257b7b4e87e869e", "NotoSansArabic.ttf"),
    (NOTO_ARABIC + "OFL.txt", "07fc70bfeb985cc1a87a8587d0a0c80bab11c86c9dc3fd95b6f0cb332f983e96", "OFL_NotoSansArabic.txt"),
    (NOTO_SC + "NotoSansSC%5Bwght%5D.ttf",
     "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da", "NotoSansSC.ttf"),
    (NOTO_SC + "OFL.txt", "1c05c68c34f9708415aada51f17e1b0092d2cea709bf4a94cd38114f9e73d7d9", "OFL_NotoSansSC.txt"),
]
FONT_DIR = os.path.join(ROOT, "assets", "fonts", "noto")

CLDR_VERSION = "48.2.3"
CLDR_BASE = f"https://raw.githubusercontent.com/unicode-org/cldr-json/{CLDR_VERSION}/cldr-json/"
CLDR_DIR = os.path.join(ROOT, "tools", "text", "cldr")
# The languages whose number formats the engine knows (the plural rules cover every language).
CLDR_LOCALES = ["ar", "cs", "da", "de", "el", "en", "es", "fa", "fi", "fr", "he", "hi", "hu", "id", "it", "ja",
                "ko", "nb", "nl", "pl", "pt", "ro", "ru", "sv", "th", "tr", "uk", "vi", "zh"]
CLDR_FILES = ["cldr-core/supplemental/plurals.json", "cldr-core/supplemental/ordinals.json"] + \
             [f"cldr-numbers-full/main/{locale}/numbers.json" for locale in CLDR_LOCALES]
# SHA-256 of each file of CLDR_FILES (--print-hashes prints them).
CLDR_SHA256 = {
    "cldr-core/supplemental/plurals.json": "6c0a48e9bcfc25856f90202f703c2c7f89c105d6868f7712a943a6ed2dcbe8f4",
    "cldr-core/supplemental/ordinals.json": "8a945afc483a4f677fb5641f08a2ccc62948ee05f316d21996be6561c2f83155",
    "cldr-numbers-full/main/ar/numbers.json": "ce31f4202130247af5c81d7bb13894cbe40cba642b441ab409df19af3f9fe773",
    "cldr-numbers-full/main/cs/numbers.json": "a982c4c839bb38efcad62bf1fb52839556aaa53111d4abafcd3287a682f8af4b",
    "cldr-numbers-full/main/da/numbers.json": "52c788d31ef79bf28657cd842ea1af6807c7f94ecd243af90941ed8093da27db",
    "cldr-numbers-full/main/de/numbers.json": "e8ab22c6d7a1ed1f1fc71c4bf0da7ba57a32a46c7ead5ad8e36f40563c18c00f",
    "cldr-numbers-full/main/el/numbers.json": "3ab681d2fc74096ee421c6224474d69e70ed58f81b5a1a9cc326aa23dfa29092",
    "cldr-numbers-full/main/en/numbers.json": "07a58e473f6d28dcc5d5562539d3ab5c8fbe37e435414969e3a2672525a80d5f",
    "cldr-numbers-full/main/es/numbers.json": "c479642ce277f82b1c40ea0d7092cc5894a38767fb6c7d33c47fbef26eed3c0b",
    "cldr-numbers-full/main/fa/numbers.json": "e5070b15bd9a788a4447bfb6bfa47516a7dad220b7eb3f540ab5701d63fb3d93",
    "cldr-numbers-full/main/fi/numbers.json": "b50c0109dd3e7456b112a8cfd7166b5e73e114f92cd20cf9003d71c42cbccc49",
    "cldr-numbers-full/main/fr/numbers.json": "f18f89da2e35b477626c21577e509583cd7831e651caccef2d82799b5c8c93e8",
    "cldr-numbers-full/main/he/numbers.json": "514ebcf87ae5fc6c113d58070210fc13985e926a0d0cc0bc33d1046be9f6eb76",
    "cldr-numbers-full/main/hi/numbers.json": "19ec7c61931c7fae1834d41c6a9ca9d1b06af4a38d0de92b7d578204ae99b439",
    "cldr-numbers-full/main/hu/numbers.json": "1239df89f7c10584d817195cd143de897f1dbea9e4413b2ab690b7c12b99ab99",
    "cldr-numbers-full/main/id/numbers.json": "987609412c51e5cc6f11a70ecad5038fa46254b098e66e3d1689c2668f051b7a",
    "cldr-numbers-full/main/it/numbers.json": "0b28ad235651ece39b60584a347df7fad6d9f4a916ceb405b89d5acdb1e1766b",
    "cldr-numbers-full/main/ja/numbers.json": "5892d4e7a0a6768eb10f4d471c8b15c1642f30eaccf5e44d17bb1ce17f08a376",
    "cldr-numbers-full/main/ko/numbers.json": "2d6014a9d294f011f93524d6c642a1263b4e7f80f490a46dce5ea404800f5edf",
    "cldr-numbers-full/main/nb/numbers.json": "b76e5509e3406fd34fd6b91d1a50e86378bde3c45cf2156decde4f92872a410a",
    "cldr-numbers-full/main/nl/numbers.json": "13d755dac1f0b9fd314d7cee083ddc530a924d3797978ce4a254c13656ad83b8",
    "cldr-numbers-full/main/pl/numbers.json": "746b197d4b4920ece76d1eab6a56790da12608e01bef98e74b5ae5b9132877e2",
    "cldr-numbers-full/main/pt/numbers.json": "a9ffb67f72a8c8d185e0d12100ebe131c9b49115d2b75001fcf7782b7d63afdc",
    "cldr-numbers-full/main/ro/numbers.json": "e2fa10e4ea3494f0eda7c2102ecb808f53af95eb50fec07a73bf366f2b6fc395",
    "cldr-numbers-full/main/ru/numbers.json": "e6f4cbfea69b3b3a21c0858b94f7b4cd42d0fcb60782ca94f39c92187251ea9f",
    "cldr-numbers-full/main/sv/numbers.json": "ddac28bb8212b28dacc9eebbedf1d3a2254ee12150f6150a36bd5c309b9e0efc",
    "cldr-numbers-full/main/th/numbers.json": "26e45e9e5ef83e01225d927acce57104a76ea4db60b6088ae20251a61ad38f49",
    "cldr-numbers-full/main/tr/numbers.json": "88d5658ec7e6f1cb6f9056986ceae636b3d4b0cb07199a433074d6d21da3d204",
    "cldr-numbers-full/main/uk/numbers.json": "381561ae3773e06016eac499492628dffbacfeb176dd44907ebdbc92fad41285",
    "cldr-numbers-full/main/vi/numbers.json": "1c3159b73e1fa10943ead03a88efca618c07846a6bb3034e63035cc2a4fe9c3f",
    "cldr-numbers-full/main/zh/numbers.json": "dbdbc861785efb5e6d77f90cfd0f838f0369e6ab4ea2e093ca1fb07655f23eaf",
}


def download(url, sha256):
    with urllib.request.urlopen(urllib.request.Request(url, headers=USER_AGENT)) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if sha256 == "print":
        print(f"  {url}: sha256 {digest}")
    elif digest != sha256:
        raise RuntimeError(f"{url}: SHA-256 {digest}, expected {sha256} (the source changed its file)")
    return data


def fetch_ucd(check):
    version_file = os.path.join(UCD_DIR, "VERSION")
    if os.path.isfile(version_file) and open(version_file, encoding="utf-8").read().strip() == UCD_VERSION and all(
            os.path.isfile(os.path.join(UCD_DIR, f)) for f in UCD_FILES):
        print(f"UCD {UCD_VERSION}: already there")
        return
    print(f"UCD {UCD_VERSION}: downloading {UCD_URL}")
    data = download(UCD_URL, check or UCD_SHA256)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for name in UCD_FILES:
            target = os.path.join(UCD_DIR, name)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as out:
                out.write(archive.read(name))
    with open(version_file, "w", encoding="utf-8") as out:
        out.write(UCD_VERSION + "\n")


def fetch_fonts(check):
    os.makedirs(FONT_DIR, exist_ok=True)
    for url, sha256, name in FONTS:
        target = os.path.join(FONT_DIR, name)
        if os.path.isfile(target):
            print(f"{name}: already there")
            continue
        print(f"{name}: downloading")
        data = download(url, check or sha256)
        with open(target, "wb") as out:
            out.write(data)


def fetch_cldr(check):
    for name in CLDR_FILES:
        target = os.path.join(CLDR_DIR, name)
        if os.path.isfile(target):
            continue
        sha = check or CLDR_SHA256.get(name)
        if sha is None:
            raise RuntimeError(f"{name}: no SHA-256 in CLDR_SHA256 (run with --print-hashes)")
        data = download(CLDR_BASE + name, sha)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as out:
            out.write(data)
    with open(os.path.join(CLDR_DIR, "VERSION"), "w", encoding="utf-8") as out:
        out.write(CLDR_VERSION + "\n")
    print(f"CLDR {CLDR_VERSION}: {len(CLDR_FILES)} files")


def main():
    # --print-hashes: download without checking and print the SHA-256 (to update this list).
    check = "print" if "--print-hashes" in sys.argv else None
    try:
        fetch_ucd(check)
        fetch_fonts(check)
        fetch_cldr(check)
    except (OSError, RuntimeError, zipfile.BadZipFile) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
