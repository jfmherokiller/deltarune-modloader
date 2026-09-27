// DeltaruneYYTK - minimal YYToolkit v5 plugin for DELTARUNE.
// Proves the Aurie + YYToolkit stack is live inside the game:
//   * prints the YYTK version it's talking to
//   * counts GML event executions (EVENT_OBJECT_CALL = YYTK's ExecuteIt hook)
//   * about once a second (every 30th obj_gamecontroller Begin Step), prints the
//     current room and DELTARUNE globals read through the YYTK interface.
//     (EVENT_FRAME is not used: YYToolkit 5.0.0 never installs its Stage 3
//     D3D11 Present hook, so frame callbacks never fire.)
// Output goes to the Aurie console and aurie.log in the game folder.
#include <YYToolkit/YYTK_Shared.hpp>
#include <atomic>
using namespace Aurie;
using namespace YYTK;

static YYTKInterface* g_Yytk = nullptr;
static std::atomic<uint64_t> g_CodeCalls{ 0 };

static void StatusTick();

static void CodeCallback(FWCodeEvent& Ctx)
{
	g_CodeCalls.fetch_add(1, std::memory_order_relaxed);

	CCode* this_code = std::get<2>(Ctx.Arguments());
	const char* this_name = this_code ? this_code->GetName() : nullptr;
	if (this_name && !strcmp(this_name, "gml_Object_obj_gamecontroller_Step_1"))
		StatusTick();

	static int logged = 0;
	if (logged < 5)
	{
		CCode* code = std::get<2>(Ctx.Arguments());
		if (code && code->GetName())
		{
			DbgPrintEx(LOG_SEVERITY_DEBUG, "[DeltaruneYYTK] GML event: %s", code->GetName());
			logged++;
		}
	}
}

// Reads global.<Name> via the GML builtins (variable_global_exists/_get).
// YYTK 5.0.0's GetInstanceMember(global, ...) crashes on this runner: it passes the
// global CInstance through variable_instance_exists and the runner's YYGetString
// faults (see modloader/README.md).
static bool ReadGlobal(const char* Name, RValue& Out)
{
	if (!g_Yytk->CallBuiltin("variable_global_exists", { Name }).ToBoolean())
		return false;
	Out = g_Yytk->CallBuiltin("variable_global_get", { Name });
	return true;
}

static void StatusTick()
{
	static uint32_t frame = 0;
	if (frame++ % 30 != 0)
		return;

	std::string room_name = "?";
	{
		RValue current_room;
		if (AurieSuccess(g_Yytk->GetBuiltin("room", nullptr, NULL_INDEX, current_room)))
		{
			room_name = g_Yytk->CallBuiltin("room_get_name", { current_room }).ToString();
		}
	}

	RValue chapter, gold;
	bool has_chapter = ReadGlobal("chapter", chapter);
	bool has_gold = ReadGlobal("gold", gold);

	DbgPrintEx(LOG_SEVERITY_INFO, "[DeltaruneYYTK] step %u | room %s | chapter %s | gold %s | GML events so far %llu",
		frame - 1,
		room_name.c_str(),
		has_chapter ? chapter.ToString().c_str() : "n/a",
		has_gold ? gold.ToString().c_str() : "n/a",
		(unsigned long long)g_CodeCalls.load()
	);
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	UNREFERENCED_PARAMETER(ModulePath);

	g_Yytk = GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

	short major = 0, minor = 0, patch = 0;
	g_Yytk->QueryVersion(major, minor, patch);
	DbgPrintEx(LOG_SEVERITY_INFO, "[DeltaruneYYTK] loaded - YYToolkit v%d.%d.%d", major, minor, patch);

	AurieStatus s1 = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	DbgPrintEx(LOG_SEVERITY_INFO, "[DeltaruneYYTK] callback object_call=%s", AurieStatusToString(s1));

	return AURIE_SUCCESS;
}
