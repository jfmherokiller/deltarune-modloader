// GML snippet -> linkable object (.gmlo). The bytecode is Underanalyzer's output, encoded the way
// UndertaleModLib writes it, except that every reference operand is left unresolved and listed in
// a relocation table instead of a data.win reference chain:
//   var    operand word keeps the reference-type bits (0xF8000000); the linker ORs in the id
//   func   operand word is 0; the linker writes the whole id
//   string operand word is the compile-time STRG index; the linker writes the runtime string index
// The runner-side rules are in compiler/RUNTIME_COMPILER.md ("Load-time operand fixup" and
// "YYC: running linked bytecode"); compiler/tools/gml_object.py reads the same format.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using Underanalyzer.Decompiler;
using UndertaleModLib;
using UndertaleModLib.Compiler;
using UndertaleModLib.Decompiler;
using UndertaleModLib.Models;

namespace GmlObjectCompiler;

public sealed record Reloc(int Offset, string Kind, string Name, int InstType = 0);

public sealed class GmlObject
{
    public string Name;
    public uint Locals, Args;
    public byte[] Code;
    public List<Reloc> Relocs = new();
    public List<string> LocalNames = new();

    static string Escape(string s) => s.Replace("\\", "\\\\").Replace("\n", "\\n").Replace("\r", "\\r").Replace("\t", "\\t");

    /// <summary>Text form: one record per line, see compiler/RUNTIME_COMPILER.md ("Linkable objects").</summary>
    public string ToText()
    {
        var sb = new StringBuilder();
        sb.Append("GMLO 1\n");
        sb.Append($"name {Name}\nlocals {Locals}\nargs {Args}\n");
        foreach (var l in LocalNames) sb.Append($"local {l}\n");
        sb.Append("code");
        for (int i = 0; i < Code.Length; i += 4) sb.Append($" {BitConverter.ToUInt32(Code, i):x8}");
        sb.Append('\n');
        foreach (var r in Relocs)
            sb.Append(r.Kind == "var" ? $"reloc {r.Offset} var {r.InstType} {r.Name}\n" : $"reloc {r.Offset} {r.Kind} {Escape(r.Name)}\n");
        sb.Append("end\n");
        return sb.ToString();
    }
}

public static class ObjectCompiler
{
    static readonly FieldInfo WriterData = typeof(UndertaleWriter).GetField("undertaleData", BindingFlags.NonPublic | BindingFlags.Instance)
        ?? throw new MissingFieldException("UndertaleWriter.undertaleData");

    /// <summary>
    /// Makes <paramref name="data"/> usable as a compile context. A YYC data.win has no
    /// CODE/VARI/FUNC chunks, so empty ones are added, and every script function listed in SCPT
    /// (gml_Script_X) is declared as global function X so calls to it compile to call.i.
    /// </summary>
    public static void PrepareContext(UndertaleData data)
    {
        if (!data.IsYYC()) return;
        void Add(UndertaleChunk c) { data.FORM.Chunks[c.Name] = c; data.FORM.ChunksTypeDict[c.GetType()] = c; }
        Add(new UndertaleChunkCODE());
        Add(new UndertaleChunkVARI());
        Add(new UndertaleChunkFUNC());
        data.BuiltinList ??= new BuiltinList(data);
        GlobalDecompileContext.BuildGlobalFunctionCache(data);
        const string prefix = "gml_Script_";
        foreach (var script in data.Scripts)
        {
            string name = script?.Name?.Content;
            if (name is null || !name.StartsWith(prefix, StringComparison.Ordinal) || name.Contains('@')) continue;
            string shortName = name[prefix.Length..];
            if (data.GlobalFunctions.FunctionNameExists(shortName)) continue;
            var fn = new UndertaleFunction { Name = data.Strings.MakeString(name, out int sid), NameStringID = sid };
            data.Functions.Add(fn);
            data.GlobalFunctions.DefineFunction(shortName, fn);
        }
    }

    /// <summary>Compiles <paramref name="source"/> as code entry <paramref name="name"/> (a gml_GlobalScript_* or
    /// gml_Object_* name selects the script kind) and returns the unlinked object.</summary>
    public static GmlObject Compile(UndertaleData data, string name, string source)
    {
        // Pass the prepared function table: a fresh GlobalDecompileContext rebuilds it from CODE/FUNC
        // and would drop the YYC script declarations PrepareContext added.
        if (data.GlobalFunctions is null) GlobalDecompileContext.BuildGlobalFunctionCache(data);
        var group = new CodeImportGroup(data, new GlobalDecompileContext(data, data.GlobalFunctions)) { AutoCreateAssets = false };
        group.QueueReplace(name, source);
        var result = group.Import(false);
        if (!result.Successful) throw new InvalidDataException("compile failed: " + result.PrintAllErrors(false));
        var code = data.Code.ByName(name);
        if (code.ChildEntries.Count > 0)
            throw new NotSupportedException($"{name}: function declarations create child entries ({code.ChildEntries.Count}); not supported in objects yet");

        var obj = new GmlObject { Name = name, Locals = code.LocalsCount, Args = code.ArgumentsCount };
        var locals = data.CodeLocals?.ByName(name);
        if (locals != null) obj.LocalNames.AddRange(locals.Locals.Select(l => l.Name.Content));

        // push.s serializes its string by index through STRG's index dictionary, which
        // UndertaleWriter only builds while saving a whole data.win.
        var strg = data.FORM.Chunks.TryGetValue("STRG", out var strgChunk) ? strgChunk as IUndertaleListChunk : null;
        strg?.GenerateIndexDict();
        try { EncodeInstructions(data, code, obj); }
        finally { strg?.ClearIndexDict(); }
        return obj;
    }

    static void EncodeInstructions(UndertaleData data, UndertaleCode code, GmlObject obj)
    {
        using var ms = new MemoryStream();
        using (var w = new UndertaleWriter(ms))
        {
            WriterData.SetValue(w, data);
            foreach (var instr in code.Instructions)
            {
                int at = (int)ms.Position;
                instr.Serialize(w);
                w.Flush();
                int operand = at + 4;
                if (instr.ValueVariable is UndertaleVariable v)
                {
                    obj.Relocs.Add(new Reloc(at, "var", v.Name.Content, (int)v.InstanceType));
                    Patch(ms, operand, ReadU32(ms, operand) & 0xF8000000u);
                }
                else if (instr.ValueFunction is UndertaleFunction f)
                {
                    obj.Relocs.Add(new Reloc(at, "func", f.Name.Content));
                    Patch(ms, operand, 0);
                }
                else if (instr.ValueString?.Resource is UndertaleString s)
                    obj.Relocs.Add(new Reloc(at, "string", s.Content));
                ms.Position = ms.Length;
            }
        }
        obj.Code = ms.ToArray();
    }

    static uint ReadU32(MemoryStream ms, int at) => BitConverter.ToUInt32(ms.GetBuffer(), at);
    static void Patch(MemoryStream ms, int at, uint value) => BitConverter.TryWriteBytes(ms.GetBuffer().AsSpan(at, 4), value);
}
