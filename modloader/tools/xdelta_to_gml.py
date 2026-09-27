"""xdelta_to_gml.py - turn an xdelta data.win mod into loose GML for the DELTARUNE modloader.

Usage:
  python xdelta_to_gml.py <game_dir> <mod_name> <chapter_dir>=<patch.xdelta> [...]
  e.g. python xdelta_to_gml.py E:/SteamLibrary/steamapps/common/DELTARUNE LocalMultiplayer \
         chapter2_windows=Chapter2LocalMultiplayerVer6_253.xdelta

For each chapter: applies the xdelta to a temp copy of the vanilla data.win, decompiles
both with UndertaleModCli, and writes only the code entries that differ to
<game_dir>/mods/<mod_name>/<chapter_dir>/code/<name>.gml. The patched data.win is
deleted afterwards; nothing in the game folder except mods/<mod_name> is touched.
Also reports non-code differences (asset counts) that loose GML cannot carry.

Needs: <game_dir>/mods/tools/utmt/UndertaleModCli.exe and xdelta3 on PATH or XDELTA3 env.
"""
import os, re, shutil, subprocess, sys, tempfile, glob

def run(cmd):
    r = subprocess.run(cmd, stdin=subprocess.DEVNULL, capture_output=True, text=True, errors="replace")
    if r.returncode != 0:
        sys.exit(f"failed: {' '.join(cmd)}\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
    return r.stdout

def load_gml(d):
    return {os.path.basename(p)[:-4]: open(p, encoding="utf-8", errors="replace").read()
            for p in glob.glob(os.path.join(d, "**", "*.gml"), recursive=True)}

def info(cli, win):
    out = run([cli, "info", win])
    return dict((k.strip(), int(v)) for v, k in re.findall(r"(\d+) ([A-Za-z][A-Za-z ]+?)(?=,|\r?\n)", out))

def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    game, mod = sys.argv[1], sys.argv[2]
    cli = os.path.join(game, "mods", "tools", "utmt", "UndertaleModCli.exe")
    xd = os.environ.get("XDELTA3", "xdelta3")
    for spec in sys.argv[3:]:
        chap, patch = spec.split("=", 1)
        vanilla = os.path.join(game, chap, "data.win")
        with tempfile.TemporaryDirectory() as tmp:
            patched = os.path.join(tmp, "patched.win")
            run([xd, "-d", "-f", "-s", vanilla, patch, patched])
            run([cli, "dump", vanilla, "-o", os.path.join(tmp, "van"), "-c", "UMT_DUMP_ALL"])
            run([cli, "dump", patched, "-o", os.path.join(tmp, "mod"), "-c", "UMT_DUMP_ALL"])
            van, new = load_gml(os.path.join(tmp, "van")), load_gml(os.path.join(tmp, "mod"))
            changed = sorted(k for k in new if van.get(k) != new[k])
            removed = sorted(set(van) - set(new))
            out = os.path.join(game, "mods", mod, chap, "code")
            if os.path.isdir(out):
                shutil.rmtree(out)
            os.makedirs(out)
            for k in changed:
                with open(os.path.join(out, k + ".gml"), "w", encoding="utf-8", newline="\n") as f:
                    f.write(new[k])
            vi, mi = info(cli, vanilla), info(cli, patched)
            diffs = {k: (vi.get(k), mi.get(k)) for k in set(vi) | set(mi)
                     if vi.get(k) != mi.get(k) and k not in ("Code Entries", "Variables", "Strings", "Code locals", "Functions")}
            print(f"{chap}: {len(changed)} code entries -> {out}"
                  f" ({sum(1 for k in changed if k not in van)} new), {len(removed)} removed in patch (ignored)")
            if diffs:
                print(f"  WARNING non-code differences not carried by loose GML: {diffs}")

if __name__ == "__main__":
    main()
