#include "Manifest.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace modhost
{
	static std::string Trim(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t\r\n");
		if (a == std::string::npos)
			return {};
		size_t b = s.find_last_not_of(" \t\r\n");
		return s.substr(a, b - a + 1);
	}

	static std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}

	static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

	static bool ValidId(const std::string& id)
	{
		if (id.empty())
			return false;
		for (unsigned char c : id)
			if (!std::isalnum(c) && c != '.' && c != '_' && c != '-')
				return false;
		return true;
	}

	bool ParseDepends(const std::string& text, std::vector<Dependency>& out, std::string& error)
	{
		out.clear();
		size_t start = 0;
		while (start <= text.size())
		{
			size_t comma = text.find(',', start);
			std::string item = Trim(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
			start = comma == std::string::npos ? text.size() + 1 : comma + 1;
			if (item.empty())
				continue;
			Dependency d;
			size_t ge = item.find(">=");
			d.id = Trim(item.substr(0, ge));
			if (ge != std::string::npos)
			{
				d.minVersion = Trim(item.substr(ge + 2));
				if (d.minVersion.empty())
				{
					error = "empty version in dependency '" + item + "'";
					return false;
				}
			}
			if (!ValidId(d.id))
			{
				error = "invalid dependency '" + item + "'";
				return false;
			}
			out.push_back(d);
		}
		return true;
	}

	bool ParseRunner(const std::string& text, RunnerReq& out)
	{
		std::string t = Lower(Trim(text));
		if (t.empty() || t == "any") out = RunnerReq::Any;
		else if (t == "vm") out = RunnerReq::VM;
		else if (t == "yyc") out = RunnerReq::YYC;
		else return false;
		return true;
	}

	int CompareVersions(const std::string& a, const std::string& b)
	{
		size_t i = 0, j = 0;
		while (i < a.size() || j < b.size())
		{
			size_t ie = a.find('.', i), je = b.find('.', j);
			std::string pa = i < a.size() ? a.substr(i, ie == std::string::npos ? std::string::npos : ie - i) : "0";
			std::string pb = j < b.size() ? b.substr(j, je == std::string::npos ? std::string::npos : je - j) : "0";
			bool na = !pa.empty() && std::all_of(pa.begin(), pa.end(), IsDigit);
			bool nb = !pb.empty() && std::all_of(pb.begin(), pb.end(), IsDigit);
			int c;
			if (na && nb)
			{
				unsigned long long x = std::stoull(pa), y = std::stoull(pb);
				c = x < y ? -1 : x > y ? 1 : 0;
			}
			else
				c = pa < pb ? -1 : pa > pb ? 1 : 0;
			if (c)
				return c;
			i = ie == std::string::npos ? a.size() : ie + 1;
			j = je == std::string::npos ? b.size() : je + 1;
		}
		return 0;
	}

	std::vector<Decision> Resolve(const std::vector<Manifest>& manifests, bool isYyc, const std::string& exeName)
	{
		std::vector<Decision> all;
		for (const Manifest& m : manifests)
			all.push_back({ m, true, {} });
		std::sort(all.begin(), all.end(), [](const Decision& a, const Decision& b) {
			return a.manifest.id != b.manifest.id ? a.manifest.id < b.manifest.id : a.manifest.dir < b.manifest.dir;
		});

		// Per-plugin checks.
		std::map<std::string, size_t> byId;   // enabled, valid plugins
		std::set<std::string> known;          // every valid manifest, for messages
		for (size_t i = 0; i < all.size(); i++)
		{
			Decision& d = all[i];
			const Manifest& m = d.manifest;
			auto skip = [&](std::string why) { d.load = false; d.reason = std::move(why); };
			if (!m.parseError.empty()) { skip("invalid plugin.ini: " + m.parseError); continue; }
			known.insert(m.id);
			if (!m.enabled) { skip("disabled"); continue; }
			auto dup = byId.find(m.id);
			if (dup != byId.end()) { skip("duplicate id, already provided by folder " + all[dup->second].manifest.dir); continue; }
			byId[m.id] = i;
			if (!m.dllExists) { skip("missing " + m.dll); continue; }
			if (m.runner == RunnerReq::VM && isYyc) { skip("needs a VM runner, game is YYC"); continue; }
			if (m.runner == RunnerReq::YYC && !isYyc) { skip("needs a YYC runner, game is VM"); continue; }
			if (!m.game.empty() && Lower(m.game) != Lower(exeName)) { skip("made for " + m.game); continue; }
		}

		// Dependencies, until nothing changes: a plugin whose dependency is skipped is skipped too.
		for (bool changed = true; changed;)
		{
			changed = false;
			for (Decision& d : all)
			{
				if (!d.load)
					continue;
				for (const Dependency& dep : d.manifest.depends)
				{
					auto it = byId.find(dep.id);
					std::string why;
					if (it == byId.end())
						why = known.count(dep.id) ? "dependency " + dep.id + " is not loaded" : "missing dependency " + dep.id;
					else if (!all[it->second].load)
						why = "dependency " + dep.id + " is not loaded";
					else if (!dep.minVersion.empty() && CompareVersions(all[it->second].manifest.version, dep.minVersion) < 0)
						why = "needs " + dep.id + " >= " + dep.minVersion + ", found " + all[it->second].manifest.version;
					if (!why.empty())
					{
						d.load = false;
						d.reason = why;
						changed = true;
						break;
					}
				}
			}
		}

		// Topological order (Kahn), lowest id first among the ready ones.
		std::vector<Decision> out;
		std::set<std::string> done;
		std::vector<size_t> pending;
		for (size_t i = 0; i < all.size(); i++)
			if (all[i].load)
				pending.push_back(i);
		while (!pending.empty())
		{
			auto ready = std::find_if(pending.begin(), pending.end(), [&](size_t i) {
				for (const Dependency& dep : all[i].manifest.depends)
					if (!done.count(dep.id))
						return false;
				return true;
			});
			if (ready == pending.end())
				break;   // the rest form or depend on a cycle
			out.push_back(all[*ready]);
			done.insert(all[*ready].manifest.id);
			pending.erase(ready);
		}
		for (size_t i : pending)
		{
			all[i].load = false;
			all[i].reason = "dependency cycle";
		}
		for (const Decision& d : all)
			if (!d.load)
				out.push_back(d);
		return out;
	}
}
