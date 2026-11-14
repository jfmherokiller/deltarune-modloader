#!/usr/bin/env python3
"""bytecode_fixup.py - the GameMaker VM runner's load-time operand fixup, reimplemented.

When the runner loads the CODE chunk it rewrites the second word of every variable and function
reference instruction in place: the file holds a reference *chain* (offset to the next occurrence,
or the name's STRG index at the last one), memory holds a runtime *id*. Nothing else changes:
opcodes, instance types, branch offsets, string indices and immediates are kept as they are.
The rules are written up in compiler/RUNTIME_COMPILER.md, "Load-time operand fixup".

Library use (the part injected bytecode needs):

    relocs = relocations_from_data_win(data, buffer_offset, length)   # or emitted by the compiler
    linked = apply_fixup(code, relocs, resolver)                       # resolver: name -> runtime id

CLI:

    bytecode_fixup.py links <served data.win> <fixture dir>
        Writes <fixture dir>/links.json: the relocations of every captured entry plus the id of
        every referenced name (user variables and scripts by the load-order rule, built-ins from
        the runtime capture). The fixture test (compiler/tests/test_bytecode_fixup.py) links the
        offline dumps with it and compares them with the runtime dumps.
    bytecode_fixup.py check <served data.win> <fixture dir>
        Links every offline dump straight from the data.win and reports byte equality.
"""
import json, os, struct, sys
from dataclasses import dataclass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_runtime_bytecode import chunks, code_entries, cstring, load_data, safe_name  # noqa: E402

REF_TYPE_MASK = 0xF8000000   # variable reference type (0xA0 normal, 0x80 stack top, 0x00 array, ...), kept
ID_MASK = 0x07FFFFFF         # chain link in the file, runtime id in memory
USER_ID_BASE = 100000        # user variables and script functions: 100000 + slot / script index
BUILTIN_VAR_MARK = -6        # VARI instance type or var id of a built-in (UTMT writes -6)


@dataclass(frozen=True)
class Reloc:
    """One reference instruction: `offset` is the byte offset of the instruction inside the buffer;
    its operand word (offset + 4) is the one rewritten. kind is "var" or "func"."""
    offset: int
    kind: str
    name: str
    inst_type: int = 0      # VARI instance type for "var" (-1 self, -5 global, -6 built-in, -7 local)

    def to_json(self):
        return [self.offset, self.kind, self.name] + ([self.inst_type] if self.kind == "var" else [])

    @staticmethod
    def from_json(row):
        return Reloc(row[0], row[1], row[2], row[3] if len(row) > 3 else 0)


class Resolver:
    """name -> runtime id. Built-in tables come from the runner (registration order, not in
    data.win); user variables and scripts follow the load-order rule (`from_data_win`)."""

    def __init__(self, builtin_vars=None, builtin_funcs=None, user_vars=None, scripts=None):
        self.builtin_vars = dict(builtin_vars or {})    # name -> builtin variable index
        self.builtin_funcs = dict(builtin_funcs or {})  # name -> routine index (GetNamedRoutineIndex)
        self.user_vars = dict(user_vars or {})          # name -> slot (id = 100000 + slot)
        self.scripts = dict(scripts or {})              # name -> SCPT index (id = 100000 + index)

    def var_id(self, name):
        if name in self.builtin_vars:
            return self.builtin_vars[name]
        if name not in self.user_vars:                  # the runner allocates the next free slot
            self.user_vars[name] = len(self.user_vars)
        return USER_ID_BASE + self.user_vars[name]

    def func_id(self, name):
        if name in self.builtin_funcs:
            return self.builtin_funcs[name]
        if name in self.scripts:
            return USER_ID_BASE + self.scripts[name]
        raise KeyError(f"unresolved function {name!r}")

    @staticmethod
    def from_data_win(d, builtin_vars=None, builtin_funcs=None):
        """User variable slots and script indices as the runner assigns them while loading d."""
        return Resolver(builtin_vars, builtin_funcs, user_variable_slots(d), script_indices(d))


def apply_fixup(code, relocs, resolver):
    """Return `code` with every relocation's operand word replaced by its runtime id."""
    out = bytearray(code)
    for r in relocs:
        at = r.offset + 4
        word = struct.unpack_from("<I", out, at)[0]
        if r.kind == "var":
            word = (word & REF_TYPE_MASK) | (resolver.var_id(r.name) & ID_MASK)
        elif r.kind == "func":
            word = resolver.func_id(r.name) & 0xFFFFFFFF
        else:
            raise ValueError(f"unknown relocation kind {r.kind!r}")
        struct.pack_into("<I", out, at, word)
    return bytes(out)


# ---- data.win side: VARI / FUNC / SCPT --------------------------------------------------------

def _u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def _i32(d, o):
    return struct.unpack_from("<i", d, o)[0]


def variables(d):
    """VARI entries (bytecode 15+): (name, instance type, var id, occurrences, first instruction)."""
    start, size = chunks(d)["VARI"]
    out, o = [], start + 12                         # header: 2 counts + max locals
    while o + 20 <= start + size:                   # the chunk is padded; do not read the tail
        out.append((cstring(d, _u32(d, o)), _i32(d, o + 4), _i32(d, o + 8), _u32(d, o + 12), _i32(d, o + 16)))
        o += 20
    return out


def functions(d):
    """FUNC entries: (name, occurrences, first instruction). GMS 2.3+ stores the address of the
    first reference's *operand* word, so 4 is subtracted here."""
    start, _ = chunks(d)["FUNC"]
    out = []
    for i in range(_u32(d, start)):
        o = start + 4 + 12 * i
        occ, first = _u32(d, o + 4), _i32(d, o + 8)
        out.append((cstring(d, _u32(d, o)), occ, first - 4 if occ else -1))
    return out


def script_indices(d):
    """SCPT name -> index. A script function's runtime id is 100000 + this index."""
    start, _ = chunks(d)["SCPT"]
    return {cstring(d, _u32(d, _u32(d, start + 4 + 4 * i))): i for i in range(_u32(d, start))}


def is_builtin_variable(inst_type, var_id):
    return inst_type == BUILTIN_VAR_MARK or var_id == BUILTIN_VAR_MARK


def user_variable_slots(d):
    """name -> slot. The runner walks VARI in file order and finds-or-adds every non-built-in name
    in one table shared by self, global and local variables; the slot is the insertion index."""
    slots = {}
    for name, inst_type, var_id, _, _ in variables(d):
        if not is_builtin_variable(inst_type, var_id) and name not in slots:
            slots[name] = len(slots)
    return slots


def walk_chain(d, first, occurrences):
    """Instruction addresses of one reference chain. Each operand's low 27 bits give the byte
    distance to the next occurrence; the last occurrence holds the name's STRG index instead."""
    addr = first
    for k in range(occurrences):
        yield addr
        if k < occurrences - 1:
            addr += _u32(d, addr + 4) & ID_MASK


def relocations_from_data_win(d, start, length):
    """Relocations for the buffer [start, start + length) of data.win `d`, sorted by offset."""
    out = []
    for name, inst_type, _var_id, occ, first in variables(d):
        out += [Reloc(a - start, "var", name, inst_type) for a in walk_chain(d, first, occ) if start <= a < start + length]
    for name, occ, first in functions(d):
        out += [Reloc(a - start, "func", name) for a in walk_chain(d, first, occ) if start <= a < start + length]
    return sorted(out, key=lambda r: r.offset)


# ---- fixtures -----------------------------------------------------------------------------------

def _read(path):
    with open(path, "rb") as f:
        return f.read()


def read_tsv(path):
    with open(path, encoding="utf-8") as f:
        lines = [l.rstrip("\r\n").split("\t") for l in f if l.strip()]
    return [dict(zip(lines[0], row)) for row in lines[1:]]


def fixture_entries(fixture_dir):
    """(name, file offset, offline bytes, runtime bytes) for every pair in a fixture dir."""
    for row in read_tsv(os.path.join(fixture_dir, "offline", "offline.tsv")):
        stem = safe_name(row["name"])
        offline, runtime = (_read(os.path.join(fixture_dir, sub, stem + ".bin")) for sub in ("offline", "runtime"))
        yield row["name"], int(row["file_offset"], 16), offline, runtime


def observed_builtins(pairs):
    """Built-in ids read off runtime dumps: pairs of (relocs, runtime bytes). Only names below
    USER_ID_BASE count as built-ins; a name seen with two different ids raises."""
    bvars, bfuncs = {}, {}
    for relocs, runtime in pairs:
        for r in relocs:
            word = _u32(runtime, r.offset + 4)
            ident = word & ID_MASK if r.kind == "var" else word
            if ident >= USER_ID_BASE:
                continue
            table = bvars if r.kind == "var" else bfuncs
            if table.setdefault(r.name, ident) != ident:
                raise ValueError(f"{r.kind} {r.name}: ids {table[r.name]} and {ident}")
    return bvars, bfuncs


def build_links(d, fixture_dir):
    entries, pairs = {}, []
    for name, off, offline, runtime in fixture_entries(fixture_dir):
        relocs = relocations_from_data_win(d, off, len(offline))
        entries[name] = {"file_offset": off, "length": len(offline), "relocs": [r.to_json() for r in relocs]}
        pairs.append((relocs, runtime))
    bvars, bfuncs = observed_builtins(pairs)
    slots, scripts = user_variable_slots(d), script_indices(d)
    used = {(r[1], r[2]) for e in entries.values() for r in e["relocs"]}
    var_names = sorted(n for k, n in used if k == "var")
    func_names = sorted(n for k, n in used if k == "func")
    return {
        "comment": "Generated by compiler/tools/bytecode_fixup.py links. user/script ids follow the "
                   "load-order rule over the whole served data.win; builtin ids are runner table "
                   "indices read off the runtime capture. See RUNTIME_COMPILER.md.",
        "user_slot_count": len(slots),
        "variables": {n: ({"builtin": bvars[n]} if n in bvars else {"slot": slots[n]}) for n in var_names},
        "functions": {n: ({"builtin": bfuncs[n]} if n in bfuncs else {"script_index": scripts[n]}) for n in func_names},
        "entries": entries,
    }


def dump_links(obj, depth=0):
    """links.json text: nested containers indented, innermost ones (a symbol, a relocation) on one line."""
    vals = obj.values() if isinstance(obj, dict) else obj
    if not isinstance(obj, (dict, list)) or not any(isinstance(v, (dict, list)) for v in vals):
        return json.dumps(obj, separators=(", ", ": ")) + ("\n" if depth == 0 else "")
    pad = " " * (depth + 1)
    if isinstance(obj, dict):
        items = [f"{pad}{json.dumps(k)}: {dump_links(v, depth + 1)}" for k, v in obj.items()]
        body, open_, close = items, "{", "}"
    else:
        body, open_, close = [pad + dump_links(v, depth + 1) for v in obj], "[", "]"
    text = open_ + "\n" + ",\n".join(body) + "\n" + " " * depth + close
    return text + ("\n" if depth == 0 else "")


def resolver_from_links(links):
    v, f = links["variables"], links["functions"]
    return Resolver({n: e["builtin"] for n, e in v.items() if "builtin" in e},
                    {n: e["builtin"] for n, e in f.items() if "builtin" in e},
                    {n: e["slot"] for n, e in v.items() if "slot" in e},
                    {n: e["script_index"] for n, e in f.items() if "script_index" in e})


def main(argv):
    if len(argv) != 3 or argv[0] not in ("links", "check"):
        sys.exit(__doc__)
    cmd, served, fixture_dir = argv
    d = load_data(served)
    links = build_links(d, fixture_dir)
    if cmd == "links":
        path = os.path.join(fixture_dir, "links.json")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(dump_links(links))
        print(f"wrote {path}: {len(links['entries'])} entries, "
              f"{sum(len(e['relocs']) for e in links['entries'].values())} relocations")
        return 0
    resolver = Resolver.from_data_win(d, *observed_builtins(
        (relocations_from_data_win(d, off, len(off_b)), rt) for _, off, off_b, rt in fixture_entries(fixture_dir)))
    bad = 0
    for name, off, offline, runtime in fixture_entries(fixture_dir):
        linked = apply_fixup(offline, relocations_from_data_win(d, off, len(offline)), resolver)
        ok = linked == runtime
        bad += not ok
        print(f"{'OK  ' if ok else 'FAIL'} {name}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
