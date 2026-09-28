// ExportSprites.csx - copy vanilla sprite frames into a mod folder, ready to edit.
// Run by export_sprites.bat (see modloader/README.md, "Sprites").
//
// env DR_SPRITES = ';'-separated sprite names; '*' and '?' wildcards allowed (spr_susie_walk_*)
// env DR_OUT_DIR = the mod's sprites folder, e.g. mods\MyMod\chapter2_windows\sprites
//
// Writes <DR_OUT_DIR>\<sprite>\<sprite>_<frame>.png at the sprite's full size (padding included),
// which is exactly the layout ImportLooseMod.csx reads back. Existing files are not overwritten.
using System;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using UndertaleModLib.Util;

EnsureDataLoaded();

string want = Environment.GetEnvironmentVariable("DR_SPRITES");
string outDir = Environment.GetEnvironmentVariable("DR_OUT_DIR");
if (string.IsNullOrWhiteSpace(want) || string.IsNullOrWhiteSpace(outDir))
    throw new ScriptException("DR_SPRITES and DR_OUT_DIR must be set");

var matched = new List<UndertaleSprite>();
foreach (string raw in want.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
{
    string pat = raw.Trim('"', '\\', ' ');
    if (pat.Length == 0) continue;
    var rx = new Regex("^" + Regex.Escape(pat).Replace(@"\*", ".*").Replace(@"\?", ".") + "$", RegexOptions.IgnoreCase);
    var hits = Data.Sprites.Where(s => s?.Name != null && rx.IsMatch(s.Name.Content)).ToList();
    if (hits.Count == 0)
    {
        var near = Data.Sprites.Where(s => s?.Name != null)
                               .Select(s => s.Name.Content)
                               .OrderBy(n => Distance(pat.ToLowerInvariant(), n.ToLowerInvariant()))
                               .Take(3);
        Console.WriteLine($"[DR] no sprite matches '{pat}'. Closest: {string.Join(", ", near)}");
        continue;
    }
    matched.AddRange(hits.Where(h => !matched.Contains(h)));
}

int written = 0, skipped = 0;
using (var worker = new TextureWorker())
    foreach (var spr in matched)
    {
        string name = spr.Name.Content;
        string dir = Path.Combine(outDir, name);
        Directory.CreateDirectory(dir);
        for (int i = 0; i < spr.Textures.Count; i++)
        {
            var tex = spr.Textures[i]?.Texture;
            if (tex is null) continue;
            string path = Path.Combine(dir, $"{name}_{i}.png");
            if (File.Exists(path)) { skipped++; continue; }
            worker.ExportAsPNG(tex, path, null, true);
            written++;
        }
        Console.WriteLine($"[DR] {name}: {spr.Textures.Count} frame(s), {spr.Width}x{spr.Height}, origin {spr.OriginX},{spr.OriginY}");
    }
Console.WriteLine($"[DR] exported {written} frame(s) of {matched.Count} sprite(s) to {outDir}" +
                  (skipped > 0 ? $" ({skipped} already existed, left untouched)" : ""));

static int Distance(string a, string b)
{
    var d = new int[a.Length + 1, b.Length + 1];
    for (int i = 0; i <= a.Length; i++) d[i, 0] = i;
    for (int j = 0; j <= b.Length; j++) d[0, j] = j;
    for (int i = 1; i <= a.Length; i++)
        for (int j = 1; j <= b.Length; j++)
            d[i, j] = Math.Min(Math.Min(d[i - 1, j] + 1, d[i, j - 1] + 1), d[i - 1, j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1));
    return d[a.Length, b.Length];
}
