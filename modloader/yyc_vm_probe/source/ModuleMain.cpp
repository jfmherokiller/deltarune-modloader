// YYCVmProbe - experiment: does a YYC runner still execute GML *bytecode*?
//
// YYC exes keep the VM interpreter linked in (VM_Exec.cpp/VMBuffer.cpp), and Code_Execute
// falls through to VM::Exec when a CCode has no compiled function but has a VMBuffer.
// This plugin builds such a CCode by hand around a tiny bytecode program that needs no
// linking (no variables, functions or strings):
//
//     push.d 42.0 ; conv.d.v ; ret.v          (GameMaker bytecode 17 encoding)
//
// and runs it through the runner's own Code_Execute (YYToolkit's "ExecuteIt" trampoline).
// If the result RValue comes back as 42, the bytecode path works on YYC and loose GML could
// be compiled to bytecode instead of native code. Logs to <game>\yyc_vm_probe.log.
// Every runner call is under SEH. Experimental: offsets are the 2024.x runner's CCode
// layout as read from Code_Execute and VM::Exec (see docs/yyc_il2cpp.md).
#include <YYToolkit/YYTK_Shared.hpp>
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>
using namespace Aurie;
using namespace YYTK;

static YYTKInterface* g_Yytk = nullptr;
static FILE* g_Log = nullptr;
static std::atomic<uint64_t> g_Frames{ 0 };
static bool g_Ran = false;

static void Log(const char* fmt, ...)
{
	char buf[1024];
	va_list va; va_start(va, fmt); vsnprintf(buf, sizeof(buf), fmt, va); va_end(va);
	DbgPrintEx(LOG_SEVERITY_INFO, "[YYCVmProbe] %s", buf);
	if (g_Log) { fprintf(g_Log, "%s\n", buf); fflush(g_Log); }
}

// bool Code_Execute(CInstance* self, CInstance* other, CCode* code, RValue* result, int argc)
using PFN_CodeExecute = bool (*)(CInstance*, CInstance*, void*, RValue*, int);

// CCode fields read by Code_Execute / VM::Exec in this runner (offsets in bytes).
constexpr size_t kCCodeSize = 0xB8;
// The rest stays zero: debug buffer 0x70, compiled function 0x90 (null selects the VM path),
// flags 0x9C, local count 0xA0, argument count 0xA4.
constexpr size_t kKind = 0x10, kVmBuffer = 0x68, kName = 0x80, kIndex = 0x88;
// VMBuffer: +8 size in bytes, +0x18 bytecode, +0x20/+0x28 converted/jump buffers (filled lazily).
constexpr size_t kVmBufferSize = 0x30;

static DWORD SehExecute(PFN_CodeExecute fn, CInstance* self, void* code, RValue* result, bool* ret)
{
#ifdef YYTK_HAS_EXECUTE_CODE_OBJECT
	// YYToolkit with ExecuteCodeObject (AurieFramework/YYToolkit#90): no trampoline lookup.
	UNREFERENCED_PARAMETER(fn);
	__try { *ret = AurieSuccess(g_Yytk->ExecuteCodeObject(self, self, (CCode*)code, *result, 0)); return 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
#else
	__try { *ret = fn(self, self, code, result, 0); return 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
#endif
}

// Wraps `words` in a hand-built VMBuffer + CCode and runs it through Code_Execute.
// The objects are leaked on purpose: the runner caches the converted buffer by bytecode address.
static void RunProgram(PFN_CodeExecute exec, CInstance* self, const char* label, const std::vector<uint32_t>& words)
{
	auto* bytecode = new uint32_t[words.size()];
	memcpy(bytecode, words.data(), words.size() * 4);
	auto* vmbuf = new (std::align_val_t(16)) unsigned char[kVmBufferSize]();
	*(int32_t*)(vmbuf + 8) = (int32_t)(words.size() * 4);
	*(void**)(vmbuf + 0x18) = bytecode;
	auto* code = new (std::align_val_t(16)) unsigned char[kCCodeSize]();
	*(int32_t*)(code + kKind) = 1;
	*(void**)(code + kVmBuffer) = vmbuf;
	*(const char**)(code + kName) = "gml_Script_yyc_vm_probe";
	*(int32_t*)(code + kIndex) = -1;

	RValue result;
	bool ret = false;
	DWORD ex = SehExecute(exec, self, code, &result, &ret);
	if (ex)
		Log("%s: Code_Execute faulted, exception 0x%08lX", label, ex);
	else
		Log("%s: Code_Execute returned %d; result kind %s value %s; converted buffer %p", label,
			ret, result.GetKindName().c_str(), result.ToString().c_str(), *(void**)(vmbuf + 0x20));
}

static void RunProbe()
{
	AurieModule* yytk = nullptr;
	wchar_t path[MAX_PATH] = {};
	HMODULE h = GetModuleHandleW(L"YYToolkit.dll");
	if (h) GetModuleFileNameW(h, path, MAX_PATH);
	AurieStatus s = h ? Internal::MdpLookupModuleByPath(path, yytk) : AURIE_OBJECT_NOT_FOUND;
	auto exec = yytk ? (PFN_CodeExecute)MmGetHookTrampoline(yytk, "ExecuteIt") : nullptr;
	Log("YYToolkit module %s, Code_Execute trampoline %p", AurieStatusToString(s), (void*)exec);
#ifdef YYTK_HAS_EXECUTE_CODE_OBJECT
	Log("using YYTKInterface::ExecuteCodeObject; IsYYCRunner() = %d", g_Yytk->IsYYCRunner());
#endif
	if (!exec)
		return;

	CInstance* global = nullptr;
	g_Yytk->GetGlobalInstance(&global);

	// 1. No references at all.
	std::vector<uint32_t> p1 = { 0xC0000000, 0, 0, 0x07500000, 0x9C050000 };   // push.d 42 ; conv.d.v ; ret.v
	double v42 = 42.0; memcpy(&p1[1], &v42, 8);
	RunProgram(exec, global, "push.d 42; conv.d.v; ret.v", p1);

	// 2. Built-in call, operand = the runtime function index (what the loader's fixup would write).
	int sqr = -1;
	g_Yytk->GetNamedRoutineIndex("sqr", &sqr);
	std::vector<uint32_t> p2 = { 0xC0000000, 0, 0, 0x07500000, 0xD9020001, (uint32_t)sqr, 0x9C050000 };
	double v7 = 7.0; memcpy(&p2[1], &v7, 8);   // push.d 7 ; conv.d.v ; call.i sqr(argc=1) ; ret.v
	Log("sqr index %d", sqr);
	RunProgram(exec, global, "sqr(7) via call.i", p2);

	// 3. Compiled-script call. A raw call.i with the script id faults on YYC: the VM's call
	// handler expects the script's CCode to hold bytecode, and YYC scripts have none. Going
	// through the script_execute built-in reaches the native code instead.
	int priest = -1, scriptExecute = -1;
	g_Yytk->GetNamedRoutineIndex("gml_Script_getPriestCount", &priest);
	g_Yytk->GetNamedRoutineIndex("script_execute", &scriptExecute);
	std::vector<uint32_t> p3 = { 0xC0000000, 0, 0, 0x07500000, 0xD9020001, (uint32_t)scriptExecute, 0x9C050000 };
	double id = priest; memcpy(&p3[1], &id, 8);   // push.d <id> ; conv.d.v ; call.i script_execute(argc=1) ; ret.v
	Log("getPriestCount id %d, script_execute index %d", priest, scriptExecute);
	RunProgram(exec, global, "script_execute(getPriestCount) via call.i", p3);

	// 4. Global variable read: push.v global.<slot> ; ret.v. The operand is
	// (VariableType.Normal 0xA0 << 24) | runtime slot, the form the loader's fixup produces.
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
		RunProgram(exec, global, "push.v global.<slot>; ret.v", p5);

		// Write: push.d 123 ; conv.d.v ; pop.v.v global.<slot> ; exit.i, then read it back
		// through YYTK and restore the original value the same way.
		auto writeProgram = [&](double v) {
			std::vector<uint32_t> w = { 0xC0000000, 0, 0, 0x07500000, 0x4555FFFB,
				0xA0000000u | ((uint32_t)slot & 0x07FFFFFF), 0x9D020000 };
			memcpy(&w[1], &v, 8);
			return w;
		};
		RunProgram(exec, global, "pop.v.v global.<slot> = 123", writeProgram(123.0));
		RValue* now = nullptr;
		g_Yytk->GetInstanceMember(RValue(global), varName.c_str(), now);
		Log("global.%s after bytecode write, read by YYTK: %s", varName.c_str(), now ? now->ToString().c_str() : "(null)");
		RunProgram(exec, global, "restore original value", writeProgram(expected.ToDouble()));
	}

	// 5. Same, with the raw call.i on the script id, last because it is expected to fault.
	std::vector<uint32_t> p4 = { 0xD9020000, (uint32_t)priest, 0x9C050000 };
	RunProgram(exec, global, "getPriestCount() via raw call.i (expected to fault)", p4);
	Log("YYCVmProbe done");
}

static void FrameCallback(FWFrame&) { g_Frames.fetch_add(1, std::memory_order_relaxed); }
static void CodeCallback(FWCodeEvent&)
{
	if (!g_Ran && g_Frames.load() > 300)
	{
		g_Ran = true;
		RunProbe();
	}
}

EXPORTED AurieStatus ModuleInitialize(IN AurieModule* Module, IN const fs::path& ModulePath)
{
	UNREFERENCED_PARAMETER(ModulePath);
	g_Yytk = GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
	wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring logp(exe); logp = logp.substr(0, logp.find_last_of(L"\\") + 1) + L"yyc_vm_probe.log";
	g_Log = _wfsopen(logp.c_str(), L"w", _SH_DENYNO);
	Log("YYCVmProbe start");
	g_Yytk->CreateCallback(Module, EVENT_FRAME, FrameCallback, 0);
	g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	return AURIE_SUCCESS;
}
