#!/usr/bin/env python3
"""compare_runtime_bytecode.py - diff the runner's in-memory bytecode against the data.win it was served.

usage: compare_runtime_bytecode.py <data.win as served> <dump dir with index.tsv + *.bin>

The RunnerCompiler spike plugin dumps CCode->VMBuffer bytecode. The runner points VMBuffer.bytecode
into the loaded data.win and rewrites operands in place while loading the CODE chunk. This tool
finds each dumped script's bytes in the served data.win (CODE entry: bytecode = entry + 12 +
relAddr, GMS2.3+ layout) and reports which instruction words changed, grouped by opcode, so the
fixup that injected bytecode must reproduce can be read off directly.
"""
import collections, os, struct, sys

# GameMaker bytecode 15+ opcodes (UndertaleModLib UndertaleInstruction.Opcode).
OPS = {0x07: "conv", 0x08: "mul", 0x09: "div", 0x0A: "rem", 0x0B: "mod", 0x0C: "add", 0x0D: "sub",
       0x0E: "and", 0x0F: "or", 0x10: "xor", 0x11: "neg", 0x12: "not", 0x13: "shl", 0x14: "shr",
       0x15: "cmp", 0x45: "pop", 0x82: "dup", 0x86: "dup?", 0x9C: "ret", 0x9D: "exit", 0x9E: "popz",
       0xB6: "b", 0xB7: "bt", 0xB8: "bf", 0xBA: "pushenv", 0xBB: "popenv", 0xC0: "push", 0xC1: "pushloc",
       0xC2: "pushglb", 0xC3: "pushbltn", 0x84: "pushi", 0xD9: "call", 0x99: "callv", 0xFF: "break"}
DATA_TYPE_SIZE = {0: 8, 1: 4, 2: 4, 3: 8, 4: 4, 5: 4, 6: 4, 15: 2}  # double,float,int,long,bool,var,string,int16


def chunks(d):
    out, o = {}, 8
    while o + 8 <= len(d):
        name, size = d[o:o + 4].decode("latin1"), struct.unpack_from("<I", d, o + 4)[0]
        out[name] = (o + 8, size); o += 8 + size
    return out


def cstring(d, ptr):
    end = d.index(b"\0", ptr)
    return d[ptr:end].decode("utf-8", "replace")


def code_entries(d):
    start, size = chunks(d)["CODE"]
    count = struct.unpack_from("<I", d, start)[0]
    entries = {}
    for i in range(count):
        e = struct.unpack_from("<I", d, start + 4 + 4 * i)[0]
        name_ptr, length = struct.unpack_from("<II", d, e)
        rel = struct.unpack_from("<i", d, e + 12)[0]
        offset = struct.unpack_from("<I", d, e + 16)[0]
        entries[cstring(d, name_ptr)] = (e + 12 + rel + offset, length)
    return entries


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
    d = open(served, "rb").read()
    entries = code_entries(d)
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(dump_dir, "index.tsv"), encoding="utf-8")][1:]
    same = changed = missing = 0
    by_op = collections.Counter(); examples = collections.defaultdict(list); size_mismatch = []
    for name, length, *_ in rows:
        if name not in entries:
            missing += 1; continue
        off, flen = entries[name]
        runtime = open(os.path.join(dump_dir, "".join(c if c.isalnum() or c in "_-" else "_" for c in name) + ".bin"), "rb").read()
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


if __name__ == "__main__":
    main()
