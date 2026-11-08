// modhost.hpp - the interface ModHost offers to the plugins it loads (and to other tools).
//
// ModHost (modloader/modhost) is an Aurie module in mods/aurie. It loads plugins from
// mods/plugins/<folder>/ according to each folder's plugin.ini (see modhost/README.md), in
// dependency order, skipping plugins made for another runner type or another game.
//
//   auto* host = modhost::Get();                       // nullptr if ModHost is not installed
//   const modhost::PluginInfo* me = host ? host->GetPluginInfo(Module) : nullptr;
//   modhost::Log(Module, "loaded, config at %ls", me->configPath);
//
// Header-only; needs the Aurie shared header (YYToolkit plugins already include it).
#pragma once
#include <Aurie/shared.hpp>
#include <cstdarg>
#include <cstdio>

namespace modhost
{
	constexpr const char* InterfaceName = "ModHost";

	struct PluginInfo
	{
		const char* id;
		const char* name;
		const char* version;
		const wchar_t* folder;       // mods\plugins\<folder>, absolute
		const wchar_t* configPath;   // <folder>\config.ini; may not exist yet
	};

	struct ModHostInterface : Aurie::AurieInterfaceBase
	{
		// The plugin that `module` belongs to, or nullptr if ModHost did not load it.
		// Valid from the plugin's ModulePreinitialize on.
		virtual const PluginInfo* GetPluginInfo(Aurie::AurieModule* module) = 0;
		// Appends a line to mods\modhost.log, prefixed with the plugin id, and to aurie.log.
		virtual void Log(Aurie::AurieModule* module, const char* message) = 0;
		// From the GML built-in code_is_compiled().
		virtual bool IsYYCRunner() = 0;
		// Loaded plugins, in load order.
		virtual size_t GetPluginCount() = 0;
		virtual const PluginInfo* GetPlugin(size_t index) = 0;
	};

	inline ModHostInterface* Get()
	{
		Aurie::AurieInterfaceBase* i = nullptr;
		if (!Aurie::AurieSuccess(Aurie::ObGetInterface(InterfaceName, i)))
			return nullptr;
		return static_cast<ModHostInterface*>(i);
	}

	inline void Log(Aurie::AurieModule* module, const char* fmt, ...)
	{
		ModHostInterface* host = Get();
		if (!host)
			return;
		char buf[2048];
		va_list va;
		va_start(va, fmt);
		vsnprintf(buf, sizeof(buf), fmt, va);
		va_end(va);
		host->Log(module, buf);
	}
}
