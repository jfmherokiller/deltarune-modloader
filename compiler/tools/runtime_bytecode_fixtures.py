#!/usr/bin/env python3
"""runtime_bytecode_fixtures.py - pair the runner's fixed-up bytecode with the file bytecode it came from.

usage: runtime_bytecode_fixtures.py <served data.win> <capture dir> <out dir> [--source <mod code dir>]

<served data.win>  the exact data.win the runner loaded. A DR_OUT_PIPE capture (8-byte length prefix,
                   as written by LooseModCompiler with DR_OUT_PIPE=<regular file>) is accepted too.
<capture dir>      RunnerCompiler named-capture output (capture.tsv + <name>.bin), i.e.
                   <game>/mods/aurie/runnercompiler/capture
<out dir>          written: runtime/ (capture copied), offline/ (same entries cut from the data.win,
                   pre-fixup, plus offline.tsv), listings/<entry>.txt (word-by-word side by side),
                   links.json (relocations and runtime ids, see bytecode_fixup.py).
--source           folder with the gml_*.gml sources, quoted at the top of each listing.

The offline bytes are what Underanalyzer (LooseModCompiler) emitted; the runtime bytes are the same
buffer after the runner's CODE-chunk load fixup. Child function entries (gml_Script_*) share their
parent's buffer: both point at the same bytes, the child starts at its 'child offset'.
"""
import os, shutil, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_runtime_bytecode import OPS, code_entries, instruction_words, load_data, safe_name  # noqa: E402
from bytecode_fixup import build_links, dump_links, read_tsv  # noqa: E402


def listing(name, offline, runtime, meta, source):
    out = [f"# {name}", f"# file offset 0x{meta[0]:x}, length {meta[1]}, locals {meta[2]}, args {meta[3]}, "
           f"child offset {meta[4]}", "# columns: byte offset, opcode, operand word index, file word, runtime word"]
    if source:
        out += ["#", "# source:"] + ["#   " + l for l in source.rstrip().splitlines()]
    out.append("")
    differing = 0
    for idx, op, size in instruction_words(offline):
        for k in range(size):
            a = struct.unpack_from("<I", offline, 4 * (idx + k))[0]
            b = struct.unpack_from("<I", runtime, 4 * (idx + k))[0]
            mark = "  *" if a != b else ""
            differing += a != b
            label = OPS.get(op, hex(op)) if k == 0 else ""
            out.append(f"{4 * (idx + k):5}  {label:<9} w{k}  {a:08x}  {b:08x}{mark}")
    out.append("")
    out.append(f"# {differing} differing word(s)")
    return "\n".join(out) + "\n"


def main():
    args = sys.argv[1:]
    source_dir = None
    if "--source" in args:
        i = args.index("--source"); source_dir = args[i + 1]; del args[i:i + 2]
    if len(args) != 3:
        sys.exit(__doc__)
    served, capture_dir, out_dir = args
    d = load_data(served)
    entries = code_entries(d, with_meta=True)
    rows = read_tsv(os.path.join(capture_dir, "capture.tsv"))
    for sub in ("runtime", "offline", "listings"):
        os.makedirs(os.path.join(out_dir, sub), exist_ok=True)
    shutil.copy(os.path.join(capture_dir, "capture.tsv"), os.path.join(out_dir, "runtime", "capture.tsv"))
    offline_rows = ["name\tfile_offset\tlength\tlocals\targs\tchild_offset\truntime_length\tdiffering_words"]
    for row in sorted(rows, key=lambda r: r["name"]):
        name = row["name"]
        if name not in entries:
            print(f"not in data.win: {name}"); continue
        off, length, locals_, args_, child = entries[name]
        runtime = open(os.path.join(capture_dir, safe_name(name) + ".bin"), "rb").read()
        offline = d[off:off + len(runtime)]
        shutil.copy(os.path.join(capture_dir, safe_name(name) + ".bin"), os.path.join(out_dir, "runtime", safe_name(name) + ".bin"))
        open(os.path.join(out_dir, "offline", safe_name(name) + ".bin"), "wb").write(offline)
        source = None
        if source_dir:
            stem = name.replace("gml_Script_", "gml_GlobalScript_")
            path = os.path.join(source_dir, stem + ".gml")
            source = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        text = listing(name, offline, runtime, (off, length, locals_, args_, child), source)
        open(os.path.join(out_dir, "listings", safe_name(name) + ".txt"), "w", encoding="utf-8").write(text)
        differing = sum(offline[i:i + 4] != runtime[i:i + 4] for i in range(0, len(runtime), 4))
        offline_rows.append(f"{name}\t0x{off:x}\t{length}\t{locals_}\t{args_}\t{child}\t{len(runtime)}\t{differing}")
        print(f"{name}: {len(runtime)} bytes, {differing} differing word(s)")
    open(os.path.join(out_dir, "offline", "offline.tsv"), "w", encoding="utf-8").write("\n".join(offline_rows) + "\n")
    links = build_links(d, out_dir)
    with open(os.path.join(out_dir, "links.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(dump_links(links))
    print(f"links.json: {sum(len(e['relocs']) for e in links['entries'].values())} relocations")


if __name__ == "__main__":
    main()
