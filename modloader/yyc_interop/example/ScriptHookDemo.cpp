// ScriptHookDemo - example mod for script_hooks.hpp. The same DLL runs on VM (bytecode) and
// YYC (native) GameMaker games; gm::Init picks the backend.
//
//   - VM only: observes every script call for 600 frames, then logs the 25 most called scripts
//     (observe-all is not available on YYC);
//   - watch=<name>: observes one script and logs self/other/argc of its first calls and a count;
//   - redirect=<from>=<to>: makes calls of `from` run `to`;
//   - screenshot=1: saves script_hook_demo.png to the game's save folder at the end. On
//     DELTARUNE under Proton the game terminates right after writing it (not yet explained;
//     the same call works on Dwarf Eats Mountain), so it is off by default.
// Options are read from [demo] in <game>\script_hook_demo.ini (optional).
// Logs to <game>\script_hook_demo.log; "demo done" marks the end.
#include <script_hooks.hpp>
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
using namespace Aurie;
using namespace YYTK;

static YYTKInterface* g_Yytk = nullptr;
static AurieModule* g_Module = nullptr;
static FILE* g_Log = nullptr;
static uint64_t g_Frames = 0, g_Calls = 0, g_Watched = 0;
static std::unordered_map<const char*, uint64_t> g_Counts;   // game thread only
static bool g_Installed = false, g_Done = false, g_Finished = false;
static std::wstring g_Ini;

static std::string Option(const wchar_t* key)
{
	wchar_t buf[512] = L"";
	GetPrivateProfileStringW(L"demo", key, L"", buf, 512, g_Ini.c_str());
	char out[512] = "";
	WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof(out), nullptr, nullptr);
	return out;
}

static void Log(const char* fmt, ...)
{
	char buf[1024];
	va_list va; va_start(va, fmt); vsnprintf(buf, sizeof(buf), fmt, va); va_end(va);
	DbgPrintEx(LOG_SEVERITY_INFO, "[ScriptHookDemo] %s", buf);
	if (g_Log) { fprintf(g_Log, "%s\n", buf); fflush(g_Log); }
}

static void CountAll(const gm::CallInfo& call, void*)
{
	g_Calls++;
	g_Counts[call.name]++;
}

static void Watched(const gm::CallInfo& call, void*)
{
	if (++g_Watched <= 5)
		Log("watched call %llu: %s self=%p other=%p argc=%d", (unsigned long long)g_Watched,
			call.name, (void*)call.self, (void*)call.other, call.argc);
}

static void Install()
{
	AurieStatus s = gm::Init(g_Module, g_Yytk);
	gm::Runner r = gm::GetRunner();
	Log("gm::Init: %s, runner %s", AurieStatusToString(s),
		r == gm::Runner::VM ? "VM" : r == gm::Runner::YYC ? "YYC" : "unknown");
	if (!AurieSuccess(s))
		return;
	if (!gm::OwnsHooks())
		Log("using the script hooks another plugin installed (GmScriptHooks)");
	else if (r == gm::Runner::VM)
		Log("VM frame-push routine exe+0x%llx, %zu scripts",
			(unsigned long long)((uintptr_t)yyc::vm::PushFrameAddress() - (uintptr_t)GetModuleHandleW(nullptr)),
			yyc::vm::ScriptCount());
	else
		Log("YYC registry: %zu compiled scripts/events", yyc::Functions().Count());
	Log("observe all: %s", AurieStatusToString(gm::Observe("", CountAll)));
	std::string w = Option(L"watch");
	if (!w.empty())
		Log("observe %s: %s", w.c_str(), AurieStatusToString(gm::Observe(w, Watched)));
	std::string spec = Option(L"redirect");
	size_t eq = spec.find('=');
	if (eq != std::string::npos)
		Log("redirect %s -> %s: %s", spec.substr(0, eq).c_str(), spec.substr(eq + 1).c_str(),
			AurieStatusToString(gm::Redirect(spec.substr(0, eq), spec.substr(eq + 1))));
}

static void Report()
{
	std::vector<std::pair<const char*, uint64_t>> v(g_Counts.begin(), g_Counts.end());
	std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
	Log("%llu script calls over %llu frames, %zu distinct scripts; watched script called %llu times",
		(unsigned long long)g_Calls, (unsigned long long)g_Frames, v.size(), (unsigned long long)g_Watched);
	for (size_t i = 0; i < v.size() && i < 25; i++)
		Log("  %8llu  %s", (unsigned long long)v[i].second, v[i].first);
}

// Runs in an object event, i.e. in the game's step, not inside the present hook.
static void CodeCallback(FWCodeEvent&)
{
	if (!g_Installed)
	{
		g_Installed = true;
		Install();
	}
	if (!g_Done || g_Finished)
		return;
	g_Finished = true;
	if (Option(L"screenshot") == "1")
	{
		RValue saved = g_Yytk->CallBuiltin("screen_save", { RValue("script_hook_demo.png") });
		Log("screen_save(script_hook_demo.png) -> %s", saved.ToString().c_str());
	}
	Log("demo done");
}

static void FrameCallback(FWFrame&)
{
	if (++g_Frames == 600 && !g_Done)
	{
		g_Done = true;
		Report();
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
	std::wstring dir(exe); dir = dir.substr(0, dir.find_last_of(L"\\") + 1);
	g_Ini = dir + L"script_hook_demo.ini";
	g_Log = _wfsopen((dir + L"script_hook_demo.log").c_str(), L"w", _SH_DENYNO);
	Log("ScriptHookDemo start");
	g_Yytk->CreateCallback(Module, EVENT_FRAME, FrameCallback, 0);
	g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	return AURIE_SUCCESS;
}
