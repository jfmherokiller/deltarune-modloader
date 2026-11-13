#!/usr/bin/env python3
"""Scan decompiled DELTARUNE code for literal global.flag[n] / global.plot uses.

Input:  RE/code/chapterN/*.gml (UTMT dumps, gitignored; see RESEARCH.md).
Output: JSON on stdout (or -o FILE):
  {"flags": {"<n>": {"<ch>": {"entries": [...], "set": [...], "cmp": [...]}}},
   "plot":  {"<ch>": {"set": {"<v>": [entries]}, "cmp": {"<v>": count}}}}

Used to check data/flags.json against the game: which chapters touch each flag,
which values get written, and which plot values the code sets or tests. Re-run
after a game update and diff the result.

  python3 tools/scan_flag_usage.py --code-root ../../RE/code -o /tmp/usage.json
"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict

IDX = r"(?:(\d+)|UnknownEnum\.Value_(\d+))"
# global.flag[N] ... with an optional following operator + value
FLAG_RE = re.compile(r"global\.flag\[" + IDX + r"\]\s*(\+\+|--|==|!=|>=|<=|>|<|\+=|-=|=(?!=))?\s*(-?[\d.]+|true|false)?")
CALL_RE = re.compile(r"scr_flag_(get|set)(?:_ext)?\(\s*" + IDX + r"\s*(?:,\s*(-?[\d.]+|true|false))?")
# indirect: chests, encounters, switches etc. store their flag index in a variable
# (itemflag = 104; encounterflag = 530; ...) set from room creation code
INDIRECT_RE = re.compile(r"\b(\w*flag\w*)\s*=\s*(\d{2,4})\s*;")
PLOT_RE = re.compile(r"global\.plot\s*(==|!=|>=|<=|>|<|=(?!=))\s*(-?\d+)")


def recruits(root):
    """{ch: {id: {"name", "required"}}} from each chapter's scr_recruit_info switch.
    Recruit progress lives in global.flag[600 + id]."""
    out = {}
    for ch in "12345":
        p = os.path.join(root, f"chapter{ch}", "gml_GlobalScript_scr_recruit_info.gml")
        if not os.path.exists(p):
            continue
        text = open(p, encoding="utf-8", errors="replace").read()
        parts = re.split(r"\n\s*case (\d+):", text)
        res = {}
        for i in range(1, len(parts), 2):
            body = parts[i + 1].split("break;")[0]
            nm = re.search(r'_name = stringsetloc\("([^"]*)"', body) or re.search(r'_name = "([^"]*)"', body)
            rc = re.search(r"_recruitcount = (\d+)", body)
            res[parts[i]] = {"name": nm.group(1).strip() if nm else None, "required": int(rc.group(1)) if rc else 1}
        out[ch] = res
    return out


def scan(root):
    flags = defaultdict(lambda: defaultdict(lambda: {"entries": set(), "set": set(), "cmp": set()}))
    plot = defaultdict(lambda: {"set": defaultdict(set), "cmp": defaultdict(int)})
    for chdir in sorted(os.listdir(root)):
        m = re.fullmatch(r"chapter(\d)", chdir)
        if not m:
            continue
        ch = m.group(1)
        d = os.path.join(root, chdir)
        for fn in os.listdir(d):
            if not fn.endswith(".gml"):
                continue
            entry = fn[:-4]
            try:
                text = open(os.path.join(d, fn), encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            for mm in FLAG_RE.finditer(text):
                n = int(mm.group(1) or mm.group(2))
                rec = flags[n][ch]
                rec["entries"].add(entry)
                op, val = mm.group(3), mm.group(4)
                if op in ("++", "--"):
                    rec["set"].add(op)
                elif op in ("+=", "-="):
                    rec["set"].add(op + (val or ""))
                elif op and val is not None:
                    (rec["set"] if op == "=" else rec["cmp"]).add(val if op in ("=", "==") else op + val)
            for mm in CALL_RE.finditer(text):
                n = int(mm.group(2) or mm.group(3))
                rec = flags[n][ch]
                rec["entries"].add(entry)
                if mm.group(1) == "set" and mm.group(4) is not None:
                    rec["set"].add(mm.group(4))
            for mm in INDIRECT_RE.finditer(text):
                var, n = mm.group(1), int(mm.group(2))
                if "plot" in var.lower() or var.startswith("global"):
                    continue
                flags[n][ch]["entries"].add(f"{entry} ({var})")
            for mm in PLOT_RE.finditer(text):
                op, v = mm.groups()
                if op == "=":
                    plot[ch]["set"][v].add(entry)
                else:
                    plot[ch]["cmp"][v] += 1
    out = {"flags": {}, "plot": {}, "recruits": recruits(root)}
    for n in sorted(flags):
        out["flags"][str(n)] = {
            ch: {"entries": sorted(r["entries"]), "set": sorted(r["set"]), "cmp": sorted(r["cmp"])}
            for ch, r in sorted(flags[n].items())
        }
    for ch, r in sorted(plot.items()):
        out["plot"][ch] = {
            "set": {v: sorted(e) for v, e in sorted(r["set"].items(), key=lambda kv: int(kv[0]))},
            "cmp": dict(sorted(r["cmp"].items(), key=lambda kv: int(kv[0]))),
        }
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--code-root", default=os.path.join(here, "..", "..", "..", "RE", "code"))
    ap.add_argument("-o", "--output")
    a = ap.parse_args()
    if not os.path.isdir(a.code_root):
        sys.exit(f"no decompiled code at {a.code_root} (dump it first, see RESEARCH.md)")
    data = scan(a.code_root)
    s = json.dumps(data, indent=1)
    if a.output:
        open(a.output, "w", encoding="utf-8").write(s)
    else:
        print(s)


if __name__ == "__main__":
    main()
