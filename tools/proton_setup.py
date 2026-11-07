#!/usr/bin/env python3
"""proton_setup.py - make Proton/Wine load the mod loader's version.dll for DELTARUNE.

Wine prefers its builtin version.dll, so the proxy next to DELTARUNE.exe is ignored unless the
DLL override says native first. Two equivalent ways:

  1. Steam > DELTARUNE > Properties > Launch Options:  WINEDLLOVERRIDES="version=n,b" %command%
  2. This script: a per-executable override in the game's Proton prefix, so no launch options
     are needed and other games in the prefix are unaffected:
       [Software\\Wine\\AppDefaults\\DELTARUNE.exe\\DllOverrides]  "version"="native,builtin"

Usage:
    python3 tools/proton_setup.py                 # find the prefix via Steam's library folders
    python3 tools/proton_setup.py --prefix <compatdata/1671210/pfx>
    python3 tools/proton_setup.py --check | --remove

Close the game first (Wine rewrites user.reg on exit). user.reg is backed up as
user.reg.bak-modloader before the first change.
"""
import argparse
import os
import re
import shutil
import sys
import time
from pathlib import Path

APPID = "1671210"
EXE = "DELTARUNE.exe"
DLL = "version"


def steam_libraries():
    roots = [Path.home() / ".local/share/Steam", Path.home() / ".steam/steam",
             Path.home() / ".var/app/com.valvesoftware.Steam/.local/share/Steam"]
    seen = []
    for root in roots:
        vdf = root / "steamapps/libraryfolders.vdf"
        if not vdf.is_file():
            continue
        for path in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(errors="replace")):
            p = Path(path.replace("\\\\", "\\"))
            if p not in seen:
                seen.append(p)
    return seen


def find_prefix():
    # Proton keeps the prefix in the library the game is installed in.
    libs = steam_libraries()
    libs.sort(key=lambda lib: not (lib / f"steamapps/appmanifest_{APPID}.acf").is_file())
    for lib in libs:
        pfx = lib / "steamapps/compatdata" / APPID / "pfx"
        if (pfx / "user.reg").is_file():
            return pfx
    sys.exit(f"Proton prefix for app {APPID} not found; run the game once through Steam or pass --prefix")


def section_name(exe):
    return f"[Software\\\\Wine\\\\AppDefaults\\\\{exe}\\\\DllOverrides]"


def split_sections(text):
    """[(header line or None, lines)] keeping everything verbatim."""
    sections, current = [], [None, []]
    for line in text.splitlines(keepends=True):
        if line.startswith("["):
            sections.append(current)
            current = [line, []]
        else:
            current[1].append(line)
    sections.append(current)
    return sections


def game_running():
    """Any process whose executable (argv[0]) is DELTARUNE.exe, as Wine reports it."""
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            argv0 = (proc / "cmdline").read_bytes().split(b"\0", 1)[0].decode(errors="replace")
        except OSError:
            continue
        if re.split(r"[\\/]", argv0)[-1].lower() == EXE.lower():
            return True
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prefix", type=Path, help="Wine/Proton prefix (the pfx directory)")
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--check", action="store_true", help="only report the current setting")
    group.add_argument("--remove", action="store_true", help="remove the override again")
    args = ap.parse_args()

    pfx = args.prefix or find_prefix()
    reg = pfx / "user.reg"
    if not reg.is_file():
        sys.exit(f"{reg} not found")
    text = reg.read_text(encoding="utf-8", errors="surrogateescape")
    sections = split_sections(text)
    header = section_name(EXE).lower()
    hits = [s for s in sections if s[0] and s[0].lower().startswith(header)]
    value_re = re.compile(rf'^"{DLL}"=.*\n?', re.I)
    current = next((l.strip() for s in hits for l in s[1] if value_re.match(l)), None)
    print(f"prefix: {pfx}\ncurrent: {current or '(no override; Wine uses builtin version.dll)'}")
    if args.check:
        return
    if not args.remove and current and "native" in current.split("=", 1)[1].split(",")[0]:
        print("already set")
        return
    if args.remove and not current:
        print("nothing to remove")
        return
    if game_running():
        sys.exit(f"{EXE} is running; close the game first (Wine rewrites user.reg on exit)")

    for s in hits:
        s[1] = [l for l in s[1] if not value_re.match(l)]
    if args.remove:  # drop override sections that are now empty
        sections = [s for s in sections if s not in hits or any(l.strip() and not l.startswith("#") for l in s[1])]
    if not args.remove:
        line = f'"{DLL}"="native,builtin"\n'
        if hits:
            body = hits[0][1]
            at = len(body)
            while at > 0 and not body[at - 1].strip():
                at -= 1
            body.insert(at, line)
        else:
            if sections[-1][1] and sections[-1][1][-1].strip():
                sections[-1][1].append("\n")
            sections.append([f"{section_name(EXE)} {int(time.time())}\n", [line, "\n"]])
    backup = reg.with_name("user.reg.bak-modloader")
    if not backup.exists():
        shutil.copy2(reg, backup)
        print(f"backup: {backup}")
    out = "".join((h or "") + "".join(body) for h, body in sections)
    tmp = reg.with_name("user.reg.tmp-modloader")
    tmp.write_text(out, encoding="utf-8", errors="surrogateescape")
    os.replace(tmp, reg)
    print("removed override" if args.remove else f'set {EXE}: "{DLL}"="native,builtin"')


if __name__ == "__main__":
    main()
