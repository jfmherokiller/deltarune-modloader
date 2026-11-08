/*
 * version.dll bootstrap proxy - the only part of the loader that the game loads itself.
 * Same role as BepInEx's Doorstop: get the framework in before any game code runs, nothing else.
 *
 * Install: copy version.dll next to the game exe. Uninstall: delete it.
 *
 *   1. Forwards every version.dll export to the system DLL (exports.c).
 *   2. Detours the exe's entry point. The main thread parks itself there; a helper thread loads
 *      mods\native\AurieCore.dll (bootstrap= in modloader.ini). AurieCore loads the rest of
 *      mods\native and mods\aurie (YYToolkit, LooseMods, ModHost, plugins), runs their early
 *      init and resumes the game. If nothing resumes it within aurie_timeout_ms, we do.
 *   3. Keeps what AurieCore needs from the environment, and undoes what it breaks:
 *      - Wine: answers NtQueryInformationThread(ThreadSystemThreadInformation) for the parked
 *        main thread, so AurieCore sees the early-launch state it checks for;
 *      - restores the working directory AurieCore changes (it breaks relative paths such as
 *        DELTARUNE's chapter "data.win");
 *      - hides the console AurieCore opens (aurie_console=1 keeps it).
 *
 * Everything that modifies the game lives in Aurie modules, not here: loose mods in
 * modloader/loosemods, plugins in modloader/modhost.
 * Settings: [loader] in mods\modloader.ini. Log: mods\modloader.log. No mods folder: inert.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <share.h>
#include "MinHook.h"

static WCHAR g_ModsDir[MAX_PATH];
static FILE* g_Log;
static CRITICAL_SECTION g_LogLock;

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

static void Hook(const char* what, void* target, void* detour, void** orig)
{
    MH_STATUS s = MH_CreateHook(target, detour, orig);
    if (s == MH_OK) s = MH_EnableHook(target);
    Log("hook %s @ %p: %s", what, target, MH_StatusToString(s));
}

/* ------------------------------------------------------------------ early launch */
typedef int (*PFN_Entry)(void* peb);
static PFN_Entry g_OrigEntry;
static HANDLE    g_MainThread;
static volatile DWORD g_MainTid;
static DWORD     g_AurieTimeoutMs = 30000;
static WCHAR     g_Bootstrap[MAX_PATH * 2];   /* absolute path of the DLL to load */
static WCHAR     g_StartDir[MAX_PATH * 2];

/* Wine only: AurieCore decides whether the process is still in early launch (and so runs
 * ModuleEntrypoint / ModulePreinitialize, which YYToolkit needs) from NtQueryInformationThread
 * class 40 (ThreadSystemThreadInformation) on the entry-point thread, falling back to
 * SystemProcessInformation. Wine implements neither: class 40 is STATUS_NOT_IMPLEMENTED and
 * every thread reports ThreadState 0 / WaitReason 0. Aurie then skips its early phase, maps
 * YYToolkit late and YYToolkit crashes. Answer class 40 for the self-suspended main thread the
 * way Windows does (Waiting / Suspended); everything else goes to Wine unchanged. */
typedef struct {
    LARGE_INTEGER KernelTime, UserTime, CreateTime;
    ULONG WaitTime;
    PVOID StartAddress;
    struct { HANDLE UniqueProcess, UniqueThread; } ClientId;
    LONG Priority, BasePriority;
    ULONG ContextSwitches, ThreadState, WaitReason;
} SysThreadInfo;
typedef LONG (NTAPI* PFN_NtQueryInformationThread)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static PFN_NtQueryInformationThread g_OrigNtQueryInformationThread;
static LONG NTAPI HookedNtQueryInformationThread(HANDLE thread, ULONG cls, PVOID info, ULONG len, PULONG ret_len)
{
    enum { ThreadSystemThreadInformation = 40, StateWaiting = 5, WaitSuspended = 5 };
    if (cls == ThreadSystemThreadInformation && g_MainTid && info && len >= sizeof(SysThreadInfo) &&
        GetThreadId(thread) == g_MainTid) {
        DWORD prev = SuspendThread(thread);         /* live suspend count, as the helper does */
        if (prev != (DWORD)-1) {
            ResumeThread(thread);
            if (prev >= 1) {
                SysThreadInfo* t = (SysThreadInfo*)info;
                memset(t, 0, sizeof *t);
                t->ClientId.UniqueProcess = (HANDLE)(ULONG_PTR)GetCurrentProcessId();
                t->ClientId.UniqueThread = (HANDLE)(ULONG_PTR)g_MainTid;
                t->ThreadState = StateWaiting;
                t->WaitReason = WaitSuspended;
                if (ret_len) *ret_len = sizeof *t;
                return 0;   /* STATUS_SUCCESS */
            }
        }
    }
    return g_OrigNtQueryInformationThread(thread, cls, info, len, ret_len);
}

static DWORD WINAPI HelperThread(LPVOID param)
{
    (void)param;
    for (int i = 0; i < 5000; i++) {                      /* wait for the self-suspend */
        DWORD prev = SuspendThread(g_MainThread);
        if (prev != (DWORD)-1) ResumeThread(g_MainThread);
        if (prev >= 1 && prev != (DWORD)-1) break;
        Sleep(1);
    }
    char n[MAX_PATH * 6]; W2U(g_Bootstrap, n, sizeof n);
    HMODULE m = LoadLibraryW(g_Bootstrap);
    if (m) Log("main thread parked at entry point; loaded %s at %p", n, (void*)m);
    else   Log("main thread parked at entry point; FAILED to load %s (error %lu)", n, GetLastError());

    DWORD start = GetTickCount();
    for (;;) {
        DWORD prev = SuspendThread(g_MainThread);
        if (prev == (DWORD)-1) break;
        ResumeThread(g_MainThread);
        if (prev == 0) { Log("main thread resumed by Aurie after %lu ms", GetTickCount() - start); break; }
        if (!m || GetTickCount() - start > g_AurieTimeoutMs) {
            Log(m ? "Aurie did not resume the game within %lu ms - resuming it ourselves"
                  : "resuming the game without mods", g_AurieTimeoutMs);
            while (ResumeThread(g_MainThread) > 1) {}
            break;
        }
        Sleep(10);
    }
    CloseHandle(g_MainThread);
    return 0;
}

/* AurieCore's ArProcessAttach does SetCurrentDirectoryW(<exe folder>). A game started with a
 * relative path and its own working directory (DELTARUNE starts chapters as
 * "DELTARUNE.exe -game data.win" from chapterN_windows\) then resolves that path against the
 * wrong folder. Restore the directory once Aurie is done with the parked main thread, before
 * any game code runs. */
static void RestoreStartDir(void)
{
    WCHAR now[MAX_PATH * 2];
    if (!g_StartDir[0]) return;
    GetCurrentDirectoryW(MAX_PATH * 2, now);
    if (_wcsicmp(now, g_StartDir) != 0) {
        SetCurrentDirectoryW(g_StartDir);
        char a[MAX_PATH * 6], b[MAX_PATH * 6]; W2U(now, a, sizeof a); W2U(g_StartDir, b, sizeof b);
        Log("working directory restored before game start: \"%s\" -> \"%s\"", a, b);
    }
}

static int EntryDetour(void* peb)
{
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_MainThread,
                    THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, 0);
    g_MainTid = GetCurrentThreadId();
    HANDLE t = CreateThread(NULL, 0, HelperThread, NULL, 0, NULL);
    if (t) {
        CloseHandle(t);
        SuspendThread(GetCurrentThread());               /* Aurie resumes us */
    } else {
        Log("CreateThread failed (%lu) - mods not loaded", GetLastError());
    }
    RestoreStartDir();
    return g_OrigEntry(peb);
}

/* AurieCore calls AllocConsole() in every process it loads into (each DELTARUNE chapter is its own
 * process). With aurie_console=0 (default) report success without creating one; Aurie's console
 * log sink then skips its writes and aurie.log is still written. */
typedef BOOL (WINAPI* PFN_AllocConsole)(void);
static PFN_AllocConsole g_OrigAllocConsole;
static BOOL WINAPI HookedAllocConsole(void)
{
    Log("AllocConsole() suppressed (set aurie_console=1 in modloader.ini to show the Aurie console)");
    return TRUE;
}

/* ------------------------------------------------------------------ setup */
static void Init(void)
{
    InitializeCriticalSection(&g_LogLock);
    WCHAR root[MAX_PATH];
    GetModuleFileNameW(NULL, root, MAX_PATH);
    WCHAR* slash = wcsrchr(root, L'\\'); if (slash) *slash = 0;
    _snwprintf(g_ModsDir, MAX_PATH, L"%s\\mods", root);

    DWORD a = GetFileAttributesW(g_ModsDir);
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return;  /* no mods folder: stay inert */

    WCHAR ini[MAX_PATH], logp[MAX_PATH];
    _snwprintf(ini, MAX_PATH, L"%s\\modloader.ini", g_ModsDir);
    _snwprintf(logp, MAX_PATH, L"%s\\modloader.log", g_ModsDir);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(logp, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 4 * 1024 * 1024 && !fa.nFileSizeHigh)
        DeleteFileW(logp);
    g_Log = _wfsopen(logp, L"a", _SH_DENYNO);

    char r[MAX_PATH * 3], cmdline[2048];
    W2U(root, r, sizeof r); W2U(GetCommandLineW(), cmdline, sizeof cmdline);
    Log("==== bootstrap proxy starting; game root \"%s\"", r);
    Log("cmdline: %s", cmdline);
    if (!GetPrivateProfileIntW(L"loader", L"enabled", 1, ini)) { Log("disabled by modloader.ini (enabled=0)"); return; }

    WCHAR rel[MAX_PATH];
    GetPrivateProfileStringW(L"loader", L"bootstrap", L"native\\AurieCore.dll", rel, MAX_PATH, ini);
    _snwprintf(g_Bootstrap, MAX_PATH * 2, L"%s\\%s", g_ModsDir, rel);
    if (GetFileAttributesW(g_Bootstrap) == INVALID_FILE_ATTRIBUTES) {
        char n[MAX_PATH * 6]; W2U(g_Bootstrap, n, sizeof n);
        Log("%s not found - nothing to load", n);
        return;
    }
    g_AurieTimeoutMs = GetPrivateProfileIntW(L"loader", L"aurie_timeout_ms", 30000, ini);
    GetCurrentDirectoryW(MAX_PATH * 2, g_StartDir);

    MH_STATUS s = MH_Initialize();
    if (s != MH_OK) { Log("MH_Initialize failed: %s", MH_StatusToString(s)); return; }

    char* base = (char*)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    Hook("entry point (early launch)", base + nt->OptionalHeader.AddressOfEntryPoint,
         (void*)EntryDetour, (void**)&g_OrigEntry);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll && GetProcAddress(ntdll, "wine_get_version")) {
        void* qit = (void*)GetProcAddress(ntdll, "NtQueryInformationThread");
        if (qit) Hook("NtQueryInformationThread (Wine: Aurie early-launch detection)", qit,
                      (void*)HookedNtQueryInformationThread, (void**)&g_OrigNtQueryInformationThread);
    }
    if (!GetPrivateProfileIntW(L"loader", L"aurie_console", 0, ini)) {
        void* ac = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "AllocConsole");
        if (ac) Hook("AllocConsole (hide Aurie console)", ac, (void*)HookedAllocConsole, (void**)&g_OrigAllocConsole);
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
