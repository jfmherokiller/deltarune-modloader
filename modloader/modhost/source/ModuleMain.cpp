// ModHost - BepInEx-style plugin host for GameMaker games (VM and YYC), on Aurie + YYToolkit.
// Reads mods\plugins\*\plugin.ini, decides what loads (Manifest.cpp), maps the plugins with
// Aurie in dependency order and serves the "ModHost" interface (include/modhost.hpp).
#include <YYToolkit/YYTK_Shared.hpp>
#include <modhost.hpp>
#include "Manifest.hpp"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

using namespace Aurie;
using namespace YYTK;
namespace fs = std::filesystem;

namespace
{
	struct Loaded
	{
		modhost::Manifest manifest;
		std::wstring folder, config, dllPath;
		modhost::PluginInfo info{};
	};

	std::deque<Loaded> g_Plugins;      // deque: PluginInfo pointers stay valid while appending
	std::mutex g_LogLock;
	FILE* g_Log = nullptr;
	bool g_IsYyc = false;

	std::string Utf8(const std::wstring& w)
	{
		if (w.empty())
			return {};
		int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
		return s;
	}

	std::wstring Lower(std::wstring s)
	{
		for (auto& c : s)
			c = (wchar_t)towlower(c);
		return s;
	}

	void WriteLog(const char* source, const char* message)
	{
		std::lock_guard lock(g_LogLock);
		SYSTEMTIME t;
		GetLocalTime(&t);
		if (g_Log)
		{
			fprintf(g_Log, "[%02d:%02d:%02d.%03d] [%s] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, source, message);
			fflush(g_Log);
		}
		DbgPrintEx(LOG_SEVERITY_INFO, "[%s] %s", source, message);
	}

	void HostLog(const char* fmt, ...)
	{
		char buf[2048];
		va_list va;
		va_start(va, fmt);
		vsnprintf(buf, sizeof(buf), fmt, va);
		va_end(va);
		WriteLog("ModHost", buf);
	}

	std::string Ini(const std::wstring& file, const wchar_t* key)
	{
		wchar_t buf[1024] = L"";
		GetPrivateProfileStringW(L"plugin", key, L"", buf, 1024, file.c_str());
		return Utf8(buf);
	}

	modhost::Manifest ReadManifest(const fs::path& dir)
	{
		modhost::Manifest m;
		std::wstring ini = (dir / L"plugin.ini").wstring();
		m.dir = Utf8(dir.filename().wstring());
		m.id = Ini(ini, L"id");
		m.name = Ini(ini, L"name");
		m.version = Ini(ini, L"version");
		m.dll = Ini(ini, L"dll");
		m.game = Ini(ini, L"game");
		std::string enabled = Ini(ini, L"enabled");
		m.enabled = enabled.empty() || enabled == "1" || enabled == "true";
		std::string error;
		if (m.id.empty())
			m.parseError = "missing id";
		else if (m.dll.empty())
			m.parseError = "missing dll";
		else if (!modhost::ParseRunner(Ini(ini, L"runner"), m.runner))
			m.parseError = "runner must be any, vm or yyc";
		else if (!modhost::ParseDepends(Ini(ini, L"depends"), m.depends, error))
			m.parseError = error;
		if (m.id.empty())
			m.id = m.dir;   // so the skip is reported under a recognisable name
		if (m.name.empty())
			m.name = m.id;
		if (m.version.empty())
			m.version = "0";
		std::error_code ec;
		m.dllExists = !m.dll.empty() && fs::is_regular_file(dir / fs::u8path(m.dll), ec);
		return m;
	}

	// Plugins depend on the image path Aurie stored, which can differ in case or separators.
	Loaded* FindByModule(AurieModule* module)
	{
		if (!module)
			return nullptr;
		std::wstring path = Lower(fs::path(Internal::MdpGetImagePath(module)).lexically_normal().wstring());
		for (auto& p : g_Plugins)
			if (Lower(fs::path(p.dllPath).lexically_normal().wstring()) == path)
				return &p;
		return nullptr;
	}

	struct HostInterface : modhost::ModHostInterface
	{
		AurieStatus Create() override { return AURIE_SUCCESS; }
		void Destroy() override {}
		void QueryVersion(short& major, short& minor, short& patch) override { major = 1; minor = 0; patch = 0; }

		const modhost::PluginInfo* GetPluginInfo(AurieModule* module) override
		{
			Loaded* p = FindByModule(module);
			return p ? &p->info : nullptr;
		}
		void Log(AurieModule* module, const char* message) override
		{
			Loaded* p = FindByModule(module);
			WriteLog(p ? p->manifest.id.c_str() : "unknown plugin", message ? message : "");
		}
		bool IsYYCRunner() override { return g_IsYyc; }
		size_t GetPluginCount() override
		{
			size_t n = 0;
			for (auto& p : g_Plugins)
				n += p.info.id != nullptr;
			return n;
		}
		const modhost::PluginInfo* GetPlugin(size_t index) override
		{
			for (auto& p : g_Plugins)
				if (p.info.id && index-- == 0)
					return &p.info;
			return nullptr;
		}
	};
	HostInterface g_Interface;
}

EXPORTED AurieStatus ModuleInitialize(IN AurieModule* Module, IN const fs::path& ModulePath)
{
	// mods\aurie\ModHost.dll -> mods\plugins
	fs::path mods = ModulePath.parent_path().parent_path();
	fs::path plugins = mods / L"plugins";
	g_Log = _wfsopen((mods / L"modhost.log").c_str(), L"w", _SH_DENYNO);

	YYTKInterface* yytk = GetInterface();
	if (!yytk)
	{
		HostLog("YYToolkit is not loaded; no plugins loaded");
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
	}
	RValue compiled;
	AurieStatus st = yytk->CallBuiltinEx(compiled, "code_is_compiled", nullptr, nullptr, {});
	if (!AurieSuccess(st))
	{
		HostLog("code_is_compiled() failed (%s); no plugins loaded", AurieStatusToString(st));
		return st;
	}
	g_IsYyc = compiled.ToBoolean();

	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::string exeName = Utf8(fs::path(exe).filename().wstring());
	HostLog("ModHost 1.0.0: %s, %s runner, plugins in %s", exeName.c_str(), g_IsYyc ? "YYC" : "VM",
		Utf8(plugins.wstring()).c_str());

	st = ObCreateInterface(Module, &g_Interface, modhost::InterfaceName);
	if (!AurieSuccess(st))
		HostLog("ObCreateInterface(ModHost) failed: %s", AurieStatusToString(st));

	std::vector<modhost::Manifest> manifests;
	std::error_code ec;
	if (fs::is_directory(plugins, ec))
		for (const auto& entry : fs::directory_iterator(plugins, ec))
			if (entry.is_directory() && fs::is_regular_file(entry.path() / L"plugin.ini"))
				manifests.push_back(ReadManifest(entry.path()));

	auto decisions = modhost::Resolve(manifests, g_IsYyc, exeName);
	size_t loaded = 0, skipped = 0;
	std::vector<std::string> failed;
	for (const auto& d : decisions)
	{
		const auto& m = d.manifest;
		std::string label = m.id + " " + m.version + " (" + m.dir + ")";
		if (!d.load)
		{
			HostLog("skip %s: %s", label.c_str(), d.reason.c_str());
			skipped++;
			continue;
		}
		// A dependency that was resolved but failed to map takes its dependents down with it.
		auto missing = std::find_if(m.depends.begin(), m.depends.end(), [&](const modhost::Dependency& dep) {
			return std::find(failed.begin(), failed.end(), dep.id) != failed.end();
		});
		if (missing != m.depends.end())
		{
			HostLog("skip %s: dependency %s failed to load", label.c_str(), missing->id.c_str());
			failed.push_back(m.id);
			skipped++;
			continue;
		}

		Loaded& p = g_Plugins.emplace_back();
		p.manifest = m;
		fs::path folder = plugins / fs::u8path(m.dir);
		p.folder = folder.wstring();
		p.config = (folder / L"config.ini").wstring();
		p.dllPath = (folder / fs::u8path(m.dll)).wstring();
		// Fill the info before mapping: the plugin may ask for it from its own init routines.
		p.info = { p.manifest.id.c_str(), p.manifest.name.c_str(), p.manifest.version.c_str(),
			p.folder.c_str(), p.config.c_str() };

		AurieModule* module = nullptr;
		st = MdMapImage(p.dllPath, module);
		if (!AurieSuccess(st))
		{
			HostLog("FAILED %s: MdMapImage returned %s", label.c_str(), AurieStatusToString(st));
			p.info.id = nullptr;   // not counted as loaded
			failed.push_back(m.id);
			continue;
		}
		HostLog("loaded %s", label.c_str());
		loaded++;
	}
	HostLog("%zu loaded, %zu skipped, %zu failed", loaded, skipped, failed.size());
	return AURIE_SUCCESS;
}
