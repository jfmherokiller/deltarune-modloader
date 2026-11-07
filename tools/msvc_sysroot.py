#!/usr/bin/env python3
"""Build a case-insensitive MSVC + Windows SDK sysroot for clang-cl on Linux.

Linux file systems are case-sensitive, but MSVC/SDK headers and libraries are
referenced with arbitrary casing (`#include <Windows.h>` vs `windows.h`,
`kernel32.Lib` vs `kernel32.lib`). This script makes a symlink farm over an
existing installation (for example a mounted Windows partition) in the same
layout `xwin splat` produces, so build scripts accept either:

    crt/include            crt/lib/x86_64
    sdk/include/{ucrt,um,shared,winrt}
    sdk/lib/{ucrt,um}/x86_64

Nothing is copied or downloaded; the farm only points at files you already have.
Without a Windows install, use xwin instead (it downloads the same files after
you accept Microsoft's license):  xwin --accept-license splat --output ~/.xwin

Usage:
    python3 tools/msvc_sysroot.py --windows-root /mnt/c --output ~/.cache/msvc-sysroot
    python3 tools/msvc_sysroot.py --vc ".../VC/Tools/MSVC/14.44.35207" \\
        --sdk ".../Windows Kits/10" --output ~/.cache/msvc-sysroot
"""
import argparse
import os
import re
import shutil
import sys
from pathlib import Path

INCLUDE_RE = re.compile(rb'^[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"]', re.M)
HEADER_EXT = {'.h', '.hpp', '.hxx', '.inl', '.ipp', ''}


def version_key(name):
    return [int(p) if p.isdigit() else p for p in re.split(r'[.]', name)]


def newest(directory, pattern=None):
    if not directory.is_dir():
        return None
    entries = [p for p in directory.iterdir() if p.is_dir() and (pattern is None or re.fullmatch(pattern, p.name))]
    return max(entries, key=lambda p: version_key(p.name)) if entries else None


def detect(windows_root):
    root = Path(windows_root)
    vc = sdk = None
    for program_files in ('Program Files', 'Program Files (x86)'):
        vs = root / program_files / 'Microsoft Visual Studio'
        if not vs.is_dir():
            continue
        for year in sorted(vs.iterdir(), reverse=True):
            for edition in (year.iterdir() if year.is_dir() else []):
                tools = newest(edition / 'VC' / 'Tools' / 'MSVC', r'[0-9.]+')
                if tools and (tools / 'include').is_dir() and (vc is None or version_key(tools.name) > version_key(vc.name)):
                    vc = tools
    kits = root / 'Program Files (x86)' / 'Windows Kits' / '10'
    if (kits / 'Include').is_dir():
        sdk = kits
    return vc, sdk


def link(target, alias):
    if alias.exists() or alias.is_symlink():
        return
    alias.symlink_to(target)


def farm(source, dest, include_names):
    """Mirror `source` into `dest`: real directories, symlinked files, case aliases."""
    dest.mkdir(parents=True, exist_ok=True)
    for entry in sorted(source.iterdir()):
        if entry.is_dir():
            sub = dest / entry.name
            farm(entry, sub, include_names)
            if entry.name.lower() != entry.name:
                link(entry.name, dest / entry.name.lower())
            continue
        names = {entry.name, entry.name.lower()}
        stem, ext = os.path.splitext(entry.name)
        if ext.lower() == '.lib':
            names |= {stem + '.lib', stem.lower() + '.lib', stem.upper() + '.LIB', stem + '.Lib'}
        names |= include_names.get(entry.name.lower(), set())
        for name in names:
            link(entry, dest / name)


def scan_includes(roots):
    """Leaf-name casings that headers actually #include (lowercase key -> casings)."""
    names = {}
    for root in roots:
        for path in root.rglob('*'):
            if not path.is_file() or path.suffix.lower() not in HEADER_EXT:
                continue
            try:
                data = path.read_bytes()
            except OSError:
                continue
            for match in INCLUDE_RE.finditer(data):
                leaf = match.group(1).decode('latin-1').replace('\\', '/').rsplit('/', 1)[-1]
                names.setdefault(leaf.lower(), set()).add(leaf)
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--windows-root', help='mounted Windows system drive (autodetects VS and the SDK)')
    parser.add_argument('--vc', help='VC/Tools/MSVC/<version> directory')
    parser.add_argument('--sdk', help='Windows Kits/10 directory')
    parser.add_argument('--sdk-version', help='SDK version (default: newest with um/x64 libraries)')
    parser.add_argument('--output', required=True)
    parser.add_argument('--force', action='store_true', help='replace an existing output directory')
    args = parser.parse_args()

    vc, sdk = (Path(args.vc) if args.vc else None), (Path(args.sdk) if args.sdk else None)
    if args.windows_root:
        found_vc, found_sdk = detect(args.windows_root)
        vc, sdk = vc or found_vc, sdk or found_sdk
    if not vc or not (vc / 'include').is_dir() or not (vc / 'lib' / 'x64').is_dir():
        sys.exit('MSVC tools not found (pass --vc .../VC/Tools/MSVC/<version>)')
    if not sdk or not (sdk / 'Include').is_dir():
        sys.exit('Windows SDK not found (pass --sdk ".../Windows Kits/10")')
    versions = [p.name for p in (sdk / 'Lib').iterdir() if (p / 'um' / 'x64').is_dir()]
    version = args.sdk_version or (max(versions, key=version_key) if versions else None)
    if not version or not (sdk / 'Include' / version / 'um').is_dir():
        sys.exit(f'No usable SDK version under {sdk} (found {versions})')

    out = Path(args.output).expanduser()
    if out.exists():
        if not args.force:
            sys.exit(f'{out} exists (use --force to rebuild it)')
        shutil.rmtree(out)

    include_roots = [vc / 'include'] + [sdk / 'Include' / version / d for d in ('ucrt', 'um', 'shared', 'winrt')]
    include_roots = [p for p in include_roots if p.is_dir()]
    names = scan_includes(include_roots)

    farm(vc / 'include', out / 'crt' / 'include', names)
    farm(vc / 'lib' / 'x64', out / 'crt' / 'lib' / 'x86_64', {})
    for d in ('ucrt', 'um', 'shared', 'winrt'):
        if (sdk / 'Include' / version / d).is_dir():
            farm(sdk / 'Include' / version / d, out / 'sdk' / 'include' / d, names)
    for d in ('ucrt', 'um'):
        farm(sdk / 'Lib' / version / d / 'x64', out / 'sdk' / 'lib' / d / 'x86_64', {})
    (out / 'SOURCE.txt').write_text(f'vc={vc}\nsdk={sdk}\nsdk_version={version}\n')
    print(f'MSVC {vc.name} + SDK {version} -> {out}')


if __name__ == '__main__':
    main()
