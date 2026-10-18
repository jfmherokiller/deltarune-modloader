#pragma once
#include "RuntimeBridge.hpp"
#include "InspectorProtocol.hpp"
#include <atomic>
#include <memory>
namespace RunnerEditor {
std::wstring InspectorPipeName(DWORD pid);
std::wstring CheatEnginePipeName(DWORD pid); // address-bearing, length-prefixed frames
struct InspectJob {
    enum Stage { Queued, Dispatched, Ready, Expired };
    InspectRequest request;
    uint64_t deadline = 0;
    std::atomic<Stage> stage{Queued};
    InspectResponse response;
    HANDLE done = nullptr;
    InspectJob();
    ~InspectJob();
};
// Data-only transport: no YYTK dependency, no engine work on the worker.
class InspectorBridge {
public:
    explicit InspectorBridge(bool cheatEngine = false);
    ~InspectorBridge();
    InspectorBridge(const InspectorBridge&) = delete;
    InspectorBridge& operator=(const InspectorBridge&) = delete;
    bool Start();
    bool Stop();
    std::shared_ptr<InspectJob> Take(); // game-thread, non-blocking, expiry recheck
    void Complete(const std::shared_ptr<InspectJob>& job, InspectResponse response);
private:
    struct State;
    State* state_;
    static unsigned __stdcall Worker(void*);
};
}
