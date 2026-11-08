// Tests for ModHost's manifest rules (no game needed). Built and run by modhost/build.sh test.
#include "../source/Manifest.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace modhost;

static int g_Failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_Failures++; } } while (0)

static Manifest M(const std::string& id, std::vector<Dependency> deps = {}, RunnerReq runner = RunnerReq::Any)
{
	Manifest m;
	m.dir = id + "_dir";
	m.id = id;
	m.name = id;
	m.version = "1.0.0";
	m.dll = id + ".dll";
	m.runner = runner;
	m.depends = std::move(deps);
	return m;
}

static const Decision* Find(const std::vector<Decision>& v, const std::string& id)
{
	for (const auto& d : v)
		if (d.manifest.id == id)
			return &d;
	return nullptr;
}

static size_t Index(const std::vector<Decision>& v, const std::string& id)
{
	for (size_t i = 0; i < v.size(); i++)
		if (v[i].manifest.id == id)
			return i;
	return (size_t)-1;
}

int main()
{
	// Versions
	CHECK(CompareVersions("1.10", "1.9") > 0);
	CHECK(CompareVersions("1.2", "1.2.0") == 0);
	CHECK(CompareVersions("2", "10") < 0);
	CHECK(CompareVersions("1.0-beta", "1.0-alpha") > 0);

	// Depends syntax
	std::vector<Dependency> deps;
	std::string err;
	CHECK(ParseDepends(" a , b>=1.2,, c ", deps, err) && deps.size() == 3);
	CHECK(deps[1].id == "b" && deps[1].minVersion == "1.2");
	CHECK(!ParseDepends("a>=", deps, err));
	CHECK(!ParseDepends("bad id", deps, err));
	RunnerReq r;
	CHECK(ParseRunner("YYC", r) && r == RunnerReq::YYC);
	CHECK(ParseRunner("", r) && r == RunnerReq::Any);
	CHECK(!ParseRunner("html5", r));

	// Order: dependencies first, otherwise by id.
	{
		auto v = Resolve({ M("c", { { "a", "" } }), M("b"), M("a", { { "b", "" } }) }, false, "game.exe");
		CHECK(v.size() == 3 && v[0].manifest.id == "b" && v[1].manifest.id == "a" && v[2].manifest.id == "c");
		CHECK(v[0].load && v[1].load && v[2].load);
	}
	// Runner, game and disabled filters, and the dependents of a skipped plugin.
	{
		Manifest vmOnly = M("vmonly", {}, RunnerReq::VM);
		Manifest yycOnly = M("yyconly", {}, RunnerReq::YYC);
		Manifest other = M("othergame");
		other.game = "DELTARUNE.exe";
		Manifest mine = M("mine");
		mine.game = "dwarf eats mountain.EXE";
		Manifest off = M("off");
		off.enabled = false;
		Manifest needsOff = M("needsoff", { { "off", "" } });
		Manifest needsVm = M("needsvm", { { "vmonly", "" } });
		auto v = Resolve({ vmOnly, yycOnly, other, mine, off, needsOff, needsVm }, true, "Dwarf Eats Mountain.exe");
		CHECK(Find(v, "yyconly")->load);
		CHECK(Find(v, "mine")->load);
		CHECK(!Find(v, "vmonly")->load && Find(v, "vmonly")->reason == "needs a VM runner, game is YYC");
		CHECK(!Find(v, "othergame")->load && Find(v, "othergame")->reason == "made for DELTARUNE.exe");
		CHECK(!Find(v, "off")->load && Find(v, "off")->reason == "disabled");
		CHECK(!Find(v, "needsoff")->load && Find(v, "needsoff")->reason == "dependency off is not loaded");
		CHECK(!Find(v, "needsvm")->load);
		CHECK(Index(v, "mine") < Index(v, "off"));   // loaded ones first
	}
	// Missing dependency, version too low, chained skip.
	{
		Manifest lib = M("lib");
		lib.version = "1.4";
		auto v = Resolve({ lib, M("ok", { { "lib", "1.2" } }), M("old", { { "lib", "2.0" } }),
			M("ghost", { { "nothere", "" } }), M("chain", { { "ghost", "" } }) }, false, "g.exe");
		CHECK(Find(v, "ok")->load);
		CHECK(!Find(v, "old")->load && Find(v, "old")->reason == "needs lib >= 2.0, found 1.4");
		CHECK(!Find(v, "ghost")->load && Find(v, "ghost")->reason == "missing dependency nothere");
		CHECK(!Find(v, "chain")->load && Find(v, "chain")->reason == "dependency ghost is not loaded");
	}
	// Duplicates, missing DLL, parse errors, cycles.
	{
		Manifest dup = M("same");
		dup.dir = "zz_dir";
		Manifest nodll = M("nodll");
		nodll.dllExists = false;
		Manifest bad = M("bad");
		bad.parseError = "missing id";
		auto v = Resolve({ M("same"), dup, nodll, bad, M("x", { { "y", "" } }), M("y", { { "x", "" } }),
			M("free") }, false, "g.exe");
		CHECK(Find(v, "same")->load && Find(v, "same")->manifest.dir == "same_dir");
		size_t dups = 0;
		for (const auto& d : v)
			if (d.manifest.id == "same" && !d.load && d.reason == "duplicate id, already provided by folder same_dir")
				dups++;
		CHECK(dups == 1);
		CHECK(!Find(v, "nodll")->load && Find(v, "nodll")->reason == "missing nodll.dll");
		CHECK(!Find(v, "bad")->load && Find(v, "bad")->reason == "invalid plugin.ini: missing id");
		CHECK(!Find(v, "x")->load && Find(v, "x")->reason == "dependency cycle");
		CHECK(!Find(v, "y")->load && Find(v, "y")->reason == "dependency cycle");
		CHECK(Find(v, "free")->load);
	}

	if (g_Failures)
	{
		std::printf("%d failure(s)\n", g_Failures);
		return 1;
	}
	std::printf("modhost manifest tests passed\n");
	return 0;
}
