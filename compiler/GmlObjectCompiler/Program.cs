// usage: GmlObjectCompiler <data.win> <out dir> <snippet.gml>...
// Each snippet file compiles to <out dir>/<stem>.gmlo. The code entry name is gml_GlobalScript_<stem>
// unless the stem already starts with gml_. The data.win is only the compile context (asset names,
// scripts, GMS version); it is not modified.
using System;
using System.IO;
using GmlObjectCompiler;
using UndertaleModLib;

if (args.Length < 3)
{
    Console.Error.WriteLine("usage: GmlObjectCompiler <data.win> <out dir> <snippet.gml>...");
    return 2;
}
UndertaleData data;
using (var fs = File.OpenRead(args[0]))
    data = UndertaleIO.Read(fs, (_, _) => { }, _ => { });
bool yyc = data.IsYYC();
ObjectCompiler.PrepareContext(data);
Console.WriteLine($"context {Path.GetFileName(args[0])}: {(yyc ? "YYC" : "VM")}, GMS {data.GeneralInfo.Major}.{data.GeneralInfo.Minor}.{data.GeneralInfo.Release}, bytecode {data.GeneralInfo.BytecodeVersion}");
Directory.CreateDirectory(args[1]);
int failed = 0;
for (int i = 2; i < args.Length; i++)
{
    string stem = Path.GetFileNameWithoutExtension(args[i]);
    string name = stem.StartsWith("gml_", StringComparison.Ordinal) ? stem : "gml_GlobalScript_" + stem;
    try
    {
        var obj = ObjectCompiler.Compile(data, name, File.ReadAllText(args[i]));
        string outPath = Path.Combine(args[1], stem + ".gmlo");
        File.WriteAllText(outPath, obj.ToText());
        Console.WriteLine($"{stem}: {obj.Code.Length} bytes, {obj.Relocs.Count} relocations, {obj.Locals} locals -> {outPath}");
    }
    catch (Exception e)
    {
        failed++;
        Console.Error.WriteLine($"{stem}: {e.Message}");
    }
}
return failed == 0 ? 0 : 1;
