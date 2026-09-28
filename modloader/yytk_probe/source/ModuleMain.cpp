// YYTKProbe - generic YYToolkit v5 self-test plugin (any GameMaker game).
// Runs each YYTK API a runtime editor depends on once, from a GML event, and logs PASS/FAIL to
// <game>\yytk_probe.log (flushed per line, so a crash still leaves the last test named).
// Each test runs under SEH so an access violation is reported instead of killing the game.
#include <YYToolkit/YYTK_Shared.hpp>
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>
using namespace Aurie;
using namespace YYTK;

static YYTKInterface* g_Yytk = nullptr;
static FILE* g_Log = nullptr;
static std::atomic<uint64_t> g_Frames{ 0 }, g_Resizes{ 0 }, g_Events{ 0 };
static bool g_Ran = false;

static void Log(const char* fmt, ...)
{
	char buf[1024];
	va_list va; va_start(va, fmt); vsnprintf(buf, sizeof(buf), fmt, va); va_end(va);
	DbgPrintEx(LOG_SEVERITY_INFO, "[YYTKProbe] %s", buf);
	if (g_Log) { fprintf(g_Log, "%s\n", buf); fflush(g_Log); }
}

// SEH wrapper: no C++ objects with destructors live in this frame.
typedef bool (*TestFn)(void* ctx, std::string* detail);
static DWORD SehRun(TestFn fn, void* ctx, std::string* detail, bool* ok)
{
	__try { *ok = fn(ctx, detail); return 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
}
static int g_Pass = 0, g_Fail = 0;
static void Run(const char* name, TestFn fn, void* ctx = nullptr)
{
	Log("RUN  %s", name);
	std::string detail; bool ok = false;
	DWORD code = SehRun(fn, ctx, &detail, &ok);
	if (code) { g_Fail++; Log("FAIL %s: exception 0x%08lX %s", name, code, detail.c_str()); }
	else if (ok) { g_Pass++; Log("PASS %s %s", name, detail.c_str()); }
	else { g_Fail++; Log("FAIL %s %s", name, detail.c_str()); }
}

static RValue GlobalRV()
{
	CInstance* g = nullptr;
	g_Yytk->GetGlobalInstance(&g);
	return RValue(g);
}

// ---------------------------------------------------------------- tests
static bool T_FrameEvent(void*, std::string* d)
{
	*d = "frames=" + std::to_string(g_Frames.load()) + " object_calls=" + std::to_string(g_Events.load());
	return g_Frames.load() > 0;
}
static bool T_GlobalInstance(void*, std::string* d)
{
	CInstance* g = nullptr;
	AurieStatus s = g_Yytk->GetGlobalInstance(&g);
	char b[64]; snprintf(b, sizeof(b), "%s ptr=%p", AurieStatusToString(s), (void*)g);
	*d = b;
	return AurieSuccess(s) && g;
}
static bool T_GlobalNamesBuiltin(void*, std::string* d)
{
	RValue names = g_Yytk->CallBuiltin("variable_instance_get_names", { GlobalRV() });
	size_t n = 0;
	if (names.IsArray()) g_Yytk->GetArraySize(names, n);
	*d = std::string("kind=") + names.GetKindName() + " count=" + std::to_string(n);
	return names.IsArray() && n > 0;
}
static bool T_GlobalExistsBuiltin(void*, std::string* d)
{
	// the exact call GetInstanceMember makes internally
	RValue r = g_Yytk->CallBuiltin("variable_instance_exists", { GlobalRV(), "room" });
	*d = std::string("result kind=") + r.GetKindName();
	return true;
}
static bool T_EnumGlobal(void*, std::string* d)
{
	int count = 0; std::string first;
	AurieStatus s = g_Yytk->EnumInstanceMembers(GlobalRV(), [&](const char* name, RValue*) {
		if (!count) first = name ? name : "?";
		count++;
		return false;   // keep going
	});
	*d = std::string(AurieStatusToString(s)) + " members=" + std::to_string(count) + " first=" + first;
	return count > 0;
}
static bool T_GetGlobalMember(void* ctx, std::string* d)
{
	const char* name = (const char*)ctx;
	RValue* v = nullptr;
	AurieStatus s = g_Yytk->GetInstanceMember(GlobalRV(), name, v);
	*d = std::string(name) + ": " + AurieStatusToString(s) + (v ? " = " + v->ToString() : "");
	return AurieSuccess(s) && v;
}
static RValue g_Inst;          // instance_find result, shared by the steps below
static CInstance* g_InstPtr = nullptr;
static bool T_InstFind(void*, std::string* d)
{
	RValue cnt; g_Yytk->GetBuiltin("instance_count", nullptr, NULL_INDEX, cnt);
	g_Inst = g_Yytk->CallBuiltin("instance_find", { RValue(-3), RValue(0) });   // all, first
	*d = "instance_count kind=" + cnt.GetKindName() + " value=" + cnt.ToString() +
		" | instance_find kind=" + g_Inst.GetKindName() + " raw m_Kind=" + std::to_string((int)g_Inst.m_Kind) +
		" i64=" + std::to_string(g_Inst.m_i64);
	return true;
}
static bool T_InstToInt(void*, std::string* d)
{
	int32_t id = g_Inst.ToInt32();
	*d = "id=" + std::to_string(id);
	return id > 0;
}
static bool T_InstObject(void*, std::string* d)
{
	int32_t id = static_cast<int32_t>(g_Inst.m_i64 & 0xFFFFFFFF);
	AurieStatus s = g_Yytk->GetInstanceObject(id, g_InstPtr);
	char b[96]; snprintf(b, sizeof(b), "GetInstanceObject(%d) %s ptr=%p", id, AurieStatusToString(s), (void*)g_InstPtr);
	*d = b;
	return AurieSuccess(s) && g_InstPtr;
}
static bool T_InstEnum(void*, std::string* d)
{
	int n = 0; std::string names;
	AurieStatus s = g_Yytk->EnumInstanceMembers(RValue(g_InstPtr), [&](const char* nm, RValue*) {
		if (n < 6) { names += nm; names += ","; } n++; return false; });
	*d = std::string(AurieStatusToString(s)) + " members=" + std::to_string(n) + " [" + names + "]";
	return n >= 0;
}
static bool T_InstMemberX(void* ctx, std::string* d)
{
	const char* name = (const char*)ctx;
	RValue* x = nullptr;
	AurieStatus s = g_Yytk->GetInstanceMember(RValue(g_InstPtr), name, x);
	*d = std::string("GetInstanceMember(") + name + ") " + AurieStatusToString(s) + (x ? "=" + x->ToString() : "");
	return AurieSuccess(s) && x;
}
static bool T_BuiltinXDenied(void*, std::string* d)
{
	RValue* x = nullptr;
	AurieStatus s = g_Yytk->GetInstanceMember(RValue(g_InstPtr), "x", x);
	*d = std::string("GetInstanceMember(x) ") + AurieStatusToString(s) + " (expected AURIE_ACCESS_DENIED, no crash)";
	return s == AURIE_ACCESS_DENIED;
}
static bool T_RoutineIndex(void* ctx, std::string* d)
{
	const char* name = (const char*)ctx;
	int idx = -12345;
	AurieStatus s = g_Yytk->GetNamedRoutineIndex(name, &idx);
	*d = std::string(name) + " -> " + AurieStatusToString(s) + " index=" + std::to_string(idx);
	return true;
}
static bool T_CallBuiltinOnVariable(void*, std::string* d)
{
	RValue r = g_Yytk->CallBuiltin("instance_count", {});
	*d = "CallBuiltin(\"instance_count\") kind=" + std::to_string((int)r.m_Kind);
	return true;
}
static bool T_InstBuiltinX(void* ctx, std::string* d)
{
	const char* name = (const char*)ctx;
	RValue bx;
	AurieStatus s = g_Yytk->GetBuiltin(name, g_InstPtr, NULL_INDEX, bx);
	*d = std::string("GetBuiltin(") + name + ") " + AurieStatusToString(s) + " kind=" + std::to_string((int)bx.m_Kind);
	if (AurieSuccess(s)) *d += " value=" + bx.ToString();
	return AurieSuccess(s);
}
static bool T_InstVarGetBuiltin(void* ctx, std::string* d)
{
	const char* name = (const char*)ctx;
	RValue v = g_Yytk->CallBuiltin("variable_instance_get", { RValue(g_InstPtr), name });
	*d = std::string("variable_instance_get(") + name + ") kind=" + std::to_string((int)v.m_Kind) + " value=" + v.ToString();
	return true;
}
static bool T_CurrentRoom(void*, std::string* d)
{
	CRoom* r = nullptr;
	AurieStatus s = g_Yytk->GetCurrentRoomData(r);
	RValue room;
	g_Yytk->GetBuiltin("room", nullptr, NULL_INDEX, room);
	RValue name = g_Yytk->CallBuiltin("room_get_name", { room });
	*d = std::string(AurieStatusToString(s)) + " room=" + name.ToString();
	return AurieSuccess(s) && r;
}

static void RunAll()
{
	Log("---- YYTKProbe start (YYTK interface %p)", (void*)g_Yytk);
	Run("EVENT_FRAME fires", T_FrameEvent);
	Run("GetGlobalInstance", T_GlobalInstance);
	Run("GetCurrentRoomData + room builtin", T_CurrentRoom);
	Run("builtin variable_instance_get_names(global)", T_GlobalNamesBuiltin);
	Run("builtin variable_instance_exists(global, name)", T_GlobalExistsBuiltin);
	Run("EnumInstanceMembers(global)", T_EnumGlobal);
	static const char* kVar = "instance_count"; static const char* kNone = "no_such_function_xyz"; static const char* kFn = "instance_find";
	Run("GetNamedRoutineIndex(builtin variable)", T_RoutineIndex, (void*)kVar);
	Run("GetNamedRoutineIndex(missing)", T_RoutineIndex, (void*)kNone);
	Run("GetNamedRoutineIndex(function)", T_RoutineIndex, (void*)kFn);
	Run("CallBuiltin on a variable name", T_CallBuiltinOnVariable);
	Run("instance_count / instance_find kinds", T_InstFind);
	Run("instance_find result ToInt32", T_InstToInt);
	Run("GetInstanceObject", T_InstObject);
	if (g_InstPtr)
	{
		static const char* kX = "x"; static const char* kUser = "drc_msgtime";
		Run("EnumInstanceMembers(instance)", T_InstEnum);
		Run("builtin variable_instance_get(inst, x)", T_InstVarGetBuiltin, (void*)kX);
		Run("GetInstanceMember(inst, user var)", T_InstMemberX, (void*)kUser);
		Run("GetBuiltin(x, inst)", T_InstBuiltinX, (void*)kX);
		Run("GetInstanceMember(inst, x) refuses builtins", T_BuiltinXDenied);
	}
	const char* env = getenv("YYTK_PROBE_GLOBAL");
	if (env && *env) Run("GetInstanceMember(global, YYTK_PROBE_GLOBAL)", T_GetGlobalMember, (void*)env);
	Log("---- YYTKProbe done: %d passed, %d failed", g_Pass, g_Fail);
}

// ---------------------------------------------------------------- callbacks
static void FrameCallback(FWFrame&) { g_Frames.fetch_add(1, std::memory_order_relaxed); }
static void ResizeCallback(FWResize&) { g_Resizes.fetch_add(1, std::memory_order_relaxed); }
static void CodeCallback(FWCodeEvent&)
{
	g_Events.fetch_add(1, std::memory_order_relaxed);
	// run once, from inside a GML event, ~10 s after the first frame (or ~600 steps if frames never fire)
	static uint64_t steps = 0;
	if (!g_Ran && (g_Frames.load() > 600 || (g_Frames.load() == 0 && ++steps > 200000)))
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
	wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring logp(exe); logp = logp.substr(0, logp.find_last_of(L"\\") + 1) + L"yytk_probe.log";
	g_Log = _wfsopen(logp.c_str(), L"w", _SH_DENYNO);
	short a = 0, b = 0, c = 0;
	g_Yytk->QueryVersion(a, b, c);
	Log("YYToolkit v%d.%d.%d", a, b, c);
	Log("callback EVENT_FRAME: %s", AurieStatusToString(g_Yytk->CreateCallback(Module, EVENT_FRAME, FrameCallback, 0)));
	Log("callback EVENT_RESIZE: %s", AurieStatusToString(g_Yytk->CreateCallback(Module, EVENT_RESIZE, ResizeCallback, 0)));
	Log("callback EVENT_OBJECT_CALL: %s", AurieStatusToString(g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0)));
	return AURIE_SUCCESS;
}
