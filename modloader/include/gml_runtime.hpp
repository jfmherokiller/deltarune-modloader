// gml_runtime.hpp - one entry point for running GML source in a live GameMaker game, VM or YYC.
//
//   gmlrt::Runtime rt(yytk, compileObject, dataWinPath);   // once, keep for the process lifetime
//   RValue result; std::string err;
//   if (!rt.CompileAndRun("global.x = 5; return x;", self, result, err)) log(err);
//
// Three stages, each usable on its own:
//   1. compile  GML -> linkable object (.gmlo text) by the in-process compiler: the managed export
//               LooseModCompiler.RuntimeExports.CompileObject (Underanalyzer, the same code as the
//               GmlObjectCompiler CLI), hosted through compiler/RunnerCompiler/native/ClrHost. The
//               data.win on disk is only the compile context (asset names, scripts, version).
//   2. link     gmlo::Linker (gmlo_link.hpp) resolves every name against the live runner. The
//               runner kind is detected there (YYTK IsYYCRunner); on YYC, compiled-script calls
//               are lowered to script_execute. Nothing else differs between the runners.
//   3. run      the runner's own Code_Execute through YYTK ExecuteCodeObject, SEH-guarded.
// Compiled+linked code is cached by source text: running the same snippet again neither
// recompiles nor allocates. Linked code is never freed (see gmlo_link.hpp).
//
// Threads: CompileAndRun/Compile/Run on the game thread only (a YYTK callback). Warm() may run on
// any thread: it loads the compile context, so the first in-game compile does not stall a frame.
// Design and limits: compiler/RUNTIME_COMPILER.md, "Runtime GML API".
#pragma once
#include <gmlo_link.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gmlrt
{
	// LooseModCompiler.RuntimeExports.CompileObject: (data.win path, code name, gml, out, capacity) ->
	// .gmlo text length in bytes, or -(error length) - 1 with the error text in `out`. Thread-safe.
	using CompileObjectFn = int (*)(const char* dataPath, const char* codeName, const char* gml, char* out, int capacity);
	inline constexpr const char* kCompilerType = "LooseModCompiler.RuntimeExports, LooseModCompiler";
	inline constexpr const char* kCompileObject = "CompileObject";

	class Runtime
	{
	public:
		// `compile` may be null (link/run of prebuilt .gmlo only). `dataWin` is the compile context.
		Runtime(YYTK::YYTKInterface* yytk, CompileObjectFn compile, std::string dataWin)
			: m_Yytk(yytk), m_Compile(compile), m_DataWin(std::move(dataWin)) {}

		// False (with `err`) when this YYToolkit cannot run code objects; nothing else may be called then.
		bool Ready(std::string& err)
		{
			if (!m_Linker)
			{
				if (!gmlo::HasExecuteCodeObject(m_Yytk))
				{
					err = "this YYToolkit.dll has no ExecuteCodeObject (needs the fork, external/YYToolkit 85ce5b9+)";
					return false;
				}
				m_Linker = std::make_unique<gmlo::Linker>(m_Yytk);
			}
			return true;
		}

		bool IsYYC() { std::string e; return Ready(e) && m_Linker->IsYYC(); }
		gmlo::Linker& Linker() { return *m_Linker; }   // after Ready()

		// Loads the compile context (any thread). Returns "context VM"/"context YYC" or the error.
		bool Warm(std::string& info) const
		{
			if (!m_Compile) { info = "no compiler"; return false; }
			std::string text;
			const bool ok = CallCompiler("gml_GlobalScript_gmlrt_warm", "", text);
			info = text;
			return ok;
		}

		// GML source -> .gmlo object (no runner access; any thread).
		bool CompileObject(const std::string& source, const std::string& name, gmlo::Object& obj, std::string& err) const
		{
			if (!m_Compile) { err = "no in-process compiler"; return false; }
			std::string text;
			if (!CallCompiler(name, source, text)) { err = "compile: " + text; return false; }
			return gmlo::Parse(text, obj, err);
		}

		// Compiles and links `source` (game thread). Cached by source text; `name` defaults to
		// gml_GlobalScript_gmlrt_<n>. Returns nullptr with `err` on a compile or link error.
		gmlo::Linked* Compile(const std::string& source, std::string& err, const std::string& name = {})
		{
			if (!Ready(err)) return nullptr;
			if (auto it = m_BySource.find(source); it != m_BySource.end()) return it->second;
			gmlo::Object obj;
			const std::string entry = name.empty() ? "gml_GlobalScript_gmlrt_" + std::to_string(m_BySource.size()) : name;
			if (!CompileObject(source, entry, obj, err)) return nullptr;
			gmlo::Linked* linked = m_Linker->Link(obj, err);
			if (linked) m_BySource.emplace(source, linked);
			return linked;
		}

		// Links a prebuilt object (.gmlo text from GmlObjectCompiler) (game thread).
		gmlo::Linked* Load(const std::string& gmloText, std::string& err)
		{
			gmlo::Object obj;
			if (!Ready(err) || !gmlo::Parse(gmloText, obj, err)) return nullptr;
			return m_Linker->Link(obj, err);
		}

		// Runs linked code as `self`/`other` (game thread). `self` must be a real instance if the
		// code touches instance variables; null means the global instance. After a fault (err starts
		// with "exception") the VM state is not unwound: stop running linked code.
		bool Run(gmlo::Linked* code, YYTK::CInstance* self, YYTK::CInstance* other, YYTK::RValue& result, std::string& err)
		{
			if (!Ready(err)) return false;
			if (!self) m_Yytk->GetGlobalInstance(&self);
			return m_Linker->Run(code, self, other ? other : self, result, err);
		}

		// The single entry point: compile (cached), link (cached) and run `source` on this runner.
		bool CompileAndRun(const std::string& source, YYTK::CInstance* self, YYTK::RValue& result, std::string& err,
			YYTK::CInstance* other = nullptr)
		{
			gmlo::Linked* code = Compile(source, err);
			return code && Run(code, self, other, result, err);
		}

	private:
		YYTK::YYTKInterface* m_Yytk;
		CompileObjectFn m_Compile;
		std::string m_DataWin;
		std::unique_ptr<gmlo::Linker> m_Linker;
		std::unordered_map<std::string, gmlo::Linked*> m_BySource;   // never freed, like the linked code

		bool CallCompiler(const std::string& name, const std::string& source, std::string& text) const
		{
			std::vector<char> out(1 << 16);
			int n = m_Compile(m_DataWin.c_str(), name.c_str(), source.c_str(), out.data(), (int)out.size());
			if (n >= (int)out.size())   // too long for the buffer: once more with the reported size
			{
				out.resize((size_t)n + 1);
				n = m_Compile(m_DataWin.c_str(), name.c_str(), source.c_str(), out.data(), (int)out.size());
			}
			const int len = n < 0 ? -n - 1 : n;
			text.assign(out.data(), (size_t)std::min<int>(len, (int)out.size() - 1));
			return n >= 0;
		}
	};
}
