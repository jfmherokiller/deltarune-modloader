// ImportGMLFolder.csx - non-interactive GML import for UndertaleModCli.
// Replaces (or creates + links) every code entry named by a *.gml file in the folder(s)
// listed in env DR_GML_DIRS (separated by ';'). Later folders win on duplicate names.
// Run by the DELTARUNE modloader at the moment the game reads data.win (see README).
using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using UndertaleModLib.Util;

EnsureDataLoaded();

string dirs = Environment.GetEnvironmentVariable("DR_GML_DIRS");
if (string.IsNullOrEmpty(dirs))
    throw new ScriptException("DR_GML_DIRS is not set");

var files = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
foreach (string dir in dirs.Split(';', StringSplitOptions.RemoveEmptyEntries))
{
    if (!Directory.Exists(dir)) continue;
    foreach (string f in Directory.GetFiles(dir, "*.gml"))
        files[Path.GetFileNameWithoutExtension(f)] = f;
}
Console.WriteLine($"[DR] importing {files.Count} GML file(s)");

var group = new UndertaleModLib.Compiler.CodeImportGroup(Data) { AutoCreateAssets = true };
foreach (var kv in files.OrderBy(k => k.Key, StringComparer.Ordinal))
    group.QueueReplace(kv.Key, File.ReadAllText(kv.Value));

var result = group.Import(false);
if (!result.Successful)
{
    Console.WriteLine(result.PrintAllErrors(true));
    throw new ScriptException("GML import failed");
}
Console.WriteLine("[DR] import OK");

// Loader mode: stream the patched data.win straight back to the game over a named pipe
// (8-byte little-endian length, then the bytes). Nothing is written to disk.
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
