// ImportLooseMod.csx - non-interactive loose-asset import for UndertaleModCli.
// Run by the DELTARUNE modloader when the game reads a data.win (see modloader/README.md).
//
// env DR_MOD_DIRS = ';'-separated mod roots for this data.win, in load order (later wins),
//                   e.g. mods\CheatMenu\chapter2_windows;mods\Other\chapter2_windows
// env DR_OUT_PIPE = named pipe to stream the patched data.win to (optional; if unset the
//                   CLI's own -o option can be used for testing)
//
// Inside each mod root:
//   sprites\<sprite_name>_<frame>.png   replace a frame / add frames / create a new sprite
//   sprites\<sprite_name>.origin.txt    optional "x y" origin for NEW sprites (default 0 0)
//   sounds\<sound_name>.ogg|.wav        replace an existing sound or add a new one
//                                       (embedded into data.win, default audio group)
//   code\<gml_CodeName>.gml             replace / create code (UTMT naming)
// Sprites and sounds are imported first so GML can reference new asset names.
using System;
using System.IO;
using System.Linq;
using System.Collections;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using UndertaleModLib.Util;
using ImageMagick;

EnsureDataLoaded();

string dirsEnv = Environment.GetEnvironmentVariable("DR_MOD_DIRS")
              ?? Environment.GetEnvironmentVariable("DR_GML_DIRS");   // old name: code dirs only
if (string.IsNullOrEmpty(dirsEnv))
    throw new ScriptException("DR_MOD_DIRS is not set");
var roots = dirsEnv.Split(';', StringSplitOptions.RemoveEmptyEntries)
                   .Select(d => Path.GetFileName(d.TrimEnd('\\', '/')).Equals("code", StringComparison.OrdinalIgnoreCase)
                                ? Path.GetDirectoryName(d.TrimEnd('\\', '/')) : d)
                   .Where(Directory.Exists).ToList();

// ---------------------------------------------------------------- collect (later mod wins)
var frameRx = new Regex(@"^(.+?)_(\d+)$");
var spriteFrames = new Dictionary<string, SortedDictionary<int, string>>(StringComparer.Ordinal);
var spriteOrigins = new Dictionary<string, (int, int)>(StringComparer.Ordinal);
var sounds = new Dictionary<string, string>(StringComparer.Ordinal);
var gml = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
var patchFiles = new List<string>();
foreach (string root in roots)
{
    string sd = Path.Combine(root, "sprites");
    if (Directory.Exists(sd))
    {
        foreach (string f in Directory.GetFiles(sd, "*.png", SearchOption.AllDirectories))
        {
            var m = frameRx.Match(Path.GetFileNameWithoutExtension(f));
            if (!m.Success) { Console.WriteLine($"[DR] WARN sprite file without _<frame>: {f} (skipped)"); continue; }
            if (!spriteFrames.TryGetValue(m.Groups[1].Value, out var frames))
                spriteFrames[m.Groups[1].Value] = frames = new SortedDictionary<int, string>();
            frames[int.Parse(m.Groups[2].Value)] = f;
        }
        foreach (string f in Directory.GetFiles(sd, "*.origin.txt", SearchOption.AllDirectories))
        {
            var p = File.ReadAllText(f).Split(new[] { ' ', ',', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
            if (p.Length >= 2) spriteOrigins[Path.GetFileName(f)[..^".origin.txt".Length]] = (int.Parse(p[0]), int.Parse(p[1]));
        }
    }
    string snd = Path.Combine(root, "sounds");
    if (Directory.Exists(snd))
        foreach (string f in Directory.GetFiles(snd))
            if (f.EndsWith(".ogg", StringComparison.OrdinalIgnoreCase) || f.EndsWith(".wav", StringComparison.OrdinalIgnoreCase))
                sounds[Path.GetFileNameWithoutExtension(f)] = f;
    string cd = Path.Combine(root, "code");
    if (Directory.Exists(cd))
        foreach (string f in Directory.GetFiles(cd).OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
        {
            string fn = Path.GetFileName(f);
            if (fn.EndsWith(".append.gml", StringComparison.OrdinalIgnoreCase) ||
                fn.EndsWith(".prepend.gml", StringComparison.OrdinalIgnoreCase) ||
                fn.EndsWith(".patch", StringComparison.OrdinalIgnoreCase) ||
                fn.EndsWith(".diff", StringComparison.OrdinalIgnoreCase))
                patchFiles.Add(f);                                   // applied in mod order
            else if (fn.EndsWith(".gml", StringComparison.OrdinalIgnoreCase))
                gml[Path.GetFileNameWithoutExtension(f)] = f;        // later mod wins
        }
}
Console.WriteLine($"[DR] {roots.Count} mod root(s): {spriteFrames.Count} sprite(s), {sounds.Count} sound(s), {gml.Count} GML file(s), {patchFiles.Count} patch file(s)");

// ---------------------------------------------------------------- sprites
if (spriteFrames.Count > 0)
    ImportSprites();

void ImportSprites()
{
    var images = new List<(string sprite, int frame, MagickImage img)>();
    try
    {
        foreach (var (name, frames) in spriteFrames)
            foreach (var (frame, path) in frames)
            {
                var img = new MagickImage(path);
                img.Format = MagickFormat.Png32;
                images.Add((name, frame, img));
            }

        // simple shelf packer, 2 px padding, pages up to 2048x2048
        const int PAGE = 2048, PAD = 2;
        var order = images.OrderByDescending(i => i.img.Height).ThenByDescending(i => i.img.Width).ToList();
        var pages = new List<List<(int x, int y, (string sprite, int frame, MagickImage img) item)>>();
        var cur = new List<(int, int, (string, int, MagickImage))>();
        int cx = 0, cy = 0, rowH = 0;
        foreach (var it in order)
        {
            int w = (int)it.img.Width, h = (int)it.img.Height;
            if (w > PAGE || h > PAGE) throw new ScriptException($"sprite {it.sprite}_{it.frame} is larger than {PAGE}x{PAGE}");
            if (cx + w > PAGE) { cx = 0; cy += rowH + PAD; rowH = 0; }
            if (cy + h > PAGE) { pages.Add(cur); cur = new(); cx = 0; cy = 0; rowH = 0; }
            cur.Add((cx, cy, it));
            cx += w + PAD; rowH = Math.Max(rowH, h);
        }
        if (cur.Count > 0) pages.Add(cur);

        bool noMasksForBasicRectangles = Data.IsVersionAtLeast(2022, 9);
        foreach (var page in pages)
        {
            int pw = 1, ph = 1;
            foreach (var (x, y, it) in page) { pw = Math.Max(pw, x + (int)it.img.Width); ph = Math.Max(ph, y + (int)it.img.Height); }
            using var atlas = new MagickImage(MagickColors.Transparent, (uint)pw, (uint)ph);
            foreach (var (x, y, it) in page)
                atlas.Composite(it.img, x, y, CompositeOperator.Copy);

            var tex = new UndertaleEmbeddedTexture();
            tex.Name = new UndertaleString($"Texture {Data.EmbeddedTextures.Count}");
            tex.TextureData.Image = GMImage.FromMagickImage(atlas).ConvertToPng();
            Data.EmbeddedTextures.Add(tex);

            foreach (var (x, y, it) in page)
            {
                int w = (int)it.img.Width, h = (int)it.img.Height;
                var tpi = new UndertaleTexturePageItem
                {
                    Name = new UndertaleString($"PageItem {Data.TexturePageItems.Count}"),
                    SourceX = (ushort)x, SourceY = (ushort)y, SourceWidth = (ushort)w, SourceHeight = (ushort)h,
                    TargetX = 0, TargetY = 0, TargetWidth = (ushort)w, TargetHeight = (ushort)h,
                    BoundingWidth = (ushort)w, BoundingHeight = (ushort)h,
                    TexturePage = tex
                };
                Data.TexturePageItems.Add(tpi);
                var entry = new UndertaleSprite.TextureEntry { Texture = tpi };

                var spr = Data.Sprites.ByName(it.sprite);
                bool isNew = spr is null;
                if (isNew)
                {
                    spr = new UndertaleSprite { Name = Data.Strings.MakeString(it.sprite) };
                    spr.Width = (uint)w; spr.Height = (uint)h;
                    spr.MarginLeft = 0; spr.MarginTop = 0; spr.MarginRight = w - 1; spr.MarginBottom = h - 1;
                    if (spriteOrigins.TryGetValue(it.sprite, out var o)) { spr.OriginX = o.Item1; spr.OriginY = o.Item2; }
                    Data.Sprites.Add(spr);
                    Console.WriteLine($"[DR] new sprite {it.sprite} ({w}x{h})");
                }
                while (spr.Textures.Count <= it.frame) spr.Textures.Add(null);
                spr.Textures[it.frame] = entry;

                if (it.frame == 0 && (w != spr.Width || h != spr.Height))
                {
                    Console.WriteLine($"[DR] sprite {it.sprite} resized {spr.Width}x{spr.Height} -> {w}x{h}");
                    spr.Width = (uint)w; spr.Height = (uint)h;
                    if (spr.BBoxMode != 2) { spr.MarginLeft = 0; spr.MarginTop = 0; spr.MarginRight = w - 1; spr.MarginBottom = h - 1; }
                }

                // collision mask from frame 0 (only where the runner needs one)
                bool needMask = it.frame == 0 && (isNew
                    ? !noMasksForBasicRectangles
                    : spr.CollisionMasks.Count > 0);
                if (needMask)
                {
                    spr.CollisionMasks.Clear();
                    spr.CollisionMasks.Add(spr.NewMaskEntry(Data));
                    var (mw, mh) = spr.CalculateMaskDimensions(Data);
                    int stride = ((mw + 7) / 8) * 8;
                    var bits = new BitArray(stride * mh);
                    var px = it.img.GetPixels();
                    for (int yy = 0; yy < mh && yy < h; yy++)
                        for (int xx = 0; xx < mw && xx < w; xx++)
                            bits[yy * stride + xx] = px.GetPixel(xx, yy).ToColor().A > 0;
                    var rev = new BitArray(bits.Length);
                    for (int i = 0; i < bits.Length; i += 8)
                        for (int j = 0; j < 8; j++)
                            rev[j + i] = bits[-(j - 7) + i];
                    var bytes = new byte[bits.Length / 8];
                    rev.CopyTo(bytes, 0);
                    Array.Copy(bytes, spr.CollisionMasks[0].Data, Math.Min(bytes.Length, spr.CollisionMasks[0].Data.Length));
                }
            }
        }
        foreach (var (name, frames) in spriteFrames)
        {
            var spr = Data.Sprites.ByName(name);
            int holes = spr.Textures.Count(t => t is null);
            if (holes > 0) throw new ScriptException($"sprite {name} has {holes} missing frame(s); new frames must be numbered from 0 without gaps");
        }
        Console.WriteLine($"[DR] sprites OK ({images.Count} frame(s) on {pages.Count} new texture page(s))");
    }
    finally
    {
        foreach (var i in images) i.img.Dispose();
    }
}

// ---------------------------------------------------------------- sounds
if (sounds.Count > 0)
{
    int builtin = Data.GetBuiltinSoundGroupID();
    var builtinGroup = Data.AudioGroups.Count > 0 ? Data.AudioGroups[builtin] : null;
    foreach (var (name, path) in sounds)
    {
        bool isOgg = path.EndsWith(".ogg", StringComparison.OrdinalIgnoreCase);
        var audio = new UndertaleEmbeddedAudio { Data = File.ReadAllBytes(path) };
        Data.EmbeddedAudio.Add(audio);
        var flags = isOgg ? UndertaleSound.AudioEntryFlags.IsCompressed | UndertaleSound.AudioEntryFlags.Regular
                          : UndertaleSound.AudioEntryFlags.IsEmbedded | UndertaleSound.AudioEntryFlags.Regular;
        var snd = Data.Sounds.ByName(name);
        if (snd is null)
        {
            snd = new UndertaleSound
            {
                Name = Data.Strings.MakeString(name),
                Volume = 1.0f, Pitch = 1.0f, Effects = 0,
            };
            Data.Sounds.Add(snd);
            Console.WriteLine($"[DR] new sound {name}");
        }
        snd.Flags = flags;
        snd.Type = Data.Strings.MakeString(isOgg ? ".ogg" : ".wav");
        snd.File = Data.Strings.MakeString(Path.GetFileName(path));
        snd.AudioFile = audio;
        snd.AudioGroup = builtinGroup;
        snd.GroupID = builtin;
    }
    Console.WriteLine($"[DR] sounds OK ({sounds.Count})");
}

// ---------------------------------------------------------------- code: full replacements
if (gml.Count > 0)
{
    var group = new UndertaleModLib.Compiler.CodeImportGroup(Data) { AutoCreateAssets = true };
    foreach (var kv in gml.OrderBy(k => k.Key, StringComparer.Ordinal))
        group.QueueReplace(kv.Key, File.ReadAllText(kv.Value));
    var result = group.Import(false);
    if (!result.Successful)
    {
        Console.WriteLine(result.PrintAllErrors(true));
        throw new ScriptException("GML import failed");
    }
    Console.WriteLine($"[DR] code OK ({gml.Count})");
}

// ---------------------------------------------------------------- code: patches
// Applied after all full replacements, in mod load order, against the *current* code
// (so a patch stacks on top of another mod's replacement). Formats:
//   <entry>.append.gml / <entry>.prepend.gml   add code at the end / start
//   <entry>.patch     blocks:  >>> find | >>> find regex | >>> find first
//                              <old lines>
//                              >>> replace
//                              <new lines>
//                              >>> end
//                     find matches whole lines, ignoring indentation; all matches
//                     are replaced unless "find first". Lines outside blocks are ignored.
//   <entry>.diff or any .diff with "+++ b/<entry>.gml" headers (git diff output):
//                     each hunk's context/- lines are found (indentation-insensitive,
//                     line numbers ignored) and swapped for its context/+ lines.
//                     "+++ b/x.gml" with "--- /dev/null" creates the entry.
int patchFail = 0, patchOk = 0;
if (patchFiles.Count > 0)
{
    foreach (string pf in patchFiles)
    {
        string fn = Path.GetFileName(pf);
        string modTag = pf.Replace('\\', '/');
        try
        {
            var edits = ParsePatchFile(pf);   // entry -> list of edits (in order)
            foreach (var (entry, ops) in edits)
            {
                var code = Data.Code.ByName(entry);
                string src;
                if (code is null)
                {
                    if (!ops.All(o => o.Kind == "create" || o.Kind == "append" || o.Kind == "prepend"))
                        throw new Exception($"code entry {entry} does not exist");
                    src = "";
                }
                else if (code.ParentEntry is not null)
                    throw new Exception($"{entry} is a child entry; patch its parent {code.ParentEntry.Name.Content}");
                else
                    src = new Underanalyzer.Decompiler.DecompileContext(new GlobalDecompileContext(Data), code, Data.ToolInfo.DecompilerSettings).DecompileToString();
                src = src.Replace("\r\n", "\n");
                foreach (var op in ops) src = ApplyEdit(src, op, entry);
                var g = new UndertaleModLib.Compiler.CodeImportGroup(Data) { AutoCreateAssets = true };
                g.QueueReplace(entry, src);
                var r = g.Import(false);
                if (!r.Successful) throw new Exception("compile failed after patching:\n" + r.PrintAllErrors(true));
                patchOk++;
            }
            Console.WriteLine($"[DR] patch OK  {modTag}");
        }
        catch (Exception ex)
        {
            patchFail++;
            Console.WriteLine($"[DR] WARN patch FAILED {modTag}: {ex.Message}");
        }
    }
    Console.WriteLine($"[DR] patches: {patchOk} entr(y/ies) patched, {patchFail} file(s) failed (skipped)");
}

record Edit(string Kind, string Find, string Replace, bool All = true);

Dictionary<string, List<Edit>> ParsePatchFile(string path)
{
    var res = new Dictionary<string, List<Edit>>(StringComparer.Ordinal);
    void Add(string e, Edit ed) { if (!res.TryGetValue(e, out var l)) res[e] = l = new(); l.Add(ed); }
    string fn = Path.GetFileName(path);
    string text = File.ReadAllText(path).Replace("\r\n", "\n");
    if (fn.EndsWith(".append.gml", StringComparison.OrdinalIgnoreCase))
    { Add(fn[..^".append.gml".Length], new Edit("append", null, text)); return res; }
    if (fn.EndsWith(".prepend.gml", StringComparison.OrdinalIgnoreCase))
    { Add(fn[..^".prepend.gml".Length], new Edit("prepend", null, text)); return res; }

    var lines = text.Split('\n');
    if (fn.EndsWith(".patch", StringComparison.OrdinalIgnoreCase))
    {
        string entry = Path.GetFileNameWithoutExtension(fn);
        int i = 0, blocks = 0;
        while (i < lines.Length)
        {
            string h = lines[i].Trim();
            if (!h.StartsWith(">>> find", StringComparison.OrdinalIgnoreCase)) { i++; continue; }
            string mode = h.Substring(8).Trim().ToLowerInvariant();
            var find = new List<string>(); var repl = new List<string>();
            i++;
            while (i < lines.Length && !lines[i].Trim().Equals(">>> replace", StringComparison.OrdinalIgnoreCase)) find.Add(lines[i++]);
            if (i >= lines.Length) throw new Exception($"{fn}: '>>> find' without '>>> replace'");
            i++;
            while (i < lines.Length && !lines[i].Trim().Equals(">>> end", StringComparison.OrdinalIgnoreCase)) repl.Add(lines[i++]);
            if (i >= lines.Length) throw new Exception($"{fn}: '>>> replace' without '>>> end'");
            i++;
            Add(entry, new Edit(mode == "regex" ? "regex" : "find", string.Join("\n", find), string.Join("\n", repl), mode != "first"));
            blocks++;
        }
        if (blocks == 0) throw new Exception($"{fn}: no '>>> find' blocks");
        return res;
    }

    // unified diff
    string target = Path.GetFileNameWithoutExtension(fn);
    bool fromNull = false;
    List<string> hf = null, hr = null;
    void Flush()
    {
        if (hf is null) return;
        if (fromNull) Add(target, new Edit("create", null, string.Join("\n", hr)));
        else Add(target, new Edit("find", string.Join("\n", hf), string.Join("\n", hr), false));
        hf = hr = null;
    }
    foreach (string raw in lines)
    {
        if (raw.StartsWith("diff ") || raw.StartsWith("index ")) { Flush(); continue; }
        if (raw.StartsWith("--- ")) { Flush(); fromNull = raw.Contains("/dev/null"); continue; }
        if (raw.StartsWith("+++ "))
        {
            string p = raw.Substring(4).Trim();
            int tab = p.IndexOf('\t'); if (tab >= 0) p = p[..tab];
            if (p != "/dev/null") target = Path.GetFileNameWithoutExtension(p.Replace('\\', '/').Split('/').Last());
            continue;
        }
        if (raw.StartsWith("@@")) { Flush(); hf = new(); hr = new(); continue; }
        if (hf is null || raw.StartsWith("\\")) continue;
        if (raw.Length == 0) { hf.Add(""); hr.Add(""); continue; }
        char c = raw[0]; string body = raw.Substring(1);
        if (c == ' ') { hf.Add(body); hr.Add(body); }
        else if (c == '-') hf.Add(body);
        else if (c == '+') hr.Add(body);
    }
    Flush();
    if (res.Count == 0) throw new Exception($"{fn}: no hunks");
    return res;
}

string ApplyEdit(string src, Edit e, string entry)
{
    switch (e.Kind)
    {
        case "create": return e.Replace;
        case "append": return src.TrimEnd('\n') + "\n" + e.Replace;
        case "prepend": return e.Replace.TrimEnd('\n') + "\n" + src;
    }
    Regex rx;
    if (e.Kind == "regex") rx = new Regex(e.Find, RegexOptions.Multiline | RegexOptions.CultureInvariant);
    else
    {
        var fl = e.Find.Split('\n').ToList();
        while (fl.Count > 0 && fl[0].Trim().Length == 0) fl.RemoveAt(0);
        while (fl.Count > 0 && fl[^1].Trim().Length == 0) fl.RemoveAt(fl.Count - 1);
        if (fl.Count == 0) throw new Exception("empty find block");
        string pat = "^" + string.Join("\n", fl.Select(l => l.Trim().Length == 0 ? @"[ \t]*" : @"[ \t]*" + Regex.Escape(l.Trim()) + @"[ \t]*")) + @"(?=\n|\z)";
        rx = new Regex(pat, RegexOptions.Multiline | RegexOptions.CultureInvariant);
    }
    int n = rx.Matches(src).Count;
    if (n == 0)
    {
        string first = e.Find.Split('\n').Select(l => l.Trim()).FirstOrDefault(l => l.Length > 0) ?? "";
        throw new Exception($"{entry}: find text not found (first line: \"{first}\")");
    }
    if (e.Kind == "regex") return rx.Replace(src, e.Replace);
    string rep = e.Replace;
    // keep the indentation of the matched first line for the replacement's unindented lines
    return e.All ? rx.Replace(src, m => Reindent(m.Value, rep)) : rx.Replace(src, m => Reindent(m.Value, rep), 1);
}

string Reindent(string matched, string rep)
{
    string indent = new string(matched.TakeWhile(ch => ch == ' ' || ch == '\t').ToArray());
    var rl = rep.Split('\n');
    int common = rl.Where(l => l.Trim().Length > 0).Select(l => l.TakeWhile(ch => ch == ' ' || ch == '\t').Count()).DefaultIfEmpty(0).Min();
    return string.Join("\n", rl.Select(l => l.Trim().Length == 0 ? "" : indent + l.Substring(Math.Min(common, l.Length))));
}

Console.WriteLine("[DR] import OK");

// ---------------------------------------------------------------- hand back to the loader
string pipe = Environment.GetEnvironmentVariable("DR_OUT_PIPE");
if (!string.IsNullOrEmpty(pipe))
{
    using var ms = new MemoryStream();
    UndertaleIO.Write(ms, Data);
    using var fs = new FileStream(pipe, FileMode.Open, FileAccess.Write);
    fs.Write(BitConverter.GetBytes((long)ms.Length), 0, 8);
    ms.Position = 0;
    ms.CopyTo(fs);
    fs.Flush();
    Console.WriteLine($"[DR] sent {ms.Length} bytes to loader");
}
