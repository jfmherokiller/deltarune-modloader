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
//      is never modified.
//   3. Redirects (mods\<ModName>\redirects.txt, "target = substitute"):
//      - object events (gml_Object_*) through YYToolkit's EVENT_OBJECT_CALL;
//      - scripts (gml_Script_*) through script_hooks.hpp (gm::Redirect), which also sees
//        script-to-script calls (they never pass through Code_Execute).
//   4. Wine only: rewrites the chapter self-relaunch command line to an absolute data.win path
//      (Wine does not apply lpCurrentDirectory before resolving it).
//
// Items 1 and 2 hook fixed addresses of the current DELTARUNE build, each checked against a
// 16-byte prologue first; in any other game or build they stay off and the log says so.
// Hooks that must be in place before the game reads data.win are installed from
// ModulePreinitialize, which Aurie runs while the main thread is still parked at the entry
// point. Redirects and log_scripts start from ModuleInitialize, once YYToolkit is up; the
// script hooks themselves are set up on the game thread (first EVENT_OBJECT_CALL), because
// ModuleInitialize runs on Aurie's thread while the game is already running.
#include <YYToolkit/YYTK_Shared.hpp>
#include <script_hooks.hpp>
#include <append_log.hpp>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
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
	int g_LogFiles = 1, g_LogScripts = 0;
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
				!_wcsicmp(fd.cFileName, L"plugins") || !_wcsicmp(fd.cFileName, L"tools"))
				continue;   // Aurie and ModHost folders, compiler
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
		static char dirs_u[16384];
		W2U(dirs, dirs_u, sizeof dirs_u);
		if (GetFileAttributesW(g_UtmtCli) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW(g_ImportScript) == INVALID_FILE_ATTRIBUTES) {
			char c[MAX_PATH * 6];
			W2U(g_UtmtCli, c, sizeof c);
			Log("loose GML: %d mod folder(s) for this data.win but compiler missing (%s) - running vanilla", mods, c);
			return nullptr;
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

	// ------------------------------------------------------------------ redirects + script log (YYToolkit)
	CCode* FindCCodeByName(const char* name)
	{
		if (!g_CCodeListHead) return nullptr;
		for (CCode* node = *g_CCodeListHead; node; node = node->m_Next)
			if (node->m_Name && strcmp(node->m_Name, name) == 0) return node;
		return nullptr;
	}

	std::unordered_set<CCode*> g_Seen;   // game thread only

	// Script redirects go through script_hooks.hpp, which must be set up on the game thread.
	void InstallScriptRedirects()
	{
		AurieStatus s = gm::Init(g_Module, GetInterface());
		Log("script hooks: %s", AurieStatusToString(s));
		for (const Redirect& r : g_ScriptRedirects)
			Log("script redirect %s -> %s: %s", r.target.c_str(), r.substitute.c_str(),
				AurieSuccess(s) ? AurieStatusToString(gm::Redirect(r.target, r.substitute)) : "skipped");
	}

	bool g_ScriptRedirectsDone = false;

	void CodeCallback(FWCodeEvent& event)
	{
		if (!g_ScriptRedirectsDone) {
			g_ScriptRedirectsDone = true;
			if (!g_ScriptRedirects.empty())
				InstallScriptRedirects();
		}
		auto& [self, other, code, result, argc] = event.Arguments();
		if (!code) return;
		if (g_LogScripts && g_Seen.size() < 16384 && g_Seen.insert(code).second)
			Log("script first run: %s", code->m_Name ? code->m_Name : "(null)");
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
	UNREFERENCED_PARAMETER(ModulePath);
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
		Log("YYToolkit not loaded: redirects.txt and log_scripts are off");
		return AURIE_SUCCESS;
	}
	if (!g_EventRedirects.empty() || !g_ScriptRedirects.empty() || g_LogScripts) {
		if (!g_EventRedirects.empty() && !g_CCodeListHead)
			Log("event redirects need the DELTARUNE code list; they are off in this exe");
		AurieStatus s = yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
		Log("EVENT_OBJECT_CALL callback (redirects, log_scripts): %s", AurieStatusToString(s));
	}
	return AURIE_SUCCESS;
}
