"""Reads a crash report of the engine (moteur/crash.hpp) with the symbols of that version
(milestone 8, part 8): each "#NN module+0xOFFSET" line of the stack gets its function, file and line.

    python tools/crash/read_report.py REPORT.txt SYMBOLS

SYMBOLS: the folder of symbols archived with the version (moteur_package puts them in
build/<preset>/packages/symbols/<game>-<version>-<os>/): on Windows the .exe and its .pdb; on macOS
the program and its .dSYM.
The version line of the report must match the symbols' (the script warns if the commit differs).

Windows: uses dbghelp.dll (part of Windows), through ctypes. macOS: uses atos (Xcode's command line
tools), with the base address the report gives for each frame.
"""

import ctypes
import os
import platform
import re
import subprocess
import sys

FRAME = re.compile(r"^#(\d+) (\S+)\+0x([0-9a-fA-F]+)(.*)$")
FRAME_MAC = re.compile(r"\(base 0x([0-9a-f]+), address 0x([0-9a-f]+)\)")


def read_windows(lines, symbols):
    from ctypes import wintypes

    dbghelp = ctypes.WinDLL("dbghelp.dll")
    process = wintypes.HANDLE(0x6D6F74)  # any unique value: no real process is read
    SYMOPT_UNDNAME, SYMOPT_DEFERRED_LOADS, SYMOPT_LOAD_LINES = 0x2, 0x4, 0x10
    dbghelp.SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES)
    if not dbghelp.SymInitializeW(process, ctypes.c_wchar_p(symbols), False):
        raise SystemExit("SymInitializeW failed")

    class SYMBOL_INFOW(ctypes.Structure):
        _fields_ = [("SizeOfStruct", wintypes.ULONG), ("TypeIndex", wintypes.ULONG), ("Reserved", ctypes.c_ulonglong * 2),
                    ("Index", wintypes.ULONG), ("Size", wintypes.ULONG), ("ModBase", ctypes.c_ulonglong),
                    ("Flags", wintypes.ULONG), ("Value", ctypes.c_ulonglong), ("Address", ctypes.c_ulonglong),
                    ("Register", wintypes.ULONG), ("Scope", wintypes.ULONG), ("Tag", wintypes.ULONG),
                    ("NameLen", wintypes.ULONG), ("MaxNameLen", wintypes.ULONG), ("Name", ctypes.c_wchar * 512)]

    class IMAGEHLP_LINEW64(ctypes.Structure):
        _fields_ = [("SizeOfStruct", wintypes.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wintypes.DWORD),
                    ("FileName", ctypes.c_wchar_p), ("Address", ctypes.c_ulonglong)]

    dbghelp.SymLoadModuleExW.restype = ctypes.c_ulonglong
    dbghelp.SymLoadModuleExW.argtypes = [wintypes.HANDLE, wintypes.HANDLE, ctypes.c_wchar_p, ctypes.c_wchar_p,
                                         ctypes.c_ulonglong, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
    dbghelp.SymFromAddrW.argtypes = [wintypes.HANDLE, ctypes.c_ulonglong, ctypes.POINTER(ctypes.c_ulonglong),
                                     ctypes.POINTER(SYMBOL_INFOW)]
    dbghelp.SymGetLineFromAddrW64.argtypes = [wintypes.HANDLE, ctypes.c_ulonglong, ctypes.POINTER(wintypes.DWORD),
                                              ctypes.POINTER(IMAGEHLP_LINEW64)]
    bases = {}
    next_base = 0x10000000

    def base_of(module):
        nonlocal next_base
        if module in bases:
            return bases[module]
        image = os.path.join(symbols, module)
        base = 0
        if os.path.exists(image):
            base = dbghelp.SymLoadModuleExW(process, None, image, None, next_base, 0, None, 0)
            next_base += 0x10000000
        bases[module] = base
        return base

    out = []
    for line in lines:
        match = FRAME.match(line)
        if not match:
            out.append(line)
            continue
        number, module, offset, rest = match.groups()
        base = base_of(module)
        if not base:
            out.append(line)
            continue
        address = base + int(offset, 16)
        symbol = SYMBOL_INFOW()
        symbol.SizeOfStruct = ctypes.sizeof(SYMBOL_INFOW) - ctypes.sizeof(ctypes.c_wchar) * 512 + ctypes.sizeof(ctypes.c_wchar)
        symbol.MaxNameLen = 511
        displacement = ctypes.c_ulonglong(0)
        text = f"#{number} {module}+0x{offset}"
        if dbghelp.SymFromAddrW(process, address, ctypes.byref(displacement), ctypes.byref(symbol)):
            text += f" {symbol.Name}"
            source = IMAGEHLP_LINEW64()
            source.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINEW64)
            column = wintypes.DWORD(0)
            if dbghelp.SymGetLineFromAddrW64(process, address, ctypes.byref(column), ctypes.byref(source)):
                text += f" ({source.FileName}:{source.LineNumber})"
        else:
            text += rest
        out.append(text)
    dbghelp.SymCleanup(process)
    return out


def read_macos(lines, symbols):
    out = []
    for line in lines:
        match = FRAME.match(line)
        where = FRAME_MAC.search(line)
        if not match or not where:
            out.append(line)
            continue
        module = match.group(2)
        candidates = [os.path.join(symbols, f"{module}.dSYM", "Contents", "Resources", "DWARF", module),
                      os.path.join(symbols, module)]
        image = next((c for c in candidates if os.path.exists(c)), None)
        if image is None:
            out.append(line)
            continue
        result = subprocess.run(["atos", "-o", image, "-l", "0x" + where.group(1), "0x" + where.group(2)],
                                capture_output=True, text=True)
        out.append(f"#{match.group(1)} {module}+0x{match.group(3)} {result.stdout.strip()}")
    return out


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    report, symbols = sys.argv[1], os.path.abspath(sys.argv[2])
    with open(report, encoding="utf-8", errors="replace") as file:
        lines = file.read().splitlines()
    version_file = os.path.join(symbols, "version.txt")
    if os.path.exists(version_file):
        with open(version_file, encoding="utf-8") as file:
            version = file.read().strip()
        engine = next((l for l in lines if l.startswith("engine: ")), "")
        if version not in engine:
            print(f"warning: symbols of '{version}', report of '{engine[len('engine: '):]}'\n")
    reader = read_windows if platform.system() == "Windows" else read_macos
    for line in reader(lines, symbols):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
