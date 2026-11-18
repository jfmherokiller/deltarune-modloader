// LooseMods - the DELTARUNE loose-mod features, as an Aurie module loaded by the bootstrap proxy.
//
// Install: mods\aurie\LooseMods.dll (next to YYToolkit.dll). Settings: [loader] in
// mods\modloader.ini. Log: mods\modloader.log (shared with the proxy, lines tagged [loosemods]).
//
//   1. File overrides: hooks the runner's whole-file read, ReadEntireFile (RVA 0x92C40,
//      RE/09_datawin_loader.md). Any file the game reads from inside its install folder can be
//      replaced by a file with the same relative path in mods\<ModName>\ .
//   2. Loose GML / sprites / sounds: when the game reads a data.win and an enabled mod has
//      code\*.gml|*.patch|*.diff, sprites\*.png or sounds\*.ogg|*.wav for it, LooseModCompiler
//      (or UTMT's CLI) patches a copy in memory and the game gets the result. data.win on disk
//      is never modified. The result is cached in mods\cache\loosegml (GmlCache.hpp).
//   3. Redirects (mods\<ModName>\redirects.txt, "target = substitute"):
//      - object events (gml_Object_*) through YYToolkit's EVENT_OBJECT_CALL;
//      - scripts (gml_Script_*) through script_hooks.hpp (gm::Redirect), which also sees
//        script-to-script calls (they never pass through Code_Execute).
//   4. Streamed music (mods\<ModName>\mus\<file>.ogg): the game streams mus\*.ogg through the
//      audio_create_stream built-in, never ReadEntireFile. Overrides are validated and resolved
//      (later mod wins) once at start; a hook on that built-in swaps the path argument for the
//      winning mod's file. Nothing is copied or written. Streamed SOND entries (the .ogg next to
//      a data.win) are replaced by name through sounds\<name>.ogg instead (ImportLooseMod.csx).
//   5. Wine only: rewrites the chapter self-relaunch command line to an absolute data.win path
//      (Wine does not apply lpCurrentDirectory before resolving it).
//
// Items 1 and 2 hook fixed addresses of the current DELTARUNE build, each checked against a
// 16-byte prologue first; in any other game or build they stay off and the log says so.
// Hooks that must be in place before the game reads data.win are installed from
// ModulePreinitialize, which Aurie runs while the main thread is still parked at the entry
// point. Redirects and log_scripts start from ModuleInitialize, once YYToolkit is up; the
// script hooks themselves are set up on the game thread (first EVENT_OBJECT_CALL), because
// ModuleInitialize runs on Aurie's thread while the game is already running.
//
// log_scripts=1|2 logs the first run of each code entry (Code_Execute) and of each script
// (gm::Observe(""), VM runners only), tagged with the loose mod(s) that provide it; 2 = mod
// code only.
#include <YYToolkit/YYTK_Shared.hpp>
#include <script_hooks.hpp>
#include <append_log.hpp>
#include "GmlCache.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Aurie;
using namespace YYTK;

namespace
{
	// ------------------------------------------------------------------ layout (DELTARUNE build 0x685E7007)
	constexpr uintptr_t RVA_READ_ENTIRE_FILE = 0x92C40;
	constexpr uintptr_t RVA_CCODE_LIST_HEAD = 0x8C9EC8;   // CCode* list head (CCode::m_Next chain)
	constexpr uintptr_t RVA_GM_ALLOC = 0x38F430;          // allocator ReadEntireFile uses (sub_14038F430)

	const unsigned char SIG_READ_ENTIRE_FILE[16] = {
		0x40, 0x55, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8D, 0x6C, 0x24, 0x30, 0x48, 0x89, 0x5D, 0x20, 0x48 };
	const unsigned char SIG_GM_ALLOC[16] = {
		0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x70, 0x04, 0x00, 0x00, 0x0F, 0x29, 0xB4, 0x24, 0x60 };

	// ------------------------------------------------------------------ state
	constexpr int MAX_MODS = 64;

	struct Redirect
	{
		std::string target, substitute;
		CCode* targetCode = nullptr;
		CCode* subCode = nullptr;
		bool resolved = false;
	};

	AurieModule* g_Module = nullptr;
	WCHAR g_GameRoot[MAX_PATH];   // folder of the exe, no trailing slash
	size_t g_GameRootLen = 0;
	WCHAR g_ModsDir[MAX_PATH];
	WCHAR g_Ini[MAX_PATH];
	WCHAR g_ModNames[MAX_MODS][MAX_PATH];
	int g_ModCount = 0;
	std::vector<Redirect> g_EventRedirects;   // gml_Object_* and other Code_Execute code
	std::vector<Redirect> g_ScriptRedirects;  // gml_Script_*
	int g_LogFiles = 1;
	// log_scripts: 0 off; 1 = first run of every code entry and script; 2 = only those a loose
	// mod provides (see g_ModCode). Each line names the providing mod(s) when there are any.
	int g_LogScripts = 0;
	modloader::AppendLog g_Log;
	CCode** g_CCodeListHead = nullptr;
	bool g_Enabled = false;

	// ------------------------------------------------------------------ logging
	void Log(const char* fmt, ...)
	{
		va_list ap;
		va_start(ap, fmt);
		g_Log.WriteV(fmt, ap);
		va_end(ap);
	}

	void W2U(const WCHAR* w, char* out, int n)
	{
		if (!w || WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, nullptr, nullptr) <= 0) snprintf(out, n, "?");
	}

	// ------------------------------------------------------------------ mods scan
	void TrimA(char* s)
	{
		char* e = s + strlen(s);
		while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
		char* b = s; while (*b == ' ' || *b == '\t') b++;
		if (b != s) memmove(s, b, strlen(b) + 1);
	}

	// Load order: deterministic and locale-independent. Mods listed in mods\load_order.txt come
	// first, in file order; every other mod follows in alphabetical order (ordinal, case-
	// insensitive). Later mods win conflicts (file overrides, full .gml replacements) and their
	// patches are applied on top of earlier ones.
	int CmpName(const WCHAR* a, const WCHAR* b)
	{
		int r = CompareStringOrdinal(a, -1, b, -1, TRUE);
		if (r == CSTR_EQUAL) r = CompareStringOrdinal(a, -1, b, -1, FALSE);   // tie-break on case
		return r - CSTR_EQUAL;
	}
	int CmpModName(const void* a, const void* b) { return CmpName((const WCHAR*)a, (const WCHAR*)b); }

	// Reorders the (alphabetically sorted) g_ModNames by mods\load_order.txt: one mod folder
	// name per line, '#' comments, blank lines ignored, case-insensitive; names that aren't
	// installed/enabled are logged and skipped. Unlisted mods keep alphabetical order after
	// the listed ones. Returns how many mods were placed by the file.
	int ApplyLoadOrderFile()
	{
		static WCHAR sorted[MAX_MODS][MAX_PATH];
		static char used[MAX_MODS];
		WCHAR path[MAX_PATH];
		_snwprintf(path, MAX_PATH, L"%s\\load_order.txt", g_ModsDir);
		FILE* f = _wfopen(path, L"rb");
		if (!f) return 0;
		memset(used, 0, sizeof used);
		int n = 0;
		char line[MAX_PATH * 3];
		while (fgets(line, sizeof line, f)) {
			char* hash = strchr(line, '#'); if (hash) *hash = 0;
			TrimA(line);
			char* s = line;
			if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3; // BOM
			if (!s[0]) continue;
			WCHAR w[MAX_PATH];
			if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, MAX_PATH)) continue;
			int found = -1;
			for (int i = 0; i < g_ModCount; i++)
				if (!used[i] && CompareStringOrdinal(w, -1, g_ModNames[i], -1, TRUE) == CSTR_EQUAL) { found = i; break; }
			if (found < 0) { Log("load_order.txt: '%s' is not an enabled mod folder, ignored", s); continue; }
			used[found] = 1;
			wcscpy(sorted[n++], g_ModNames[found]);
		}
		fclose(f);
		int listed = n;
		for (int i = 0; i < g_ModCount; i++)
			if (!used[i]) wcscpy(sorted[n++], g_ModNames[i]);
		memcpy(g_ModNames, sorted, sizeof g_ModNames[0] * g_ModCount);
		return listed;
	}

	int LoadRedirects(int mod)
	{
		WCHAR path[MAX_PATH];
		_snwprintf(path, MAX_PATH, L"%s\\%s\\redirects.txt", g_ModsDir, g_ModNames[mod]);
		FILE* f = _wfopen(path, L"r");
		if (!f) return 0;
		int count = 0;
		char line[512];
		while (fgets(line, sizeof line, f)) {
			char* hash = strchr(line, '#'); if (hash) *hash = 0;
			char* eq = strchr(line, '='); if (!eq) continue;
			*eq = 0;
			char a[256], b[256];
			snprintf(a, sizeof a, "%s", line); snprintf(b, sizeof b, "%s", eq + 1);
			TrimA(a); TrimA(b);
			if (!a[0] || !b[0]) continue;
			Redirect r;
			r.target = a;
			r.substitute = b;
			(strncmp(a, "gml_Script_", 11) == 0 ? g_ScriptRedirects : g_EventRedirects).push_back(r);
			count++;
		}
		fclose(f);
		return count;
	}

	void ScanMods()
	{
		WCHAR pattern[MAX_PATH];
		_snwprintf(pattern, MAX_PATH, L"%s\\*", g_ModsDir);
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW(pattern, &fd);
		if (h == INVALID_HANDLE_VALUE) return;
		do {
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
			if (fd.cFileName[0] == L'.' || fd.cFileName[0] == L'_') continue;
			if (!_wcsicmp(fd.cFileName, L"native") || !_wcsicmp(fd.cFileName, L"aurie") ||
				!_wcsicmp(fd.cFileName, L"plugins") || !_wcsicmp(fd.cFileName, L"tools") ||
				!_wcsicmp(fd.cFileName, L"cache"))
				continue;   // Aurie and ModHost folders, compiler, loose-GML cache
			if (g_ModCount >= MAX_MODS) break;
			wcsncpy(g_ModNames[g_ModCount++], fd.cFileName, MAX_PATH - 1);
		} while (FindNextFileW(h, &fd));
		FindClose(h);
		qsort(g_ModNames, g_ModCount, sizeof g_ModNames[0], CmpModName);
		int listed = ApplyLoadOrderFile();
		Log("load order: %d mod(s), %d from load_order.txt, rest alphabetical; later mods win", g_ModCount, listed);
		for (int i = 0; i < g_ModCount; i++) {
			char n[MAX_PATH * 3]; W2U(g_ModNames[i], n, sizeof n);
			Log("mod #%d: %s (%d redirect(s))", i, n, LoadRedirects(i));
		}
	}

	// ------------------------------------------------------------------ file overrides
	using PFN_ReadEntireFile = void* (*)(const char*, unsigned int*);
	PFN_ReadEntireFile g_OrigReadEntireFile = nullptr;

	// Map a game-requested path to mods\<Mod>\<relative path>, last mod wins.
	int FindOverride(const char* path_utf8, char* out_utf8, int out_n)
	{
		WCHAR wpath[MAX_PATH * 2], full[MAX_PATH * 2], cand[MAX_PATH * 2];
		if (!path_utf8 || !MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, wpath, MAX_PATH * 2)) return 0;
		DWORD n = GetFullPathNameW(wpath, MAX_PATH * 2, full, nullptr);
		if (!n || n >= MAX_PATH * 2) return 0;
		for (WCHAR* p = full; *p; p++) if (*p == L'/') *p = L'\\';
		if (_wcsnicmp(full, g_GameRoot, g_GameRootLen) != 0 || full[g_GameRootLen] != L'\\') return 0;
		const WCHAR* rel = full + g_GameRootLen + 1;
		if (_wcsnicmp(rel, L"mods\\", 5) == 0) return 0;   // never override the mods folder itself
		for (int i = g_ModCount - 1; i >= 0; i--) {
			_snwprintf(cand, MAX_PATH * 2, L"%s\\%s\\%s", g_ModsDir, g_ModNames[i], rel);
			DWORD a = GetFileAttributesW(cand);
			if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
				return WideCharToMultiByte(CP_UTF8, 0, cand, -1, out_utf8, out_n, nullptr, nullptr) > 0;
		}
		return 0;
	}

	// ------------------------------------------------------------------ loose GML / sprites / sounds
	// mods\<Mod>\<chapter>\code\<gml_CodeName>.gml : decompiled GML (UTMT naming).
	// mods\<Mod>\<chapter>\sprites\<spr_name>_<frame>.png, sounds\<snd_name>.ogg|.wav :
	// replaced or new assets (see tools\ImportLooseMod.csx).
	// When the game reads <chapter>\data.win and any enabled mod has such files, the compiler
	// (LooseModCompiler, or UndertaleModCli + tools\ImportLooseMod.csx) runs on the vanilla
	// data.win, the patched bytes come back over a named pipe and are handed to the game in a
	// buffer from the runner's own allocator.
	using PFN_GmAlloc = void* (*)(size_t, __int64, __int64, char);
	PFN_GmAlloc g_GmAlloc = nullptr;
	WCHAR g_UtmtCli[MAX_PATH * 2], g_ImportScript[MAX_PATH * 2];
	DWORD g_GmlTimeoutMs = 300000;
	int g_GmlCache = 1;            // [loader] gml_cache, overridden by env DR_GML_CACHE
	std::wstring g_ModulePath;     // LooseMods.dll, part of the cache key

	void* GmAllocBytes(size_t n) { return g_GmAlloc(n, 0, 0, 0); }

	std::vector<std::wstring> SplitDirs(const WCHAR* dirs)
	{
		std::vector<std::wstring> out;
		for (const WCHAR* p = dirs; *p;) {
			const WCHAR* e = wcschr(p, L';');
			size_t n = e ? (size_t)(e - p) : wcslen(p);
			if (n) out.emplace_back(p, n);
			p += n + (e ? 1 : 0);
		}
		return out;
	}

	struct PipeRx { HANDLE pipe; unsigned char* buf; unsigned long long len, got; int ok; };
	DWORD WINAPI PipeReader(LPVOID p)
	{
		PipeRx* rx = (PipeRx*)p;
		if (!ConnectNamedPipe(rx->pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) return 0;
		DWORD n;
		unsigned long long len = 0;
		if (!ReadFile(rx->pipe, &len, 8, &n, nullptr) || n != 8 || len == 0 || len > 0xFFFFFFF0ull) return 0;
		rx->buf = (unsigned char*)g_GmAlloc((size_t)len + 1, 0, 0, 0);
		if (!rx->buf) return 0;
		rx->len = len;
		while (rx->got < len) {
			DWORD want = (DWORD)((len - rx->got) > (1u << 20) ? (1u << 20) : (len - rx->got));
			if (!ReadFile(rx->pipe, rx->buf + rx->got, want, &n, nullptr) || n == 0) return 0;
			rx->got += n;
		}
		rx->buf[len] = 0;   // ReadEntireFile null-terminates too
		rx->ok = 1;
		return 0;
	}

	int DirHas(const WCHAR* root, const WCHAR* sub, const WCHAR* mask)
	{
		WCHAR pat[MAX_PATH * 2];
		WIN32_FIND_DATAW fd;
		_snwprintf(pat, MAX_PATH * 2, L"%s\\%s\\%s", root, sub, mask);
		HANDLE h = FindFirstFileW(pat, &fd);
		if (h == INVALID_HANDLE_VALUE) return 0;
		FindClose(h);
		return 1;
	}

	int HasLooseAssets(const WCHAR* dir)
	{
		return DirHas(dir, L"code", L"*.gml") || DirHas(dir, L"code", L"*.patch") || DirHas(dir, L"code", L"*.diff") ||
			DirHas(dir, L"sprites", L"*.png") || DirHas(dir, L"sounds", L"*.ogg") || DirHas(dir, L"sounds", L"*.wav");
	}

	// Collects, per enabled mod in load order: mods\<Mod>\all_chapters (only for chapterN_windows
	// data.win files) and then mods\<Mod>\<rel_dir>, when they contain loose assets.
	int CollectGmlDirs(const WCHAR* rel_dir, WCHAR* out, size_t out_n)
	{
		int count = 0;
		int is_chapter = _wcsnicmp(rel_dir, L"chapter", 7) == 0;
		out[0] = 0;
		for (int i = 0; i < g_ModCount; i++) {
			for (int pass = 0; pass < 2; pass++) {
				WCHAR dir[MAX_PATH * 2];
				if (pass == 0) {
					if (!is_chapter) continue;
					_snwprintf(dir, MAX_PATH * 2, L"%s\\%s\\all_chapters", g_ModsDir, g_ModNames[i]);
				} else {
					_snwprintf(dir, MAX_PATH * 2, L"%s\\%s\\%s", g_ModsDir, g_ModNames[i], rel_dir);
				}
				if (!HasLooseAssets(dir)) continue;
				if (wcslen(out) + wcslen(dir) + 2 >= out_n) break;
				if (out[0]) wcscat(out, L";");
				wcscat(out, dir);
				count++;
			}
		}
		return count;
	}

	// ------------------------------------------------------------------ log_scripts attribution
	// Code entry / script name -> providing mod(s), from the loose-GML folders used for this
	// process's data.win: file stems in code\ (gml_GlobalScript_X also gives gml_Script_X) and
	// every "function name(" line in a .gml file (a global script can declare several).
	// Built only when log_scripts is on, before the game runs any code; read on the game thread.
	std::unordered_map<std::string, std::string> g_ModCode;

	void AddModCode(const std::string& name, const std::string& mod)
	{
		std::string& v = g_ModCode[name];
		if (v.empty()) v = mod;
		else if (("+" + v + "+").find("+" + mod + "+") == std::string::npos) v += "+" + mod;   // several mods
	}

	void ScanGmlFunctions(const WCHAR* file, const std::string& mod)
	{
		FILE* f = _wfopen(file, L"rb");
		if (!f) return;
		char line[1024];
		while (fgets(line, sizeof line, f)) {
			const char* p = line;
			while (*p == ' ' || *p == '	') p++;
			if (strncmp(p, "function", 8) != 0 || (p[8] != ' ' && p[8] != '	')) continue;
			p += 9;
			while (*p == ' ' || *p == '	') p++;
			const char* e = p;
			while ((*e >= 'a' && *e <= 'z') || (*e >= 'A' && *e <= 'Z') || (*e >= '0' && *e <= '9') || *e == '_') e++;
			if (e > p) AddModCode("gml_Script_" + std::string(p, e - p), mod);
		}
		fclose(f);
	}

	void BuildModCodeMap(const WCHAR* dirs)
	{
		size_t modsLen = wcslen(g_ModsDir);
		for (const std::wstring& dir : SplitDirs(dirs)) {
			if (dir.size() <= modsLen + 1) continue;
			std::wstring modW = dir.substr(modsLen + 1);   // <Mod>\<sub>
			modW = modW.substr(0, modW.find(L'\\'));
			char mod[MAX_PATH * 3]; W2U(modW.c_str(), mod, sizeof mod);
			WCHAR pat[MAX_PATH * 2];
			_snwprintf(pat, MAX_PATH * 2, L"%s\\code\\*", dir.c_str());
			WIN32_FIND_DATAW fd;
			HANDLE h = FindFirstFileW(pat, &fd);
			if (h == INVALID_HANDLE_VALUE) continue;
			do {
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
				const WCHAR* ext = wcsrchr(fd.cFileName, L'.');
				if (!ext || (_wcsicmp(ext, L".gml") && _wcsicmp(ext, L".patch") && _wcsicmp(ext, L".diff"))) continue;
				char stem[MAX_PATH * 3]; W2U(fd.cFileName, stem, sizeof stem);
				std::string name(stem, strlen(stem) - wcslen(ext));
				AddModCode(name, mod);
				if (name.rfind("gml_GlobalScript_", 0) == 0)
					AddModCode("gml_Script_" + name.substr(17), mod);
				if (!_wcsicmp(ext, L".gml")) {
					WCHAR file[MAX_PATH * 2];
					_snwprintf(file, MAX_PATH * 2, L"%s\\code\\%s", dir.c_str(), fd.cFileName);
					ScanGmlFunctions(file, mod);
				}
			} while (FindNextFileW(h, &fd));
			FindClose(h);
		}
		Log("log_scripts: %zu code entry/script name(s) attributed to loose mods", g_ModCode.size());
	}

	void* CompileLooseGml(const char* path_utf8, unsigned int* out_size)
	{
		static WCHAR wpath[MAX_PATH * 2], full[MAX_PATH * 2], rel_dir[MAX_PATH * 2], dirs[8192];
		if (!g_GmAlloc || !path_utf8 || !MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, wpath, MAX_PATH * 2)) return nullptr;
		DWORD n = GetFullPathNameW(wpath, MAX_PATH * 2, full, nullptr);
		if (!n || n >= MAX_PATH * 2) return nullptr;
		for (WCHAR* p = full; *p; p++) if (*p == L'/') *p = L'\\';
		const WCHAR* base = wcsrchr(full, L'\\');
		if (!base || _wcsicmp(base + 1, L"data.win") != 0) return nullptr;
		if (_wcsnicmp(full, g_GameRoot, g_GameRootLen) != 0 || full[g_GameRootLen] != L'\\') return nullptr;
		size_t rel_len = (size_t)(base - (full + g_GameRootLen + 1));
		if (base <= full + g_GameRootLen) rel_len = 0;
		if (rel_len == 0) wcscpy(rel_dir, L".");   // launcher data.win: mods\<Mod>\code
		else { wcsncpy(rel_dir, full + g_GameRootLen + 1, rel_len); rel_dir[rel_len] = 0; }

		int mods = CollectGmlDirs(rel_dir, dirs, sizeof dirs / sizeof dirs[0]);
		if (!mods) return nullptr;
		if (g_LogScripts) { g_ModCode.clear(); BuildModCodeMap(dirs); }
		static char dirs_u[16384];
		W2U(dirs, dirs_u, sizeof dirs_u);
		if (GetFileAttributesW(g_UtmtCli) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW(g_ImportScript) == INVALID_FILE_ATTRIBUTES) {
			char c[MAX_PATH * 6];
			W2U(g_UtmtCli, c, sizeof c);
			Log("loose GML: %d mod folder(s) for this data.win but compiler missing (%s) - running vanilla", mods, c);
			return nullptr;
		}
		// Compile cache (GmlCache.hpp): an unchanged set of inputs gets the stored result.
		gmlcache::Inputs ci;
		std::string key;
		char rel_u[MAX_PATH * 6];
		W2U(rel_dir, rel_u, sizeof rel_u);
		if (g_GmlCache) {
			DWORD tc = GetTickCount();
			ci = { g_GameRoot, g_ModsDir, g_ModulePath, g_UtmtCli, g_ImportScript, full, rel_dir, SplitDirs(dirs) };
			std::string manifest, reason;
			gmlcache::ReadSmallFile(gmlcache::CacheDir(ci) + L"\\manifest.txt", manifest);
			key = gmlcache::BuildKey(ci, manifest);
			if (void* hit = gmlcache::Load(ci, key, manifest, GmAllocBytes, out_size, reason)) {
				Log("loose GML: cache HIT for %s (%d mod folder(s)): %u bytes served from mods\\cache in %lu ms, compiler skipped",
					rel_u, mods, out_size ? *out_size : 0, GetTickCount() - tc);
				return hit;
			}
			Log("loose GML: cache MISS for %s (%s; checked in %lu ms)", rel_u, reason.c_str(), GetTickCount() - tc);
		} else {
			Log("loose GML: cache off (gml_cache=0 or DR_GML_CACHE=0)");
		}
		{
			char c[MAX_PATH * 6];
			const WCHAR* name = wcsrchr(g_UtmtCli, L'\\');
			W2U(name ? name + 1 : g_UtmtCli, c, sizeof c);
			Log("loose GML: compiling %s into %s (%s)", dirs_u, path_utf8, c);
		}

		WCHAR pipe_name[128];
		_snwprintf(pipe_name, 128, L"\\\\.\\pipe\\dr_modloader_%lu_%lu", GetCurrentProcessId(), GetTickCount());
		HANDLE pipe = CreateNamedPipeW(pipe_name, PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 0, 1 << 20, 0, nullptr);
		if (pipe == INVALID_HANDLE_VALUE) { Log("loose GML: CreateNamedPipe failed %lu", GetLastError()); return nullptr; }
		PipeRx rx = { pipe };
		HANDLE reader = CreateThread(nullptr, 0, PipeReader, &rx, 0, nullptr);

		static WCHAR cmd[MAX_PATH * 8];
		WCHAR logpath[MAX_PATH * 2];
		_snwprintf(cmd, MAX_PATH * 8, L"\"%s\" load \"%s\" -s \"%s\"", g_UtmtCli, full, g_ImportScript);
		_snwprintf(logpath, MAX_PATH * 2, L"%s\\modloader.gml.log", g_ModsDir);
		SECURITY_ATTRIBUTES sa = { sizeof sa, nullptr, TRUE };
		HANDLE logf = CreateFileW(logpath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, 0, nullptr);
		HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
		STARTUPINFOW si = { sizeof si };
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = nul; si.hStdOutput = logf; si.hStdError = logf;
		PROCESS_INFORMATION pi = {};
		SetEnvironmentVariableW(L"DR_MOD_DIRS", dirs);
		SetEnvironmentVariableW(L"DR_OUT_PIPE", pipe_name);
		DWORD t0 = GetTickCount();
		BOOL started = CreateProcessW(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
		SetEnvironmentVariableW(L"DR_MOD_DIRS", nullptr);
		SetEnvironmentVariableW(L"DR_OUT_PIPE", nullptr);
		if (logf != INVALID_HANDLE_VALUE) CloseHandle(logf);
		if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
		DWORD exit_code = (DWORD)-1;
		if (started) {
			if (WaitForSingleObject(pi.hProcess, g_GmlTimeoutMs) == WAIT_TIMEOUT) {
				Log("loose GML: compiler timed out after %lu ms, killing it", g_GmlTimeoutMs);
				TerminateProcess(pi.hProcess, 1);
			}
			GetExitCodeProcess(pi.hProcess, &exit_code);
			CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
		} else {
			Log("loose GML: failed to start compiler (%lu)", GetLastError());
		}
		// unblock the reader if the child never connected
		HANDLE dummy = CreateFileW(pipe_name, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (dummy != INVALID_HANDLE_VALUE) CloseHandle(dummy);
		WaitForSingleObject(reader, 10000);
		CloseHandle(reader);
		CloseHandle(pipe);

		if (exit_code != 0 || !rx.ok) {
			Log("loose GML: compile FAILED (exit %ld, received %llu/%llu bytes) - see mods\\modloader.gml.log; running vanilla",
				(long)exit_code, rx.got, rx.len);
			return nullptr;   // rx.buf came from the runner's allocator; leaking it once on failure is harmless
		}
		Log("loose GML: compiled OK in %lu ms, %llu bytes served from memory", GetTickCount() - t0, rx.len);
		if (g_GmlCache && !gmlcache::StoreAsync(ci, key, rx.buf, rx.len, Log))
			Log("loose GML: cache write FAILED for %s (%lu); the next launch compiles again", rel_u, GetLastError());
		if (out_size) *out_size = (unsigned int)rx.len;
		return rx.buf;
	}

	void* HookedReadEntireFile(const char* path, unsigned int* out_size)
	{
		char over[MAX_PATH * 6];
		if (g_ModCount > 0) {
			void* compiled = CompileLooseGml(path, out_size);
			if (compiled) return compiled;
		}
		int hit = g_ModCount > 0 && FindOverride(path, over, sizeof over);
		void* buf = g_OrigReadEntireFile(hit ? over : path, out_size);
		if (hit || g_LogFiles)
			Log("%s \"%s\"%s%s%s -> %u bytes", hit ? "OVERRIDE" : "read", path ? path : "(null)",
				hit ? " => \"" : "", hit ? over : "", hit ? "\"" : "", (buf && out_size) ? *out_size : 0);
		return buf;
	}

	// ------------------------------------------------------------------ streamed audio overrides
	// DELTARUNE streams its music with audio_create_stream("mus/<file>.ogg") (snd_init; the
	// launcher passes working_directory + "../mus/<file>.ogg"). Those files have no SOND entry and
	// the audio engine opens them itself, so they can't be served through ReadEntireFile.
	// mods\<Mod>\mus\<file>.ogg overrides <game>\mus\<file>.ogg: validated once at start (the target
	// must exist in the game, the file must be readable Ogg data), the last mod in the load order
	// wins, and the hook below swaps the path argument. The game folder is never written to.
	struct StreamOverride { std::wstring file; std::string mod; };
	std::unordered_map<std::wstring, StreamOverride> g_StreamOverrides;   // key: lower-case "mus\<file>"
	TRoutine g_OrigAudioCreateStream = nullptr;
	bool g_StreamHookTried = false;

	std::wstring Lower(std::wstring s)
	{
		if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
		return s;
	}

	// "" when readable Ogg data, else why not.
	const char* CheckOgg(const WCHAR* path)
	{
		HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE) return "can't be opened";
		char magic[4] = {};
		DWORD n = 0;
		BOOL ok = ReadFile(h, magic, 4, &n, nullptr);
		CloseHandle(h);
		if (!ok) return "can't be read";
		if (n != 4 || memcmp(magic, "OggS", 4) != 0) return "is not Ogg Vorbis data";
		return "";
	}

	// Warns about .ogg files a mod put next to a data.win (mods\<Mod>\<chapter>\x.ogg): the runner
	// opens the streamed SOND files there itself, so a same-path file override does nothing.
	void WarnMisplacedOgg(int mod, const WCHAR* sub, const char* modU)
	{
		WCHAR pat[MAX_PATH * 2];
		_snwprintf(pat, MAX_PATH * 2, L"%s\\%s\\%s%s*.ogg", g_ModsDir, g_ModNames[mod], sub, sub[0] ? L"\\" : L"");
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW(pat, &fd);
		if (h == INVALID_HANDLE_VALUE) return;
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			char f[MAX_PATH * 3], d[MAX_PATH * 3]; W2U(fd.cFileName, f, sizeof f); W2U(sub, d, sizeof d);
			const char* dot = strrchr(f, '.'); if (dot) f[dot - f] = 0;
			Log("WARN stream override: %s\\%s%s%s.ogg (mod %s) is ignored: a streamed sound next to data.win is "
				"replaced by name with %s%ssounds\\%s.ogg", modU, d, d[0] ? "\\" : "", f, modU, d, d[0] ? "\\" : "", f);
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}

	void ScanStreamOverrides()
	{
		std::unordered_map<std::wstring, std::vector<std::string>> providers;
		int bad = 0;
		for (int i = 0; i < g_ModCount; i++) {
			char modU[MAX_PATH * 3]; W2U(g_ModNames[i], modU, sizeof modU);
			// Misplaced files: mod root (launcher), all_chapters, chapterN_windows.
			WarnMisplacedOgg(i, L"", modU);
			{
				WCHAR pat[MAX_PATH * 2];
				_snwprintf(pat, MAX_PATH * 2, L"%s\\%s\\*", g_ModsDir, g_ModNames[i]);
				WIN32_FIND_DATAW fd;
				HANDLE h = FindFirstFileW(pat, &fd);
				if (h != INVALID_HANDLE_VALUE) {
					do {
						if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
							(_wcsnicmp(fd.cFileName, L"chapter", 7) == 0 || !_wcsicmp(fd.cFileName, L"all_chapters")))
							WarnMisplacedOgg(i, fd.cFileName, modU);
					} while (FindNextFileW(h, &fd));
					FindClose(h);
				}
			}
			WCHAR pat[MAX_PATH * 2];
			_snwprintf(pat, MAX_PATH * 2, L"%s\\%s\\mus\\*", g_ModsDir, g_ModNames[i]);
			WIN32_FIND_DATAW fd;
			HANDLE h = FindFirstFileW(pat, &fd);
			if (h == INVALID_HANDLE_VALUE) continue;
			do {
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
				char f[MAX_PATH * 3]; W2U(fd.cFileName, f, sizeof f);
				const WCHAR* ext = wcsrchr(fd.cFileName, L'.');
				if (!ext || _wcsicmp(ext, L".ogg")) {
					Log("WARN stream override: %s\\mus\\%s (mod %s) is not an .ogg file; skipped", modU, f, modU);
					bad++;
					continue;
				}
				WCHAR cand[MAX_PATH * 2], vanilla[MAX_PATH * 2];
				_snwprintf(cand, MAX_PATH * 2, L"%s\\%s\\mus\\%s", g_ModsDir, g_ModNames[i], fd.cFileName);
				_snwprintf(vanilla, MAX_PATH * 2, L"%s\\mus\\%s", g_GameRoot, fd.cFileName);
				DWORD va = GetFileAttributesW(vanilla);
				if (va == INVALID_FILE_ATTRIBUTES || (va & FILE_ATTRIBUTE_DIRECTORY)) {
					Log("WARN stream override: %s\\mus\\%s (mod %s) matches no streamed track (the game has no mus\\%s); skipped",
						modU, f, modU, f);
					bad++;
					continue;
				}
				const char* why = CheckOgg(cand);
				if (why[0]) {
					Log("WARN stream override: %s\\mus\\%s (mod %s) %s; skipped", modU, f, modU, why);
					bad++;
					continue;
				}
				std::wstring key = Lower(std::wstring(L"mus\\") + fd.cFileName);
				g_StreamOverrides[key] = { cand, modU };   // later mod wins
				providers[key].push_back(modU);
			} while (FindNextFileW(h, &fd));
			FindClose(h);
		}
		for (auto& [key, mods] : providers) {
			const StreamOverride& o = g_StreamOverrides[key];
			char k[MAX_PATH * 3], file[MAX_PATH * 6]; W2U(key.c_str(), k, sizeof k); W2U(o.file.c_str(), file, sizeof file);
			if (mods.size() > 1) {
				std::string list;
				for (auto& m : mods) list += (list.empty() ? "" : ", ") + m;
				Log("stream override conflict: %s provided by %s; %s wins (load order)", k, list.c_str(), o.mod.c_str());
			}
			Log("stream override: %s <- %s (mod %s)", k, file, o.mod.c_str());
		}
		if (!g_StreamOverrides.empty() || bad)
			Log("stream overrides: %zu track(s) replaced, %d file(s) skipped", g_StreamOverrides.size(), bad);
	}

	// Maps a path handed to audio_create_stream to an override, or nullptr. Relative paths are
	// resolved like the runner does (current directory = game folder).
	const StreamOverride* FindStreamOverride(const char* path_utf8)
	{
		WCHAR wpath[MAX_PATH * 2], full[MAX_PATH * 2];
		if (!path_utf8 || !MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, wpath, MAX_PATH * 2)) return nullptr;
		DWORD n = GetFullPathNameW(wpath, MAX_PATH * 2, full, nullptr);
		if (!n || n >= MAX_PATH * 2) return nullptr;
		for (WCHAR* p = full; *p; p++) if (*p == L'/') *p = L'\\';
		if (_wcsnicmp(full, g_GameRoot, g_GameRootLen) != 0 || full[g_GameRootLen] != L'\\') return nullptr;
		auto it = g_StreamOverrides.find(Lower(full + g_GameRootLen + 1));
		return it == g_StreamOverrides.end() ? nullptr : &it->second;
	}

	void HookedAudioCreateStream(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
	{
		const StreamOverride* o = nullptr;
		std::string orig;
		if (argc >= 1 && args && args[0].m_Kind == VALUE_STRING) {
			orig = args[0].ToString();
			o = FindStreamOverride(orig.c_str());
		}
		if (!o) {
			g_OrigAudioCreateStream(result, self, other, argc, args);
			if (g_LogFiles) Log("stream \"%s\" -> %s", orig.c_str(), result.ToString().c_str());
			return;
		}
		char file[MAX_PATH * 6]; W2U(o->file.c_str(), file, sizeof file);
		RValue saved = args[0];
		args[0] = RValue(std::string_view(file));
		g_OrigAudioCreateStream(result, self, other, argc, args);
		args[0] = saved;
		Log("STREAM OVERRIDE \"%s\" => \"%s\" (mod %s) -> %s", orig.c_str(), file, o->mod.c_str(),
			result.ToString().c_str());
	}

	// Game thread, first code event: before any GML has called audio_create_stream.
	void InstallStreamHook()
	{
		g_StreamHookTried = true;
		PVOID target = nullptr, trampoline = nullptr;
		AurieStatus s = GetInterface()->GetNamedRoutinePointer("audio_create_stream", &target);
		if (AurieSuccess(s))
			s = MmCreateHook(g_Module, "audio_create_stream", target, reinterpret_cast<PVOID>(&HookedAudioCreateStream), &trampoline);
		g_OrigAudioCreateStream = reinterpret_cast<TRoutine>(trampoline);
		Log("hook audio_create_stream @ %p (%zu stream override(s)): %s", target, g_StreamOverrides.size(),
			AurieStatusToString(s));
	}

	// ------------------------------------------------------------------ redirects + script log (YYToolkit)
	CCode* FindCCodeByName(const char* name)
	{
		if (!g_CCodeListHead) return nullptr;
		for (CCode* node = *g_CCodeListHead; node; node = node->m_Next)
			if (node->m_Name && strcmp(node->m_Name, name) == 0) return node;
		return nullptr;
	}

	// log_scripts: first run of each code entry (Code_Execute: object events, global script
	// bodies) and of each script (gm::Observe(""): script calls, VM runners only). The seen sets
	// are game thread only; after its first run a script costs one hash lookup per call.
	constexpr size_t MAX_LOGGED = 16384;
	std::unordered_set<CCode*> g_Seen;
	std::unordered_set<CScript*> g_SeenScripts;

	void LogFirstRun(const char* kind, const char* name)
	{
		auto it = name ? g_ModCode.find(name) : g_ModCode.end();
		if (it != g_ModCode.end())
			Log("%s first run: %s [mod: %s]", kind, name, it->second.c_str());
		else if (g_LogScripts == 1)
			Log("%s first run: %s", kind, name ? name : "(null)");
	}

	void ScriptCallObserver(const gm::CallInfo& call, void*)
	{
		if (g_SeenScripts.size() < MAX_LOGGED && g_SeenScripts.insert(call.script).second)
			LogFirstRun("script", call.name);
	}

	// Script redirects and the log_scripts observer go through script_hooks.hpp, which must be
	// set up on the game thread.
	void InstallScriptHooks()
	{
		AurieStatus s = gm::Init(g_Module, GetInterface());
		Log("script hooks: %s", AurieStatusToString(s));
		for (const Redirect& r : g_ScriptRedirects)
			Log("script redirect %s -> %s: %s", r.target.c_str(), r.substitute.c_str(),
				AurieSuccess(s) ? AurieStatusToString(gm::Redirect(r.target, r.substitute)) : "skipped");
		if (g_LogScripts) {
			AurieStatus o = AurieSuccess(s) ? gm::Observe("", ScriptCallObserver) : s;
			Log("log_scripts: script calls %s", AurieSuccess(o) ? "observed through gm::Observe" :
				o == AURIE_NOT_IMPLEMENTED ? "not observable on YYC; only Code_Execute code is logged" :
				AurieStatusToString(o));
		}
	}

	bool g_ScriptHooksDone = false;

	void CodeCallback(FWCodeEvent& event)
	{
		if (!g_ScriptHooksDone) {
			g_ScriptHooksDone = true;
			if (!g_ScriptRedirects.empty() || g_LogScripts)
				InstallScriptHooks();
			if (!g_StreamOverrides.empty() && !g_StreamHookTried)
				InstallStreamHook();
		}
		auto& [self, other, code, result, argc] = event.Arguments();
		if (!code) return;
		if (g_LogScripts && g_Seen.size() < MAX_LOGGED && g_Seen.insert(code).second)
			LogFirstRun("code", code->m_Name);
		for (Redirect& r : g_EventRedirects) {
			if (r.targetCode ? code != r.targetCode : (!code->m_Name || r.target != code->m_Name))
				continue;
			r.targetCode = code;
			if (!r.resolved) {
				r.resolved = true;
				r.subCode = FindCCodeByName(r.substitute.c_str());
				Log(r.subCode ? "redirect active: %s -> %s" : "redirect FAILED (substitute not found): %s -> %s",
					r.target.c_str(), r.substitute.c_str());
			}
			if (r.subCode && !event.CalledOriginal()) {
				CCode* sub = r.subCode;
				event.Call(self, other, sub, result, argc);
			}
			return;
		}
	}

	// ------------------------------------------------------------------ Wine relaunch fix
	using PFN_CreateProcessW = BOOL(WINAPI*)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
		BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
	PFN_CreateProcessW g_OrigCreateProcessW = nullptr;

	BOOL WINAPI HookedCreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
		LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env, LPCWSTR cwd,
		LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi)
	{
		static WCHAR rewritten[2048];
		WCHAR dir[1024];
		LPWSTR use = cmd;
		if (cmd && cwd && cwd[0]) {
			DWORD n = GetFullPathNameW(cwd, 1024, dir, nullptr);
			if (n > 0 && n < 1024 && dir[n - 1] == L'\\') dir[--n] = 0;
			LPWSTR m = (n > 0 && n < 1024) ? wcsstr(cmd, L"data.win") : nullptr;
			if (m && m > cmd && m[-1] == L' ') {
				size_t pre = (size_t)(m - cmd);
				if (pre + n + 1 + wcslen(m) + 1 < 2048) {
					wcsncpy(rewritten, cmd, pre); rewritten[pre] = 0;
					wcscat(rewritten, dir); wcscat(rewritten, L"\\"); wcscat(rewritten, m);
					use = rewritten;
				}
			}
		}
		char a[2048]; W2U(use, a, sizeof a);
		Log("CreateProcessW cmdline=\"%s\" (wine fix %s)", a, use == cmd ? "not needed" : "applied");
		return g_OrigCreateProcessW(app, use, pa, ta, inherit, flags, env, cwd, si, pi);
	}

	// ------------------------------------------------------------------ setup
	bool CheckSig(const char* what, const void* addr, const unsigned char* sig)
	{
		if (memcmp(addr, sig, 16) == 0) return true;
		Log("%s at %p does not match this exe (not DELTARUNE, or another DELTARUNE build): "
			"file overrides and loose GML are off", what, addr);
		return false;
	}

	template <class T>
	bool Hook(const char* what, void* target, T detour, T* original)
	{
		PVOID trampoline = nullptr;
		AurieStatus s = MmCreateHook(g_Module, what, target, reinterpret_cast<PVOID>(detour), &trampoline);
		Log("hook %s @ %p: %s", what, target, AurieStatusToString(s));
		if (!AurieSuccess(s)) return false;
		*original = reinterpret_cast<T>(trampoline);
		return true;
	}

	std::wstring IniString(const wchar_t* key, const wchar_t* fallback = L"")
	{
		WCHAR v[MAX_PATH * 2];
		GetPrivateProfileStringW(L"loader", key, fallback, v, MAX_PATH * 2, g_Ini);
		return v;
	}
}

// Runs while the main thread is parked at the entry point: nothing has read data.win yet.
EXPORTED AurieStatus ModulePreinitialize(IN AurieModule* Module, IN const fs::path& ModulePath)
{
	g_Module = Module;
	GetModuleFileNameW(nullptr, g_GameRoot, MAX_PATH);
	WCHAR* slash = wcsrchr(g_GameRoot, L'\\'); if (slash) *slash = 0;
	g_GameRootLen = wcslen(g_GameRoot);
	_snwprintf(g_ModsDir, MAX_PATH, L"%s\\mods", g_GameRoot);
	_snwprintf(g_Ini, MAX_PATH, L"%s\\modloader.ini", g_ModsDir);

	WCHAR logp[MAX_PATH];
	_snwprintf(logp, MAX_PATH, L"%s\\modloader.log", g_ModsDir);
	g_Log.Open(logp, "[loosemods] ");
	g_LogFiles = GetPrivateProfileIntW(L"loader", L"log_files", 1, g_Ini);
	g_LogScripts = GetPrivateProfileIntW(L"loader", L"log_scripts", 0, g_Ini);
	g_GmlTimeoutMs = GetPrivateProfileIntW(L"loader", L"gml_timeout_ms", 300000, g_Ini);
	g_GmlCache = GetPrivateProfileIntW(L"loader", L"gml_cache", 1, g_Ini);
	WCHAR env[16];
	if (GetEnvironmentVariableW(L"DR_GML_CACHE", env, 16) && env[0]) g_GmlCache = _wtoi(env);
	g_ModulePath = ModulePath.wstring();

	// Loose GML compiler: utmt_cli in modloader.ini, else mods\tools\compiler\LooseModCompiler.exe
	// (ImportLooseMod compiled in; same command line and pipe protocol), else UTMT's CLI with the
	// ImportLooseMod.csx script.
	std::wstring cli = IniString(L"utmt_cli");
	if (!cli.empty()) wcsncpy(g_UtmtCli, cli.c_str(), MAX_PATH * 2 - 1);
	else {
		_snwprintf(g_UtmtCli, MAX_PATH * 2, L"%s\\tools\\compiler\\LooseModCompiler.exe", g_ModsDir);
		if (GetFileAttributesW(g_UtmtCli) == INVALID_FILE_ATTRIBUTES)
			_snwprintf(g_UtmtCli, MAX_PATH * 2, L"%s\\tools\\utmt\\UndertaleModCli.exe", g_ModsDir);
	}
	_snwprintf(g_ImportScript, MAX_PATH * 2, L"%s\\tools\\ImportLooseMod.csx", g_ModsDir);
	if (GetFileAttributesW(g_ImportScript) == INVALID_FILE_ATTRIBUTES)   // older installs
		_snwprintf(g_ImportScript, MAX_PATH * 2, L"%s\\tools\\ImportGMLFolder.csx", g_ModsDir);

	ScanMods();
	ScanStreamOverrides();
	g_Enabled = true;

	char* base = (char*)GetModuleHandleW(nullptr);
	void* ref = base + RVA_READ_ENTIRE_FILE;
	if (CheckSig("ReadEntireFile", ref, SIG_READ_ENTIRE_FILE)) {
		g_CCodeListHead = (CCode**)(base + RVA_CCODE_LIST_HEAD);
		if (CheckSig("GM allocator", base + RVA_GM_ALLOC, SIG_GM_ALLOC))
			g_GmAlloc = (PFN_GmAlloc)(base + RVA_GM_ALLOC);
		Hook("ReadEntireFile", ref, &HookedReadEntireFile, &g_OrigReadEntireFile);
	}

	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if (ntdll && GetProcAddress(ntdll, "wine_get_version")) {
		void* cpw = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateProcessW");
		Log("running under Wine - enabling chapter relaunch fix");
		Hook("CreateProcessW", cpw, &HookedCreateProcessW, &g_OrigCreateProcessW);
	}
	return AURIE_SUCCESS;
}

// YYToolkit is fully initialized here.
EXPORTED AurieStatus ModuleInitialize(IN AurieModule* Module, IN const fs::path& ModulePath)
{
	UNREFERENCED_PARAMETER(ModulePath);
	if (!g_Enabled)
		return AURIE_SUCCESS;
	YYTKInterface* yytk = GetInterface();
	if (!yytk) {
		Log("YYToolkit not loaded: redirects.txt, log_scripts and stream overrides are off");
		return AURIE_SUCCESS;
	}
	if (!g_EventRedirects.empty() || !g_ScriptRedirects.empty() || g_LogScripts || !g_StreamOverrides.empty()) {
		if (!g_EventRedirects.empty() && !g_CCodeListHead)
			Log("event redirects need the DELTARUNE code list; they are off in this exe");
		AurieStatus s = yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
		Log("EVENT_OBJECT_CALL callback (redirects, log_scripts, stream overrides): %s", AurieStatusToString(s));
	}
	return AURIE_SUCCESS;
}
