/*
 * version.dll export forwarding for native Windows.
 * The proxy loads the real %SystemRoot%\System32\version.dll on first use and
 * tail-calls into it. (A .def "version_orig.X" forward needs a renamed copy of
 * the system DLL next to the game; this avoids shipping one.)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HMODULE g_Real;

static FARPROC RealProc(const char* name)
{
    if (!g_Real) {
        WCHAR path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (n == 0 || n > MAX_PATH - 14) return NULL;
        lstrcatW(path, L"\\version.dll");
        HMODULE h = LoadLibraryW(path);
        if (!h) return NULL;
        g_Real = h;
    }
    return GetProcAddress(g_Real, name);
}

#define FWD(ret, name, params, args, fail)                              \
    typedef ret (WINAPI* PFN_##name) params;                            \
    ret WINAPI Proxy_##name params {              \
        static PFN_##name p;                                            \
        if (!p) p = (PFN_##name)RealProc(#name);                        \
        if (!p) { SetLastError(ERROR_PROC_NOT_FOUND); return fail; }    \
        return p args;                                                  \
    }

FWD(BOOL,  GetFileVersionInfoA,       (LPCSTR f, DWORD h, DWORD l, LPVOID d),                     (f, h, l, d), FALSE)
FWD(BOOL,  GetFileVersionInfoW,       (LPCWSTR f, DWORD h, DWORD l, LPVOID d),                    (f, h, l, d), FALSE)
FWD(BOOL,  GetFileVersionInfoExA,     (DWORD fl, LPCSTR f, DWORD h, DWORD l, LPVOID d),           (fl, f, h, l, d), FALSE)
FWD(BOOL,  GetFileVersionInfoExW,     (DWORD fl, LPCWSTR f, DWORD h, DWORD l, LPVOID d),          (fl, f, h, l, d), FALSE)
FWD(DWORD, GetFileVersionInfoSizeA,   (LPCSTR f, LPDWORD h),                                      (f, h), 0)
FWD(DWORD, GetFileVersionInfoSizeW,   (LPCWSTR f, LPDWORD h),                                     (f, h), 0)
FWD(DWORD, GetFileVersionInfoSizeExA, (DWORD fl, LPCSTR f, LPDWORD h),                            (fl, f, h), 0)
FWD(DWORD, GetFileVersionInfoSizeExW, (DWORD fl, LPCWSTR f, LPDWORD h),                           (fl, f, h), 0)
FWD(BOOL,  VerQueryValueA,            (LPCVOID b, LPCSTR s, LPVOID* o, PUINT l),                  (b, s, o, l), FALSE)
FWD(BOOL,  VerQueryValueW,            (LPCVOID b, LPCWSTR s, LPVOID* o, PUINT l),                 (b, s, o, l), FALSE)
FWD(DWORD, VerLanguageNameA,          (DWORD w, LPSTR s, DWORD n),                                (w, s, n), 0)
FWD(DWORD, VerLanguageNameW,          (DWORD w, LPWSTR s, DWORD n),                               (w, s, n), 0)
FWD(DWORD, VerFindFileA,              (DWORD f, LPCSTR a, LPCSTR b, LPCSTR c, LPSTR d, PUINT e, LPSTR g, PUINT h),
                                      (f, a, b, c, d, e, g, h), 0)
FWD(DWORD, VerFindFileW,              (DWORD f, LPCWSTR a, LPCWSTR b, LPCWSTR c, LPWSTR d, PUINT e, LPWSTR g, PUINT h),
                                      (f, a, b, c, d, e, g, h), 0)
FWD(DWORD, VerInstallFileA,           (DWORD f, LPCSTR a, LPCSTR b, LPCSTR c, LPCSTR d, LPCSTR e, LPSTR g, PUINT h),
                                      (f, a, b, c, d, e, g, h), 0)
FWD(DWORD, VerInstallFileW,           (DWORD f, LPCWSTR a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPCWSTR e, LPWSTR g, PUINT h),
                                      (f, a, b, c, d, e, g, h), 0)
