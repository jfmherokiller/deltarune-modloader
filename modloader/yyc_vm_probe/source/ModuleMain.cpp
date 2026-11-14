// YYCVmProbe - GML bytecode on a YYC runner.
//
// YYC exes keep the VM interpreter linked in (VM_Exec.cpp/VMBuffer.cpp), and Code_Execute
// falls through to VM::Exec when a CCode has no compiled function but has a VMBuffer.
//
// Stage 1 (probe): hand-encoded bytecode-17 programs that need no linking (push.d 42; ret),
// a built-in call, script_execute, a global read and write.
// Stage 2 (GMLO): every <game>\gmlo\*.gmlo (GML compiled by compiler/GmlObjectCompiler)
// is linked against the running game with gmlo_link.hpp and run repeatedly; the checks in
// the matching .expect file verify the side effects through YYTK, independently of the
// bytecode. See compiler/RUNTIME_COMPILER.md "YYC: running linked bytecode".
// Stage 3: the raw call.i on a compiled script, which is expected to fault (last, because a
// fault inside VM::Exec skips its cleanup).
//
// Code runs through YYTK ExecuteCodeObject (the runner's own Code_Execute), so this needs the
// repo's YYToolkit fork (external/YYToolkit). Logs to <game>\yyc_vm_probe.log. Every runner call
// is under SEH. Offsets are the 2024.x runner's CCode layout (docs/yyc_il2cpp.md).
#include <YYToolkit/YYTK_Shared.hpp>
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <new>
#include <sstream>
#include <string>
#include <vector>
#include "append_log.hpp"
#include "gmlo_link.hpp"
using namespace Aurie;
using namespace YYTK;

static YYTKInterface* g_Yytk = nullptr;
static modloader::AppendLog g_Log;
static std::atomic<uint64_t> g_Frames{ 0 };
static std::wstring g_GameDir;
static AurieModule* g_Module = nullptr;
static std::map<std::string, int> g_DebugMessages;   // "gmlo..." show_debug_message texts -> calls

static void Log(const char* fmt, ...)
{
	char buf[2048];
	va_list va; va_start(va, fmt); vsnprintf(buf, sizeof(buf), fmt, va); va_end(va);
	DbgPrintEx(LOG_SEVERITY_INFO, "[YYCVmProbe] %s", buf);
	g_Log.Write("%s", buf);
}

static CInstance* GlobalInstance()
{
	CInstance* g = nullptr;
	g_Yytk->GetGlobalInstance(&g);
	return g;
}

// ---- stage 1: hand-encoded programs ----------------------------------------------------------

static DWORD SehExecute(CInstance* self, void* code, RValue* result, bool* ret)
{
	__try { *ret = AurieSuccess(g_Yytk->ExecuteCodeObject(self, self, (CCode*)code, *result, 0)); return 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
}

// Wraps `words` in a hand-built VMBuffer + CCode and runs it through Code_Execute.
// The objects are leaked on purpose: the runner caches the converted buffer by bytecode address.
static void RunProgram(CInstance* self, const char* label, const std::vector<uint32_t>& words)
{
	auto* bytecode = new uint32_t[words.size()];
	memcpy(bytecode, words.data(), words.size() * 4);
	auto* vmbuf = new (std::align_val_t(16)) unsigned char[gmlo::kVmBufferSize]();
	*(int32_t*)(vmbuf + gmlo::kVmBufferLength) = (int32_t)(words.size() * 4);
	*(void**)(vmbuf + gmlo::kVmBufferCode) = bytecode;
	auto* code = new (std::align_val_t(16)) unsigned char[gmlo::kCCodeSize]();
	*(int32_t*)(code + gmlo::kCCodeKind) = 1;
	*(void**)(code + gmlo::kCCodeVmBuffer) = vmbuf;
	*(const char**)(code + gmlo::kCCodeName) = "gml_Script_yyc_vm_probe";
	*(int32_t*)(code + gmlo::kCCodeIndex) = -1;

	RValue result;
	bool ret = false;
	DWORD ex = SehExecute(self, code, &result, &ret);
	if (ex)
		Log("%s: Code_Execute faulted, exception 0x%08lX", label, ex);
	else
		Log("%s: Code_Execute returned %d; result kind %s value %s; converted buffer %p", label,
			ret, result.GetKindName().c_str(), result.ToString().c_str(), *(void**)(vmbuf + gmlo::kVmBufferPrepared));
}

static int ScriptId(const char* name)
{
	int id = -1;
	g_Yytk->GetNamedRoutineIndex(name, &id);
	return id;
}

static void RunProbe()
{
	Log("IsYYCRunner() = %d", g_Yytk->IsYYCRunner());
	CInstance* global = GlobalInstance();

	// 1. No references at all.
	std::vector<uint32_t> p1 = { 0xC0000000, 0, 0, 0x07500000, 0x9C050000 };   // push.d 42 ; conv.d.v ; ret.v
	double v42 = 42.0; memcpy(&p1[1], &v42, 8);
	RunProgram(global, "push.d 42; conv.d.v; ret.v", p1);

	// 2. Built-in call, operand = the runtime function index (what the loader's fixup would write).
	int sqr = ScriptId("sqr");
	std::vector<uint32_t> p2 = { 0xC0000000, 0, 0, 0x07500000, 0xD9020001, (uint32_t)sqr, 0x9C050000 };
	double v7 = 7.0; memcpy(&p2[1], &v7, 8);   // push.d 7 ; conv.d.v ; call.i sqr(argc=1) ; ret.v
	Log("sqr index %d", sqr);
	RunProgram(global, "sqr(7) via call.i", p2);

	// 3. Compiled-script call through the script_execute built-in.
	int priest = ScriptId("gml_Script_getPriestCount"), scriptExecute = ScriptId("script_execute");
	if (priest >= 0)
	{
		std::vector<uint32_t> p3 = { 0xC0000000, 0, 0, 0x07500000, 0xD9020001, (uint32_t)scriptExecute, 0x9C050000 };
		double id = priest; memcpy(&p3[1], &id, 8);   // push.d <id> ; conv.d.v ; call.i script_execute(argc=1) ; ret.v
		Log("getPriestCount id %d, script_execute index %d", priest, scriptExecute);
		RunProgram(global, "script_execute(getPriestCount) via call.i", p3);
	}

	// 4. Global variable read and write: operand (0xA0 << 24) | runtime slot.
	std::string varName; RValue expected;
	g_Yytk->EnumInstanceMembers(RValue(global), [&](const char* name, RValue* value) {
		if (!value || (value->m_Kind & 0xFFFFFF) != VALUE_REAL || value->m_Real == 0.0) return false;   // a non-zero real
		varName = name; expected = *value; return true;
	});
	int32_t slot = -1;
	if (!varName.empty())
		g_Yytk->GetVariableSlot(RValue(global), varName.c_str(), slot);
	Log("global.%s: YYTK reads %s, slot %d", varName.c_str(), expected.ToString().c_str(), slot);
	if (slot >= 0)
	{
		std::vector<uint32_t> p5 = { 0xC005FFFB, 0xA0000000u | ((uint32_t)slot & 0x07FFFFFF), 0x9C050000 };
		RunProgram(global, "push.v global.<slot>; ret.v", p5);
		auto writeProgram = [&](double v) {
			std::vector<uint32_t> w = { 0xC0000000, 0, 0, 0x07500000, 0x4555FFFB,
				0xA0000000u | ((uint32_t)slot & 0x07FFFFFF), 0x9D020000 };
			memcpy(&w[1], &v, 8);
			return w;
		};
		RunProgram(global, "pop.v.v global.<slot> = 123", writeProgram(123.0));
		RValue* now = nullptr;
		g_Yytk->GetInstanceMember(RValue(global), varName.c_str(), now);
		Log("global.%s after bytecode write, read by YYTK: %s", varName.c_str(), now ? now->ToString().c_str() : "(null)");
		RunProgram(global, "restore original value", writeProgram(expected.ToDouble()));
	}
}

static void RunFaultProbe()
{
	int priest = ScriptId("gml_Script_getPriestCount");
	if (priest < 0) return;
	std::vector<uint32_t> p4 = { 0xD9020000, (uint32_t)priest, 0x9C050000 };
	RunProgram(GlobalInstance(), "getPriestCount() via raw call.i (expected to fault)", p4);
}

// ---- stage 2: linked GML objects -------------------------------------------------------------

struct Snippet
{
	std::string stem;
	gmlo::Object obj;
	gmlo::Linked* linked = nullptr;
	std::vector<std::vector<std::string>> checks;   // .expect lines, split on spaces
	int repeat = 100, runs = 0, faults = 0, passedRounds = 0, failedRounds = 0;
	std::string firstResult, lastResult;
};

static std::vector<Snippet> g_Snippets;
static gmlo::Linker* g_Linker = nullptr;
static int g_Round = 0;
static constexpr int kRounds = 3;

static std::string ReadFile(const std::wstring& path)
{
	std::ifstream f(path, std::ios::binary);
	std::stringstream ss; ss << f.rdbuf();
	return ss.str();
}

static std::string Narrow(const std::wstring& w)
{
	std::string s;
	for (wchar_t c : w) s += (char)c;
	return s;
}

static std::vector<std::string> Split(const std::string& line)
{
	std::vector<std::string> out;
	std::istringstream is(line);
	std::string t;
	while (is >> t) out.push_back(t);
	return out;
}

static size_t PrivateBytes()
{
	PROCESS_MEMORY_COUNTERS_EX pmc{};
	K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
	return pmc.PrivateUsage;
}

static void LoadSnippets()
{
	std::wstring dir = g_GameDir + L"gmlo\\";
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"*.gmlo").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) { Log("GMLO: no objects in %s", Narrow(dir).c_str()); return; }
	std::vector<std::wstring> files;
	do files.push_back(fd.cFileName); while (FindNextFileW(h, &fd));
	FindClose(h);
	std::sort(files.begin(), files.end());
	for (auto& f : files)
	{
		Snippet s;
		std::wstring stem = f.substr(0, f.size() - 5);
		s.stem = Narrow(stem);
		std::string err;
		if (!gmlo::Parse(ReadFile(dir + f), s.obj, err)) { Log("GMLO %s: parse failed: %s", s.stem.c_str(), err.c_str()); continue; }
		std::istringstream ex(ReadFile(dir + stem + L".expect"));
		std::string line;
		while (std::getline(ex, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			auto t = Split(line);
			if (t.empty() || t[0][0] == '#') continue;
			if (t[0] == "repeat" && t.size() == 2) s.repeat = atoi(t[1].c_str());
			else
			{
				// keep the raw tail for value comparisons that contain spaces
				t.push_back(line);
				s.checks.push_back(t);
			}
		}
		g_Snippets.push_back(std::move(s));
	}
}

static std::string Tail(const std::string& line, int skipTokens)
{
	size_t p = 0;
	for (int i = 0; i < skipTokens; i++)
	{
		p = line.find_first_not_of(' ', p);
		p = line.find(' ', p);
		if (p == std::string::npos) return "";
	}
	return line.substr(p + 1);
}

static std::string GlobalValue(const std::string& name)
{
	RValue* v = nullptr;
	if (!AurieSuccess(g_Yytk->GetInstanceMember(RValue(GlobalInstance()), name.c_str(), v)) || !v) return "(missing)";
	return v->ToString();
}

// Checks (one per .expect line):
//   result <text>                          the code's return value, as RValue::ToString
//   global <name> <text>                   global.<name>, read through YYTK
//   global_eq_script <name> <script> <n>.. global.<name> equals the script called natively by YYTK
//   global_eq_builtin <name> <builtin>     global.<name> equals the built-in variable, read by YYTK
//   debug_message <text>                   show_debug_message(<text>) was called once per run
static bool Check(const Snippet& s, const std::vector<std::string>& c, std::string& detail)
{
	const std::string& raw = c.back();
	std::string want, got;
	if (c[0] == "result") { want = Tail(raw, 1); got = s.lastResult; }
	else if (c[0] == "global" && c.size() >= 3) { want = Tail(raw, 2); got = GlobalValue(c[1]); }
	else if (c[0] == "global_eq_script" && c.size() >= 4)
	{
		std::vector<RValue> args;
		for (size_t i = 3; i + 1 < c.size(); i++) args.push_back(RValue(atof(c[i].c_str())));
		want = g_Yytk->CallGameScript(c[2], args).ToString();
		got = GlobalValue(c[1]);
	}
	else if (c[0] == "global_eq_builtin" && c.size() >= 4)
	{
		RValue v;
		want = AurieSuccess(g_Yytk->GetBuiltin(c[2], nullptr, NULL_INDEX, v)) ? v.ToString() : "(no builtin)";
		got = GlobalValue(c[1]);
	}
	else if (c[0] == "debug_message" && c.size() >= 3)
	{
		// every run printed the message once
		std::string text = Tail(raw, 1);
		want = std::to_string(s.runs) + " x '" + text + "'";
		got = std::to_string(g_DebugMessages[text]) + " x '" + text + "'";
	}
	else { detail = "unknown check: " + raw; return false; }
	detail = raw + " -> got '" + got + "', want '" + want + "'";
	return got == want;
}

// show_debug_message, observed: under the loader the runner's stdout is not captured, so the
// built-in itself is hooked and every message that starts with "gmlo" is counted.
static TRoutine g_ShowDebugMessage = nullptr;
static void ShowDebugMessageHook(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
{
	if (argc > 0)
	{
		std::string text = args[0].ToString();
		if (text.rfind("gmlo", 0) == 0) g_DebugMessages[text]++;
	}
	g_ShowDebugMessage(result, self, other, argc, args);
}

static void HookShowDebugMessage()
{
	PVOID target = nullptr, trampoline = nullptr;
	AurieStatus st = g_Yytk->GetNamedRoutinePointer("show_debug_message", &target);
	if (AurieSuccess(st))
		st = MmCreateHook(g_Module, "gmlo_show_debug_message", target, (PVOID)ShowDebugMessageHook, &trampoline);
	g_ShowDebugMessage = (TRoutine)trampoline;
	Log("GMLO: show_debug_message hook at %p: %s", target, AurieStatusToString(st));
}

static void LinkSnippets()
{
	HookShowDebugMessage();
	if (!gmlo::HasExecuteCodeObject(g_Yytk))
	{
		Log("GMLO: this YYToolkit.dll has no ExecuteCodeObject (needs the fork, external/YYToolkit 85ce5b9+)");
		return;
	}
	g_Linker = new gmlo::Linker(g_Yytk);
	auto& strings = g_Linker->Strings();
	if (strings.Discover())
	{
		std::string v0, err;
		bool ok = g_Linker->ReadRunnerString(0, v0, err);
		Log("GMLO: runner string table: image %p, table slot %p -> %p, %u strings; push.s 0 = '%s'%s",
			strings.Image(), (void*)strings.TableSlot(), (void*)*strings.TableSlot(), strings.Count(), v0.c_str(), ok ? "" : err.c_str());
	}
	else
		Log("GMLO: runner string table not found: %s", strings.error.c_str());

	for (auto& s : g_Snippets)
	{
		std::string err;
		s.linked = g_Linker->Link(s.obj, err);
		if (!s.linked) { Log("GMLO %s: link failed: %s", s.stem.c_str(), err.c_str()); continue; }
		std::string words;
		for (size_t i = 0; i < s.linked->words; i++) { char b[12]; snprintf(b, sizeof(b), " %08x", s.linked->code[i]); words += b; }
		Log("GMLO %s: linked %zu words (%zu relocations, %d script calls lowered, %d new strings):%s", s.stem.c_str(),
			s.linked->words, s.obj.relocs.size(), s.linked->loweredScriptCalls, s.linked->newStrings, words.c_str());
	}
	// The appended strings read back through the VM's own push.s.
	for (auto& s : g_Snippets)
		for (auto& r : s.obj.relocs)
			if (r.kind == "string")
			{
				uint32_t idx = 0;
				std::string v, err;
				if (strings.Intern(r.name, idx) && g_Linker->ReadRunnerString(idx, v, err))
					Log("GMLO %s: string '%s' -> runtime index %u, push.s reads '%s' (%s)", s.stem.c_str(), r.name.c_str(), idx,
						v.c_str(), v == r.name ? "match" : "MISMATCH");
			}
}

static void RunRound()
{
	CInstance* global = GlobalInstance();
	for (auto& s : g_Snippets)
	{
		if (!s.linked) continue;
		size_t before = PrivateBytes();
		for (int i = 0; i < s.repeat; i++)
		{
			RValue result;
			std::string err;
			if (!g_Linker->Run(s.linked, global, global, result, err)) { s.faults++; Log("GMLO %s run %d: %s", s.stem.c_str(), s.runs, err.c_str()); break; }
			s.lastResult = result.ToString();
			if (s.runs++ == 0) s.firstResult = s.lastResult;
		}
		size_t after = PrivateBytes();
		bool all = s.faults == 0;
		for (auto& c : s.checks)
		{
			std::string detail;
			bool ok = Check(s, c, detail);
			all &= ok;
			Log("GMLO %s round %d: %s %s", s.stem.c_str(), g_Round + 1, ok ? "PASS" : "FAIL", detail.c_str());
		}
		(all ? s.passedRounds : s.failedRounds)++;
		Log("GMLO %s round %d: %d runs total, result '%s', private bytes %+lld KB over %d runs", s.stem.c_str(), g_Round + 1,
			s.runs, s.lastResult.c_str(), ((long long)after - (long long)before) / 1024, s.repeat);
	}
	if (++g_Round == kRounds)
	{
		int pass = 0;
		for (auto& s : g_Snippets) pass += s.linked && s.failedRounds == 0 && s.passedRounds == kRounds;
		Log("GMLO summary: %d of %zu snippets passed all %d rounds", pass, g_Snippets.size(), kRounds);
	}
}

// ---- driver ----------------------------------------------------------------------------------

static void FrameCallback(FWFrame&) { g_Frames.fetch_add(1, std::memory_order_relaxed); }
static void CodeCallback(FWCodeEvent&)
{
	static int stage = 0;
	uint64_t f = g_Frames.load();
	if (stage == 0 && f > 300)
	{
		stage = 1;
		RunProbe();
		LoadSnippets();
		LinkSnippets();
	}
	// Rounds on separate frames, 60 frames apart: the linked code is reused across frames.
	if (stage >= 1 && stage <= kRounds && f > 300 + 60ull * stage)
	{
		stage++;
		RunRound();
	}
	if (stage == kRounds + 1 && f > 300 + 60ull * (kRounds + 1))
	{
		stage++;
		RunFaultProbe();
		Log("YYCVmProbe done");
	}
}

EXPORTED AurieStatus ModuleInitialize(IN AurieModule* Module, IN const fs::path& ModulePath)
{
	UNREFERENCED_PARAMETER(ModulePath);
	g_Module = Module;
	g_Yytk = GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
	wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
	g_GameDir = exe;
	g_GameDir = g_GameDir.substr(0, g_GameDir.find_last_of(L"\\") + 1);
	std::wstring logp = g_GameDir + L"yyc_vm_probe.log";
	DeleteFileW(logp.c_str());
	g_Log.Open(logp.c_str());
	Log("YYCVmProbe start");
	g_Yytk->CreateCallback(Module, EVENT_FRAME, FrameCallback, 0);
	g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	return AURIE_SUCCESS;
}
