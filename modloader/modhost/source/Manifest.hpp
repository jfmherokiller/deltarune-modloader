// Plugin manifests and load-order resolution for ModHost. No Aurie/YYToolkit dependency, so the
// rules can be tested on their own (tests/resolve.cpp).
#pragma once
#include <string>
#include <vector>

namespace modhost
{
	enum class RunnerReq { Any, VM, YYC };

	struct Dependency
	{
		std::string id;
		std::string minVersion;   // empty: any version
	};

	// One mods/plugins/<dir>/plugin.ini.
	struct Manifest
	{
		std::string dir;          // folder name under mods/plugins
		std::string id;
		std::string name;
		std::string version;
		std::string dll;          // file name inside the plugin folder
		std::string game;         // exe file name to require (case-insensitive); empty: any game
		RunnerReq runner = RunnerReq::Any;
		std::vector<Dependency> depends;
		bool enabled = true;
		bool dllExists = true;
		std::string parseError;   // set when plugin.ini is invalid
	};

	struct Decision
	{
		Manifest manifest;
		bool load = false;
		std::string reason;       // why it is skipped; empty when loaded
	};

	// Parses "a, b>=1.2 ,c" into dependencies. Returns false on a malformed entry.
	bool ParseDepends(const std::string& text, std::vector<Dependency>& out, std::string& error);
	bool ParseRunner(const std::string& text, RunnerReq& out);

	// Dotted numeric comparison: "1.10" > "1.9", missing parts count as 0. Non-numeric parts
	// compare as text.
	int CompareVersions(const std::string& a, const std::string& b);

	// Decides which plugins load and in which order. Loaded plugins come first, dependencies
	// before dependents and otherwise sorted by id; skipped ones follow with a reason.
	// `isYyc`: the game's runner type. `exeName`: the game executable's file name.
	std::vector<Decision> Resolve(const std::vector<Manifest>& manifests, bool isYyc, const std::string& exeName);
}
