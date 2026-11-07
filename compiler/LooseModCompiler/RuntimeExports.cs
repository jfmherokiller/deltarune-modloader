// In-process entry points for native hosts (hostfxr + hdt_get_function_pointer), used by the
// RunnerCompiler YYToolkit plugin. Same assembly as the CLI, so one shipped .NET runtime serves both.
// SPIKE: compiles against a data.win loaded in-process (UndertaleModLib). The long-term design builds
// Underanalyzer's IGameContext from the live runner instead; see compiler/RUNTIME_COMPILER.md.
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace LooseModCompiler;

public static unsafe class RuntimeExports
{
    static UndertaleData loaded;
    static string loadedPath;

    static string Utf8(byte* p) => p == null ? null : Marshal.PtrToStringUTF8((IntPtr)p);

    static int Reply(string text, byte* output, int capacity)
    {
        var bytes = Encoding.UTF8.GetBytes(text);
        int n = Math.Min(bytes.Length, Math.Max(0, capacity - 1));
        if (output != null && capacity > 0) { Marshal.Copy(bytes, 0, (IntPtr)output, n); output[n] = 0; }
        return bytes.Length;
    }

    /// <summary>Liveness check: runtime version and process id, proving the CLR runs in the host.</summary>
    [UnmanagedCallersOnly]
    public static int Ping(byte* output, int capacity) =>
        Reply($"LooseModCompiler runtime {Environment.Version} in pid {Environment.ProcessId}, {RuntimeInformation.OSDescription}", output, capacity);

    /// <summary>
    /// Compile <paramref name="gml"/> as code entry <paramref name="codeName"/> against data.win at
    /// <paramref name="dataPath"/> (loaded once and cached) and return a disassembly report.
    /// Returns the report length in bytes, or -1 on failure (report holds the error).
    /// </summary>
    [UnmanagedCallersOnly]
    public static int Compile(byte* dataPath, byte* codeName, byte* gml, byte* output, int capacity)
    {
        try
        {
            var path = Utf8(dataPath); var name = Utf8(codeName); var source = Utf8(gml);
            var report = new StringBuilder();
            var clock = Stopwatch.StartNew();
            if (loaded == null || loadedPath != path)
            {
                using var fs = File.OpenRead(path);
                loaded = UndertaleIO.Read(fs, (_, _) => { }, _ => { });
                loadedPath = path;
                report.Append($"loaded {path} in {clock.ElapsedMilliseconds} ms; ");
                clock.Restart();
            }
            var group = new CodeImportGroup(loaded) { AutoCreateAssets = false };
            group.QueueReplace(name, source);
            var result = group.Import(false);
            if (!result.Successful) return -Math.Abs(Reply("compile failed: " + result.PrintAllErrors(false), output, capacity)) - 1;
            var code = loaded.Code.ByName(name);
            report.Append($"compiled {name} in {clock.ElapsedMilliseconds} ms: {code.Instructions.Count} instructions, {code.Length} bytes\n");
            report.Append(code.Disassemble(loaded.Variables, loaded.CodeLocals?.For(code), loaded.CodeLocals == null));
            return Reply(report.ToString(), output, capacity);
        }
        catch (Exception e)
        {
            Reply(e.ToString(), output, capacity);
            return -1;
        }
    }
}
