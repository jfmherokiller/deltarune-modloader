#!/usr/bin/env python3
"""compare_runtime_bytecode.py - diff the runner's in-memory bytecode against the data.win it was served.

usage: compare_runtime_bytecode.py <data.win as served> <dump dir with index.tsv or capture.tsv + *.bin>

The RunnerCompiler spike plugin dumps CCode->VMBuffer bytecode. The runner points VMBuffer.bytecode
into the loaded data.win and rewrites operands in place while loading the CODE chunk. This tool
finds each dumped script's bytes in the served data.win (CODE entry: bytecode = entry + 12 +
relAddr, GMS2.3+ layout; child functions such as gml_Script_* share their parent's buffer, and the
runner's VMBuffer for them is the whole parent buffer, not parent + child offset) and reports which instruction words changed, grouped by opcode, so the
fixup that injected bytecode must reproduce can be read off directly.

It then classifies every operand word by kind (variable reference by instance type and reference
type, function reference builtin/script, branch offset, string index, immediate) using the data.win
reference chains, applies the fixup rules (bytecode_fixup.py) and lists every word they do not
reproduce. Exit status 1 if there is any.
"""
import collections, os, struct, sys

# GameMaker bytecode 15+ opcodes (UndertaleModLib UndertaleInstruction.Opcode).
OPS = {0x07: "conv", 0x08: "mul", 0x09: "div", 0x0A: "rem", 0x0B: "mod", 0x0C: "add", 0x0D: "sub",
       0x0E: "and", 0x0F: "or", 0x10: "xor", 0x11: "neg", 0x12: "not", 0x13: "shl", 0x14: "shr",
       0x15: "cmp", 0x45: "pop", 0x82: "dup", 0x86: "dup?", 0x9C: "ret", 0x9D: "exit", 0x9E: "popz",
       0xB6: "b", 0xB7: "bt", 0xB8: "bf", 0xBA: "pushenv", 0xBB: "popenv", 0xC0: "push", 0xC1: "pushloc",
       0xC2: "pushglb", 0xC3: "pushbltn", 0x84: "pushi", 0xD9: "call", 0x99: "callv", 0xFF: "break"}
DATA_TYPE_SIZE = {0: 8, 1: 4, 2: 4, 3: 8, 4: 4, 5: 4, 6: 4, 15: 2}  # double,float,int,long,bool,var,string,int16


def load_data(path):
    """data.win bytes; also accepts a DR_OUT_PIPE capture (8-byte length prefix + data.win)."""
    d = open(path, "rb").read()
    if d[:4] != b"FORM" and d[8:12] == b"FORM":
        d = d[8:]
    return d


def chunks(d):
    out, o = {}, 8
    while o + 8 <= len(d):
        name, size = d[o:o + 4].decode("latin1"), struct.unpack_from("<I", d, o + 4)[0]
        out[name] = (o + 8, size); o += 8 + size
    return out


def cstring(d, ptr):
    end = d.index(b"\0", ptr)
    return d[ptr:end].decode("utf-8", "replace")


def code_entries(d, with_meta=False):
    """name -> (bytecode file offset, length). The offset is the start of the (parent) buffer, which is
    what the runner's VMBuffer points at; with_meta adds (locals, args, child offset)."""
    start, size = chunks(d)["CODE"]
    count = struct.unpack_from("<I", d, start)[0]
    entries = {}
    for i in range(count):
        e = struct.unpack_from("<I", d, start + 4 + 4 * i)[0]
        name_ptr, length, locals_, args = struct.unpack_from("<IIHH", d, e)
        rel = struct.unpack_from("<i", d, e + 12)[0]
        offset = struct.unpack_from("<I", d, e + 16)[0]
        entries[cstring(d, name_ptr)] = (e + 12 + rel, length, locals_, args, offset) if with_meta else (e + 12 + rel, length)
    return entries


def safe_name(name):
    return "".join(c if c.isalnum() or c in "_-" else "_" for c in name)


def instruction_words(code):
    """Yield (word index, opcode, words in instruction)."""
    i, n = 0, len(code) // 4
    while i < n:
        w = struct.unpack_from("<I", code, 4 * i)[0]
        op, t1 = w >> 24, (w >> 16) & 0xF
        size = 1
        if op in (0xC0, 0xC1, 0xC2, 0xC3, 0x84):        # push family: operand follows unless int16
            size = 1 if t1 == 15 else 1 + DATA_TYPE_SIZE.get(t1, 4) // 4
        elif op in (0x45, 0xD9):                         # pop var / call: reference operand
            size = 2
        yield i, op, size
        i += size


def main():
    served, dump_dir = sys.argv[1], sys.argv[2]
    d = load_data(served)
    entries = code_entries(d)
    index = os.path.join(dump_dir, "index.tsv")
    if not os.path.exists(index):
        index = os.path.join(dump_dir, "capture.tsv")
    rows = [l.rstrip("\r\n").split("\t") for l in open(index, encoding="utf-8")][1:]
    same = changed = missing = 0
    by_op = collections.Counter(); examples = collections.defaultdict(list); size_mismatch = []
    for name, length, *_ in rows:
        if name not in entries:
            missing += 1; continue
        off, flen = entries[name]
        runtime = open(os.path.join(dump_dir, safe_name(name) + ".bin"), "rb").read()
        if len(runtime) != flen:
            size_mismatch.append((name, flen, len(runtime))); continue
        original = d[off:off + flen]
        if original == runtime:
            same += 1; continue
        changed += 1
        for idx, op, size in instruction_words(original):
            for k in range(size):
                a = original[4 * (idx + k):4 * (idx + k) + 4]; b = runtime[4 * (idx + k):4 * (idx + k) + 4]
                if a != b:
                    key = f"{OPS.get(op, hex(op))} word{k}"
                    by_op[key] += 1
                    if len(examples[key]) < 4:
                        examples[key].append(f"{name}@{4 * (idx + k)}: file {a[::-1].hex()} -> mem {b[::-1].hex()}")
    print(f"{len(rows)} dumped: {same} identical, {changed} rewritten, {missing} not in data.win, {len(size_mismatch)} size mismatch")
    for name, a, b in size_mismatch[:5]:
        print(f"  size {name}: file {a} mem {b}")
    for key, n in by_op.most_common():
        print(f"{n:6}  {key}")
        for ex in examples[key]:
            print("          " + ex)
    skip = {s[0] for s in size_mismatch}
    pairs = [(name, d[entries[name][0]:entries[name][0] + entries[name][1]],
              open(os.path.join(dump_dir, safe_name(name) + ".bin"), "rb").read(), entries[name][0])
             for name, *_ in rows if name in entries and name not in skip]
    sys.exit(1 if classify(d, pairs) else 0)


INSTANCE_TYPES = {0: "stack", -1: "self", -2: "other", -3: "all", -4: "noone", -5: "global", -6: "builtin", -7: "local",
                  -9: "stacktop", -15: "arg", -16: "static"}
REF_TYPES = {0xA0: "normal", 0x80: "stacktop", 0x00: "array", 0x10: "arraypushaf", 0x90: "arraypopaf", 0xE0: "instance"}


def classify(d, pairs):
    """Explain every operand word by kind, using the data.win reference chains (bytecode_fixup):
    rewritten words must sit on a VARI/FUNC reference and equal the fixup rule's result; every other
    operand word (branch offsets, string indices, immediates) must be unchanged."""
    from bytecode_fixup import (ID_MASK, USER_ID_BASE, Resolver, apply_fixup, observed_builtins,
                                relocations_from_data_win)
    relocs = {name: relocations_from_data_win(d, off, len(a)) for name, a, _, off in pairs}
    resolver = Resolver.from_data_win(d, *observed_builtins((relocs[n], b) for n, _, b, _ in pairs))
    kinds = collections.Counter(); examples = collections.defaultdict(list); bad = []
    for name, a, b, _ in pairs:
        at = {r.offset: r for r in relocs[name]}
        expected = apply_fixup(a, relocs[name], resolver)
        for idx, op, size in instruction_words(a):
            w0 = struct.unpack_from("<I", a, 4 * idx)[0]
            for k in range(1, size) if size > 1 else [0]:
                fw = struct.unpack_from("<I", a, 4 * (idx + k))[0]; rw = struct.unpack_from("<I", b, 4 * (idx + k))[0]
                r = at.get(4 * idx) if k == 1 else None
                if r and r.kind == "var":
                    inst = struct.unpack("<h", struct.pack("<H", w0 & 0xFFFF))[0]
                    ident = rw & ID_MASK
                    key = (f"var {INSTANCE_TYPES.get(inst, inst)} {'user' if ident >= USER_ID_BASE else 'builtin'} "
                           f"ref={REF_TYPES.get(fw >> 24 & 0xF8, hex(fw >> 24 & 0xF8))} ({OPS.get(op)})")
                elif r:
                    key = f"func {'script' if rw >= USER_ID_BASE else 'builtin'} ({OPS.get(op)})"
                elif op in (0xB6, 0xB7, 0xB8, 0xBA, 0xBB):
                    key = "branch offset (unchanged)"
                elif op == 0xC0 and (w0 >> 16) & 0xF == 6:
                    key = "string index push.s (unchanged)"
                elif size > 1 or op in (0x84, 0xC0, 0xC1, 0xC2, 0xC3):
                    key = f"immediate {OPS.get(op, hex(op))} (unchanged)"
                else:
                    continue                                  # opcode-only word
                kinds[key] += 1
                if fw != rw and not r:
                    bad.append(f"{name}@{4 * (idx + k)}: {key}: file {fw:08x} -> mem {rw:08x}, no reference")
                elif expected[4 * (idx + k):4 * (idx + k) + 4] != b[4 * (idx + k):4 * (idx + k) + 4]:
                    bad.append(f"{name}@{4 * (idx + k)}: {key}: rule gives "
                               f"{struct.unpack_from('<I', expected, 4 * (idx + k))[0]:08x}, mem {rw:08x}")
                if len(examples[key]) < 3 and (r or fw != rw):
                    examples[key].append(f"{name}@{4 * (idx + k)} {r.name if r else ''}: {fw:08x} -> {rw:08x}")
        for i in range(0, len(b), 4):                         # catch-all: opcode words, anything missed
            if expected[i:i + 4] != b[i:i + 4] and not any(line.startswith(f"{name}@{i}:") for line in bad):
                bad.append(f"{name}@{i}: word not reproduced: rule {expected[i:i + 4][::-1].hex()}, mem {b[i:i + 4][::-1].hex()}")
    print("\nOperand words by kind (all pairs):")
    for key, n in sorted(kinds.items()):
        print(f"{n:6}  {key}")
        for ex in examples[key]:
            print("          " + ex)
    print(f"{len(bad)} operand word(s) not explained by the fixup rules")
    for line in bad[:20]:
        print("  " + line)
    return bad


if __name__ == "__main__":
    main()
