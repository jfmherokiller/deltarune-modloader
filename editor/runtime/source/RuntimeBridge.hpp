#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <string>
namespace RunnerEditor {
constexpr size_t MaxMessageBytes = 4096;
// From the GML built-in code_is_compiled(): YYC runners run native code, VM runners bytecode.
enum class RunnerType : uint8_t { Unknown, VM, YYC };
struct Snapshot {
    char room[513]{};
    bool hasRoom = false;
    bool inspectVariables = false; // Advertise only after independent inspector starts.
    RunnerType runnerType = RunnerType::Unknown;
    uint64_t events = 0, sampledAt = 0, sampledTick = 0;
};
uint64_t EpochMilliseconds();
std::wstring PipeName(DWORD pid);
// Data-only cache and local transport; this translation unit NEVER uses YYTK.
class RuntimeBridge {
public:
    RuntimeBridge();
    ~RuntimeBridge();
    RuntimeBridge(const RuntimeBridge&) = delete;
    RuntimeBridge& operator=(const RuntimeBridge&) = delete;
    bool Start();
    bool Stop(); // <=1500ms join; false means resources are retained, never freed under worker.
    void Publish(const Snapshot& snapshot);
private:
    struct State;
    State* state_;
    static unsigned __stdcall Worker(void* context);
};
}
