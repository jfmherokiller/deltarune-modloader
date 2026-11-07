// YYCRuntimeDump - dumps names the runner only knows at run time, for any YYToolkit v5 game.
// Built for YYC games (no symbols in the exe), but the dump is valid for VM games too.
//
// About 10 s after the first frame, from inside a GML event, it writes <game>\yyc_runtime\:
//   runtime.json      exe base/size, scripts (GetScriptData), built-in variables
//                     (GetBuiltinVariableInformation), built-in functions (Code_Function_Find on
//                     every identifier string in the exe), and the raw YYRunnerInterface slots
//   data_section.bin  the exe's .data after startup (resolved YYVAR ids, runtime pointers)
//   dump.log          progress; ends with "YYCRuntimeDump done" (or the step that faulted)
// tools/yyc_dump.py --runtime <dir> merges this into the static dump and the IDA script.
// Read-only toward the game. Each step runs under SEH so a fault is logged, not fatal.
#include <yyc_interop.hpp>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>
using namespace Aurie;
using namespace YYTK;

static YYTKInterface* g_Yytk = nullptr;
static FILE* g_Log = nullptr;
static std::wstring g_OutDir;
static std::atomic<uint64_t> g_Frames{ 0 };
static bool g_Ran = false;
static uintptr_t g_Base = 0;
static size_t g_ImageSize = 0;

static void Log(const char* fmt, ...)
{
	char buf[1024];
	va_list va; va_start(va, fmt); vsnprintf(buf, sizeof(buf), fmt, va); va_end(va);
	DbgPrintEx(LOG_SEVERITY_INFO, "[YYCRuntimeDump] %s", buf);
	if (g_Log) { fprintf(g_Log, "%s\n", buf); fflush(g_Log); }
}

// ------------------------------------------------------------------ helpers
static std::string Json(const char* s)
{
	if (!s) return "null";
	std::string o = "\"";
	for (const unsigned char* p = (const unsigned char*)s; *p; p++)
	{
		if (*p == '"' || *p == '\\') { o += '\\'; o += (char)*p; }
		else if (*p < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", *p); o += b; }
		else o += (char)*p;
	}
	return o + "\"";
}
static std::string Addr(const void* p)
{
	uintptr_t a = (uintptr_t)p;
	char b[48];
	if (!a) return "null";
	if (a >= g_Base && a < g_Base + g_ImageSize) snprintf(b, sizeof(b), "\"0x%llx\"", (unsigned long long)(a - g_Base));
	else snprintf(b, sizeof(b), "\"abs:0x%llx\"", (unsigned long long)a);   // outside the exe
	return b;
}
using yyc::detail::Readable;
static const IMAGE_SECTION_HEADER* Section(const char* name)
{
	auto dos = (const IMAGE_DOS_HEADER*)g_Base;
	auto nt = (const IMAGE_NT_HEADERS64*)(g_Base + dos->e_lfanew);
	auto s = IMAGE_FIRST_SECTION(nt);
	for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
		if (!strncmp((const char*)s->Name, name, 8)) return s;
	return nullptr;
}

// Each step appends JSON members to *out. SEH wrapper: no C++ objects with destructors here.
typedef bool (*Step)(std::string* out);
static DWORD SehStep(Step fn, std::string* out, bool* ok)
{
	__try { *ok = fn(out); return 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
}

// ------------------------------------------------------------------ steps
static bool S_Scripts(std::string* o)
{
	*o += "\"scripts\": [";
	int n = 0;
	for (int i = 0; i < 200000; i++)
	{
		CScript* s = nullptr;
		if (!AurieSuccess(g_Yytk->GetScriptData(i, s)) || !s) break;
		YYGMLFuncs* f = s->m_Functions;
		bool fok = f && Readable(f, sizeof(*f));
		*o += std::string(n++ ? ",\n" : "\n") + "{\"index\": " + std::to_string(i) +
			", \"name\": " + Json(Readable(s->m_Name, 1) ? s->m_Name : nullptr) +
			", \"funcs\": " + Addr(f) +
			", \"funcs_name\": " + Json(fok && Readable(f->m_Name, 1) ? f->m_Name : nullptr) +
			", \"func\": " + Addr(fok ? (void*)f->m_ScriptFunction : nullptr) +
			", \"has_vm_code\": " + (s->m_Code ? "true" : "false") + "}";
	}
	*o += "],\n";
	Log("scripts: %d", n);
	return n > 0;
}

static bool S_BuiltinVariables(std::string* o)
{
	*o += "\"builtin_variables\": [";
	int n = 0;
	for (size_t i = 0; i < 10000; i++)
	{
		RVariableRoutine* r = nullptr;
		if (!AurieSuccess(g_Yytk->GetBuiltinVariableInformation(i, r)) || !r) break;
		// RVariableRoutine (YYTK Tool.hpp): { const char* name; getter; setter; bool can_set; }
		void** f = (void**)r;
		*o += std::string(n++ ? ",\n" : "\n") + "{\"index\": " + std::to_string(i) +
			", \"name\": " + Json(Readable(f[0], 1) ? (const char*)f[0] : nullptr) +
			", \"get\": " + Addr(f[1]) + ", \"set\": " + Addr(f[2]) +
			", \"can_set\": " + (((const bool*)r)[24] ? "true" : "false") + "}";
	}
	*o += "],\n";
	Log("builtin variables: %d", n);
	return n > 0;
}

// The runner has no public way to enumerate built-in functions by index, so look up every
// identifier-shaped string in the exe's read-only data; each registered name is a literal there.
static bool S_BuiltinFunctions(std::string* o)
{
	auto t0 = std::chrono::steady_clock::now();
	const IMAGE_SECTION_HEADER* rd = Section(".rdata");
	if (!rd) { Log("no .rdata"); return false; }
	const char* p = (const char*)(g_Base + rd->VirtualAddress);
	const char* end = p + rd->Misc.VirtualSize;
	std::unordered_set<int> seen;
	int candidates = 0, n = 0, maxIndex = -1;
	*o += "\"builtin_functions\": [";
	while (p < end)
	{
		const char* s = p;
		while (p < end && *p) p++;
		size_t len = p - s;
		p++;
		if (len < 2 || len > 63) continue;
		bool ident = isalpha((unsigned char)s[0]) || s[0] == '_' || s[0] == '@' || s[0] == '$';
		for (size_t k = 1; ident && k < len; k++)
			ident = isalnum((unsigned char)s[k]) || s[k] == '_' || s[k] == '@' || s[k] == '$';
		if (!ident) continue;
		candidates++;
		int index = -1;
		if (!AurieSuccess(g_Yytk->GetNamedRoutineIndex(s, &index)) || index < 0 || index >= 100000) continue;
		if (!seen.insert(index).second) continue;
		PVOID fn = nullptr;
		g_Yytk->GetNamedRoutinePointer(s, &fn);
		if (index > maxIndex) maxIndex = index;
		*o += std::string(n++ ? ",\n" : "\n") + "{\"index\": " + std::to_string(index) +
			", \"name\": " + Json(s) + ", \"func\": " + Addr(fn) + "}";
	}
	*o += "],\n\"builtin_function_max_index\": " + std::to_string(maxIndex) + ",\n";
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	Log("builtin functions: %d found from %d candidate strings, max index %d, %lld ms", n, candidates, maxIndex, (long long)ms);
	return n > 0;
}

static bool S_RunnerInterface(std::string* o)
{
	const YYRunnerInterface& ri = g_Yytk->GetRunnerInterface();
	const void* const* slot = (const void* const*)&ri;
	size_t count = sizeof(YYRunnerInterface) / sizeof(void*);
	*o += "\"runner_interface\": [";
	for (size_t i = 0; i < count; i++)
		*o += std::string(i ? ", " : "") + Addr(slot[i]);
	*o += "],\n";
	Log("runner interface: %zu slots", count);
	return true;
}

static bool S_DataSection(std::string* o)
{
	const IMAGE_SECTION_HEADER* d = Section(".data");
	if (!d) return false;
	std::wstring path = g_OutDir + L"data_section.bin";
	FILE* f = _wfopen(path.c_str(), L"wb");
	if (!f) return false;
	size_t n = fwrite((const void*)(g_Base + d->VirtualAddress), 1, d->Misc.VirtualSize, f);
	fclose(f);
	char b[160];
	snprintf(b, sizeof(b), "\"data_section\": {\"rva\": \"0x%lx\", \"size\": %lu, \"file\": \"data_section.bin\"},\n",
		(unsigned long)d->VirtualAddress, (unsigned long)d->Misc.VirtualSize);
	*o += b;
	Log("data section: %zu bytes", n);
	return n == d->Misc.VirtualSize;
}

static void RunAll()
{
	std::string out = "{\n";
	char head[256];
	snprintf(head, sizeof(head), "\"image_base\": \"0x%llx\",\n\"image_size\": %zu,\n\"frames_at_dump\": %llu,\n",
		(unsigned long long)g_Base, g_ImageSize, (unsigned long long)g_Frames.load());
	out += head;
	struct { const char* name; Step fn; } steps[] = {
		{ "scripts", S_Scripts }, { "builtin variables", S_BuiltinVariables },
		{ "builtin functions", S_BuiltinFunctions }, { "runner interface", S_RunnerInterface },
		{ "data section", S_DataSection },
	};
	for (auto& s : steps)
	{
		std::string part; bool ok = false;
		DWORD code = SehStep(s.fn, &part, &ok);
		if (code) { Log("FAULT in %s: exception 0x%08lX", s.name, code); continue; }
		if (!ok) Log("step %s returned nothing", s.name);
		out += part;
	}
	out += "\"complete\": true\n}\n";
	FILE* f = _wfopen((g_OutDir + L"runtime.json").c_str(), L"wb");
	if (f) { fwrite(out.data(), 1, out.size(), f); fclose(f); }
	Log("YYCRuntimeDump done (%zu bytes of JSON)", out.size());
}

// ------------------------------------------------------------------ callbacks
static void FrameCallback(FWFrame&) { g_Frames.fetch_add(1, std::memory_order_relaxed); }
static void CodeCallback(FWCodeEvent&)
{
	if (!g_Ran && g_Frames.load() > 600)
	{
		g_Ran = true;
		RunAll();
	}
}

EXPORTED AurieStatus ModuleInitialize(IN AurieModule* Module, IN const fs::path& ModulePath)
{
	UNREFERENCED_PARAMETER(ModulePath);
	g_Yytk = GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
	HMODULE exe = GetModuleHandleW(nullptr);
	g_Base = (uintptr_t)exe;
	auto nt = (const IMAGE_NT_HEADERS64*)(g_Base + ((const IMAGE_DOS_HEADER*)exe)->e_lfanew);
	g_ImageSize = nt->OptionalHeader.SizeOfImage;

	wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
	g_OutDir = std::wstring(path);
	g_OutDir = g_OutDir.substr(0, g_OutDir.find_last_of(L"\\") + 1) + L"yyc_runtime\\";
	CreateDirectoryW(g_OutDir.c_str(), nullptr);
	g_Log = _wfsopen((g_OutDir + L"dump.log").c_str(), L"w", _SH_DENYNO);
	short a = 0, b = 0, c = 0;
	g_Yytk->QueryVersion(a, b, c);
	Log("YYToolkit v%d.%d.%d, exe base %p size 0x%zx", a, b, c, (void*)g_Base, g_ImageSize);
	Log("callback EVENT_FRAME: %s", AurieStatusToString(g_Yytk->CreateCallback(Module, EVENT_FRAME, FrameCallback, 0)));
	Log("callback EVENT_OBJECT_CALL: %s", AurieStatusToString(g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0)));
	return AURIE_SUCCESS;
}
