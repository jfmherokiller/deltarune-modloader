"""gml_to_patches.py - shrink a loose-GML mod: turn full .gml replacements into .diff patches.

For every mods/<Mod>/<chapter>/code/<entry>.gml that also exists in vanilla, writes a unified
diff against the vanilla decompile (same UTMT decompiler settings the loader uses) when that
is smaller than the file. New entries and heavily rewritten ones stay as full .gml. Sprites/
sounds/other files are copied as-is.

Usage:
  python gml_to_patches.py <game_dir> <src_mod_dir> <out_mod_dir>
  e.g. python gml_to_patches.py "E:/SteamLibrary/steamapps/common/DELTARUNE" ^
         ".../mods/LocalMultiplayer" ".../mods/LocalMultiplayerLite"

Needs <game_dir>/mods/tools/utmt/UndertaleModCli.exe and ExportVanillaFor.csx next to this script.
Check the result by loading it: every patch should report OK in mods/modloader.gml.log.
"""
import difflib, os, shutil, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))


def export_vanilla(cli, data_win, names_dir, out_dir):
    env = dict(os.environ, DR_NAMES_DIR=names_dir, DR_OUT_DIR=out_dir)
    p = subprocess.run([cli, "load", data_win, "-s", os.path.join(HERE, "ExportVanillaFor.csx")],
                       stdin=subprocess.DEVNULL, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", env=env)
    if p.returncode != 0:
        sys.exit(f"UTMT failed on {data_win}:\n{p.stdout[-2000:]}{p.stderr[-2000:]}")


def convert_chapter(game, cli, src_root, out_root, rel, tmp):
    src_code = os.path.join(src_root, rel, "code")
    out_code = os.path.join(out_root, rel, "code")
    # copy everything except code/ unchanged
    for d, _, files in os.walk(os.path.join(src_root, rel)):
        if os.path.normcase(os.path.abspath(d)).startswith(os.path.normcase(os.path.abspath(src_code))):
            continue
        for f in files:
            dst = os.path.join(out_root, os.path.relpath(os.path.join(d, f), src_root))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(d, f), dst)
    if not os.path.isdir(src_code):
        return f"{rel}: no code/"
    os.makedirs(out_code, exist_ok=True)
    data_win = os.path.join(game, "data.win") if rel == "." else os.path.join(game, rel, "data.win")
    van_dir = os.path.join(tmp, rel.replace("\\", "_").replace("/", "_"))
    export_vanilla(cli, data_win, src_code, van_dir)
    nd = nf = other = 0
    before = after = 0
    for fn in sorted(os.listdir(src_code)):
        sp = os.path.join(src_code, fn)
        if not fn.endswith(".gml") or fn.endswith((".append.gml", ".prepend.gml")):
            shutil.copy2(sp, os.path.join(out_code, fn)); other += 1; continue
        name = fn[:-4]
        mod = open(sp, encoding="utf-8").read().replace("\r\n", "\n")
        before += len(mod.encode())
        vp = os.path.join(van_dir, fn)
        if os.path.exists(vp):
            van = open(vp, encoding="utf-8").read()
            diff = "\n".join(difflib.unified_diff(van.split("\n"), mod.split("\n"),
                                                  f"a/{fn}", f"b/{fn}", n=3, lineterm="")) + "\n"
            if len(diff) < len(mod):
                open(os.path.join(out_code, name + ".diff"), "w", encoding="utf-8", newline="\n").write(diff)
                nd += 1; after += len(diff.encode()); continue
        open(os.path.join(out_code, fn), "w", encoding="utf-8", newline="\n").write(mod)
        nf += 1; after += len(mod.encode())
    return f"{rel}: {nd} diffs, {nf} full .gml, {other} other; {before/1e6:.2f} MB -> {after/1e6:.2f} MB"


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    game, src, out = (os.path.abspath(a) for a in sys.argv[1:])
    cli = os.path.join(game, "mods", "tools", "utmt", "UndertaleModCli.exe")
    if not os.path.exists(cli):
        sys.exit(f"missing {cli}")
    if os.path.exists(out):
        sys.exit(f"{out} already exists; remove it first")
    rels = [d for d in sorted(os.listdir(src)) if os.path.isdir(os.path.join(src, d, "code")) and d.startswith("chapter")]
    if os.path.isdir(os.path.join(src, "code")):
        rels.insert(0, ".")
    with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(5) as ex:
        for line in ex.map(lambda r: convert_chapter(game, cli, src, out, r, tmp), rels):
            print(line)
    for f in os.listdir(src):   # top-level files (readme, meta)
        if os.path.isfile(os.path.join(src, f)):
            shutil.copy2(os.path.join(src, f), os.path.join(out, f))


if __name__ == "__main__":
    main()
