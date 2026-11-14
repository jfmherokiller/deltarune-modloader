// RunnerCompiler SPIKE (YYToolkit v5 plugin). Four experiments:
//  1. Bytecode dump: the first distinct scripts the runner executes (CCode* from EVENT_OBJECT_CALL)
//     have their in-memory VMBuffer copied to mods/aurie/runnercompiler/<name>.bin, so offline tools
//     can diff them against the data.win bytes the game was served and learn the load-time operand
//     fixup that injected bytecode must reproduce.
//  2. In-process compiler: the .NET runtime shipped in mods/tools/compiler is hosted via hostfxr on
//     a worker thread and LooseModCompiler.RuntimeExports.Compile compiles a test snippet.
//  3. Named capture: if mods/aurie/runnercompiler/capture.txt lists code-name prefixes (one per line),
//     the first EVENT_OBJECT_CALL walks the runner's global CCode list (all code entries, executed or
//     not, already fixed up by the CODE-chunk loader) and copies the bytecode of every matching entry
//     to runnercompiler/capture/<name>.bin + capture.tsv. Used for compiler/fixtures/runtime_bytecode.
//  4. Link and run: if mods/aurie/runnercompiler/link.txt exists, the objects it lists are run through
//     gmlrt::Runtime (modloader/include/gml_runtime.hpp), with checks; results in runnercompiler/link.log.
//     A .gmlo entry (compiler/GmlObjectCompiler output) is linked and run; a .gml entry is GML source,
//     compiled in-process (RuntimeExports.CompileObject) and run with Runtime::CompileAndRun on every
//     run. The same plugin and plan work on VM and YYC runners. In this mode experiment 1 is skipped.
//     Needs a YYToolkit with ExecuteCodeObject (fork, 85ce5b9+).
// Experiments 1-3 write nothing back into the game; experiment 4 executes the linked code.
// Results: aurie.log and mods/aurie/runnercompiler/report.txt (link.log for experiment 4).
#include <YYToolkit/YYTK_Shared.hpp>
#include "ClrHost.hpp"
#include <gml_runtime.hpp>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
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

// Named capture (experiment 3). CCode layout from RE/10_vm_and_scripts.md: next @8, VMBuffer* @104,
// name @128, scriptIndex @136, localCount @160, argCount @164, flags @168. The list head is a
// static of this exe build only (modloader/README.md "Addresses"), so the walk checks the stamp.
constexpr uint32_t kExeStamp = 0x685E7007;
constexpr uintptr_t kCCodeHeadRva = 0x8C9EC8, kCCodeCountRva = 0x8C9EBC;
struct Named { Dump d; uint64_t ccode = 0; int32_t scriptIndex = 0; uint32_t locals = 0, args = 0, flags = 0; };
std::vector<std::string> g_Prefixes;
std::vector<Named> g_Named;
std::string g_CaptureStatus;
std::atomic<bool> g_CaptureRequested{false}, g_CaptureDone{false};

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

void CaptureNamed() {
    auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    std::vector<Named> found;
    char status[256];
    if (nt->FileHeader.TimeDateStamp != kExeStamp) {
        snprintf(status, sizeof status, "named capture skipped: exe TimeDateStamp 0x%08lX, expected 0x%08X",
                 static_cast<unsigned long>(nt->FileHeader.TimeDateStamp), kExeStamp);
    } else {
        uint8_t* node = nullptr; uint32_t count = 0; size_t walked = 0;
        CopyGuarded(base + kCCodeHeadRva, &node, sizeof node);
        CopyGuarded(base + kCCodeCountRva, &count, sizeof count);
        for (; node && walked < 1000000; ++walked) {
            uint8_t cc[184]{};
            if (!CopyGuarded(node, cc, sizeof cc)) break;
            const char* name = nullptr; void* vm = nullptr;
            memcpy(&name, cc + 128, 8); memcpy(&vm, cc + 104, 8);
            char nameCopy[256]{};
            if (name && CopyGuarded(name, nameCopy, 255)) {
                bool match = false;
                for (auto& p : g_Prefixes) match |= strncmp(nameCopy, p.c_str(), p.size()) == 0;
                uint8_t header[32]{};
                if (match && vm && CopyGuarded(vm, header, sizeof header)) {
                    Named n; n.ccode = reinterpret_cast<uint64_t>(node); n.d.name = nameCopy;
                    memcpy(&n.scriptIndex, cc + 136, 4); memcpy(&n.locals, cc + 160, 4);
                    memcpy(&n.args, cc + 164, 4); memcpy(&n.flags, cc + 168, 4);
                    memcpy(&n.d.length, header + 8, 4); memcpy(&n.d.f0c, header + 12, 4); memcpy(&n.d.f10, header + 16, 4);
                    memcpy(&n.d.bytecode, header + 24, 8); n.d.buffer = reinterpret_cast<uint64_t>(vm);
                    if (n.d.length && n.d.length <= MaxBytes && n.d.bytecode) {
                        n.d.bytes.resize(n.d.length);
                        if (!CopyGuarded(reinterpret_cast<void*>(n.d.bytecode), n.d.bytes.data(), n.d.length)) n.d.bytes.clear();
                    }
                    found.push_back(std::move(n));
                }
            }
            memcpy(&node, cc + 8, 8);
        }
        snprintf(status, sizeof status, "named capture: walked %zu of %u CCode objects, %zu matched", walked, count, found.size());
    }
    std::lock_guard lock(g_Lock);
    g_Named = std::move(found);
    g_CaptureStatus = status;
    g_CaptureDone.store(true);
}

// Experiment 4: link and run compiled GML through gmlrt::Runtime (modloader/include/gml_runtime.hpp;
// its linker gmlo_link.hpp is shared with YYCVmProbe). Plan file runnercompiler/link.txt, one
// entry per line:  <file.gmlo | file.gml> <runs> [check ; check ...]
// with checks  "result <v>", "global <name> <v>", "self <name> <v>"  (v: number or text) and
// "debug <text>" (show_debug_message was called with exactly <text>),
// evaluated right after the last run. Each EVENT_OBJECT_CALL (after a warm-up) runs every object
// once against that event's self/other, so the runs spread over many events and frames.
std::wstring SafeFile(const std::string& name);
struct LinkCheck { std::string where, name, value; };
struct LinkJob {
    std::string file;
    int runs = 0, done = 0, failures = 0;
    std::vector<LinkCheck> checks;
    bool source = false;   // .gml: compiled in the game by Runtime::CompileAndRun
    std::string text;      // .gmlo text or GML source
    gmlo::Linked* linked = nullptr;
};
std::vector<LinkJob> g_LinkJobs;
bool g_LinkPlanned = false, g_LinkFinished = false;
uint64_t g_LinkEvents = 0;
size_t g_PrivateAtRun10 = 0;
uint32_t g_RunnerStrings = 0;
std::unique_ptr<gmlrt::Runtime> g_Runtime;   // game thread only
// In-process compiler for .gml entries: set by Worker once the CLR is up and the context is loaded.
std::atomic<gmlrt::CompileObjectFn> g_CompileObject{nullptr};
std::atomic<int> g_CompilerState{0};   // 0 starting, 1 ready, -1 failed
std::string DataWinPath();
std::ofstream g_LinkLog;
AurieModule* g_Module = nullptr;

// "debug <text>" checks: show_debug_message is hooked while a link plan runs and its messages
// are kept, so the check does not depend on where the game's debug output goes.
std::vector<std::string> g_DebugMessages;
TRoutine g_ShowDebugMessage = nullptr;
void HookShowDebugMessage(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args) {
    if (argc > 0 && args && g_DebugMessages.size() < 1000) g_DebugMessages.push_back(args[0].ToString());
    g_ShowDebugMessage(result, self, other, argc, args);
}

void LinkLog(const std::string& line) {
    g_LinkLog << line << "\n"; g_LinkLog.flush();
    DbgPrintEx(LOG_SEVERITY_INFO, "[RunnerCompiler] %s", line.substr(0, 400).c_str());
}

size_t PrivateBytes() {
    PROCESS_MEMORY_COUNTERS pmc{};   // PagefileUsage = private commit (the _EX field fails under Wine)
    pmc.cb = sizeof pmc;
    return K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc) ? pmc.PagefileUsage : 0;
}

// Whole file through Win32. Not std::ifstream: under Wine 11 (builtin msvcp140/ucrtbase) reading a
// text-mode ifstream in ModuleInitialize faulted in ucrtbase (_ungetc_nolock, write to NULL).
bool ReadWholeFile(const std::wstring& path, std::string& text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    text.clear();
    char buf[1 << 14];
    DWORD n = 0;
    bool ok = true;
    while ((ok = ReadFile(h, buf, sizeof buf, &n, nullptr) != FALSE) && n) text.append(buf, n);
    CloseHandle(h);
    return ok;
}

bool LoadLinkPlan(const std::wstring& dir) {
    std::string planText;
    if (!ReadWholeFile(dir + L"\\link.txt", planText)) return false;
    std::istringstream plan(planText);
    for (std::string line; std::getline(plan, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        LinkJob job;
        std::istringstream in(line);
        in >> job.file >> job.runs;
        std::string rest;
        std::getline(in, rest);
        for (size_t start = 0; start < rest.size();) {
            size_t semi = rest.find(';', start);
            std::istringstream check(rest.substr(start, semi == std::string::npos ? std::string::npos : semi - start));
            LinkCheck c;
            check >> c.where;
            if (c.where == "global" || c.where == "self") check >> c.name;
            std::getline(check >> std::ws, c.value);
            while (!c.value.empty() && c.value.back() == ' ') c.value.pop_back();
            if (!c.where.empty()) job.checks.push_back(c);
            if (semi == std::string::npos) break;
            start = semi + 1;
        }
        const bool f = ReadWholeFile(dir + L"\\" + std::wstring(job.file.begin(), job.file.end()), job.text);
        job.source = job.file.size() > 4 && job.file.compare(job.file.size() - 4, 4, ".gml") == 0;
        gmlo::Object parsed;
        std::string error;
        if (!f || (!job.source && !gmlo::Parse(job.text, parsed, error))) {
            LinkLog("link plan: " + job.file + ": " + (f ? error : std::string("cannot read")));
            continue;
        }
        g_LinkJobs.push_back(std::move(job));
    }
    return true;
}

bool Matches(const RValue& v, const std::string& expected) {
    char* end = nullptr;
    double want = strtod(expected.c_str(), &end);
    if (end && *end == 0 && !expected.empty()) {
        const auto kind = v.m_Kind & 0xFFFFFF;
        return (kind == VALUE_REAL || kind == VALUE_INT32 || kind == VALUE_INT64 || kind == VALUE_BOOL) && v.ToDouble() == want;
    }
    return (v.m_Kind & 0xFFFFFF) == VALUE_STRING && v.ToString() == expected;
}

void RunChecks(LinkJob& job, CInstance* self, const RValue& result) {
    for (auto& c : job.checks) {
        std::string got = "(missing)";
        bool ok = false;
        if (c.where == "result") { got = result.ToString(); ok = Matches(result, c.value); }
        else if (c.where == "debug") {
            for (auto& m : g_DebugMessages) if (m == c.value) { got = m; ok = true; }
            if (!ok) got = std::to_string(g_DebugMessages.size()) + " message(s), none matching";
        }
        else {
            CInstance* target = self;
            if (c.where == "global") g_Yytk->GetGlobalInstance(&target);
            RValue* member = nullptr;
            if (target && AurieSuccess(g_Yytk->GetInstanceMember(RValue(target), c.name.c_str(), member)) && member) {
                got = member->ToString(); ok = Matches(*member, c.value);
            }
        }
        LinkLog(std::string("  check ") + (ok ? "PASS" : "FAIL") + ": " + c.where + (c.name.empty() ? "" : " " + c.name) +
                " == " + c.value + " (got " + got + ")");
        job.failures += !ok;
    }
}

void LinkStep(CInstance* self, CInstance* other, CCode* code) {
    if (g_LinkFinished || ++g_LinkEvents < 200) return;   // let the game finish its first room
    // Object events only: their self is a real instance (built-ins like x are valid on it).
    char name[12]{};
    if (!code || !code->m_Name || !CopyGuarded(code->m_Name, name, 11) || strncmp(name, "gml_Object_", 11) != 0) return;
    if (!g_LinkPlanned) {
        const bool needsCompiler = std::any_of(g_LinkJobs.begin(), g_LinkJobs.end(), [](auto& j) { return j.source; });
        if (needsCompiler && g_CompilerState.load() == 0) return;   // the CLR is still starting
        g_LinkPlanned = true;
        g_Runtime = std::make_unique<gmlrt::Runtime>(g_Yytk, g_CompileObject.load(), DataWinPath());
        if (std::string error; !g_Runtime->Ready(error)) {
            LinkLog("link: " + error + "; nothing linked");
            g_LinkFinished = true;
            return;
        }
        auto& strings = g_Runtime->Linker().Strings();
        if (!strings.Discover()) LinkLog("link: string table not found (" + strings.error + "); string relocations will fail");
        const uint32_t runnerStrings = strings.Count();
        char info[256];
        snprintf(info, sizeof info, "link: %s runner, data.win image %p, string table slot %p (exe+0x%llx), %u strings",
                 g_Runtime->IsYYC() ? "YYC" : "VM", strings.Image(), static_cast<void*>(strings.TableSlot()),
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(strings.TableSlot()) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))),
                 runnerStrings);
        LinkLog(info);
        PVOID showDebug = nullptr;
        if (AurieSuccess(g_Yytk->GetNamedRoutinePointer("show_debug_message", &showDebug)) && showDebug &&
            AurieSuccess(MmCreateHook(g_Module, "show_debug_message", showDebug, reinterpret_cast<PVOID>(HookShowDebugMessage),
                                      reinterpret_cast<PVOID*>(&g_ShowDebugMessage))))
            LinkLog("link: show_debug_message hooked for \"debug\" checks");
        else LinkLog("link: show_debug_message hook FAILED; \"debug\" checks will fail");
        // Self-test: the YYTK lookups gmlo::Linker uses give the ids the launcher's own bytecode
        // uses (compiler/fixtures/runtime_bytecode/launcher/links.json). Only on that exe.
        const auto* exeNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr)) +
                                                                      reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr))->e_lfanew);
        if (exeNt->FileHeader.TimeDateStamp == kExeStamp) {
            std::string selfTest = "link: resolver self-test (launcher fixture ids):";
            CInstance* global = nullptr;
            g_Yytk->GetGlobalInstance(&global);
            for (auto [kind, name, want] : {std::tuple{'b', "room_speed", 136}, {'b', "x", 0}, {'v', "i", 100049},
                                           {'f', "method", 162}, {'f', "abs", 200}, {'f', "@@NewGMLArray@@", 2253},
                                           {'f', "gml_Script_strlen", 100081}}) {
                int32_t id = -1;
                size_t builtin = 0;
                bool ok = kind == 'b' ? AurieSuccess(g_Yytk->GetBuiltinVariableIndex(name, builtin))
                        : kind == 'v' ? AurieSuccess(g_Yytk->GetVariableSlot(RValue(global), name, id))
                                      : AurieSuccess(g_Yytk->GetNamedRoutineIndex(name, &id));
                if (kind == 'b') id = static_cast<int32_t>(builtin);
                selfTest += std::string(" ") + name + "=" + (ok ? std::to_string(id) : "?") + (ok && id == want ? "" : "(!=" + std::to_string(want) + ")");
            }
            LinkLog(selfTest);
        }
        if (needsCompiler) LinkLog(std::string("link: in-process compiler ") + (g_CompileObject.load() ? "ready" : "FAILED (see report.txt)"));
        for (auto& job : g_LinkJobs) {
            std::string error;
            const auto t0 = std::chrono::steady_clock::now();
            job.linked = job.source ? g_Runtime->Compile(job.text, error) : g_Runtime->Load(job.text, error);
            const long long us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
            char line[512];
            if (job.linked)
                snprintf(line, sizeof line, "link: %s -> %s, %zu bytes, %d new strings, %d script calls lowered, CCode %p (%s in %lld us)",
                         job.file.c_str(), job.linked->name.c_str(), job.linked->words * 4, job.linked->newStrings,
                         job.linked->loweredScriptCalls, job.linked->ccode, job.source ? "compiled + linked" : "linked", us);
            else snprintf(line, sizeof line, "link: %s FAILED: %s", job.file.c_str(), error.c_str());
            LinkLog(line);
            if (job.linked) {   // linked/<stem>.bin: the bytes as linked (compiler/tests/test_gml_object.py)
                CreateDirectoryW((g_OutDir + L"\\linked").c_str(), nullptr);
                std::string stem = job.file.substr(0, job.file.rfind('.'));
                std::ofstream bin(g_OutDir + L"\\linked\\" + SafeFile(stem) + L".bin", std::ios::binary | std::ios::trunc);
                bin.write(reinterpret_cast<const char*>(job.linked->code), static_cast<std::streamsize>(job.linked->words * 4));
            }
        }
        g_RunnerStrings = runnerStrings;
        LinkLog("link: " + std::to_string(runnerStrings) + " runner strings, " + std::to_string(strings.Count() - runnerStrings) +
                " appended; private bytes " + std::to_string(PrivateBytes()));
        return;
    }
    bool pending = false;
    for (auto& job : g_LinkJobs) {
        if (!job.linked || job.done >= job.runs) continue;
        // Relinking an identical object (recompiling the same source) must hit the cache: no new
        // CCode, no new strings. A .gml entry runs through the single entry point every time.
        std::string error;
        RValue result;
        bool ok;
        if (job.source) {
            if (g_Runtime->Compile(job.text, error) != job.linked) { ++job.failures; LinkLog("link: " + job.file + " recompile missed the cache"); }
            ok = g_Runtime->CompileAndRun(job.text, self, result, error, other);
        } else {
            if (g_Runtime->Load(job.text, error) != job.linked) { ++job.failures; LinkLog("link: " + job.file + " relink missed the cache"); }
            ok = g_Runtime->Run(job.linked, self, other, result, error);
        }
        if (!ok) {
            ++job.failures;
            LinkLog("run: " + job.file + " #" + std::to_string(job.done + 1) + " FAILED: " + error);
            if (error.rfind("exception", 0) == 0) { LinkLog("link: stopping after a fault"); g_LinkFinished = true; return; }
        }
        if (++job.done == 1 || job.done == job.runs)
            LinkLog("run: " + job.file + " #" + std::to_string(job.done) + " -> " + result.GetKindName() + " " + result.ToString());
        if (job.done == job.runs) RunChecks(job, self, result);
        pending |= job.done < job.runs;
    }
    size_t round = 0;
    for (auto& job : g_LinkJobs) round = std::max<size_t>(round, job.done);
    if (round == 10 && !g_PrivateAtRun10) g_PrivateAtRun10 = PrivateBytes();
    static size_t lastSample = 0;
    if (round != lastSample && (round == 10 || round % 100 == 0)) {
        lastSample = round;
        LinkLog("mem: run " + std::to_string(round) + ", private bytes " + std::to_string(PrivateBytes()));
    }
    if (!pending) {
        g_LinkFinished = true;
        int failures = 0, linked = 0;
        for (auto& job : g_LinkJobs) { failures += job.failures + !job.linked; linked += job.linked != nullptr; }
        const size_t now = PrivateBytes();
        LinkLog("link: done, " + std::to_string(linked) + "/" + std::to_string(g_LinkJobs.size()) + " objects linked, " +
                std::to_string(g_Runtime->Linker().LinkedCount()) + " CCode objects, " + std::to_string(g_Runtime->Linker().Strings().Count() - g_RunnerStrings) +
                " appended strings, private bytes " + std::to_string(g_PrivateAtRun10) + " at run 10 -> " + std::to_string(now) +
                " (" + std::to_string(static_cast<long long>(now) - static_cast<long long>(g_PrivateAtRun10)) + "), " +
                std::to_string(failures) + " failure(s)");
    }
}

void CodeCallback(FWCodeEvent& event) {
    if (!g_Prefixes.empty() && !g_CaptureRequested.exchange(true)) CaptureNamed();
    auto& [self, other, code, argc, args] = event.Arguments();
    if (!g_LinkJobs.empty()) LinkStep(self, other, code);
    if (g_Done.load(std::memory_order_relaxed)) return;
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
    std::mutex logLock;
    auto log = [&](const std::string& line) {
        std::lock_guard lock(logLock);
        report << line << "\n"; report.flush();
        DbgPrintEx(LOG_SEVERITY_INFO, "[RunnerCompiler] %s", line.substr(0, 400).c_str());
    };
    log("==== spike start, data.win " + DataWinPath());
    // Experiment 3: written as soon as the game thread has walked the CCode list.
    std::thread capture([&log] {
        if (g_Prefixes.empty()) return;
        for (int tick = 0; tick < 600 && !g_CaptureDone.load(); ++tick) Sleep(100);
        if (!g_CaptureDone.load()) { log("named capture: no EVENT_OBJECT_CALL within 60 s"); return; }
        std::vector<Named> named; std::string status;
        { std::lock_guard lock(g_Lock); named.swap(g_Named); status = g_CaptureStatus; }
        const std::wstring dir = g_OutDir + L"\\capture";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::ofstream tsv(dir + L"\\capture.tsv", std::ios::trunc);
        tsv << "name\tlength\tvm_f0c\tvm_f10\tccode\tvmbuffer\tbytecode\tscript_index\tlocals\targs\tflags\n";
        for (auto& n : named) {
            std::ofstream f(dir + L"\\" + SafeFile(n.d.name) + L".bin", std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(n.d.bytes.data()), std::streamsize(n.d.bytes.size()));
            char line[640];
            snprintf(line, sizeof line, "%s\t%u\t%u\t%u\t0x%llx\t0x%llx\t0x%llx\t%d\t%u\t%u\t0x%x\n", n.d.name.c_str(), n.d.length,
                     n.d.f0c, n.d.f10, static_cast<unsigned long long>(n.ccode), static_cast<unsigned long long>(n.d.buffer),
                     static_cast<unsigned long long>(n.d.bytecode), n.scriptIndex, n.locals, n.args, n.flags);
            tsv << line;
        }
        log(status + " -> " + Narrow(dir));
    });
    // Experiment 2 first: it is independent of the game thread.
    auto t0 = std::chrono::steady_clock::now();
    auto ms = [&] { return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()); };
    RunnerCompiler::ClrHost host;
    const bool linkMode = !g_LinkJobs.empty();
    if (!host.Start(g_CompilerDir, L"LooseModCompiler")) { log("CLR start FAILED: " + host.Error()); g_CompilerState.store(-1); }
    else if (linkMode) {
        // Link mode: publish CompileObject for gmlrt::Runtime once the compile context is loaded.
        auto fn = reinterpret_cast<gmlrt::CompileObjectFn>(host.Function(gmlrt::kCompilerType, gmlrt::kCompileObject));
        if (!fn) { log("CompileObject lookup FAILED: " + host.Error()); g_CompilerState.store(-1); }
        else {
            std::string info;
            const bool ok = gmlrt::Runtime(nullptr, fn, DataWinPath()).Warm(info);
            log("CLR up, compile context loaded after " + ms() + " ms: " + info);
            if (ok) g_CompileObject.store(fn);
            g_CompilerState.store(ok ? 1 : -1);
        }
    } else {
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
    if (!g_LinkJobs.empty()) { g_Done.store(true); capture.join(); log("link mode: bytecode dump skipped"); return; }
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
    capture.join();
    log("bytecode dump: " + std::to_string(written) + " scripts in " + Narrow(g_OutDir));
}
}

EXPORTED AurieStatus ModuleInitialize(IN AurieModule* Module, IN const fs::path& ModulePath) {
    g_Yytk = GetInterface();
    if (!g_Yytk) return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
    g_Module = Module;
    // mods/aurie/RunnerCompiler.dll -> mods/aurie/runnercompiler/, mods/tools/compiler/
    const auto aurieDir = ModulePath.parent_path();
    g_OutDir = (aurieDir / L"runnercompiler").wstring();
    g_CompilerDir = (aurieDir.parent_path() / L"tools" / L"compiler").wstring();
    CreateDirectoryW(g_OutDir.c_str(), nullptr);
    if (std::string text; ReadWholeFile(g_OutDir + L"\\capture.txt", text)) {
        std::istringstream prefixes(text);
        for (std::string line; std::getline(prefixes, line);) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty() && line[0] != '#') g_Prefixes.push_back(line);
        }
    }
    if (GetFileAttributesW((g_OutDir + L"\\link.txt").c_str()) != INVALID_FILE_ATTRIBUTES) {
        g_LinkLog.open(g_OutDir + L"\\link.log", std::ios::trunc);
        LoadLinkPlan(g_OutDir);
        LinkLog("link plan: " + std::to_string(g_LinkJobs.size()) + " object(s)");
        if (!g_LinkJobs.empty()) g_Done.store(true);   // no bytecode dump in link mode
    }
    const auto status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
    if (!AurieSuccess(status)) return status;
    std::thread(Worker).detach();
    return AURIE_SUCCESS;
}
