// HelloPlugin - smallest ModHost plugin: reports what ModHost knows about it, reads one value
// from its config.ini and logs through ModHost. Ships with example/plugin.ini.
#include <YYToolkit/YYTK_Shared.hpp>
#include <modhost.hpp>
#include <windows.h>
using namespace Aurie;
using namespace YYTK;

static AurieModule* g_Module = nullptr;
static int g_Frames = 0;

static void FrameCallback(FWFrame&)
{
	if (++g_Frames == 300)
		modhost::Log(g_Module, "300 frames rendered");
}

EXPORTED AurieStatus ModuleInitialize(IN AurieModule* Module, IN const fs::path& ModulePath)
{
	UNREFERENCED_PARAMETER(ModulePath);
	g_Module = Module;
	modhost::ModHostInterface* host = modhost::Get();
	if (!host)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;   // needs ModHost
	const modhost::PluginInfo* me = host->GetPluginInfo(Module);
	if (!me)
		return AURIE_MODULE_INITIALIZATION_FAILED;
	wchar_t greeting[256] = L"";
	GetPrivateProfileStringW(L"hello", L"greeting", L"hello (no config.ini)", greeting, 256, me->configPath);
	modhost::Log(Module, "%s %s on a %s runner; greeting from config: %ls; %zu plugin(s) loaded so far, counting this one",
		me->name, me->version, host->IsYYCRunner() ? "YYC" : "VM", greeting, host->GetPluginCount());
	if (YYTKInterface* yytk = GetInterface())
		yytk->CreateCallback(Module, EVENT_FRAME, FrameCallback, 0);
	return AURIE_SUCCESS;
}
