/*
 * DELTARUNE mod loader - native Windows build (version.dll proxy + MinHook).
 *
 * Install: copy version.dll next to DELTARUNE.exe. Uninstall: delete it.
 * Ported from the Wine-only research proxy mod_test/aurie_build/proxy/dllmain.c
 * (that rig was deleted 2026-09-27).
 *
 * What it does
 *   1. File overrides - hooks the runner's whole-file read primitive
 *      ReadEntireFile (RVA 0x92C40, RE/09_datawin_loader.md). Any file the game
 *      reads from inside its install folder can be replaced by dropping a file
 *      with the same relative path into  mods\<ModName>\ .
 *        e.g. mods\MyMod\chapter1_windows\lang\lang_en.json
 *      Loose .ogg audio does NOT go through this path (see RE/TODO.md).
 *   2. Script/event redirects - hooks ExecuteIt (RVA 0x1BBC90,
 *      RE/10_vm_and_scripts.md). mods\<ModName>\redirects.txt lines of the form
 *        gml_Object_obj_ui_version_Draw_0 = gml_Object_obj_ui_version_Create_0
 *      run the substitute's already-compiled code in place of the target.
 *   3. Wine only: rewrites the chapter self-relaunch command line to an
 *      absolute data.win path (works around Wine not applying
 *      lpCurrentDirectory). Not installed on real Windows.
 *
 * Mods load in alphabetical folder order; for file overrides the LAST mod that
 * has the file wins. Folders starting with '_' or '.' are ignored (disabled).
 * Settings: mods\modloader.ini   Log: mods\modloader.log
 *
 * Safety: before hooking, the first 16 bytes of each target function are
 * compared against this exact game build (TimeDateStamp 0x685E7007). If the
 * game updates and they differ, no game hooks are installed and the game runs
 * unmodded (the log says why).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <share.h>
#include "MinHook.h"

/* ------------------------------------------------------------------ layout */
#define RVA_READ_ENTIRE_FILE 0x92C40
#define RVA_EXECUTE_IT       0x1BBC90
#define RVA_CCODE_LIST_HEAD  0x8C9EC8   /* struct CCode* list head */
#define RVA_GM_ALLOC         0x38F430   /* runner allocator ReadEntireFile uses (sub_14038F430) */
#define CCODE_NEXT_OFFSET    8
#define CCODE_NAME_OFFSET    128        /* corrected from 120, see 10_vm_and_scripts.md */

static const unsigned char SIG_READ_ENTIRE_FILE[16] = {
    0x40, 0x55, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8D, 0x6C, 0x24, 0x30, 0x48, 0x89, 0x5D, 0x20, 0x48 };
static const unsigned char SIG_GM_ALLOC[16] = {
    0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x70, 0x04, 0x00, 0x00, 0x0F, 0x29, 0xB4, 0x24, 0x60 };
static const unsigned char SIG_EXECUTE_IT[16] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41 };

/* ------------------------------------------------------------------ state */
#define MAX_MODS      64
#define MAX_REDIRECTS 256

typedef struct { char target[128]; char substitute[128]; void* target_code; void* sub_code; int mod; } Redirect;

static WCHAR  g_GameRoot[MAX_PATH];     /* folder of DELTARUNE.exe, no trailing slash */
static size_t g_GameRootLen;
static WCHAR  g_ModsDir[MAX_PATH];
static WCHAR  g_ModNames[MAX_MODS][MAX_PATH];
static int    g_ModCount;
static Redirect g_Redirects[MAX_REDIRECTS];
static int    g_RedirectCount;
static int    g_LogFiles = 1, g_LogScripts = 0;
static FILE*  g_Log;
static CRITICAL_SECTION g_LogLock;
static void** g_CCodeListHead;

/* ------------------------------------------------------------------ logging */
static void Log(const char* fmt, ...)
{
    if (!g_Log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    va_list ap; va_start(ap, fmt);
    EnterCriticalSection(&g_LogLock);
    fprintf(g_Log, "[%02d:%02d:%02d.%03d pid %lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
            (unsigned long)GetCurrentProcessId());
    vfprintf(g_Log, fmt, ap);
    fputc('\n', g_Log);
    fflush(g_Log);
    LeaveCriticalSection(&g_LogLock);
    va_end(ap);
}

static void W2U(const WCHAR* w, char* out, int n)
{
    if (!w || WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL) <= 0) snprintf(out, n, "?");
}

/* ------------------------------------------------------------------ mods scan */
static int CmpModName(const void* a, const void* b) { return _wcsicmp((const WCHAR*)a, (const WCHAR*)b); }

static void TrimA(char* s)
{
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    char* b = s; while (*b == ' ' || *b == '\t') b++;
    if (b != s) memmove(s, b, strlen(b) + 1);
}

static void LoadRedirects(int mod)
{
    WCHAR path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%s\\%s\\redirects.txt", g_ModsDir, g_ModNames[mod]);
    FILE* f = _wfopen(path, L"r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char* hash = strchr(line, '#'); if (hash) *hash = 0;
        char* eq = strchr(line, '='); if (!eq) continue;
        *eq = 0;
        char a[256], b[256];
        snprintf(a, sizeof a, "%s", line); snprintf(b, sizeof b, "%s", eq + 1);
        TrimA(a); TrimA(b);
        if (!a[0] || !b[0]) continue;
        if (g_RedirectCount >= MAX_REDIRECTS) { Log("redirect limit reached, ignoring the rest"); break; }
        Redirect* r = &g_Redirects[g_RedirectCount++];
        memset(r, 0, sizeof *r);
        snprintf(r->target, sizeof r->target, "%s", a);
        snprintf(r->substitute, sizeof r->substitute, "%s", b);
        r->mod = mod;
    }
    fclose(f);
}

static void ScanMods(void)
{
    WCHAR pattern[MAX_PATH];
    _snwprintf(pattern, MAX_PATH, L"%s\\*", g_ModsDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.' || fd.cFileName[0] == L'_') continue;
        if (!_wcsicmp(fd.cFileName, L"native") || !_wcsicmp(fd.cFileName, L"aurie")) continue; /* Aurie folders */
        if (g_ModCount >= MAX_MODS) break;
        wcsncpy(g_ModNames[g_ModCount++], fd.cFileName, MAX_PATH - 1);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(g_ModNames, g_ModCount, sizeof g_ModNames[0], CmpModName);
    for (int i = 0; i < g_ModCount; i++) {
        char n[MAX_PATH * 3]; W2U(g_ModNames[i], n, sizeof n);
        int before = g_RedirectCount;
        LoadRedirects(i);
        Log("mod #%d: %s (%d redirect(s))", i, n, g_RedirectCount - before);
    }
}

/* ------------------------------------------------------------------ file overrides */
typedef void* (*PFN_ReadEntireFile)(const char*, unsigned int*);
static PFN_ReadEntireFile g_OrigReadEntireFile;

/* Map a game-requested path to mods\<Mod>\<relative path>, last mod wins. */
static int FindOverride(const char* path_utf8, char* out_utf8, int out_n)
{
    WCHAR wpath[MAX_PATH * 2], full[MAX_PATH * 2], cand[MAX_PATH * 2];
    if (!path_utf8 || !MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, wpath, MAX_PATH * 2)) return 0;
    DWORD n = GetFullPathNameW(wpath, MAX_PATH * 2, full, NULL);
    if (!n || n >= MAX_PATH * 2) return 0;
    for (WCHAR* p = full; *p; p++) if (*p == L'/') *p = L'\\';
    if (_wcsnicmp(full, g_GameRoot, g_GameRootLen) != 0 || full[g_GameRootLen] != L'\\') return 0;
    const WCHAR* rel = full + g_GameRootLen + 1;
    if (_wcsnicmp(rel, L"mods\\", 5) == 0) return 0;   /* never override the mods folder itself */
    for (int i = g_ModCount - 1; i >= 0; i--) {
        _snwprintf(cand, MAX_PATH * 2, L"%s\\%s\\%s", g_ModsDir, g_ModNames[i], rel);
        DWORD a = GetFileAttributesW(cand);
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
            return WideCharToMultiByte(CP_UTF8, 0, cand, -1, out_utf8, out_n, NULL, NULL) > 0;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ loose GML
 * mods\<Mod>\<chapter>\code\<gml_CodeName>.gml : decompiled GML (UTMT naming).
 * When the game reads <chapter>\data.win and any enabled mod has such files, the loader
 * runs UTMT's compiler (UndertaleModCli + tools\ImportGMLFolder.csx) on the vanilla
 * data.win, receives the patched bytes over a named pipe and hands them to the game in
 * a buffer from the runner's own allocator. data.win on disk is never modified and no
 * patched copy is written anywhere. */
typedef void* (*PFN_GmAlloc)(size_t, __int64, __int64, char);
static PFN_GmAlloc g_GmAlloc;
static WCHAR g_UtmtCli[MAX_PATH * 2], g_ImportScript[MAX_PATH * 2];
static DWORD g_GmlTimeoutMs = 300000;

typedef struct { HANDLE pipe; unsigned char* buf; unsigned long long len, got; int ok; } PipeRx;
static DWORD WINAPI PipeReader(LPVOID p)
{
    PipeRx* rx = (PipeRx*)p;
    if (!ConnectNamedPipe(rx->pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) return 0;
    DWORD n;
    unsigned long long len = 0;
    if (!ReadFile(rx->pipe, &len, 8, &n, NULL) || n != 8 || len == 0 || len > 0xFFFFFFF0ull) return 0;
    rx->buf = (unsigned char*)g_GmAlloc((size_t)len + 1, 0, 0, 0);
    if (!rx->buf) return 0;
    rx->len = len;
    while (rx->got < len) {
        DWORD want = (DWORD)((len - rx->got) > (1u << 20) ? (1u << 20) : (len - rx->got));
        if (!ReadFile(rx->pipe, rx->buf + rx->got, want, &n, NULL) || n == 0) return 0;
        rx->got += n;
    }
    rx->buf[len] = 0;   /* ReadEntireFile null-terminates too */
    rx->ok = 1;
    return 0;
}

/* Collects mods\<Mod>\<rel_dir>\code for every enabled mod that has *.gml there. */
static int CollectGmlDirs(const WCHAR* rel_dir, WCHAR* out, size_t out_n)
{
    int count = 0;
    out[0] = 0;
    for (int i = 0; i < g_ModCount; i++) {
        WCHAR dir[MAX_PATH * 2], pat[MAX_PATH * 2];
        WIN32_FIND_DATAW fd;
        _snwprintf(dir, MAX_PATH * 2, L"%s\\%s\\%s\\code", g_ModsDir, g_ModNames[i], rel_dir);
        _snwprintf(pat, MAX_PATH * 2, L"%s\\*.gml", dir);
        HANDLE h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        FindClose(h);
        if (wcslen(out) + wcslen(dir) + 2 >= out_n) break;
        if (out[0]) wcscat(out, L";");
        wcscat(out, dir);
        count++;
    }
    return count;
}

static void* CompileLooseGml(const char* path_utf8, unsigned int* out_size)
{
    WCHAR wpath[MAX_PATH * 2], full[MAX_PATH * 2], rel_dir[MAX_PATH * 2], dirs[8192];
    if (!g_GmAlloc || !path_utf8 || !MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, wpath, MAX_PATH * 2)) return NULL;
    DWORD n = GetFullPathNameW(wpath, MAX_PATH * 2, full, NULL);
    if (!n || n >= MAX_PATH * 2) return NULL;
    for (WCHAR* p = full; *p; p++) if (*p == L'/') *p = L'\\';
    const WCHAR* base = wcsrchr(full, L'\\');
    if (!base || _wcsicmp(base + 1, L"data.win") != 0) return NULL;
    if (_wcsnicmp(full, g_GameRoot, g_GameRootLen) != 0 || full[g_GameRootLen] != L'\\') return NULL;
    size_t rel_len = (size_t)(base - (full + g_GameRootLen + 1));
    if (base <= full + g_GameRootLen) rel_len = 0;
    if (rel_len == 0) wcscpy(rel_dir, L".");   /* launcher data.win: mods\<Mod>\code */
    else { wcsncpy(rel_dir, full + g_GameRootLen + 1, rel_len); rel_dir[rel_len] = 0; }

    int mods = CollectGmlDirs(rel_dir, dirs, sizeof dirs / sizeof dirs[0]);
    if (!mods) return NULL;
    char dirs_u[16384];
    W2U(dirs, dirs_u, sizeof dirs_u);
    if (GetFileAttributesW(g_UtmtCli) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW(g_ImportScript) == INVALID_FILE_ATTRIBUTES) {
        char c[MAX_PATH * 6];
        W2U(g_UtmtCli, c, sizeof c);
        Log("loose GML: %d mod folder(s) for this data.win but compiler missing (%s) - running vanilla", mods, c);
        return NULL;
    }
    Log("loose GML: compiling %s into %s", dirs_u, path_utf8);

    WCHAR pipe_name[128];
    _snwprintf(pipe_name, 128, L"\\\\.\\pipe\\dr_modloader_%lu_%lu", GetCurrentProcessId(), GetTickCount());
    HANDLE pipe = CreateNamedPipeW(pipe_name, PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 0, 1 << 20, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) { Log("loose GML: CreateNamedPipe failed %lu", GetLastError()); return NULL; }
    PipeRx rx = { pipe };
    HANDLE reader = CreateThread(NULL, 0, PipeReader, &rx, 0, NULL);

    WCHAR cmd[MAX_PATH * 8], logpath[MAX_PATH * 2];
    _snwprintf(cmd, MAX_PATH * 8, L"\"%s\" load \"%s\" -s \"%s\"", g_UtmtCli, full, g_ImportScript);
    _snwprintf(logpath, MAX_PATH * 2, L"%s\\modloader.gml.log", g_ModsDir);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE logf = CreateFileW(logpath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, 0, NULL);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    STARTUPINFOW si = { sizeof si };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul; si.hStdOutput = logf; si.hStdError = logf;
    PROCESS_INFORMATION pi = { 0 };
    SetEnvironmentVariableW(L"DR_GML_DIRS", dirs);
    SetEnvironmentVariableW(L"DR_OUT_PIPE", pipe_name);
    DWORD t0 = GetTickCount();
    BOOL started = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    SetEnvironmentVariableW(L"DR_GML_DIRS", NULL);
    SetEnvironmentVariableW(L"DR_OUT_PIPE", NULL);
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
    /* unblock the reader if the child never connected */
    HANDLE dummy = CreateFileW(pipe_name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (dummy != INVALID_HANDLE_VALUE) CloseHandle(dummy);
    WaitForSingleObject(reader, 10000);
    CloseHandle(reader);
    CloseHandle(pipe);

    if (exit_code != 0 || !rx.ok) {
        Log("loose GML: compile FAILED (exit %ld, received %llu/%llu bytes) - see mods\\modloader.gml.log; running vanilla",
            (long)exit_code, rx.got, rx.len);
        /* rx.buf came from the runner's allocator; leaking it once on failure is harmless */
        return NULL;
    }
    Log("loose GML: compiled OK in %lu ms, %llu bytes served from memory", GetTickCount() - t0, rx.len);
    if (out_size) *out_size = (unsigned int)rx.len;
    return rx.buf;
}

static void* HookedReadEntireFile(const char* path, unsigned int* out_size)
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

/* ------------------------------------------------------------------ script redirects */
typedef char (*PFN_ExecuteIt)(void*, void*, void*, void*, int);
static PFN_ExecuteIt g_OrigExecuteIt;

static const char* CodeName(void* code)
{
    if (!code) return NULL;
    return *(const char**)((char*)code + CCODE_NAME_OFFSET);
}

static void* FindCCodeByName(const char* name)
{
    if (!g_CCodeListHead) return NULL;
    for (void* node = *g_CCodeListHead; node; node = *(void**)((char*)node + CCODE_NEXT_OFFSET)) {
        const char* n = CodeName(node);
        if (n && strcmp(n, name) == 0) return node;
    }
    return NULL;
}

/* first-seen script logging (log_scripts=1): open-addressed pointer set */
#define SEEN_CAP 16384
static void* g_Seen[SEEN_CAP];
static int   g_SeenCount;
static int MarkSeen(void* p)
{
    size_t i = ((size_t)p >> 4) & (SEEN_CAP - 1);
    for (int probes = 0; probes < SEEN_CAP; probes++, i = (i + 1) & (SEEN_CAP - 1)) {
        if (g_Seen[i] == p) return 0;
        if (!g_Seen[i]) { if (g_SeenCount >= SEEN_CAP * 3 / 4) return 0; g_Seen[i] = p; g_SeenCount++; return 1; }
    }
    return 0;
}

static char HookedExecuteIt(void* self, void* other, void* code, void* args, int flags)
{
    void* use = code;
    if (g_RedirectCount && code) {
        for (int i = 0; i < g_RedirectCount; i++) {
            Redirect* r = &g_Redirects[i];
            int match = r->target_code ? (code == r->target_code) : 0;
            if (!match && !r->target_code) {
                const char* n = CodeName(code);
                if (n && strcmp(n, r->target) == 0) { r->target_code = code; match = 1; }
            }
            if (!match) continue;
            if (!r->sub_code) {
                r->sub_code = FindCCodeByName(r->substitute);
                Log(r->sub_code ? "redirect active: %s -> %s" : "redirect FAILED (substitute not found): %s -> %s",
                    r->target, r->substitute);
                if (!r->sub_code) r->sub_code = (void*)1;   /* don't search again */
            }
            if (r->sub_code != (void*)1) use = r->sub_code;
            break;
        }
    }
    if (g_LogScripts && code && MarkSeen(code)) {
        const char* n = CodeName(code);
        Log("script first run: %s", n ? n : "(null)");
    }
    return g_OrigExecuteIt(self, other, use, args, flags);
}

/* ------------------------------------------------------------------ Wine relaunch fix */
typedef BOOL (WINAPI* PFN_CreateProcessW)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
    BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
static PFN_CreateProcessW g_OrigCreateProcessW;

static BOOL WINAPI HookedCreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
    LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env, LPCWSTR cwd,
    LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi)
{
    static WCHAR rewritten[2048];
    WCHAR dir[1024];
    LPWSTR use = cmd;
    if (cmd && cwd && cwd[0]) {
        DWORD n = GetFullPathNameW(cwd, 1024, dir, NULL);
        if (n > 0 && n < 1024 && dir[n - 1] == L'\\') dir[--n] = 0;
        LPWSTR m = (n > 0 && n < 1024) ? wcsstr(cmd, L"data.win") : NULL;
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

/* ------------------------------------------------------------------ setup */
/* ------------------------------------------------------------------ Aurie / YYToolkit */
/* Aurie's own layout: mods\native\*.dll are loaded first (AurieCore.dll), and AurieCore
 * itself then loads mods\aurie\*.dll (YYToolkit.dll + YYTK plugins) on its own thread.
 * We only have to LoadLibrary the native DLLs - the same thing AuriePatcher's
 * entry-point trampoline does, without patching DELTARUNE.exe. */
static int LoadNativeMods(int dry_run)
{
    int count = 0;
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    _snwprintf(pattern, MAX_PATH, L"%s\\native\\*.dll", g_ModsDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        count++;
        if (dry_run) continue;
        _snwprintf(path, MAX_PATH, L"%s\\native\\%s", g_ModsDir, fd.cFileName);
        HMODULE m = LoadLibraryW(path);
        char n[MAX_PATH * 3]; W2U(fd.cFileName, n, sizeof n);
        if (m) Log("native mod loaded: %s at %p", n, (void*)m);
        else   Log("native mod FAILED: %s (error %lu)", n, GetLastError());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return count;
}

/* Early launch, AuriePatcher-style but at runtime (the exe is not modified):
 * the game's entry point is detoured; the main thread starts a helper, then
 * suspends itself before any game code runs. The helper waits until the main
 * thread is really suspended and loads mods\native\*.dll. AurieCore then sees a
 * suspended entry-point thread, runs YYToolkit's ModuleEntrypoint /
 * ModulePreinitialize (YYTK's runner-interface hook must be in place before the
 * runner starts) and resumes the process itself (NtResumeProcess).
 * Safety net: if nobody resumes the main thread within aurie_timeout_ms, the
 * helper resumes it so the game can never hang here. */
typedef int (*PFN_Entry)(void* peb);
static PFN_Entry g_OrigEntry;
static HANDLE    g_MainThread;

static DWORD WINAPI AurieHelperThread(LPVOID param)
{
    DWORD timeout_ms = (DWORD)(ULONG_PTR)param;
    for (int i = 0; i < 5000; i++) {                      /* wait for the self-suspend */
        DWORD prev = SuspendThread(g_MainThread);
        if (prev != (DWORD)-1) ResumeThread(g_MainThread);
        if (prev >= 1 && prev != (DWORD)-1) break;
        Sleep(1);
    }
    Log("main thread parked at entry point; loading native mods");
    LoadNativeMods(0);

    DWORD start = GetTickCount();
    for (;;) {
        DWORD prev = SuspendThread(g_MainThread);
        if (prev == (DWORD)-1) break;
        ResumeThread(g_MainThread);
        if (prev == 0) { Log("main thread resumed by Aurie after %lu ms", GetTickCount() - start); break; }
        if (GetTickCount() - start > timeout_ms) {
            Log("Aurie did not resume the game within %lu ms - resuming it ourselves", timeout_ms);
            while (ResumeThread(g_MainThread) > 1) {}
            break;
        }
        Sleep(10);
    }
    CloseHandle(g_MainThread);
    return 0;
}

static DWORD g_AurieTimeoutMs = 30000;

/* AurieCore's ArProcessAttach does SetCurrentDirectoryW(<exe folder>). The
 * chapter launcher starts chapters as "DELTARUNE.exe -game data.win" with the
 * working directory set to chapterN_windows\, so after Aurie's chdir the relative
 * data.win resolves to the ROOT launcher data.win and every chapter "loops" back
 * to chapter select. We capture the directory before Aurie loads and restore it
 * once Aurie is done with the parked main thread (before any game code runs). */
static WCHAR g_StartDir[MAX_PATH * 2];

static void RestoreStartDir(const char* when)
{
    WCHAR now[MAX_PATH * 2];
    if (!g_StartDir[0]) return;
    GetCurrentDirectoryW(MAX_PATH * 2, now);
    if (_wcsicmp(now, g_StartDir) != 0) {
        SetCurrentDirectoryW(g_StartDir);
        char a[MAX_PATH * 6], b[MAX_PATH * 6]; W2U(now, a, sizeof a); W2U(g_StartDir, b, sizeof b);
        Log("working directory restored (%s): \"%s\" -> \"%s\"", when, a, b);
    }
}

static int EntryDetour(void* peb)
{
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_MainThread,
                    THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, 0);
    HANDLE t = CreateThread(NULL, 0, AurieHelperThread, (LPVOID)(ULONG_PTR)g_AurieTimeoutMs, 0, NULL);
    if (t) {
        CloseHandle(t);
        SuspendThread(GetCurrentThread());               /* Aurie resumes us */
    } else {
        Log("CreateThread failed (%lu) - Aurie not loaded", GetLastError());
    }
    RestoreStartDir("before game start");
    return g_OrigEntry(peb);
}

/* AurieCore calls AllocConsole() unconditionally in every process it loads into,
 * and every chapter is its own DELTARUNE.exe process -> one new console window per
 * chapter switch. With aurie_console=0 (default) we report success without creating
 * a console. Aurie's spdlog console sink then has no handle and skips writes
 * (wincolor_sink checks for NULL/INVALID); aurie.log is still written. */
typedef BOOL (WINAPI* PFN_AllocConsole)(void);
static PFN_AllocConsole g_OrigAllocConsole;
static BOOL WINAPI HookedAllocConsole(void)
{
    Log("AllocConsole() suppressed (set aurie_console=1 in modloader.ini to show the Aurie console)");
    return TRUE;
}

static int CheckSig(const char* what, void* addr, const unsigned char* sig)
{
    if (memcmp(addr, sig, 16) == 0) return 1;
    Log("version check FAILED for %s at %p - unknown DELTARUNE.exe build, game hooks disabled", what, addr);
    return 0;
}

static void Hook(const char* what, void* target, void* detour, void** orig)
{
    MH_STATUS s = MH_CreateHook(target, detour, orig);
    if (s == MH_OK) s = MH_EnableHook(target);
    Log("hook %s @ %p: %s", what, target, MH_StatusToString(s));
}

static void Init(void)
{
    InitializeCriticalSection(&g_LogLock);
    GetModuleFileNameW(NULL, g_GameRoot, MAX_PATH);
    WCHAR* slash = wcsrchr(g_GameRoot, L'\\'); if (slash) *slash = 0;
    g_GameRootLen = wcslen(g_GameRoot);
    _snwprintf(g_ModsDir, MAX_PATH, L"%s\\mods", g_GameRoot);

    DWORD a = GetFileAttributesW(g_ModsDir);
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return;  /* no mods folder: stay inert */

    WCHAR ini[MAX_PATH], logp[MAX_PATH];
    _snwprintf(ini, MAX_PATH, L"%s\\modloader.ini", g_ModsDir);
    g_LogFiles   = GetPrivateProfileIntW(L"loader", L"log_files", 1, ini);
    g_LogScripts = GetPrivateProfileIntW(L"loader", L"log_scripts", 0, ini);
    int enabled  = GetPrivateProfileIntW(L"loader", L"enabled", 1, ini);

    _snwprintf(logp, MAX_PATH, L"%s\\modloader.log", g_ModsDir);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(logp, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 4 * 1024 * 1024 && !fa.nFileSizeHigh)
        DeleteFileW(logp);
    g_Log = _wfsopen(logp, L"a", _SH_DENYNO);

    char root[MAX_PATH * 3], cmdline[2048];
    W2U(g_GameRoot, root, sizeof root); W2U(GetCommandLineW(), cmdline, sizeof cmdline);
    Log("==== DELTARUNE modloader starting; game root \"%s\"", root);
    Log("cmdline: %s", cmdline);
    if (!enabled) { Log("disabled by modloader.ini (enabled=0)"); return; }

    ScanMods();

    /* loose GML compiler: mods\tools\utmt\UndertaleModCli.exe by default */
    {
        WCHAR v[MAX_PATH * 2];
        GetPrivateProfileStringW(L"loader", L"utmt_cli", L"", v, MAX_PATH * 2, ini);
        if (v[0]) wcscpy(g_UtmtCli, v);
        else _snwprintf(g_UtmtCli, MAX_PATH * 2, L"%s\\tools\\utmt\\UndertaleModCli.exe", g_ModsDir);
        _snwprintf(g_ImportScript, MAX_PATH * 2, L"%s\\tools\\ImportGMLFolder.csx", g_ModsDir);
        g_GmlTimeoutMs = GetPrivateProfileIntW(L"loader", L"gml_timeout_ms", 300000, ini);
    }

    MH_STATUS s = MH_Initialize();
    if (s != MH_OK) { Log("MH_Initialize failed: %s", MH_StatusToString(s)); return; }

    char* base = (char*)GetModuleHandleW(NULL);
    void* ref = base + RVA_READ_ENTIRE_FILE;
    void* exe = base + RVA_EXECUTE_IT;
    if (CheckSig("ReadEntireFile", ref, SIG_READ_ENTIRE_FILE) && CheckSig("ExecuteIt", exe, SIG_EXECUTE_IT)) {
        g_CCodeListHead = (void**)(base + RVA_CCODE_LIST_HEAD);
        if (CheckSig("GM allocator", base + RVA_GM_ALLOC, SIG_GM_ALLOC))
            g_GmAlloc = (PFN_GmAlloc)(base + RVA_GM_ALLOC);
        Hook("ReadEntireFile", ref, (void*)HookedReadEntireFile, (void**)&g_OrigReadEntireFile);
        if (g_RedirectCount || g_LogScripts)
            Hook("ExecuteIt", exe, (void*)HookedExecuteIt, (void**)&g_OrigExecuteIt);
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll && GetProcAddress(ntdll, "wine_get_version")) {
        void* cpw = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateProcessW");
        Log("running under Wine - enabling chapter relaunch fix");
        Hook("CreateProcessW", cpw, (void*)HookedCreateProcessW, (void**)&g_OrigCreateProcessW);
    }

    /* Aurie / YYToolkit: park the main thread at the game's entry point and load
     * mods\native\*.dll from a helper thread (see EntryDetour). */
    g_AurieTimeoutMs = GetPrivateProfileIntW(L"loader", L"aurie_timeout_ms", 30000, ini);
    GetCurrentDirectoryW(MAX_PATH * 2, g_StartDir);
    if (GetPrivateProfileIntW(L"loader", L"load_aurie", 1, ini) && LoadNativeMods(1) > 0) {
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        void* oep = base + nt->OptionalHeader.AddressOfEntryPoint;
        Hook("entry point (Aurie early launch)", oep, (void*)EntryDetour, (void**)&g_OrigEntry);
        if (!GetPrivateProfileIntW(L"loader", L"aurie_console", 0, ini)) {
            void* ac = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "AllocConsole");
            if (ac) Hook("AllocConsole (hide Aurie console)", ac, (void*)HookedAllocConsole, (void**)&g_OrigAllocConsole);
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        Init();
    } else if (reason == DLL_PROCESS_DETACH && g_Log) {
        Log("process exiting");
        fclose(g_Log); g_Log = NULL;
    }
    return TRUE;
}
