#!/usr/bin/env python3
"""Static symbol dumper for YYC-compiled GameMaker games (x64 Windows).

YYC games ship no GML bytecode: every script and object event is native code. The
runner still needs a name for each one, so the exe keeps a static table of
YYGMLFuncs records in .data:

    struct YYGMLFuncs { const char* name; void* func; YYVAR* func_var; };   // 24 bytes
    struct YYVAR      { const char* name; int32_t id; };                    // 16 bytes

This tool finds that table (and the YYVAR records the compiled code references),
then writes:

    symbols.json     functions (name, kind, object/event, RVA, script id, callees,
                     referenced variables) and variables (name, RVA, static id)
    objects.json     per object: events and the variable names its events touch
    ida_names.py     IDAPython script that applies the names (and comments the raw name)

It reads the exe only, needs nothing beyond the Python standard library, and does
not run the game. Usage:

    python3 tools/yyc_dump.py "<game>/Game.exe" --out yyc_out
    python3 tools/yyc_dump.py "<game>/Game.exe" --out yyc_out --runtime "<game>/yyc_runtime"

--runtime merges a YYCRuntimeDump folder (modloader/yyc_runtime_dump): script indices,
YYVAR runtime ids, built-in function/variable and runner-interface names. See
docs/yyc_il2cpp.md.
"""
import argparse
import collections
import hashlib
import json
import os
import re
import struct
import sys

EVENTS = {"Create", "Destroy", "Alarm", "Step", "Collision", "Keyboard", "Mouse",
          "Other", "Draw", "KeyPress", "KeyRelease", "Trigger", "CleanUp",
          "Gesture", "PreCreate"}
SCRIPT_ID_BASE = 100000  # runner function ids: builtins < 100000 <= scripts < 500000


class PE:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        if d[:2] != b"MZ":
            raise ValueError("not a PE file")
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise ValueError("bad PE signature")
        machine, nsec = struct.unpack_from("<HH", d, pe + 4)
        if machine != 0x8664:
            raise ValueError("only x64 executables are supported")
        opt_size = struct.unpack_from("<H", d, pe + 20)[0]
        opt = pe + 24
        if struct.unpack_from("<H", d, opt)[0] != 0x20B:
            raise ValueError("expected a PE32+ optional header")
        self.image_base = struct.unpack_from("<Q", d, opt + 24)[0]
        self.timestamp = struct.unpack_from("<I", d, pe + 8)[0]
        exc_rva, exc_size = struct.unpack_from("<II", d, opt + 112 + 3 * 8)
        self.sections = {}
        for i in range(nsec):
            o = opt + opt_size + i * 40
            name = d[o:o + 8].rstrip(b"\0").decode("latin1")
            vsize, va, rsize, raw = struct.unpack_from("<IIII", d, o + 8)
            self.sections[name] = (va, vsize, raw, rsize)
        self.functions = {}  # begin rva -> end rva, from .pdata
        for i in range(0, exc_size - 11, 12):
            b, e, _ = struct.unpack_from("<III", d, self.off(exc_rva) + i)
            if b and e > b:
                self.functions.setdefault(b, e)

    def off(self, rva):
        for va, vsize, raw, rsize in self.sections.values():
            if va <= rva < va + max(vsize, rsize) and rva - va < rsize:
                return raw + rva - va
        return None

    def section(self, name):
        va, vsize, raw, rsize = self.sections[name]
        return va, self.data[raw:raw + min(vsize, rsize)]

    def in_section(self, name, rva):
        va, vsize, _, _ = self.sections[name]
        return va <= rva < va + vsize

    def cstr(self, rva, limit=512):
        o = self.off(rva)
        if o is None:
            return None
        e = self.data.find(b"\0", o, o + limit)
        if e <= o:
            return None
        try:
            s = self.data[o:e].decode("utf-8")
        except UnicodeDecodeError:
            return None
        return s if s.isprintable() else None


def parse_name(name):
    """gml_Object_<obj>_<Event>_<n> -> (kind, object, event, subevent)."""
    m = re.match(r"gml_(Script|GlobalScript|Object|RoomCC|Room|Timeline)_(.*)$", name)
    if not m:
        return "Other", None, None, None
    kind, rest = m.groups()
    if kind == "Object":
        parts = rest.split("_")
        # Event names never contain '_'; object names may. The subevent is a number,
        # or a collision target object name, so search for the event token from the right.
        for i in range(len(parts) - 2, 0, -1):
            if parts[i] in EVENTS:
                return kind, "_".join(parts[:i]), parts[i], "_".join(parts[i + 1:])
    return kind, None, None, None


def find_func_table(pe):
    """Longest run of 24-byte {name ptr -> "gml_*", code ptr -> .text, var ptr} records."""
    dva, draw = pe.section(".data")
    base = pe.image_base
    best = []
    i, n = 0, len(draw) - 24
    while i < n:
        run = []
        j = i
        while j < n:
            a, f, v = struct.unpack_from("<QQQ", draw, j)
            if not (a > base and f > base and pe.in_section(".text", f - base)):
                break
            s = pe.cstr(a - base)
            if not s or not s.startswith("gml_"):
                break
            run.append((dva + j, s, f - base, v - base if v else None))
            j += 24
        if run:
            if len(run) > len(best):
                best = run
            i = j
        else:
            i += 8
    return best


def find_yyvars(pe, func_var_rvas):
    """Candidate YYVAR records: {char* identifier in .rdata, int32 id} in .data."""
    dva, draw = pe.section(".data")
    base = pe.image_base
    out = {}
    ident = re.compile(r"[A-Za-z_][A-Za-z0-9_]*$")
    for i in range(0, len(draw) - 16, 8):
        a, val = struct.unpack_from("<Qi", draw, i)
        if a <= base or not pe.in_section(".rdata", a - base):
            continue
        s = pe.cstr(a - base, 256)
        if s and ident.match(s):
            out[dva + i] = (s, val)
    for r in func_var_rvas:
        out.pop(r, None)  # those are the scripts' own function-id records
    return out


RIP_MODRM = re.compile(rb"[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]")


def scan_body(pe, begin, end, func_starts, yyvar_rvas):
    """Direct E8 callees and RIP-relative YYVAR references inside [begin, end)."""
    tva, _, traw, _ = pe.sections[".text"]
    o = traw + begin - tva
    body = pe.data[o:o + (end - begin)]
    callees, vars_ = set(), set()
    for i in range(len(body) - 4):
        if body[i] == 0xE8 and i + 5 <= len(body):
            t = begin + i + 5 + struct.unpack_from("<i", body, i + 1)[0]
            if t in func_starts and t != begin:
                callees.add(t)
    # Heuristic: a ModRM byte with mod=00 rm=101 followed by disp32. Most such loads/leas
    # end right after the displacement; a field access lands up to 15 bytes into the record.
    for m in RIP_MODRM.finditer(body):
        i = m.start() + 1
        if i + 4 > len(body):
            break
        t = begin + i + 4 + struct.unpack_from("<i", body, i)[0]
        for rec in (t, t - 8, t & ~7):
            if rec in yyvar_rvas:
                vars_.add(rec)
                break
    return callees, vars_


def runner_version(data):
    m = re.search(rb"\b(20[12]\d\.\d+\.\d+\.\d+)\b", data)
    return m.group(1).decode() if m else None


def safe(name):
    return re.sub(r"[^A-Za-z0-9_@$?]", "_", name)


DEFAULT_YYTK_HEADER = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "external", "YYToolkit", "YYToolkit",
    "source", "YYTK", "Shared", "YYTK_Shared_Types.hpp")


def runner_interface_fields(header):
    """Member names of YYRunnerInterface, in declaration order (one pointer each)."""
    text = open(header, encoding="utf-8", errors="replace").read()
    m = re.search(r"struct YYRunnerInterface\s*\{(.*?)\n\t\};", text, re.S)
    if not m:
        raise ValueError("YYRunnerInterface not found in " + header)
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    body = re.sub(r"\{[^{}]*\}", ";", body)                  # inline member function bodies
    names = []
    for decl in body.split(";"):
        decl = " ".join(decl.split())
        if not decl or re.match(r"(~?YYRunnerInterface|[\w:<>]+ operator)\b", decl) or \
                decl.startswith("YYRunnerInterface("):
            continue                                         # constructors, operators
        fn = re.search(r"\(\s*\*\s*(\w+)\s*\)\s*\(", decl)      # function pointer member
        var = re.search(r"\*\s*(\w+)$", decl)                    # data pointer member
        m2 = fn or var
        if not m2:
            raise ValueError("unexpected YYRunnerInterface member: " + decl)
        names.append(m2.group(1))
    return names


def rt_rva(v):
    """runtime.json address: "0x<rva>" inside the exe, "abs:0x..." outside, or null."""
    return int(v, 16) if v and not v.startswith("abs:") else None


def runner_helpers(pe, runner):
    """Exported runner-interface functions that are thin wrappers name their implementation.

    A wrapper here is a .pdata function of at most 96 bytes with exactly one direct call to
    another .pdata function (e.g. FREE_RValue -> the internal free that compiled GML calls).
    """
    exported = {int(r["rva"], 16) for r in runner}
    tva, _, traw, _ = pe.sections[".text"]
    out = []
    for r in runner:
        rva = int(r["rva"], 16)
        end = pe.functions.get(rva)
        if not end or end - rva > 96:
            continue
        body = pe.data[traw + rva - tva:traw + end - tva]
        targets = set()
        for i in range(len(body) - 4):
            if body[i] == 0xE8:
                t = rva + i + 5 + struct.unpack_from("<i", body, i + 1)[0]
                if t in pe.functions and t != rva:
                    targets.add(t)
        if len(targets) == 1 and (t := targets.pop()) not in exported:
            out.append({"name": r["name"] + "_impl", "rva": hex(t), "wrapper": r["name"]})
    return out


def merge_runtime(pe, rt_dir, header, functions, variables):
    rt = json.load(open(os.path.join(rt_dir, "runtime.json")))
    by_rva = {int(f["rva"], 16): f for f in functions}
    matched = mismatched = 0
    for s in rt.get("scripts", []):
        f = by_rva.get(rt_rva(s["func"]))
        if f and f["name"] == (s["funcs_name"] or s["name"]):
            f["script_index"] = s["index"]
            matched += 1
        else:
            mismatched += 1

    # YYVAR ids are assigned at startup: read them from the .data snapshot.
    ds = rt.get("data_section")
    resolved = 0
    if ds:
        blob = open(os.path.join(rt_dir, ds["file"]), "rb").read()
        base = int(ds["rva"], 16)
        for v in variables:
            o = int(v["rva"], 16) + 8 - base
            if 0 <= o <= len(blob) - 4:
                v["runtime_id"] = struct.unpack_from("<i", blob, o)[0]
                resolved += v["runtime_id"] >= 0

    builtins = [{"index": b["index"], "name": b["name"], "rva": hex(r)}
                for b in rt.get("builtin_functions", []) if (r := rt_rva(b["func"])) is not None]
    bvars = [{"index": b["index"], "name": b["name"], "can_set": b["can_set"],
              "get": hex(g) if (g := rt_rva(b["get"])) is not None else None,
              "set": hex(t) if (t := rt_rva(b["set"])) is not None else None}
             for b in rt.get("builtin_variables", [])]
    slots = rt.get("runner_interface", [])
    names = runner_interface_fields(header)
    if len(names) != len(slots):
        raise ValueError(f"YYRunnerInterface has {len(names)} members in {header} but the "
                         f"dump has {len(slots)} slots; rebuild YYCRuntimeDump against it")
    runner = [{"name": n, "rva": hex(r)} for n, v in zip(names, slots)
              if (r := rt_rva(v)) is not None]
    helpers = runner_helpers(pe, runner)
    max_index = rt.get("builtin_function_max_index", -1)
    found = {b["index"] for b in rt.get("builtin_functions", [])}
    return {
        "summary": {
            "scripts": len(rt.get("scripts", [])), "scripts_matched": matched,
            "scripts_mismatched": mismatched,
            "builtin_functions": len(builtins),
            "builtin_function_indices_missing": [i for i in range(max_index + 1) if i not in found],
            "builtin_variables": len(bvars),
            "runner_interface_in_exe": len(runner),
            "runner_helpers": len(helpers),
            "variables_with_runtime_id": resolved,
        },
        "builtin_functions": builtins,
        "builtin_variables": bvars,
        "runner_interface": runner,
        "runner_helpers": helpers,
    }


def write_scripts(out):
    ida = '''# Generated by tools/yyc_dump.py. Run in IDA via File > Script file...
import json, os
import idaapi, idc, ida_funcs, ida_name
HERE = os.path.dirname(os.path.abspath(__file__))
DATA = json.load(open(os.path.join(HERE, "symbols.json")))
BASE = idaapi.get_imagebase()
FLAGS = ida_name.SN_NOWARN | ida_name.SN_NOCHECK | ida_name.SN_FORCE
# Minimal runner types (layout as in YYToolkit's YYTK_Shared_Types.hpp; its C++ header is
# not parseable by IDA, so only what the prototypes below need is declared here).
idc.parse_decls("""
struct CInstance;
struct RValue { union { double real; __int64 i64; void* ptr; } v; unsigned int flags; unsigned int kind; };
""", idc.PT_SILENT)
SCRIPT_T = "RValue* __fastcall f(CInstance* self, CInstance* other, RValue* result, int argc, RValue** argv);"
EVENT_T = "void __fastcall f(CInstance* self, CInstance* other);"
BUILTIN_T = "void __fastcall f(RValue* result, CInstance* self, CInstance* other, int argc, RValue* args);"
def set_type(ea, decl):
    t = idc.parse_decl(decl, idc.PT_SILENT)
    return bool(t) and idc.apply_type(ea, t, idc.TINFO_DEFINITE)
n = typed = 0
for f in DATA["functions"]:
    ea = BASE + int(f["rva"], 16)
    if not ida_funcs.get_func(ea):
        ida_funcs.add_func(ea)
    if ida_name.set_name(ea, f["ida_name"], FLAGS):
        n += 1
    idc.set_func_cmt(ea, f["name"], 1)
    typed += set_type(ea, EVENT_T if f["kind"] == "Object" else SCRIPT_T)
for v in DATA["variables"]:
    ea = BASE + int(v["rva"], 16)
    ida_name.set_name(ea, "yyvar_" + v["ida_name"], FLAGS)
    # Compiled code reads the id field (record + 8) directly, so naming it makes
    # Hex-Rays show the GML variable name at each access.
    ida_name.set_name(ea + 8, "v_" + v["ida_name"], FLAGS)
ida_name.set_name(BASE + int(DATA["func_table"]["rva"], 16), "g_YYGMLFuncs", FLAGS)
# Runtime names (present only when yyc_dump.py ran with --runtime).
def name_fn(rva, name, cmt=None):
    ea = BASE + int(rva, 16)
    if not ida_funcs.get_func(ea):
        ida_funcs.add_func(ea)
    ida_name.set_name(ea, name, FLAGS)
    if cmt:
        idc.set_func_cmt(ea, cmt, 1)
for r in DATA.get("runner_interface", []):
    name_fn(r["rva"], r["name"], "YYRunnerInterface::" + r["name"])
for r in DATA.get("runner_helpers", []):
    name_fn(r["rva"], r["name"], "implementation called by the exported " + r["wrapper"])
for b in DATA.get("builtin_functions", []):
    name_fn(b["rva"], "F_" + b["name"], "GML built-in %s (index %d)" % (b["name"], b["index"]))
    typed += set_type(BASE + int(b["rva"], 16), BUILTIN_T)
for b in DATA.get("builtin_variables", []):
    if b["get"]:
        name_fn(b["get"], "GV_" + b["name"], "get built-in variable " + b["name"])
    if b["set"]:
        name_fn(b["set"], "SV_" + b["name"], "set built-in variable " + b["name"])
for f in DATA["functions"]:
    if f.get("script_index") is not None:
        idc.set_func_cmt(BASE + int(f["rva"], 16),
                         "%s\\nscript index %d (id %d)" % (f["name"], f["script_index"],
                                                          100000 + f["script_index"]), 1)
print("yyc_dump: named %d functions, %d variables, %d runtime names, typed %d functions" % (
    n, len(DATA["variables"]), len(DATA.get("runner_interface", [])) +
    len(DATA.get("builtin_functions", [])) + len(DATA.get("builtin_variables", [])), typed))
'''
    with open(os.path.join(out, "ida_names.py"), "w", newline="\n") as fh:
        fh.write(ida)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("exe")
    ap.add_argument("--out", default="yyc_out")
    ap.add_argument("--no-xrefs", action="store_true",
                    help="skip the per-function callee/variable scan")
    ap.add_argument("--runtime", metavar="DIR",
                    help="merge a YYCRuntimeDump folder (yyc_runtime/ next to the game exe)")
    ap.add_argument("--yytk-header", default=DEFAULT_YYTK_HEADER,
                    help="YYTK_Shared_Types.hpp, for YYRunnerInterface field names")
    a = ap.parse_args()

    pe = PE(a.exe)
    table = find_func_table(pe)
    if len(table) < 2:
        sys.exit("no YYGMLFuncs table found: this is probably a VM build (bytecode lives "
                 "in data.win; use UndertaleModTool) or an unsupported runner")
    func_var_rvas = {v for _, _, _, v in table if v}
    yyvars = find_yyvars(pe, func_var_rvas)
    func_starts = {f for _, _, f, _ in table}
    by_rva = {f: s for _, s, f, _ in table}

    functions, refs_by_var = [], collections.Counter()
    for entry_rva, name, rva, var_rva in table:
        kind, obj, ev, sub = parse_name(name)
        # The record's own function id, minus 100000. It counts object events too, so it
        # equals the runner's script index (GetScriptData) only up to the first nested
        # script; --runtime adds the real script_index.
        func_id = None
        if var_rva and pe.off(var_rva) is not None:
            v = struct.unpack_from("<i", pe.data, pe.off(var_rva) + 8)[0]
            if v >= SCRIPT_ID_BASE:
                func_id = v - SCRIPT_ID_BASE
        rec = {"name": name, "ida_name": safe(name), "kind": kind, "rva": hex(rva),
               "table_rva": hex(entry_rva), "func_id": func_id,
               "has_pdata": rva in pe.functions}
        if obj:
            rec.update(object=obj, event=ev, subevent=sub)
        if not a.no_xrefs and rva in pe.functions:
            callees, vars_ = scan_body(pe, rva, pe.functions[rva], func_starts, yyvars)
            rec["calls"] = sorted(by_rva[c] for c in callees)
            rec["variables"] = sorted({yyvars[v][0] for v in vars_})
            refs_by_var.update(vars_)
        functions.append(rec)

    # Keep only YYVAR candidates that compiled GML actually references (drops look-alike
    # {name, int} records from the runner, imgui, shaders, ...). Without xrefs keep all.
    keep = refs_by_var if not a.no_xrefs else yyvars
    variables = [{"name": yyvars[r][0], "ida_name": safe(yyvars[r][0]), "rva": hex(r),
                  "static_id": yyvars[r][1], "code_refs": refs_by_var.get(r, 0)}
                 for r in sorted(keep)]

    objects = collections.defaultdict(lambda: {"events": [], "variables": set()})
    for f in functions:
        if f.get("object"):
            o = objects[f["object"]]
            o["events"].append(f"{f['event']}_{f['subevent']}")
            o["variables"].update(f.get("variables", []))
    objects = {k: {"events": sorted(v["events"]), "variables": sorted(v["variables"])}
               for k, v in sorted(objects.items())}

    runtime = None
    if a.runtime:
        runtime = merge_runtime(pe, a.runtime, a.yytk_header, functions, variables)

    os.makedirs(a.out, exist_ok=True)
    kinds = collections.Counter(f["kind"] for f in functions)
    summary = {
        "exe": os.path.basename(a.exe),
        "sha256": hashlib.sha256(pe.data).hexdigest(),
        "pe_timestamp": pe.timestamp,
        "image_base": hex(pe.image_base),
        "runner_version": runner_version(pe.data),
        "func_table": {"rva": hex(table[0][0]), "count": len(table)},
        "kinds": dict(kinds),
        "functions_without_pdata": sum(not f["has_pdata"] for f in functions),
        "variables": len(variables),
        "objects": len(objects),
    }
    if runtime:
        summary["runtime"] = runtime.pop("summary")
    with open(os.path.join(a.out, "symbols.json"), "w", newline="\n") as fh:
        json.dump({**summary, "functions": functions, "variables": variables,
                   **(runtime or {})}, fh, indent=1)
    with open(os.path.join(a.out, "objects.json"), "w", newline="\n") as fh:
        json.dump(objects, fh, indent=1)
    write_scripts(a.out)
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
