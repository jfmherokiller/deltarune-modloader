#include "RuntimeBridge.hpp"
#include <process.h>
#include <sddl.h>
#include <cmath>
#include <cstdio>
#include <vector>

namespace RunnerEditor {
uint64_t EpochMilliseconds() {
    FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ticks{}; ticks.LowPart = ft.dwLowDateTime; ticks.HighPart = ft.dwHighDateTime;
    return (ticks.QuadPart - 116444736000000000ULL) / 10000;
}
std::wstring PipeName(DWORD pid) { return L"\\\\.\\pipe\\RunnerEditor-" + std::to_wstring(pid); }
struct RuntimeBridge::State {
    SRWLOCK cacheLock = SRWLOCK_INIT;
    Snapshot sample{};
    HANDLE stop = nullptr, worker = nullptr, pipe = INVALID_HANDLE_VALUE, ioEvent = nullptr;
};
static void Close(HANDLE& handle) {
    if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    handle = nullptr;
}
// Restrict access to the process user. No Everyone or network grants.
static PSECURITY_DESCRIPTOR UserSecurity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
    DWORD bytes = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> buffer(bytes);
    if (!bytes || !GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes)) {
        CloseHandle(token); return nullptr;
    }
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) return nullptr;
    const std::wstring sddl = L"D:P(A;;GRGW;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return nullptr;
    return descriptor;
}
RuntimeBridge::RuntimeBridge() : state_(new State) {}
RuntimeBridge::~RuntimeBridge() {
    // A timed-out worker retains its state, OVERLAPPED storage, and handles. Production
    // also pins its DLL, so a failed join cannot become a code/data use-after-free.
    if (Stop()) delete state_;
}
bool RuntimeBridge::Start() {
    auto& s = *state_;
    if (s.worker) return false;
    auto sd = UserSecurity();
    if (!sd) return false;
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    s.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    s.ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    // Duplex is needed because Node net opens pipes read/write. We NEVER ReadFile
    // from a client, parse commands, or touch game state from this worker.
    s.pipe = CreateNamedPipeW(PipeName(GetCurrentProcessId()).c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, static_cast<DWORD>(MaxMessageBytes), 256, 0, &sa);
    LocalFree(sd);
    if (s.stop && s.ioEvent && s.pipe != INVALID_HANDLE_VALUE) {
        s.worker = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, Worker, &s, 0, nullptr));
    }
    if (!s.worker) { Close(s.pipe); Close(s.ioEvent); Close(s.stop); return false; }
    return true;
}
bool RuntimeBridge::Stop() {
    auto& s = *state_;
    if (!s.worker) return true;
    SetEvent(s.stop);
    if (WaitForSingleObject(s.worker, 1500) != WAIT_OBJECT_0) return false;
    Close(s.worker); Close(s.pipe); Close(s.ioEvent); Close(s.stop);
    return true;
}
void RuntimeBridge::Publish(const Snapshot& value) {
    auto& s = *state_;
    // Never stall the game thread behind the telemetry reader.
    if (!TryAcquireSRWLockExclusive(&s.cacheLock)) return;
    s.sample = value;
    ReleaseSRWLockExclusive(&s.cacheLock);
}
static std::string Quote(const char* text) {
    std::string result = "\"";
    for (size_t i = 0; i < 512 && text[i]; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 32) { char escaped[7]; sprintf_s(escaped, "\\u%04x", static_cast<unsigned>(c)); result += escaped; }
            else result += static_cast<char>(c);
        }
    }
    return result + '"';
}
static std::string Encode(const Snapshot& value) {
    const uint64_t now = GetTickCount64();
    const uint64_t age = value.sampledAt && now >= value.sampledTick ? now - value.sampledTick : 9007199254740991ULL;
    std::string json = "{\"protocol\":1,\"pid\":" + std::to_string(GetCurrentProcessId()) +
        ",\"room\":" + (value.hasRoom ? Quote(value.room) : "null") +
        ",\"events\":" + std::to_string(value.events) +
        ",\"sampledAt\":" + std::to_string(value.sampledAt) +
        ",\"ageMs\":" + std::to_string(age) +
        ",\"runnerType\":\"" + std::string(value.runnerType == RunnerType::VM ? "vm" :
            value.runnerType == RunnerType::YYC ? "yyc" : "unknown") + "\",\"capabilities\":{\"inspect\":true,\"liveCode\":false,\"liveAssets\":false" +
        (value.inspectVariables ? ",\"inspectVariables\":true" : "") + "}}\n";
    return json.size() <= MaxMessageBytes ? json : std::string{};
}
// All pending I/O belongs to this worker; cancellation and completion are handled
// here, never by a racing CloseHandle on the unload thread.
static bool Complete(HANDLE pipe, HANDLE stop, OVERLAPPED& op, DWORD timeout, DWORD& count) {
    HANDLE waits[] = {stop, op.hEvent};
    if (WaitForMultipleObjects(2, waits, FALSE, timeout) == WAIT_OBJECT_0 + 1)
        return GetOverlappedResult(pipe, &op, &count, FALSE) != FALSE;
    CancelIoEx(pipe, &op);
    // CancelIoEx is only a request. Keep the OVERLAPPED/buffer alive until completion.
    GetOverlappedResult(pipe, &op, &count, TRUE);
    return false;
}
unsigned __stdcall RuntimeBridge::Worker(void* context) {
    auto& s = *static_cast<State*>(context);
    try {
        while (WaitForSingleObject(s.stop, 0) == WAIT_TIMEOUT) {
            ResetEvent(s.ioEvent);
            OVERLAPPED connect{}; connect.hEvent = s.ioEvent;
            bool connected = ConnectNamedPipe(s.pipe, &connect) != FALSE;
            DWORD transferred = 0;
            if (!connected) {
                const DWORD error = GetLastError();
                if (error == ERROR_PIPE_CONNECTED) connected = true;
                else if (error == ERROR_IO_PENDING) connected = Complete(s.pipe, s.stop, connect, INFINITE, transferred);
            }
            if (!connected) {
                if (WaitForSingleObject(s.stop, 20) == WAIT_OBJECT_0) break;
                DisconnectNamedPipe(s.pipe); continue;
            }
            Snapshot sample;
            AcquireSRWLockShared(&s.cacheLock); sample = s.sample; ReleaseSRWLockShared(&s.cacheLock);
            const std::string json = Encode(sample);
            if (!json.empty() && WaitForSingleObject(s.stop, 0) == WAIT_TIMEOUT) {
                ResetEvent(s.ioEvent);
                OVERLAPPED write{}; write.hEvent = s.ioEvent;
                const BOOL done = WriteFile(s.pipe, json.data(), static_cast<DWORD>(json.size()), &transferred, &write);
                if (!done && GetLastError() == ERROR_IO_PENDING) Complete(s.pipe, s.stop, write, 250, transferred);
                // Do not FlushFileBuffers: it can block forever on an unread client.
                // Allow Node to consume the buffered line and close; stop interrupts grace.
                for (int i = 0; i < 25 && WaitForSingleObject(s.stop, 10) == WAIT_TIMEOUT; ++i) {
                    DWORD available = 0;
                    if (!PeekNamedPipe(s.pipe, nullptr, 0, nullptr, &available, nullptr)) break;
                }
            }
            DisconnectNamedPipe(s.pipe);
        }
    } catch (...) {
        // No exceptions may cross a CRT thread entry. Handles remain owned until Stop.
    }
    return 0;
}
}
