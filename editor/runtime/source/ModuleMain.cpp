// RunnerEditor: generic, read-only YYToolkit v5 runtime telemetry.
// No game-specific objects/globals, patches, eval, asset replacement, or UI hooks.
// RunnerCE-<pid> adds RValue/CInstance addresses for the Cheat Engine dissector; CE does any writing.
#include <YYToolkit/YYTK_Shared.hpp>
#include "RuntimeBridge.hpp"
#include "InspectorBridge.hpp"
#include "InspectorSampler.hpp"
#include <atomic>
#include <cstring>
using namespace Aurie;
using namespace YYTK;
using namespace RunnerEditor;

static YYTKInterface* g_Yytk = nullptr;
// Process-lifetime allocation by design: pinning protects in-flight callback/worker
// instructions even when Aurie unloads a module while the game thread is parked.
static RuntimeBridge* g_Bridge = nullptr;
static InspectorBridge* g_Inspector = nullptr;
static InspectorBridge* g_CheatEngine = nullptr; // RunnerCE-<pid>: same sampler, RValue addresses for CE
static bool g_InspectorStarted = false; // fixed before callback registration
static bool g_CheatEngineStarted = false;
static bool g_InspectorFault = false; // established game-thread only
static bool g_TelemetryFault = false; // independent from inspector failure
static std::atomic<bool> g_Stopping{true};
static std::atomic<DWORD> g_GameThread{0};
static std::atomic<uint32_t> g_ActiveCallbacks{0};
static std::atomic<uint64_t> g_Events{0};
static thread_local bool g_InCallback = false;
static uint64_t g_LastSample = 0; // accessed only by the established game callback thread

static void Sample() {
    Snapshot sample{};
    sample.inspectVariables = g_InspectorStarted;
    sample.events = g_Events.load(std::memory_order_relaxed);
    RValue room, name;
    if (AurieSuccess(g_Yytk->GetBuiltin("room", nullptr, NULL_INDEX, room)) &&
        AurieSuccess(g_Yytk->CallBuiltinEx(name, "room_get_name", nullptr, nullptr, {room})) &&
        name.m_Kind == VALUE_STRING) {
        const std::string text = name.ToString();
        // Reject oversized strings rather than splitting UTF-8 or growing the wire frame.
        if (text.size() <= 512) {
            memcpy(sample.room, text.data(), text.size());
            sample.room[text.size()] = '\0';
            sample.hasRoom = true;
        }
    }
    sample.sampledAt = EpochMilliseconds();
    sample.sampledTick = GetTickCount64();
    g_Bridge->Publish(sample); // POD only: no engine pointers cross into the worker.
}
// Like the existing generic YYTK probe, keep SEH in a frame without C++ destructors.
static bool SampleProtected() {
    __try { Sample(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void Serve(InspectorBridge* bridge);
static void CodeCallback(FWCodeEvent&) {
    if (g_Stopping.load(std::memory_order_acquire) || g_InCallback) return;
    const DWORD current = GetCurrentThreadId();
    DWORD expected = 0;
    g_GameThread.compare_exchange_strong(expected, current);
    if (g_GameThread.load() != current) return; // never read runner state on another thread
    g_ActiveCallbacks.fetch_add(1);
    if (g_Stopping.load(std::memory_order_acquire)) {
        g_ActiveCallbacks.fetch_sub(1); return;
    }
    g_InCallback = true;
    g_Events.fetch_add(1, std::memory_order_relaxed);
    const uint64_t now = GetTickCount64();
    if (!g_TelemetryFault && (!g_LastSample || now - g_LastSample >= 250)) {
        g_LastSample = now;
        try {
            if (!SampleProtected()) g_TelemetryFault = true;
        } catch (...) {
            // Stop sampling, leaving the last cached snapshot to age out. No console
            // output, filesystem operations, or worker management on the game thread.
            g_TelemetryFault = true;
        }
    }
    if (g_InspectorStarted) Serve(g_Inspector);
    if (g_CheatEngineStarted) Serve(g_CheatEngine);
    g_InCallback = false;
    g_ActiveCallbacks.fetch_sub(1);
}
static void Serve(InspectorBridge* bridge) {
    {
        if (auto job = bridge->Take()) {
            InspectResponse response;
            try {
                if (!g_InspectorFault) g_InspectorFault = !SampleInspector(g_Yytk,job->request,response);
                else {
                    response.request=job->request; response.pid=GetCurrentProcessId();
                    response.sampledAt=EpochMilliseconds(); response.sampledTick=GetTickCount64();
                    response.error="Inspector disabled after an unsafe runner read";
                }
                bridge->Complete(job,std::move(response));
            } catch (...) {
                // Allocation/adapter failure cannot strand the callback reentrancy
                // guard or stop telemetry. Complete the job as an expired response.
                g_InspectorFault=true;
                job->stage.store(InspectJob::Expired); SetEvent(job->done);
            }
        }
    }
}

EXPORTED AurieStatus ModuleInitialize(IN AurieModule* Module, IN const fs::path& ModulePath) {
    UNREFERENCED_PARAMETER(ModulePath);
    if (g_Bridge) return AURIE_MODULE_INITIALIZATION_FAILED; // pinned hot reload is unsupported
    g_Yytk = GetInterface();
    if (!g_Yytk) return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
    short major = 0, minor = 0, patch = 0;
    g_Yytk->QueryVersion(major, minor, patch);
    if (major != 5) return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
    // Framework interface lookup/registration occur in Aurie's init context. All
    // runner data reads, RValue construction/destruction and conversions are callback-only.
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&ModuleInitialize), &self)) return AURIE_MODULE_INITIALIZATION_FAILED;
    try { g_Bridge = new RuntimeBridge; }
    catch (...) { return AURIE_MODULE_INITIALIZATION_FAILED; }
    if (!g_Bridge->Start()) return AURIE_MODULE_INITIALIZATION_FAILED;
    // Optional service: failure to create this pipe never disables telemetry v1.
    try { g_Inspector = new InspectorBridge; g_InspectorStarted = g_Inspector->Start(); }
    catch (...) { g_InspectorStarted = false; }
    try { g_CheatEngine = new InspectorBridge(true); g_CheatEngineStarted = g_CheatEngine->Start(); }
    catch (...) { g_CheatEngineStarted = false; }
    const auto status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
    if (!AurieSuccess(status)) { if (g_CheatEngine) g_CheatEngine->Stop(); if (g_Inspector) g_Inspector->Stop(); g_Bridge->Stop(); return status; }
    g_Stopping.store(false, std::memory_order_release);
    return AURIE_SUCCESS;
}

EXPORTED AurieStatus ModuleUnload(IN AurieModule* Module, IN const fs::path& ModulePath) {
    UNREFERENCED_PARAMETER(Module); UNREFERENCED_PARAMETER(ModulePath);
    g_Stopping.store(true, std::memory_order_release);
    // YYTK's ModuleOperationCallback already removes this module's registrations
    // before ModuleUnload. Do NOT mutate its unprotected callback vector ourselves.
    // A parked/in-flight game callback cannot be forcibly joined: retain the pinned
    // code and cache instead. Worker Stop is independently bounded and cancellable.
    if (g_CheatEngine) g_CheatEngine->Stop();
    if (g_Inspector) g_Inspector->Stop();
    if (g_Bridge) g_Bridge->Stop();
    return AURIE_SUCCESS;
}
