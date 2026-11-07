// Supplies the parts of UndertaleModLib's script interface that ImportLooseMod.csx uses (Data,
// EnsureDataLoaded, ScriptException) plus UTMT's default script imports, so the same file builds
// as a C# program. Command line (UndertaleModCli compatible): load <data.win> [-s <script>]
// or just <data.win>. The -s script is ignored: the import code is compiled in.
global using System;
global using System.IO;
global using System.Collections.Generic;
global using System.Text.RegularExpressions;
global using UndertaleModLib;
global using UndertaleModLib.Models;
global using UndertaleModLib.Decompiler;
global using UndertaleModLib.Scripting;
global using UndertaleModLib.Compiler;
global using static LooseModCompiler.ScriptHost;

using System.Runtime.CompilerServices;

namespace LooseModCompiler;

public static class ScriptHost
{
    static UndertaleData data;
    static string dataPath;

    public static UndertaleData Data => data ?? throw new ScriptException("No data file is currently loaded!");

    [ModuleInitializer]
    internal static void Init()
    {
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
        {
            // UndertaleModCli prints the script error and exits non-zero; the loader then runs vanilla.
            Console.Out.Flush();
            Console.Error.WriteLine(e.ExceptionObject is ScriptException se ? se.Message : e.ExceptionObject.ToString());
            Console.Error.Flush();
            Environment.Exit(1);
        };
        string[] args = Environment.GetCommandLineArgs();
        for (int i = 1; i < args.Length; i++)
        {
            if (args[i] == "load" || args[i] == "--verbose" || args[i] == "-v") continue;
            if (args[i] == "-s" || args[i] == "--scripts") { i++; continue; }
            dataPath ??= args[i];
        }
    }

    public static void EnsureDataLoaded()
    {
        if (data != null) return;
        if (dataPath == null)
            throw new ScriptException("usage: LooseModCompiler [load] <data.win> [-s <script>]  (env DR_MOD_DIRS, DR_OUT_PIPE)");
        var file = new FileInfo(dataPath);
        Console.WriteLine($"Trying to load file: '{file.FullName}'");
        using var fs = file.OpenRead();
        data = UndertaleIO.Read(fs, (warning, _) => Console.WriteLine("[warning] " + warning), _ => { });
    }
}
