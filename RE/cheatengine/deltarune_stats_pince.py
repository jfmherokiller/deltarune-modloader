#!/usr/bin/env python3
# ============================================================================
#  DELTARUNE stat resolver — Linux / PINCE port of deltarune_stats.lua
#  (GameMaker VC_Runner engine; game runs under Wine/Proton)
#
#  This is a direct port of the Cheat Engine Lua script in this folder. It does
#  the same job with no Cheat Engine and no GUI dependency: it walks the GML
#  global-variable hashmap, locks onto each variable by *name* (or by value as a
#  fallback), re-resolves the real address every tick, and can freeze / set the
#  underlying double.
#
#  Two ways to run it
#  ------------------
#  1) Standalone (reads/writes /proc/<pid>/mem directly). On most kernels a
#     same-user process can do this without root even with yama ptrace_scope=1;
#     if not, prefix with sudo.
#
#         python3 cheatengine/deltarune_stats_pince.py                 # auto-find pid
#         python3 cheatengine/deltarune_stats_pince.py --pid 1234
#
#     It resolves everything, prints the table, starts the freezer thread, then
#     drops into a Python REPL with `dr` bound:
#
#         >>> dr.dump()
#         >>> dr.freeze("Gold", 999999)
#         >>> dr.freeze("HP Kris")          # freeze at the current value
#         >>> dr.unfreeze("HP")
#         >>> dr.set_value("TP (tension)", 250)   # one-shot write  (alias: dr.poke)
#         >>> dr.stop()
#
#  2) Inside PINCE — attach PINCE to DELTARUNE.exe first, then open
#     "Libpince Engine" (Tools menu) and run:
#
#         import sys; sys.path.insert(0, "<workspace>/RE/cheatengine")
#         import deltarune_stats_pince as dr
#         dr.start()                        # uses PINCE's attached pid; auto-adds
#                                           # the results to PINCE's address table
#                                           # as "[DR] ..." rows
#         dr.dump()
#         dr.from_pince()                   # remove the [DR] rows (dr.stop() also does)
#         dr.to_pince()                     # (re-)push them
#
#     The [DR] rows use the current RValue root: scalars as a static address,
#     array elements as a PINCE pointer chain [rv] -> +0x90 -> +16*i
#     that PINCE re-walks every refresh. Freeze/edit them with PINCE's own Freeze
#     checkbox and Value column - or keep using dr.freeze(...) / dr.set_value(...).
#     Imported as a module its state persists across Engine runs. PINCE already
#     runs as root (pkexec), so /proc/<pid>/mem is writable.
#
#  Layout (reverse-engineered — see ../07_globals_and_variable_storage.md):
#     g_pGlobalObject = [DELTARUNE.exe + 0x6A9DC0]
#     varHashMap      = [g_pGlobalObject + 72]   (+0 numSlots i32, +16 slots ptr)
#     slot (16 B)     = +0 RValue* value | +8 nameIndex i32 | +12 (var_id+1) i32  (<=0 empty)
#     RValue (16 B)   = +0 double/ptr | +8 flags | +12 kind (& 0xFFFFFF; 0=REAL, 2=ARRAY)
#     GMArray         = +0x90 elements (RValue*, stride 16) | +0xA4 length i32
#     name table ptr  = [DELTARUNE.exe + 0x8C9EE0]; name = [ [tbl] + 8*(nameIndex-100000) ]
#     alt name table  = [DELTARUNE.exe + 0x5FCD08]
#
#  Live-instance enumeration (added 2026-09-17, see ../08_instances.md):
#     g_pInstanceManager   = [DELTARUNE.exe + 0x6A9DC8]
#     InstancePoolHashMap  = [ [g_pInstanceManager + 136] ]   <- note the extra
#       +0 capacity u32 | +8 (unused here) | +16 slots ptr       pointer dereference:
#                                                                 +136 holds a POINTER
#                                                                 to the header, not
#                                                                 the header inline
#                                                                 (confirmed live -
#                                                                 reading it inline
#                                                                 gives a bogus huge
#                                                                 "capacity" and a
#                                                                 NULL slots ptr).
#     InstancePoolSlot (24 B) = +0 value ptr | +8 key ptr (= the instance) | +16 hash u32 (0=empty)
#     YYObjectBase.kind   = [instance + 0x7C]  (i32; 1 = a real CInstance, not debug-gated)
#     CInstance.x / .y    = [instance + 232] / [instance + 236]  (both 4-byte float)
#     Every live instance (any GML object, not just the player party) is reachable this
#     way — this is a NEW capability beyond the global-hashmap stat walk above: it lets
#     the trainer see *positions* of everything currently alive in the room. It does NOT
#     (yet) tell you *which GML object* an instance is — the natural per-type index field
#     (`YYObjectBase.typeIndex`, self+0x70) is debug-build-only and unreliable here in a
#     retail build; unconfirmed further, so `list_instances()` reports positions/pointers
#     only, not object names. See ../08_instances.md for the open ends.
# ============================================================================

from __future__ import annotations

import os
import re
import struct
import sys
import threading
import time

# --------------------------------------------------------------- FALLBACK CONFIG
# Only used if the by-name table can't be read. Put your CURRENT values here.
CFG = {
    "hp_party": [90, 110, 70],   # current HP for char id 1,2,3 (Kris, Susie, Ralsei)
    "gold": 0,
    "tension": 0,
}

PARTY = {1: "Kris", 2: "Susie", 3: "Ralsei", 4: "Noelle"}   # char id -> name (4 = Noelle, joins Ch2)
REFRESH_MS = 1000
GUARD_REFRESH_MS = 50
GUARD_FRAMES = 120.0  # finite grace period if the trainer exits unexpectedly

# Which globals to expose, and how.
#   kind "scalar"        -> single RValue (the value double is at the RValue itself)
#   kind "array", chars  -> expose array indices 1,2,3 (party members)
#   kind "array", count  -> expose array indices 0..count-1
STATS = [
    {"var": "gold",       "kind": "scalar", "label": "Gold"},
    {"var": "tension",    "kind": "scalar", "label": "TP (tension)"},
    {"var": "maxtension", "kind": "scalar", "label": "Max TP"},
    {"var": "xp",         "kind": "scalar", "label": "EXP"},
    {"var": "lv",         "kind": "scalar", "label": "LV"},
    {"var": "hp",         "kind": "array",  "chars": True, "label": "HP"},
    {"var": "maxhp",      "kind": "array",  "chars": True, "label": "MaxHP"},
    {"var": "at",         "kind": "array",  "chars": True, "label": "AT"},
    {"var": "df",         "kind": "array",  "chars": True, "label": "DF"},
    {"var": "mag",        "kind": "array",  "chars": True, "label": "MAG"},
    {"var": "guts",       "kind": "array",  "chars": True, "label": "GUTS"},
    {"var": "charweapon", "kind": "array",  "chars": True, "label": "Weapon id"},
    {"var": "chararmor1", "kind": "array",  "chars": True, "label": "Armor1 id"},
    {"var": "chararmor2", "kind": "array",  "chars": True, "label": "Armor2 id"},
    {"var": "item",       "kind": "array",  "count": 13,   "label": "Item"},
    {"var": "keyitem",    "kind": "array",  "count": 13,   "label": "KeyItem"},
    # Ch2 scr_gamestart initializes 48 equipment / 72 pocket slots; Ch1 is shorter.
    {"var": "weapon",     "kind": "array",  "count": 48,   "label": "WeaponStock"},
    {"var": "armor",      "kind": "array",  "count": 48,   "label": "ArmorStock"},
    {"var": "pocketitem", "kind": "array",  "count": 72,   "label": "PocketItem"},
]

# --------------------------------------------------------------------- LAYOUT
MODULE      = "DELTARUNE.exe"
RVA_GLOBAL  = 0x6A9DC0     # -> g_pGlobalObject
RVA_NAMES   = 0x8C9EE0     # -> user-var name table pointer
RVA_NAMES_A = 0x5FCD08     # -> alternate name table pointer
OFF_VARHASH = 72
SLOT_STRIDE = 16
ARR_ELEMS   = 0x90
ARR_LEN     = 0xA4
NAME_BIAS   = 100000
MAX_SLOTS   = (16 << 20) // SLOT_STRIDE   # sanity cap on numSlots

# Live-instance enumeration (see the module docstring's layout note above and
# ../08_instances.md — confirmed 2026-09-17 while decompiling InstancePool_HashMap_Insert).
RVA_INSTANCE_MGR     = 0x6A9DC8   # -> g_pInstanceManager
OFF_INSTANCE_HASHMAP = 136        # g_pInstanceManager + 136 -> InstancePoolHashMap header
INSTANCE_SLOT_STRIDE = 24         # struct InstancePoolSlot
OFF_INSTANCE_KIND    = 0x7C       # YYObjectBase.kind (i32); 1 = CInstance
OFF_INSTANCE_X       = 232        # CInstance.x (float)
OFF_INSTANCE_Y       = 236        # CInstance.y (float)
KIND_CINSTANCE       = 1
MAX_INSTANCE_SLOTS   = (16 << 20) // INSTANCE_SLOT_STRIDE   # sanity cap on capacity

_NAME_RE = re.compile(rb"[A-Za-z0-9_@]+")


# ============================================================================
#  Process memory access — plain /proc/<pid>/mem, no ptrace/gdb needed as root
# ============================================================================
class Mem:
    def __init__(self, pid: int):
        self.pid = pid
        self.writable = False
        try:
            self.fd = os.open(f"/proc/{pid}/mem", os.O_RDWR)
            self.writable = True
        except PermissionError:
            self.fd = os.open(f"/proc/{pid}/mem", os.O_RDONLY)
        except OSError as e:
            raise SystemExit(f"[!] cannot open /proc/{pid}/mem: {e} "
                             f"(run as root, or attach PINCE first)")

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass

    def raw(self, addr, n):
        if not addr or addr < 0 or n <= 0:
            return None
        try:
            data = os.pread(self.fd, n, addr)
        except OSError:
            return None
        return data if len(data) == n else None

    def u64(self, addr):
        b = self.raw(addr, 8)
        return struct.unpack("<Q", b)[0] if b else None

    def u32(self, addr):
        b = self.raw(addr, 4)
        return struct.unpack("<I", b)[0] if b else None

    def i32(self, addr):
        b = self.raw(addr, 4)
        return struct.unpack("<i", b)[0] if b else None

    def f64(self, addr):
        b = self.raw(addr, 8)
        return struct.unpack("<d", b)[0] if b else None

    def cstr(self, addr, maxlen=63):
        b = self.raw(addr, maxlen + 1)
        if not b:
            return None
        nul = b.find(b"\x00")
        if nul >= 0:
            b = b[:nul]
        try:
            return b.decode("utf-8")
        except UnicodeDecodeError:
            return b.decode("latin-1")

    def write_f64(self, addr, value):
        # Only REAL payloads may be overwritten with a double. Preserve flags/kind.
        kind = self.u32(addr + 12) if addr else None
        if not self.writable or kind is None or (kind & 0xFFFFFF) != 0:
            return False
        try:
            return os.pwrite(self.fd, struct.pack("<d", float(value)), addr) == 8
        except OSError:
            return False


def module_base(pid: int, name: str = MODULE):
    """Load base of a mapped module (min start-fileoffset over its mappings)."""
    want = name.lower()
    base = None
    try:
        with open(f"/proc/{pid}/maps") as f:
            for line in f:
                parts = line.split(None, 5)
                if len(parts) < 6:
                    continue
                path = parts[5].strip()
                if not path or os.path.basename(path).lower() != want:
                    continue
                start = int(parts[0].split("-")[0], 16)
                foff = int(parts[2], 16)
                b = start - foff
                base = b if base is None else min(base, b)
    except OSError:
        return None
    return base


# ============================================================================
#  PID discovery
# ============================================================================
def _iter_pids():
    for entry in os.listdir("/proc"):
        if entry.isdigit():
            yield int(entry)


def _pince_attached_pid():
    """If we're running inside PINCE and it's attached, reuse that pid."""
    try:
        from libpince import debugcore  # type: ignore
    except Exception:
        return None
    pid = getattr(debugcore, "currentpid", -1)
    return pid if isinstance(pid, int) and pid > 0 else None


def _pince_window():
    """Return PINCE's MainForm window if this code runs inside a live PINCE GUI, else None."""
    w = getattr(sys.modules.get("__main__"), "window", None)
    if w is not None and hasattr(w, "add_entry_to_addresstable"):
        return w
    try:
        from PyQt6.QtWidgets import QApplication  # type: ignore
        app = QApplication.instance()
        if app is not None:
            for widget in app.topLevelWidgets():
                if hasattr(widget, "add_entry_to_addresstable") and hasattr(widget, "treeWidget_AddressTable"):
                    return widget
    except Exception:
        pass
    return None


def find_pid():
    hit = _pince_attached_pid()
    if hit:
        return hit
    candidates = []
    for pid in _iter_pids():
        try:
            with open(f"/proc/{pid}/comm") as f:
                comm = f.read().strip()
        except OSError:
            continue
        cmdline = ""
        try:
            with open(f"/proc/{pid}/cmdline", "rb") as f:
                cmdline = f.read().replace(b"\x00", b" ").decode("latin-1")
        except OSError:
            pass
        if comm == MODULE or "DELTARUNE.exe" in cmdline:
            candidates.append(pid)
    # Prefer a pid that actually has the PE mapped AND a live global object.
    for pid in candidates:
        base = module_base(pid)
        if base is None:
            continue
        try:
            m = Mem(pid)
        except SystemExit:
            continue
        try:
            g = m.u64(base + RVA_GLOBAL)
            if g:
                return pid
        finally:
            m.close()
    return candidates[0] if candidates else None


# ============================================================================
#  GML global-variable hashmap walk
# ============================================================================
class Resolver:
    def __init__(self, mem: Mem, base: int):
        self.mem = mem
        self.base = base
        self.name_base = None      # resolved name-table pointer, or None
        self.name_ok = False

    # -- primitives -----------------------------------------------------------
    def global_obj(self):
        return self.mem.u64(self.base + RVA_GLOBAL)

    def iter_vars(self):
        """Yield (rvalue_ptr, var_id, name_index) for every occupied slot."""
        g = self.global_obj()
        if not g:
            return
        hm = self.mem.u64(g + OFF_VARHASH)
        if not hm:
            return
        n = self.mem.i32(hm) or 0
        slots = self.mem.u64(hm + 16)
        if not slots or n <= 0 or n > MAX_SLOTS:
            return
        blob = self.mem.raw(slots, n * SLOT_STRIDE)
        if not blob:
            return
        for i in range(n):
            val, name_index, key = struct.unpack_from("<Qii", blob, i * SLOT_STRIDE)
            if key > 0 and val:
                yield val, key - 1, name_index

    def kind_of(self, rv):
        k = self.mem.u32(rv + 12)
        return (k & 0xFFFFFF) if k is not None else -1

    def arr_elem_addr(self, rv, i):
        """array RValue -> address of the editable double at index i."""
        if rv is None or self.kind_of(rv) != 2:
            return None
        arr = self.mem.u64(rv)
        if not arr:
            return None
        buf = self.mem.u64(arr + ARR_ELEMS)
        length = self.mem.i32(arr + ARR_LEN)
        if not buf or length is None or i < 0 or i >= length:
            return None
        elem = buf + 16 * i
        return elem if self.kind_of(elem) == 0 else None

    # -- live-instance enumeration (../08_instances.md) ----------------------
    def instance_manager(self):
        return self.mem.u64(self.base + RVA_INSTANCE_MGR)

    def iter_instances(self):
        """Yield (instance_ptr, kind) for every live entry in g_pInstanceManager's
        pointer-keyed hashmap. kind==KIND_CINSTANCE (1) is a real game-object
        CInstance; other kinds (CScriptRef=3, etc.) share the same table because
        Instance_RegisterWithManager is also called from gml_method() and friends."""
        mgr = self.instance_manager()
        if not mgr:
            return
        # g_pInstanceManager+136 is a POINTER TO the hashmap header, not the header
        # itself - confirmed from Instance_RegisterWithManager's actual decompile:
        # InstancePool_HashMap_Insert(*(QWORD*)(mgr+136), instance, instance). Missing
        # this extra indirection reads garbage (a huge bogus "capacity" and a NULL
        # slots pointer) - caught by live-testing against a running game, not by the
        # static analysis alone. See 08_instances.md.
        hm = self.mem.u64(mgr + OFF_INSTANCE_HASHMAP)
        if not hm:
            return
        capacity = self.mem.u32(hm)
        slots = self.mem.u64(hm + 16)
        if not slots or not capacity or capacity > MAX_INSTANCE_SLOTS:
            return
        blob = self.mem.raw(slots, capacity * INSTANCE_SLOT_STRIDE)
        if not blob:
            return
        for i in range(capacity):
            _value, key, h = struct.unpack_from("<QQI", blob, i * INSTANCE_SLOT_STRIDE)
            if h != 0 and key:
                yield key, self.instance_kind(key)

    def instance_kind(self, ptr):
        k = self.mem.i32(ptr + OFF_INSTANCE_KIND)
        return k if k is not None else -1

    def instance_pos(self, ptr):
        """(x, y) floats for a live CInstance, or None if unreadable."""
        b = self.mem.raw(ptr + OFF_INSTANCE_X, 8)
        return struct.unpack("<ff", b) if b else None

    # -- name resolution ----------------------------------------------------
    @staticmethod
    def _plausible(s):
        return bool(s) and 0 < len(s) < 64 and _NAME_RE.fullmatch(s.encode("latin-1", "ignore")) is not None

    def read_var_name(self, tbl, name_index):
        if not tbl or name_index < NAME_BIAS:
            return None
        p = self.mem.u64(tbl + 8 * (name_index - NAME_BIAS))
        if not p:
            return None
        s = self.mem.cstr(p, 63)
        return s if self._plausible(s) else None

    def detect_name_table(self):
        for rva in (RVA_NAMES, RVA_NAMES_A):
            tbl = self.mem.u64(self.base + rva)
            if not tbl:
                continue
            seen = set()
            for _rv, _vid, name_index in self.iter_vars():
                nm = self.read_var_name(tbl, name_index)
                if nm:
                    seen.add(nm)
            if seen & {"hp", "gold", "maxhp"}:
                self.name_base, self.name_ok = tbl, True
                return True
        self.name_ok = False
        return False

    def map_by_name(self):
        m = {}
        if not self.name_ok:
            return m
        for rv, _vid, name_index in self.iter_vars():
            nm = self.read_var_name(self.name_base, name_index)
            if nm:
                m[nm] = rv
        return m


# ============================================================================
#  Spec building — { desc, get(snapshot) -> address }
# ============================================================================
def _idxs_for(stat):
    if stat.get("chars"):
        return list(PARTY.keys())   # 1,2,3,4 = Kris,Susie,Ralsei,Noelle (Ch1 party arrays are only
                                     # 4 long, so [4] reads back "(unresolved)" until she joins in Ch2)
    if "count" in stat:
        return list(range(stat["count"]))
    return []


def build_specs_by_name(r: Resolver):
    specs = []
    present = r.map_by_name()
    for s in STATS:
        var = s["var"]
        if var not in present:
            continue
        if s["kind"] == "scalar":
            specs.append({
                "desc": f'{s["label"]}  (global.{var})',
                "get": (lambda snap, v=var: snap.get(v)),
                "pince": {"kind": "scalar", "var": var},
            })
        else:
            for i in _idxs_for(s):
                # Inventory capacities differ by chapter. Omit absent/non-REAL slots.
                if "count" in s and r.arr_elem_addr(present[var], i) is None:
                    continue
                who = PARTY.get(i, f"id{i}") if s.get("chars") else f"[{i}]"
                specs.append({
                    "desc": f'{s["label"]} {who}  (global.{var}[{i}])',
                    "get": (lambda snap, v=var, ix=i: r.arr_elem_addr(snap.get(v), ix)),
                    "pince": {"kind": "array", "var": var, "index": i},
                })
    return specs


def build_specs_by_value(r: Resolver):
    """Fallback: match CFG['hp_party'] against every len>=4 array; match scalars by value.

    Captures the candidate RValue root and re-derives array elements each tick.
    Root lifetime is not established; restart discovery after chapter changes.
    """
    def approx(a, b):
        return a is not None and b is not None and abs(a - b) < 0.5

    specs = []

    health_rvs = []
    for rv, _vid, _ni in r.iter_vars():
        if r.kind_of(rv) != 2:
            continue
        arr = r.mem.u64(rv)
        buf = r.mem.u64(arr + ARR_ELEMS) if arr else None
        length = r.mem.i32(arr + ARR_LEN) if arr else None
        if buf and length and length >= 4:
            if all(approx(r.mem.f64(buf + 16 * cid), CFG["hp_party"][cid - 1]) for cid in (1, 2, 3)):
                health_rvs.append(rv)

    for n, rv in enumerate(health_rvs, start=1):
        for cid in (1, 2, 3):
            specs.append({
                "desc": f'Health Value {n} - {PARTY.get(cid, f"id{cid}")}',
                "get": (lambda snap, rr=rv, ci=cid: r.arr_elem_addr(rr, ci)),
                "pince": {"kind": "array", "rv": rv, "index": cid},
            })

    def scalar_by_value(val, label):
        matches = [rv for rv, _vid, _ni in r.iter_vars()
                   if r.kind_of(rv) == 0 and approx(r.mem.f64(rv), val)]
        if len(matches) != 1:
            print(f"[!] fallback: {label}: {len(matches)} candidates; "
                  "not exposed. Update CFG with a distinctive current value.")
            return
        rv = matches[0]
        specs.append({"desc": label, "get": (lambda snap, rr=rv: rr),
                      "pince": {"kind": "scalar", "rv": rv}})

    scalar_by_value(CFG["gold"], "Gold (value-matched)")
    scalar_by_value(CFG["tension"], "TP (value-matched)")
    if not health_rvs:
        print("[!] fallback: no HP-matching array found - update CFG['hp_party'] to current values")
    return specs


# ============================================================================
#  Driver — resolve, print, background refresh + freeze
# ============================================================================
class Trainer:
    def __init__(self):
        self.mem = None
        self.r = None
        self.specs = []
        self.addr = {}        # desc -> last resolved address (int) or None
        self.frozen = {}      # desc -> float value to hold
        self._stop = threading.Event()
        self._thread = None
        self.mode = None
        self.pince_win = None      # PINCE MainForm, when pushed to the address table
        self.pince_rows = []       # QTreeWidgetItem list we added (prefixed "[DR] ")
        self._guard_lock = threading.RLock()
        self._damage_guard = False
        self._guard_global = None
        self._guard_toolbar = None
        self._guard_checkbox = None
        self._guard_status = None
        self._guard_timer = None
        self._guard_control_error = None

    # -- lifecycle --------------------------------------------------------
    def start(self, pid=None, push_to_pince=None):
        self.stop()
        pid = pid or find_pid()
        if not pid:
            print("[!] DELTARUNE.exe process not found - launch the game (a chapter) first")
            return self
        base = module_base(pid)
        if base is None:
            print(f"[!] pid {pid}: DELTARUNE.exe module not mapped")
            return self
        self.mem = Mem(pid)
        self.r = Resolver(self.mem, base)
        print(f"[*] pid {pid}  module {MODULE} @ {base:#x}"
              f"{'  (read-only!)' if not self.mem.writable else ''}")
        if not self.r.global_obj():
            print("[!] global object is null - load a save / be in-game, then call dr.start() again")
            return self
        self._resolve_specs()
        if not self.specs:
            print("[!] nothing resolved - see messages above")
            return self
        self._refresh_once()
        self.dump()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()
        print(f"[*] refresh thread running ({REFRESH_MS} ms). "
              f"dr.freeze('name'[, value]) / dr.unfreeze('name') / dr.set_value('name', v) / dr.stop()")
        if push_to_pince is None:
            push_to_pince = _pince_window() is not None
        if push_to_pince:
            self.to_pince()
        return self

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join()
        self._thread = None
        if self._damage_guard:
            self.no_damage(False)
        self._stop.clear()
        if self.pince_win is not None:
            self._remove_pince_rows()
        if self.mem:
            self.mem.close()
            self.mem = None
        self.specs, self.addr, self.frozen = [], {}, {}
        return self

    # -- resolution -----------------------------------------------------
    def _resolve_specs(self):
        if self.r.detect_name_table():
            self.mode = "by-name"
            self.specs = build_specs_by_name(self.r)
        else:
            self.mode = "by-value (fallback)"
            self.specs = build_specs_by_value(self.r)
        print(f"[*] mode: {self.mode}   ({len(self.specs)} entries)")

    def _snapshot(self):
        return self.r.map_by_name() if self.r.name_ok else {}

    def _refresh_once(self):
        snap = self._snapshot()
        for sp in self.specs:
            try:
                self.addr[sp["desc"]] = sp["get"](snap)
            except Exception:
                self.addr[sp["desc"]] = None

    def _loop(self):
        while not self._stop.wait((GUARD_REFRESH_MS if self._damage_guard else REFRESH_MS) / 1000.0):
            with self._guard_lock:
                if self._damage_guard:
                    self._guard_tick()
            snap = self._snapshot()
            for sp in self.specs:
                desc = sp["desc"]
                try:
                    a = sp["get"](snap)
                except Exception:
                    a = None
                self.addr[desc] = a
                if a and desc in self.frozen:
                    self.mem.write_f64(a, self.frozen[desc])

    # -- user ops -----------------------------------------------------
    def _guard_tick(self):
        # Called under _guard_lock. Never guess inv from its numeric value.
        if self.r.global_obj() != self._guard_global:
            self._damage_guard = False
            self._guard_global = None
            print("[!] damage guard disarmed: global object changed; restart in the chapter")
            return False
        addr = self.r.map_by_name().get("inv")
        if not addr or not self.mem.write_f64(addr, GUARD_FRAMES):
            self._damage_guard = False
            self._guard_global = None
            print("[!] damage guard disarmed: global.inv unresolved, non-REAL, or write failed")
            return False
        return True

    def no_damage(self, enabled=True):
        """Toggle the inv timer guard. Scripted resets/direct HP writes can bypass it."""
        with self._guard_lock:
            if not enabled:
                if not self._damage_guard:
                    return True
                self._damage_guard = False
                same_global = self.r.global_obj() == self._guard_global
                self._guard_global = None
                addr = self.r.map_by_name().get("inv") if same_global else None
                ok = bool(addr and self.mem.write_f64(addr, 0))
                print("[*] damage guard disabled" if ok else
                      "[!] guard disabled; timer reset failed (remaining timer may expire naturally)")
                return ok
            if not self.mem or not self.r or not self.r.name_ok or not self.mem.writable:
                print("[!] damage guard requires an attached writable process and by-name resolution")
                return False
            self._guard_global = self.r.global_obj()
            if not self._guard_global:
                print("[!] damage guard: no global object")
                return False
            if not self._guard_tick():
                return False
            self._damage_guard = True
            print("[*] damage guard enabled: global.inv (scripted bypasses remain; see README)")
            return True

    def _match(self, pattern):
        p = pattern.lower()
        return [sp["desc"] for sp in self.specs if p in sp["desc"].lower()]

    def dump(self):
        if not self.specs:
            print("(nothing resolved)")
            return
        for sp in self.specs:
            desc = sp["desc"]
            a = self.addr.get(desc)
            val = self.mem.f64(a) if a else None
            tag = "  [FROZEN]" if desc in self.frozen else ""
            astr = f"@{a:#x}" if a else "(unresolved)"
            try:
                vstr = "?" if val is None else (f"{val:.0f}" if val == int(val) else f"{val}")
            except (ValueError, OverflowError):
                vstr = str(val)
            print(f"   {desc:<42} {astr:>16} = {vstr}{tag}")

    def list_instances(self, kind=KIND_CINSTANCE, limit=200):
        """List live instances via g_pInstanceManager (../08_instances.md).

        kind=1 (default) filters to real CInstance game objects; pass kind=None
        to see every entry regardless of kind (includes CScriptRef method refs
        etc. sharing the same table). Prints pointer + (x, y); does not (yet)
        identify *which* GML object each instance is - see the module docstring
        and 08_instances.md's open ends for why (typeIndex is debug-build-only).
        Returns the list of (ptr, kind, x, y) tuples.
        """
        if not self.r:
            print("(not resolved - call start() first)")
            return []
        out = []
        n = 0
        for ptr, k in self.r.iter_instances():
            if kind is not None and k != kind:
                continue
            pos = self.r.instance_pos(ptr) if k == KIND_CINSTANCE else None
            x, y = pos if pos else (None, None)
            out.append((ptr, k, x, y))
            n += 1
            if n >= limit:
                print(f"[*] stopping at limit={limit} (pass a higher limit for more)")
                break
        for ptr, k, x, y in out:
            pos_str = f"x={x:.1f} y={y:.1f}" if x is not None else "(no position - not a CInstance)"
            print(f"   0x{ptr:x}  kind={k}  {pos_str}")
        print(f"[*] {len(out)} instance(s){' (kind=' + str(kind) + ')' if kind is not None else ''}")
        return out

    def get(self, pattern):
        out = {}
        for desc in self._match(pattern):
            a = self.addr.get(desc)
            out[desc] = self.mem.f64(a) if a else None
        return out

    def set(self, pattern, value):
        if self.specs:
            self._refresh_once()
        hits = self._match(pattern)
        if not hits:
            print(f"[!] no entry matches {pattern!r}")
        for desc in hits:
            a = self.addr.get(desc)
            if a and self.mem.write_f64(a, value):
                print(f"[=] {desc} <- {value}")
            else:
                print(f"[!] {desc}: {'unresolved' if not a else 'write failed'}")

    def freeze(self, pattern, value=None):
        if self.specs:
            self._refresh_once()
        hits = self._match(pattern)
        if not hits:
            print(f"[!] no entry matches {pattern!r}")
        for desc in hits:
            a = self.addr.get(desc)
            v = value
            if v is None:
                v = self.mem.f64(a) if a else None
            if v is None:
                print(f"[!] {desc}: can't read current value to freeze")
                continue
            if not a or not self.mem.write_f64(a, v):
                print(f"[!] {desc}: unresolved, non-REAL, or write failed; not frozen")
                continue
            self.frozen[desc] = float(v)
            print(f"[*] freezing {desc} @ {v:g}")

    def unfreeze(self, pattern):
        for desc in list(self.frozen):
            if pattern.lower() in desc.lower():
                del self.frozen[desc]
                print(f"[*] unfroze {desc}")

    # -- PINCE address-table integration --------------------------------
    def to_pince(self, clear_existing=True):
        """Add the resolved entries to PINCE's address table (prefixed "[DR] ").

        Scalars go in as a static absolute address. Array elements go in as a PINCE
        pointer chain rooted at the current RValue,
        so PINCE re-walks [rv] -> +0x90 -> +16*i every refresh and survives buffer moves.
        Freeze with PINCE's own Freeze checkbox; dr.stop() removes the rows.
        """
        win = _pince_window()
        if win is None:
            print("[!] not running inside a live PINCE GUI - nothing to push to. "
                  "Use dr.dump() / dr.freeze(...) instead, or run this from PINCE's Libpince Engine.")
            return self
        try:
            from libpince import debugcore, typedefs  # type: ignore
        except Exception as e:
            print(f"[!] libpince not importable: {e}")
            return self
        if not self.specs:
            print("[!] nothing resolved yet - call dr.start() first")
            return self

        pince_pid = getattr(debugcore, "currentpid", -1)
        if pince_pid != self.mem.pid:
            print(f"[!] PINCE is attached to pid {pince_pid}, resolver is on pid {self.mem.pid}. "
                  f"Attach PINCE to the same DELTARUNE.exe or the rows won't show values.")
            return self

        if clear_existing:
            self._remove_pince_rows()

        f64 = typedefs.FloatValueType(64)
        snap = self._snapshot()
        added = 0
        for sp in self.specs:
            meta = sp.get("pince")
            if not meta:
                continue
            rv = meta.get("rv") or (snap.get(meta["var"]) if meta.get("var") else None)
            if not rv:
                continue
            if meta["kind"] == "scalar":
                if self.r.kind_of(rv) != 0:
                    continue
                expr = hex(rv)
            else:
                if self.r.arr_elem_addr(rv, meta["index"]) is None:
                    continue
                expr = typedefs.PointerChainRequest(hex(rv), [ARR_ELEMS, 16 * meta["index"]])
            try:
                item = win.add_entry_to_addresstable("[DR] " + sp["desc"], expr, f64)
                self.pince_rows.append(item)
                added += 1
            except Exception as e:
                print(f"[!] add '{sp['desc']}' failed: {e}")
        try:
            win.update_address_table()
        except Exception:
            pass
        self.pince_win = win
        self._install_guard_controls(win)
        print(f"[*] added {added} '[DR] ' entries to PINCE's address table. "
              f"Tick Freeze to lock a row; dr.stop() (or dr.from_pince()) removes them.")
        return self

    def from_pince(self):
        """Remove the [DR] rows this trainer added to PINCE's address table."""
        self._remove_pince_rows()
        return self

    def _install_guard_controls(self, win):
        """GUI-thread controls; the worker never touches Qt widgets."""
        if self._guard_toolbar is not None:
            return
        from PyQt6.QtCore import QTimer
        from PyQt6.QtWidgets import QCheckBox, QLabel, QToolBar
        toolbar = QToolBar("DELTARUNE trainer", win)
        toolbar.setObjectName("deltaruneTrainerToolbar")
        checkbox = QCheckBox("[DR] No damage", toolbar)
        checkbox.setToolTip("Blocks normal damage checks using invulnerability. "
                            "Some scripted hits bypass this protection.")
        status = QLabel(toolbar)
        toolbar.addWidget(checkbox)
        toolbar.addWidget(status)
        win.addToolBar(toolbar)
        self._guard_toolbar, self._guard_checkbox = toolbar, checkbox
        self._guard_status = status
        checkbox.toggled.connect(self._guard_checkbox_toggled)
        timer = QTimer(toolbar)
        timer.setInterval(100)
        timer.timeout.connect(self._sync_guard_controls)
        self._guard_timer = timer
        self._sync_guard_controls()
        timer.start()

    def _guard_checkbox_toggled(self, checked):
        # Recheck PINCE's target at click time, not only on the UI timer.
        self._guard_control_error = None
        if checked and _pince_attached_pid() != getattr(self.mem, "pid", None):
            self._sync_guard_controls()
            return
        ok = self.no_damage(checked)
        if not ok:
            self._guard_control_error = "  Could not update guard — see tooltip"
            self._guard_status.setToolTip("global.inv could not be resolved or written. "
                                         "Check the attached chapter and trainer connection.")
        self._sync_guard_controls()

    def _sync_guard_controls(self):
        if self._guard_checkbox is None:
            return
        same_pid = self.mem is not None and _pince_attached_pid() == self.mem.pid
        if not same_pid and self._damage_guard:
            self.no_damage(False)
        ready = bool(same_pid and self.mem.writable and self.r and self.r.name_ok)
        checkbox = self._guard_checkbox
        checkbox.blockSignals(True)
        checkbox.setChecked(self._damage_guard)
        checkbox.setEnabled(ready)
        checkbox.blockSignals(False)
        self._guard_status.setText("  Active — scripted hits may bypass" if self._damage_guard else
                                   (self._guard_control_error or "  Off") if ready else
                                   "  Attach the trainer's chapter to enable")

    def _remove_guard_controls(self):
        if self._guard_toolbar is None:
            return
        if self._damage_guard:
            self.no_damage(False)
        self._guard_timer.stop()
        self._guard_toolbar.parentWidget().removeToolBar(self._guard_toolbar)
        self._guard_toolbar.deleteLater()
        self._guard_toolbar = self._guard_checkbox = None
        self._guard_status = self._guard_timer = None

    def _remove_pince_rows(self):
        self._remove_guard_controls()
        win = self.pince_win or _pince_window()
        if win is None:
            self.pince_rows = []
            return
        tree = getattr(win, "treeWidget_AddressTable", None)
        removed = 0
        for item in self.pince_rows:
            try:
                idx = tree.indexOfTopLevelItem(item)
                if idx >= 0:
                    tree.takeTopLevelItem(idx)
                    removed += 1
            except Exception:
                pass
        self.pince_rows = []
        self.pince_win = None
        if removed:
            try:
                win.update_address_table()
                win.mark_address_tree_changed()
            except Exception:
                pass
            print(f"[*] removed {removed} '[DR] ' entries from PINCE's address table")

    # Lua-parity aliases
    set_value = set
    poke = set

    def dr_find(self):
        return self.start()

    def dr_stop(self):
        return self.stop()


# Module-level singleton so state survives across PINCE Libpince-Engine runs.
_T = Trainer()

def start(pid=None, push_to_pince=None): return _T.start(pid, push_to_pince)
def stop():            return _T.stop()
def dump():            return _T.dump()
def list_instances(kind=KIND_CINSTANCE, limit=200): return _T.list_instances(kind, limit)
def get(p):            return _T.get(p)
def set_value(p, v):   return _T.set(p, v)        # not named set() - would shadow builtin set in this module
def poke(p, v):        return _T.set(p, v)
def freeze(p, v=None): return _T.freeze(p, v)
def unfreeze(p):       return _T.unfreeze(p)
def no_damage(enabled=True): return _T.no_damage(enabled)
def to_pince(clear_existing=True): return _T.to_pince(clear_existing)
def from_pince():      return _T.from_pince()
def dr_find():         return _T.start()
def dr_stop():         return _T.stop()


def _main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="DELTARUNE stat resolver (Linux/PINCE port)")
    ap.add_argument("--pid", type=int, default=None, help="target pid (default: auto-detect)")
    ap.add_argument("--once", action="store_true", help="resolve + dump once, don't start the freezer/REPL")
    args = ap.parse_args(argv)

    # On most kernels a same-user process can open /proc/<pid>/mem read-write without
    # root even with yama ptrace_scope=1. If it can't, start() prints "(read-only!)"
    # and writes are refused - re-run with sudo in that case.
    _T.start(args.pid)
    if args.once or not _T.specs:
        _T.stop()
        return

    banner = ("\nInteractive: dr.dump() | dr.freeze('Gold', 999999) | dr.freeze('HP Kris') |"
              " dr.unfreeze('HP') | dr.no_damage(True/False) | dr.set('TP', 250) | dr.stop()"
              "\n(here `dr` is the trainer object, so dr.set(...) works; the module-level"
              " alias is set_value())\nCtrl-D to quit.\n")
    try:
        import code
        code.interact(banner=banner, local={"dr": _T, **globals()})
    except SystemExit:
        pass
    finally:
        _T.stop()


if __name__ == "__main__":
    _main(sys.argv[1:])
