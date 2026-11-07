// YYCHookDemo - example mod for yyc_interop.hpp, written for Dwarf Eats Mountain (YYC) but
// using only GML names, so the same pattern works on any YYC game.
//
//   - detours gml_Script_loc (the game's localisation lookup, reached through ~400 direct
//     `call` sites) and counts calls and distinct call sites;
//   - detours the object event gml_Object_oGUI_Step_0;
//   - calls gml_Script_getPriestCount directly and through YYTK's CallGameScript, and logs both;
//   - with YYC_DEMO_TAG=1 in the environment, prefixes every localised string with "MOD " (not "[mod]": the game draws text
//     through Scribble, which treats square brackets as formatting tags and hides them),
//     which changes the text drawn on screen.
// Logs to <game>\yyc_hook_demo.log; "demo done" marks the end of the scripted checks.
#include <yyc_interop.hpp>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <map>
#include <set>
#include <string>
using namespace Aurie;
using namespace YYTK;

static YYTKInterface* g_Yytk = nullptr;
static AurieModule* g_Module = nullptr;
static FILE* g_Log = nullptr;
static std::atomic<uint64_t> g_Frames{ 0 }, g_LocCalls{ 0 }, g_GuiSteps{ 0 };
static std::set<uintptr_t> g_LocCallSites;   // game thread only
static std::set<std::string> g_LocKeys;
static bool g_Tag = false, g_Installed = false, g_Done = false;

static void Log(const char* fmt, ...)
{
	char buf[1024];
	va_list va; va_start(va, fmt); vsnprintf(buf, sizeof(buf), fmt, va); va_end(va);
	DbgPrintEx(LOG_SEVERITY_INFO, "[YYCHookDemo] %s", buf);
	if (g_Log) { fprintf(g_Log, "%s\n", buf); fflush(g_Log); }
}

// ------------------------------------------------------------------ hooks
static PFUNC_YYGMLScript g_OrigLoc = nullptr;
static RValue& HkLoc(CInstance* self, CInstance* other, RValue& result, int argc, RValue** argv)
{
	uint64_t n = g_LocCalls.fetch_add(1) + 1;
	g_LocCallSites.insert((uintptr_t)_ReturnAddress());
	RValue& r = g_OrigLoc(self, other, result, argc, argv);
	std::string key = argc > 0 && argv[0] ? argv[0]->ToString() : "";
	if (g_LocKeys.insert(key).second && g_LocKeys.size() <= 40)
		Log("loc(%s) -> %s  [call %llu]", key.c_str(), r.ToString().c_str(), (unsigned long long)n);
	if (g_Tag && r.IsString())
		r = RValue("MOD " + r.ToString());
	return r;
}

static PFUNC_YYGML g_OrigGuiStep = nullptr;
static void HkGuiStep(CInstance* self, CInstance* other)
{
	g_GuiSteps.fetch_add(1);
	g_OrigGuiStep(self, other);
}

// ------------------------------------------------------------------ steps on the game thread
static void Install()
{
	if (!yyc::Functions().Init(g_Yytk))
	{
		Log("yyc::Registry::Init failed (not a YYC runner?)");
		return;
	}
	Log("registry: %zu compiled scripts/events", yyc::Functions().Count());
	Log("hook loc: %s", AurieStatusToString(yyc::Hook(g_Module, "loc", HkLoc, &g_OrigLoc)));
	Log("hook gml_Object_oGUI_Step_0: %s",
		AurieStatusToString(yyc::Hook(g_Module, "gml_Object_oGUI_Step_0", HkGuiStep, &g_OrigGuiStep)));
}

static void CallChecks()
{
	CInstance* global = nullptr;
	g_Yytk->GetGlobalInstance(&global);
	RValue direct;
	bool ok = yyc::CallScript("getPriestCount", global, global, direct);
	RValue viaYytk = g_Yytk->CallGameScript("gml_Script_getPriestCount", {});
	Log("getPriestCount: yyc::CallScript %s -> %s | CallGameScript -> %s",
		ok ? "ok" : "not found", direct.ToString().c_str(), viaYytk.ToString().c_str());
	RValue text;
	ok = yyc::CallScript("loc", global, global, text, { RValue("ui.tutorial.header") });
	Log("loc(\"ui.tutorial.header\") via yyc::CallScript: %s -> %s", ok ? "ok" : "not found", text.ToString().c_str());
}

// Names the function containing each loc call site, from the exe's unwind data (.pdata).
static void LogCallSites()
{
	std::map<uintptr_t, const char*> byCode;
	yyc::Functions().ForEach([&](const yyc::Function& f) { byCode[(uintptr_t)f.code] = f.name; });
	for (uintptr_t site : g_LocCallSites)
	{
		DWORD64 imageBase = 0;
		PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(site, &imageBase, nullptr);
		uintptr_t begin = rf ? (uintptr_t)(imageBase + rf->BeginAddress) : 0;
		auto it = byCode.find(begin);
		Log("  loc call site in %s+0x%llx", it != byCode.end() ? it->second : "(not compiled GML)",
			(unsigned long long)(site - begin));
	}
}

static void FrameCallback(FWFrame&)
{
	uint64_t f = g_Frames.fetch_add(1) + 1;
	if (f % 300 == 0)
		Log("frame %llu: loc calls %llu from %zu call sites, oGUI Step %llu", (unsigned long long)f,
			(unsigned long long)g_LocCalls.load(), g_LocCallSites.size(), (unsigned long long)g_GuiSteps.load());
}

static void CodeCallback(FWCodeEvent&)
{
	if (!g_Installed)
	{
		g_Installed = true;
		Install();
	}
	else if (!g_Done && g_Frames.load() > 900)
	{
		g_Done = true;
		CallChecks();
		LogCallSites();
		// The engine's own screenshot (written to the game's save folder), so the on-screen
		// effect of tag mode can be checked without a desktop capture tool.
		RValue saved = g_Yytk->CallBuiltin("screen_save", { RValue("yyc_hook_demo.png") });
		Log("screen_save(yyc_hook_demo.png) -> %s", saved.ToString().c_str());
		Log("demo done: loc calls %llu from %zu call sites, oGUI Step %llu",
			(unsigned long long)g_LocCalls.load(), g_LocCallSites.size(), (unsigned long long)g_GuiSteps.load());
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
	std::wstring logp(exe); logp = logp.substr(0, logp.find_last_of(L"\\") + 1) + L"yyc_hook_demo.log";
	g_Log = _wfsopen(logp.c_str(), L"w", _SH_DENYNO);
	const char* tag = getenv("YYC_DEMO_TAG");
	g_Tag = tag && *tag == '1';
	Log("YYCHookDemo start, tag mode %s", g_Tag ? "on" : "off");
	g_Yytk->CreateCallback(Module, EVENT_FRAME, FrameCallback, 0);
	g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	return AURIE_SUCCESS;
}
