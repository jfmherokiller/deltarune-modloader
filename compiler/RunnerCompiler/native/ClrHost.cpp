#include "ClrHost.hpp"
#include <cstdint>
#include <cstdio>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#define HOSTFXR_CALLTYPE __cdecl
#define STR(s) L##s
using char_t = wchar_t;
static void* OpenLibrary(const std::wstring& path) { return LoadLibraryW(path.c_str()); }
static void* Symbol(void* lib, const char* name) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name)); }
static const wchar_t* HostfxrName = L"hostfxr.dll";
static const wchar_t Slash = L'\\';
#else
#include <dlfcn.h>
#define HOSTFXR_CALLTYPE
#define STR(s) s
using char_t = char;
static void* OpenLibrary(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
static void* Symbol(void* lib, const char* name) { return dlsym(lib, name); }
static const char* HostfxrName = "libhostfxr.so";
static const char Slash = '/';
#endif

namespace RunnerCompiler {
namespace {
// hostfxr.h / coreclr_delegates.h (MIT, dotnet/runtime), reduced to what is used here.
struct hostfxr_initialize_parameters { size_t size; const char_t* host_path; const char_t* dotnet_root; };
using hostfxr_handle = void*;
using init_cmdline_fn = int32_t(HOSTFXR_CALLTYPE*)(int argc, const char_t** argv,
    const hostfxr_initialize_parameters* parameters, hostfxr_handle* handle);
using get_delegate_fn = int32_t(HOSTFXR_CALLTYPE*)(hostfxr_handle, int type, void** delegate);
using close_fn = int32_t(HOSTFXR_CALLTYPE*)(hostfxr_handle);
constexpr int hdt_get_function_pointer = 6;
using get_function_pointer_fn = int(HOSTFXR_CALLTYPE*)(const char_t* type_name, const char_t* method_name,
    const char_t* delegate_type_name, void* load_context, void* reserved, void** delegate);
#define UNMANAGEDCALLERSONLY_METHOD ((const char_t*)-1)

std::string Hex(int32_t rc) { char b[16]; snprintf(b, sizeof b, "0x%08x", static_cast<uint32_t>(rc)); return b; }
#ifdef _WIN32
std::wstring Wide(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}
#else
std::string Wide(const char* s) { return s; }
#endif
}

bool ClrHost::Start(const PathString& appDir, const PathString& appName) {
    if (getFunctionPointer_) return true;
    const PathString dir = appDir.empty() || appDir.back() == Slash ? appDir : appDir + Slash;
    void* fxr = OpenLibrary(dir + HostfxrName);
    if (!fxr) { error_ = "hostfxr not found in the compiler folder"; return false; }
    auto init = reinterpret_cast<init_cmdline_fn>(Symbol(fxr, "hostfxr_initialize_for_dotnet_command_line"));
    auto getDelegate = reinterpret_cast<get_delegate_fn>(Symbol(fxr, "hostfxr_get_runtime_delegate"));
    auto close = reinterpret_cast<close_fn>(Symbol(fxr, "hostfxr_close"));
    if (!init || !getDelegate || !close) { error_ = "hostfxr exports missing"; return false; }
    const PathString app = dir + appName + STR(".dll");
#ifdef _WIN32
    const PathString host = dir + appName + L".exe";
#else
    const PathString host = dir + appName;
#endif
    // Self-contained: the app folder is both the app base and the runtime root.
    hostfxr_initialize_parameters params{sizeof(params), host.c_str(), dir.c_str()};
    const char_t* argv[] = {app.c_str()};
    hostfxr_handle handle = nullptr;
    int32_t rc = init(1, argv, &params, &handle);
    if (rc < 0 || !handle) { error_ = "hostfxr_initialize_for_dotnet_command_line failed " + Hex(rc); return false; }
    rc = getDelegate(handle, hdt_get_function_pointer, &getFunctionPointer_);
    close(handle); // the runtime stays loaded; the handle is only needed to obtain delegates
    if (rc < 0 || !getFunctionPointer_) { error_ = "hdt_get_function_pointer failed " + Hex(rc); getFunctionPointer_ = nullptr; return false; }
    return true;
}

void* ClrHost::Function(const char* typeName, const char* method) {
    if (!getFunctionPointer_) { error_ = "runtime not started"; return nullptr; }
    void* fn = nullptr;
    const auto type = Wide(typeName), name = Wide(method);
    int rc = reinterpret_cast<get_function_pointer_fn>(getFunctionPointer_)(type.c_str(), name.c_str(),
        UNMANAGEDCALLERSONLY_METHOD, nullptr, nullptr, &fn);
    if (rc < 0 || !fn) { error_ = std::string("get_function_pointer(") + typeName + "::" + method + ") failed " + Hex(rc); return nullptr; }
    return fn;
}
}
