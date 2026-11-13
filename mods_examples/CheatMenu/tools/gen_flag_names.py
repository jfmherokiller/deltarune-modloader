#!/usr/bin/env python3
"""Build and export CheatMenu's known-flag list (data/flags.json).

The game's own flag-name table (scr_flag_names / global.flagname) is stripped from
release builds, so the flag viewer ships this list instead. data/flags.json is the
single source of truth; the GML the menu compiles in is generated from it.

  gml     data/flags.json -> chapterN_windows/code/gml_GlobalScript_scr_drcheat_flagnames.gml
          (no external input; run after every edit of flags.json)

  import  refresh the community entries and the chapter lists:
            --tenna DIR      checkout of github.com/tennaproject/tenna-editor (zlib);
                             read through `node` (src/data/flags.ts, plot-points.ts)
            --utdrwiki DIR   checkout of github.com/utdrwiki/code-viewer (MIT);
                             data/deltarune/flags.json, names only, fallback
            --usage FILE     output of tools/scan_flag_usage.py (chapters per flag,
                             recruit names); optional but recommended
          Entries whose "src" is "ours" are never overwritten (only their "ch" list is
          refreshed), so hand edits survive a re-import: set "src": "ours" on any entry
          you correct. Everything else is rebuilt from the sources.

See data/README.md for the file format.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
MOD = os.path.dirname(HERE)
DATA = os.path.join(MOD, "data", "flags.json")
GML_NAME = "gml_GlobalScript_scr_drcheat_flagnames.gml"
CHAPTERS = (1, 2, 3, 4, 5)

TENNA_DUMP = r"""
const dir = process.argv[2];
const f = await import(dir + '/flags.ts');
const p = await import(dir + '/plot-points.ts');
const st = await import(dir + '/story-sections.ts');
const group = {};  // FLAG_ID -> "Section / Cluster" from the editor's Story tab
for (const secs of Object.values(st.STORY_SECTIONS))
  for (const s of secs)
    for (const c of (s.clusters || [{ title: null, flags: s.flags }]))
      for (const fl of c.flags) if (!(fl in group)) group[fl] = c.title || s.title;
const names = {};
for (const [k, v] of Object.entries(f.FLAGS)) names[v] = k;
const out = { flags: {}, plot: {} };
for (const [i, k] of Object.entries(names)) out.flags[i] = { id: k, group: group[k], ...(f.FLAGS_META[i] || {}) };
for (const [ch, meta] of Object.entries(p.PLOT_META_BY_CHAPTER)) {
  const rev = {};
  for (const [k, v] of Object.entries(p[`CH${ch}_PLOT`] || {})) rev[v] = k;
  out.plot[ch] = {};
  for (const [v, m] of Object.entries(meta)) out.plot[ch][v] = { id: rev[v], ...m };
}
console.log(JSON.stringify(out, (k, v) => v instanceof Set ? [...v] : v));
"""

# descriptions that admit the meaning is a guess
HEDGE = re.compile(r"(?i)something to do with|possibly|probably|unclear|unknown|\bmaybe\b|might be related|TODO")
TYPE_MAP = {"boolean": "bool", "number": "number", "map": "enum", "color": "color"}


def load(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def save(path, data):
    # one flag / plot value per line: readable and diff-friendly
    def one(v):
        return json.dumps(v, ensure_ascii=False)
    L = ["{"]
    keys = list(data)
    for i, k in enumerate(keys):
        v, comma = data[k], "," if i < len(keys) - 1 else ""
        if k == "flags":
            L.append(f' "flags": {{')
            items = list(v.items())
            L += [f"  {one(n)}: {one(e)}" + ("," if j < len(items) - 1 else "") for j, (n, e) in enumerate(items)]
            L.append(" }" + comma)
        elif k == "plot":
            L.append(' "plot": {')
            chs = list(v.items())
            for j, (ch, vals) in enumerate(chs):
                L.append(f"  {one(ch)}: {{")
                items = list(vals.items())
                L += [f"   {one(n)}: {one(e)}" + ("," if m < len(items) - 1 else "") for m, (n, e) in enumerate(items)]
                L.append("  }" + ("," if j < len(chs) - 1 else ""))
            L.append(" }" + comma)
        else:
            L.append(f" {one(k)}: " + json.dumps(v, ensure_ascii=False, indent=1).replace("\n", "\n ") + comma)
    L.append("}")
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")


FIELD_ORDER = ("name", "label", "group", "desc", "type", "values", "min", "max", "ch", "conf", "src",
               "alias", "volatile", "unused")


def tidy(e):
    return {k: e[k] for k in sorted(e, key=lambda k: FIELD_ORDER.index(k) if k in FIELD_ORDER else 99)}


def numkey(k):
    return float(k)


def tenna(dir_):
    src = os.path.join(os.path.abspath(dir_), "src", "data")
    with tempfile.NamedTemporaryFile("w", suffix=".mjs", delete=False) as fh:
        fh.write(TENNA_DUMP)
        script = fh.name
    try:
        out = subprocess.run(["node", script, src], check=True, capture_output=True, text=True).stdout
    finally:
        os.unlink(script)
    return json.loads(out)


def from_tenna(t):
    e = {"name": t["id"].lower(), "label": t.get("displayName") or t["id"].replace("_", " ").capitalize()}
    if t.get("group"):
        e["group"] = t["group"]
    if t.get("description"):
        e["desc"] = t["description"]
    typ = TYPE_MAP.get(t.get("valueType"))
    if typ:
        e["type"] = typ
    rules = t.get("valueRules") or {}
    if rules.get("map"):
        e["values"] = {k: v for k, v in sorted(rules["map"].items(), key=lambda kv: numkey(kv[0]))}
    for k in ("min", "max"):
        if k in rules:
            e[k] = rules[k]
    if t.get("volatile"):
        e["volatile"] = True
    if t.get("unused"):
        e["unused"] = True
    e["conf"] = "uncertain" if HEDGE.search(e.get("desc", "")) else "community"
    e["src"] = "tenna"
    return e


def chapters_of(usage, n):
    return sorted(int(c) for c in usage.get("flags", {}).get(str(n), {}))


def cmd_import(a):
    old = load(DATA) if os.path.exists(DATA) else {}
    flags_old = old.get("flags", {})
    usage = load(a.usage) if a.usage else {}
    t = tenna(a.tenna) if a.tenna else {"flags": {}, "plot": {}}
    wiki = load(os.path.join(a.utdrwiki, "data", "deltarune", "flags.json")) if a.utdrwiki else {}

    flags = {}
    for k, e in flags_old.items():
        if e.get("src") == "ours":
            flags[k] = dict(e)
    for k, te in t["flags"].items():
        if k not in flags:
            flags[k] = from_tenna(te)
    for k, nm in wiki.items():
        if k in flags:
            # keep the wiki's short name as a searchable alias
            if flags[k].get("src") == "tenna" and flags[k]["name"] != nm.lower():
                flags[k]["alias"] = nm
            continue
        flags[k] = {"name": nm.rstrip("?"), "label": nm.rstrip("?").replace("_", " ").capitalize(),
                    "conf": "uncertain" if nm.endswith("?") else "community", "src": "utdrwiki"}
    # recruit progress: flag[600 + id] = recruited / required (-1 = lost)
    for ch, recs in usage.get("recruits", {}).items():
        for rid, info in recs.items():
            k = str(600 + int(rid))
            if not info.get("name") or (k in flags and flags[k].get("src") in ("ours", "tenna")):
                continue
            flags[k] = {"name": "recruit_" + re.sub(r"\W+", "_", info["name"].lower()).strip("_"),
                        "label": "Recruit: " + info["name"],
                        "desc": f"Recruit progress, count/required (needs {info.get('required', 1)}); -1 = lost.",
                        "type": "number", "conf": "verified", "src": "recruit_info"}
    # recruit flags are fractions, not booleans: scr_recruit adds 1/recruitcount per recruit,
    # capped at 1; -1 marks a lost recruit. They are read through computed indices
    # (global.flag[600 + monstertype]), so their chapters come from scr_recruit_info.
    rec_ch = {}
    for ch, recs in usage.get("recruits", {}).items():
        for rid in recs:
            rec_ch.setdefault(str(600 + int(rid)), set()).add(int(ch))
    for k, e in flags.items():
        if 600 < int(k) < 700 and e.get("src") == "tenna" and e["name"].startswith("recruit"):
            e["type"] = "number"
            e["values"] = {"-1": "Lost", "0": "Not met", "1": "Recruited"}
            e["desc"] = ("Recruit progress, recruited/required as a fraction (scr_recruit adds "
                         "1/recruitcount). " + e.get("desc", "")).strip()
            e["conf"] = "verified"
    for k, e in flags.items():
        ch = sorted(set(chapters_of(usage, k)) | rec_ch.get(k, set())) if usage else e.get("ch", [])
        if ch:
            e["ch"] = ch
        else:
            e.pop("ch", None)

    plot = {}
    for ch in map(str, CHAPTERS):
        p = {}
        for v, e in old.get("plot", {}).get(ch, {}).items():
            if e.get("src") == "ours":
                p[v] = e
        for v, te in t["plot"].get(ch, {}).items():
            if v in p:
                continue
            e = {"name": te["id"].lower(), "label": te["displayName"]}
            if te.get("description"):
                e["desc"] = te["description"]
            if te.get("unused"):
                e["unused"] = True
            e["src"] = "tenna"
            p[v] = e
        plot[ch] = dict(sorted(p.items(), key=lambda kv: numkey(kv[0])))

    out = {k: v for k, v in old.items() if k not in ("flags", "plot")}
    rev = out.setdefault("revisions", {})
    for key, d in (("tenna", a.tenna), ("utdrwiki", a.utdrwiki)):
        if d:
            r = subprocess.run(["git", "-C", d, "log", "-1", "--format=%h %cs"], capture_output=True, text=True)
            if r.returncode == 0:
                rev[key] = r.stdout.strip()
    out["flags"] = {k: tidy(e) for k, e in sorted(flags.items(), key=lambda kv: int(kv[0]))}
    out["plot"] = plot
    save(DATA, out)
    print(f"wrote {DATA}: {len(flags)} flags, " + ", ".join(f"ch{c} plot {len(p)}" for c, p in plot.items()))


# ---------------------------------------------------------------- GML export
ASCII = {"\u2018": "'", "\u2019": "'", "\u201c": '"', "\u201d": '"', "\u2013": "-", "\u2014": "-",
         "\u2026": "...", "\u00d7": "x", "\u2192": "->", "\u2190": "<-", "\u00a0": " "}


def gml_str(s):
    s = "".join(ASCII.get(c, c) for c in str(s))
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ") + '"'


def values_text(e):
    parts = []
    if e.get("values"):
        parts.append(", ".join(f"{k}={v}" for k, v in e["values"].items()))
    elif e.get("type") == "bool":
        parts.append("0/1")
    if "min" in e or "max" in e:
        parts.append(f"{e.get('min', '')}..{e.get('max', '')}")
    return "; ".join(parts)


def flag_in_chapter(e, ch):
    # flags carry over between chapters through the save, so a chapter shows every flag
    # first used in it or an earlier one; entries with no known use are shown everywhere
    c = e.get("ch")
    return not c or min(c) <= ch


def cmd_gml(a):
    d = load(DATA)
    for ch in CHAPTERS:
        rows = [(int(k), e) for k, e in d["flags"].items() if flag_in_chapter(e, ch)]
        plot = d["plot"].get(str(ch), {})
        top = max(k for k, _ in rows) + 1
        L = [
            "// generated by CheatMenu/tools/gen_flag_names.py from data/flags.json; do not edit.",
            f"// Chapter {ch}: {len(rows)} known global.flag entries, {len(plot)} global.plot milestones.",
            "// scr_drcheat_flagnames(): array indexed by flag number; entry is undefined (unknown)",
            "// or [short name, label, description, value notes, confidence].",
            "// confidence: 0 = uncertain, 1 = community list, 2 = checked against the game code.",
            "function scr_drcheat_flagnames()",
            "{",
            f"    var f = array_create({top}, undefined);",
        ]
        conf = {"uncertain": 0, "community": 1, "verified": 2}
        for k, e in sorted(rows):
            desc = e.get("desc", "")
            if e.get("unused"):
                desc = (desc + " (unused)").strip()
            label = e["label"]
            g = e.get("group")
            if g and g.lower() not in label.lower():
                label = f"{g}: {label}"  # Tenna labels are often bare ("Defeated") within a group
            L.append(f"    f[{k}] = [{gml_str(e['name'])}, {gml_str(label)}, {gml_str(desc)}, "
                     f"{gml_str(values_text(e))}, {conf.get(e.get('conf'), 1)}];")
        L += ["    return f;", "}", "",
              "// scr_drcheat_plotnames(): [[global.plot value, label, description], ...] ascending.",
              "// The current milestone is the last entry whose value <= global.plot.",
              "function scr_drcheat_plotnames()",
              "{",
              "    return ["]
        items = sorted(plot.items(), key=lambda kv: numkey(kv[0]))
        for i, (v, e) in enumerate(items):
            sep = "," if i < len(items) - 1 else ""
            L.append(f"        [{v}, {gml_str(e['label'])}, {gml_str(e.get('desc', ''))}]{sep}")
        L += ["    ];", "}", ""]
        out = os.path.join(MOD, f"chapter{ch}_windows", "code", GML_NAME)
        with open(out, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("\n".join(L))
        print(f"wrote {os.path.relpath(out, MOD)}: {len(rows)} flags, {len(items)} plot values")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    i = sub.add_parser("import")
    i.add_argument("--tenna")
    i.add_argument("--utdrwiki")
    i.add_argument("--usage")
    sub.add_parser("gml")
    a = ap.parse_args()
    {"import": cmd_import, "gml": cmd_gml}[a.cmd](a)


if __name__ == "__main__":
    main()
