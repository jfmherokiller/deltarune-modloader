"""Assemble the release zip from built binaries. Used by CI and for local builds.

usage: python tools/package_release.py <version> <out.zip>
       env YYTK_DLL   = built YYToolkit.dll          (default external build output)
       env AURIE_DLL  = AurieCore.dll (Aurie v2.0.2)  (default modloader/third_party/aurie_bin)
       env PLUGIN_DLL = DeltaruneYYTK.dll            (optional; shipped disabled as .example)
"""
import os, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ver, out = sys.argv[1], sys.argv[2]

def need(p):
    if not os.path.isfile(p):
        sys.exit(f"missing: {p}")
    return p

files = [  # (source, path in zip)
    (need(os.path.join(ROOT, "modloader/build/version.dll")), "version.dll"),
    (need(os.environ.get("AURIE_DLL", os.path.join(ROOT, "modloader/third_party/aurie_bin/AurieCore.dll"))), "mods/native/AurieCore.dll"),
    (need(os.environ.get("YYTK_DLL", os.path.join(ROOT, "external/YYToolkit/x64/Release/YYToolkit.dll"))), "mods/aurie/YYToolkit.dll"),
    (need(os.path.join(ROOT, "modloader/README.md")), "MODLOADER_README.md"),
    (need(os.path.join(ROOT, "LICENSE")), "LICENSE.txt"),
]
if os.environ.get("PLUGIN_DLL"):
    files.append((need(os.environ["PLUGIN_DLL"]), "mods/aurie/DeltaruneYYTK.dll.example"))
for t in ("ImportLooseMod.csx", "ImportGMLFolder.csx", "ExportSprites.csx", "export_sprites.bat",
          "ExportVanillaFor.csx", "xdelta_to_gml.py", "gml_to_patches.py"):
    p = os.path.join(ROOT, "modloader/tools", t)
    if os.path.isfile(p):
        files.append((p, f"mods/tools/{t}"))

ini = """[loader]
enabled=1
; log every file the game reads (1) or only overrides (0)
log_files=0
; log each GML script/event name the first time it runs (research)
log_scripts=0
; load mods\\native\\*.dll (AurieCore) -> YYToolkit + plugins in mods\\aurie
load_aurie=1
; show the Aurie Framework console window (0 = hidden, logs still go to aurie.log)
aurie_console=0
; loose .gml compiler (default mods\\tools\\utmt\\UndertaleModCli.exe)
;utmt_cli=
gml_timeout_ms=300000
"""
utmt_note = """Put UndertaleModTool CLI 0.9.2.0 here (UndertaleModCli.exe and its files):
https://github.com/UnderminersTeam/UndertaleModTool/releases/tag/0.9.2.0  (UTMT_CLI_v0.9.2.0-Windows.zip)
It is needed for loose .gml / sprite / sound mods and for export_sprites.bat.
"""
examples = os.path.join(ROOT, "mods_examples")

os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for src, arc in files:
        z.write(src, arc)
    z.writestr("mods/modloader.ini", ini)
    z.writestr("mods/tools/utmt/PUT_UTMT_CLI_HERE.txt", utmt_note)
    z.writestr("VERSION.txt", ver + "\n")
    # example mods ship disabled ('_' prefix); rename the folder to enable
    for mod in sorted(os.listdir(examples)):
        base = os.path.join(examples, mod)
        if not os.path.isdir(base) or mod.startswith(("_", ".")):
            continue
        for dp, _, fs in os.walk(base):
            for f in sorted(fs):
                full = os.path.join(dp, f)
                rel = os.path.relpath(full, base).replace(os.sep, "/")
                z.write(full, f"mods/_{mod}/{rel}")
print(f"wrote {out}")
with zipfile.ZipFile(out) as z:
    for n in z.namelist():
        print("  " + n)
