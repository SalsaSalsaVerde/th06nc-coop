"""Generates two module-definition (.def) files from a dumpbin /exports dump
of the real steam_api64.dll:

1. `<output>.def` — forwards every export to steam_api64_orig.dll (the
   renamed real DLL). This becomes our proxy DLL's own export table.
2. `<output>_orig_imports.def` — a plain (non-forwarding) export list naming
   the module `steam_api64_orig`, used to generate an import library
   (steam_api64_orig.lib) via `lib.exe /DEF:...`. This is NOT optional:
   MSVC's LINK.exe only emits a true cross-module forwarder RVA for
   `entryname1=modulename.entrypointname` syntax if it can resolve
   `modulename` against a known import library already in the link. Without
   this, it silently falls back to local-symbol resolution on just the
   entrypointname (post-dot) and fails with a confusing "unresolved
   external symbol" naming the *target* function, not the forward. Ordinals
   are preserved here (not in the forwarding def) so they line up with the
   real DLL's actual ordinal-based exports at runtime.

Usage:
    python generate_def.py <dumpbin_exports.txt> <output_basename>

<dumpbin_exports.txt> is the raw output of:
    dumpbin /exports steam_api64.dll > dumpbin_exports.txt
"""
import re
import sys

EXPORT_LINE = re.compile(
    r"^\s*(\d+)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$"
)

FORWARD_TARGET = "steam_api64_orig"
DLL_NAME = "steam_api64"


def parse_exports(text: str) -> list[tuple[int, str]]:
    exports = []
    in_table = False
    for line in text.splitlines():
        if "ordinal hint RVA      name" in line:
            in_table = True
            continue
        if not in_table:
            continue
        if not line.strip():
            if exports:
                break
            continue
        m = EXPORT_LINE.match(line)
        if not m:
            continue
        ordinal = int(m.group(1))
        name = m.group(4)
        exports.append((ordinal, name))
    return exports


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    src_path, out_base = sys.argv[1], sys.argv[2]
    with open(src_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    exports = parse_exports(text)
    if not exports:
        print("No exports parsed - check the input file is a dumpbin /exports dump.")
        sys.exit(1)

    forward_path = f"{out_base}.def"
    with open(forward_path, "w", encoding="ascii") as f:
        f.write(f"LIBRARY {DLL_NAME}\n")
        f.write("EXPORTS\n")
        for ordinal, name in exports:
            f.write(f"    {name}={FORWARD_TARGET}.{name}\n")
    print(f"Wrote {len(exports)} forwarded exports to {forward_path}")

    imports_path = f"{out_base}_orig_imports.def"
    with open(imports_path, "w", encoding="ascii") as f:
        f.write(f"LIBRARY {FORWARD_TARGET}\n")
        f.write("EXPORTS\n")
        for ordinal, name in exports:
            f.write(f"    {name} @{ordinal}\n")
    print(f"Wrote {len(exports)} import declarations to {imports_path}")


if __name__ == "__main__":
    main()
