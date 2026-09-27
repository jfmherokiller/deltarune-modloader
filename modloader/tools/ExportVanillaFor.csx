// ExportVanillaFor.csx - decompile the code entries named by *.gml in DR_NAMES_DIR into DR_OUT_DIR,
// with the same decompiler settings ImportLooseMod.csx uses (so generated diffs apply cleanly).
using System.IO;
using System.Linq;
EnsureDataLoaded();
string names = Environment.GetEnvironmentVariable("DR_NAMES_DIR");
string outDir = Environment.GetEnvironmentVariable("DR_OUT_DIR");
Directory.CreateDirectory(outDir);
var ctx = new GlobalDecompileContext(Data);
int n = 0, missing = 0;
foreach (var f in Directory.GetFiles(names, "*.gml"))
{
    string entry = Path.GetFileNameWithoutExtension(f);
    var code = Data.Code.ByName(entry);
    if (code is null || code.ParentEntry is not null) { missing++; continue; }
    string s = new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
    File.WriteAllText(Path.Combine(outDir, entry + ".gml"), s.Replace("\r\n", "\n"));
    n++;
}
Console.WriteLine($"[DR] exported {n} vanilla entries, {missing} not in vanilla");
