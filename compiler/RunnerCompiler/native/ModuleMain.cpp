// RunnerCompiler SPIKE (YYToolkit v5 plugin). Read-only toward the game. Two experiments:
//  1. Bytecode dump: the first distinct scripts the runner executes (CCode* from EVENT_OBJECT_CALL)
//     have their in-memory VMBuffer copied to mods/aurie/runnercompiler/<name>.bin, so offline tools
//     can diff them against the data.win bytes the game was served and learn the load-time operand
//     fixup that injected bytecode must reproduce.
//  2. In-process compiler: the .NET runtime shipped in mods/tools/compiler is hosted via hostfxr on
//     a worker thread and LooseModCompiler.RuntimeExports.Compile compiles a test snippet.
// Nothing is written back into the game. Results: aurie.log and mods/aurie/runnercompiler/report.txt.
#include <YYToolkit/YYTK_Shared.hpp>
#include "ClrHost.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
using namespace Aurie;
using namespace YYTK;

namespace {
YYTKInterface* g_Yytk = nullptr;
std::wstring g_OutDir, g_CompilerDir;
constexpr size_t MaxDumps = 400, MaxBytes = 1 << 16;

// GameMaker VM runner layout (RE/10_vm_and_scripts.md): VMBuffer { vtable; uint32 length @8;
// uint32 locals @12; uint32 args @16; uint8* bytecode @24 }. CCode::m_VmInstance is the VMBuffer*.
struct Dump { std::string name; uint32_t length = 0, f0c = 0, f10 = 0; uint64_t buffer = 0, bytecode = 0; std::vector<uint8_t> bytes; };
std::mutex g_Lock;
std::vector<Dump> g_Pending;
std::set<uintptr_t> g_Seen; // game thread only
std::atomic<bool> g_Done{false};

bool CopyGuarded(const void* src, void* dst, size_t n) {
    __try { memcpy(dst, src, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Capture(CCode* code) {
    if (!code || g_Seen.size() >= MaxDumps || !g_Seen.insert(reinterpret_cast<uintptr_t>(code)).second) return;
    const char* name = nullptr; void* vm = nullptr;
    if (!CopyGuarded(&code->m_Name, &name, sizeof name) || !CopyGuarded(&code->m_VmInstance, &vm, sizeof vm) || !vm || !name) return;
    uint8_t header[32]{};
    if (!CopyGuarded(vm, header, sizeof header)) return;
    Dump d;
    char nameCopy[256]{};
    if (!CopyGuarded(name, nameCopy, 255)) return;
    d.name = nameCopy;
    memcpy(&d.length, header + 8, 4); memcpy(&d.f0c, header + 12, 4); memcpy(&d.f10, header + 16, 4);
    memcpy(&d.bytecode, header + 24, 8); d.buffer = reinterpret_cast<uint64_t>(vm);
    if (!d.length || d.length > MaxBytes || !d.bytecode) return;
    d.bytes.resize(d.length);
    if (!CopyGuarded(reinterpret_cast<void*>(d.bytecode), d.bytes.data(), d.length)) return;
    std::lock_guard lock(g_Lock);
    g_Pending.push_back(std::move(d));
}

void CodeCallback(FWCodeEvent& event) {
    if (g_Done.load(std::memory_order_relaxed)) return;
    auto& [self, other, code, argc, args] = event.Arguments();
    Capture(code);
    if (g_Seen.size() >= MaxDumps) g_Done.store(true);
}

std::string Narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring SafeFile(const std::string& name) {
    std::wstring w;
    for (char c : name) w += (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? wchar_t(c) : L'_';
    return w;
}

// data.win of this process: "-game <path>" on the command line, else data.win next to the exe.
std::string DataWinPath() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring path;
    for (int i = 1; argv && i + 1 < argc; ++i) if (_wcsicmp(argv[i], L"-game") == 0) path = argv[i + 1];
    if (argv) LocalFree(argv);
    if (path.empty()) path = L"data.win";
    wchar_t full[MAX_PATH * 2]{};
    GetFullPathNameW(path.c_str(), MAX_PATH * 2, full, nullptr);
    return Narrow(full);
}

void Worker() {
    std::ofstream report(g_OutDir + L"\\report.txt", std::ios::app);
    auto log = [&](const std::string& line) {
        report << line << "\n"; report.flush();
        DbgPrintEx(LOG_SEVERITY_INFO, "[RunnerCompiler] %s", line.substr(0, 400).c_str());
    };
    log("==== spike start, data.win " + DataWinPath());
    // Experiment 2 first: it is independent of the game thread.
    auto t0 = std::chrono::steady_clock::now();
    auto ms = [&] { return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()); };
    RunnerCompiler::ClrHost host;
    if (!host.Start(g_CompilerDir, L"LooseModCompiler")) log("CLR start FAILED: " + host.Error());
    else {
        using PingFn = int (*)(char*, int);
        using CompileFn = int (*)(const char*, const char*, const char*, char*, int);
        std::vector<char> out(1 << 16);
        if (auto ping = reinterpret_cast<PingFn>(host.Function("LooseModCompiler.RuntimeExports, LooseModCompiler", "Ping"))) {
            ping(out.data(), int(out.size()));
            log("CLR up after " + ms() + " ms: " + out.data());
        } else log("Ping lookup FAILED: " + host.Error());
        if (auto compile = reinterpret_cast<CompileFn>(host.Function("LooseModCompiler.RuntimeExports, LooseModCompiler", "Compile"))) {
            const char* gml = "if (keyboard_check_pressed(vk_f10))\n    show_debug_message(\"compiled in-process: \" + room_get_name(room));\n";
            for (int round = 0; round < 2; ++round) {
                t0 = std::chrono::steady_clock::now();
                int rc = compile(DataWinPath().c_str(), "gml_Object_obj_time_Step_1", gml, out.data(), int(out.size()));
                log("compile round " + std::to_string(round) + " rc=" + std::to_string(rc) + " in " + ms() + " ms: " +
                    std::string(out.data()).substr(0, std::string(out.data()).find('\n')));
            }
        } else log("Compile lookup FAILED: " + host.Error());
    }
    // Experiment 1: flush captured bytecode until the cap is reached (or 60 s pass).
    size_t written = 0;
    std::ofstream index(g_OutDir + L"\\index.tsv", std::ios::trunc);
    index << "name\tlength\tf0c\tf10\tvmbuffer\tbytecode\n";
    for (int tick = 0; tick < 600; ++tick) {
        std::vector<Dump> batch;
        { std::lock_guard lock(g_Lock); batch.swap(g_Pending); }
        for (auto& d : batch) {
            std::ofstream f(g_OutDir + L"\\" + SafeFile(d.name) + L".bin", std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(d.bytes.data()), std::streamsize(d.bytes.size()));
            char line[512];
            snprintf(line, sizeof line, "%s\t%u\t%u\t%u\t0x%llx\t0x%llx\n", d.name.c_str(), d.length, d.f0c, d.f10,
                     static_cast<unsigned long long>(d.buffer), static_cast<unsigned long long>(d.bytecode));
            index << line; ++written;
        }
        index.flush();
        if (g_Done.load() && batch.empty()) break;
        Sleep(100);
    }
    g_Done.store(true);
    log("bytecode dump: " + std::to_string(written) + " scripts in " + Narrow(g_OutDir));
}
}

EXPORTED AurieStatus ModuleInitialize(IN AurieModule* Module, IN const fs::path& ModulePath) {
    g_Yytk = GetInterface();
    if (!g_Yytk) return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
    // mods/aurie/RunnerCompiler.dll -> mods/aurie/runnercompiler/, mods/tools/compiler/
    const auto aurieDir = ModulePath.parent_path();
    g_OutDir = (aurieDir / L"runnercompiler").wstring();
    g_CompilerDir = (aurieDir.parent_path() / L"tools" / L"compiler").wstring();
    CreateDirectoryW(g_OutDir.c_str(), nullptr);
    const auto status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
    if (!AurieSuccess(status)) return status;
    std::thread(Worker).detach();
    return AURIE_SUCCESS;
}
