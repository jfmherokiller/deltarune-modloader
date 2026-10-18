#include "InspectorBridge.hpp"
#include <process.h>
#include <sddl.h>
#include <vector>
namespace RunnerEditor {
std::wstring InspectorPipeName(DWORD pid) { return LR"(\\.\pipe\RunnerInspector-)" + std::to_wstring(pid); }
std::wstring CheatEnginePipeName(DWORD pid) { return LR"(\\.\pipe\RunnerCE-)" + std::to_wstring(pid); }
InspectJob::InspectJob() : done(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
InspectJob::~InspectJob() { if (done) CloseHandle(done); }
struct InspectorBridge::State {
    SRWLOCK lock = SRWLOCK_INIT;
    std::shared_ptr<InspectJob> pending;
    std::atomic<bool> running{false};
    bool cheatEngine = false;
    HANDLE stop = nullptr, worker = nullptr, pipe = INVALID_HANDLE_VALUE, ioEvent = nullptr;
};
static void Close(HANDLE& h) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = nullptr; }
static PSECURITY_DESCRIPTOR UserSecurity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
    DWORD bytes = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> buffer(bytes);
    if (!bytes || !GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes)) { CloseHandle(token); return nullptr; }
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) return nullptr;
    const std::wstring sddl = L"D:P(A;;GRGW;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return nullptr;
    return sd;
}
InspectorBridge::InspectorBridge(bool cheatEngine) : state_(new State) { state_->cheatEngine = cheatEngine; }
InspectorBridge::~InspectorBridge() { if (Stop()) delete state_; }
bool InspectorBridge::Start() {
    auto& s = *state_;
    if (s.worker) return false;
    auto sd = UserSecurity(); if (!sd) return false;
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    s.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    s.ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    s.pipe = CreateNamedPipeW((s.cheatEngine ? CheatEnginePipeName : InspectorPipeName)(GetCurrentProcessId()).c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, static_cast<DWORD>(InspectorMaxResponse + 4), static_cast<DWORD>(InspectorMaxRequest), 0, &sa);
    LocalFree(sd);
    if (s.stop && s.ioEvent && s.pipe != INVALID_HANDLE_VALUE) {
        s.running.store(true);
        s.worker = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, Worker, &s, 0, nullptr));
    }
    if (!s.worker) { s.running.store(false); Close(s.pipe); Close(s.ioEvent); Close(s.stop); return false; }
    return true;
}
bool InspectorBridge::Stop() {
    auto& s = *state_; s.running.store(false);
    if (!s.worker) return true;
    SetEvent(s.stop);
    if (WaitForSingleObject(s.worker, 1500) != WAIT_OBJECT_0) return false;
    Close(s.worker); Close(s.pipe); Close(s.ioEvent); Close(s.stop);
    return true;
}
std::shared_ptr<InspectJob> InspectorBridge::Take() {
    auto& s = *state_;
    if (!s.running.load() || !TryAcquireSRWLockShared(&s.lock)) return {};
    auto job = s.pending;
    ReleaseSRWLockShared(&s.lock);
    if (!job || !s.running.load()) return {};
    auto expected = InspectJob::Queued;
    if (GetTickCount64() >= job->deadline) {
        job->stage.compare_exchange_strong(expected, InspectJob::Expired); return {};
    }
    if (!job->stage.compare_exchange_strong(expected, InspectJob::Dispatched)) return {};
    if (!s.running.load() || GetTickCount64() >= job->deadline) { job->stage.store(InspectJob::Expired); return {}; }
    return job;
}
void InspectorBridge::Complete(const std::shared_ptr<InspectJob>& job, InspectResponse response) {
    if (!job || job->stage.load() != InspectJob::Dispatched) return;
    if (GetTickCount64() >= job->deadline || !state_->running.load()) job->stage.store(InspectJob::Expired);
    else { job->response = std::move(response); job->stage.store(InspectJob::Ready, std::memory_order_release); }
    SetEvent(job->done);
}
// CancelIoEx requests cancellation; preserve buffers until the kernel completes it.
// Stop retains state (and production pins code) if cancellation never completes.
static bool CompleteIO(HANDLE pipe, HANDLE stop, OVERLAPPED& op, DWORD timeout, DWORD& count) {
    HANDLE waits[] = {stop, op.hEvent};
    if (WaitForMultipleObjects(2, waits, FALSE, timeout) == WAIT_OBJECT_0 + 1)
        return GetOverlappedResult(pipe, &op, &count, FALSE) != FALSE;
    CancelIoEx(pipe, &op); GetOverlappedResult(pipe, &op, &count, TRUE); return false;
}
static bool ReadRequest(HANDLE pipe, HANDLE stop, HANDLE event, InspectRequest& request) {
    char bytes[InspectorMaxRequest + 1]{};
    size_t used = 0;
    const auto end = GetTickCount64() + 250;
    while (used < sizeof(bytes) && GetTickCount64() < end) {
        ResetEvent(event); OVERLAPPED op{}; op.hEvent = event; DWORD n = 0;
        bool ok = ReadFile(pipe, bytes + used, static_cast<DWORD>(sizeof(bytes) - used), &n, &op) != FALSE;
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            const auto now = GetTickCount64();
            ok = CompleteIO(pipe, stop, op, now < end ? static_cast<DWORD>(end-now) : 0, n);
        }
        if (!ok || !n) return false;
        used += n;
        std::string_view line(bytes,used);
        if (line.find('\n') != std::string_view::npos) {
            DWORD extra = 0;
            if (!PeekNamedPipe(pipe,nullptr,0,nullptr,&extra,nullptr) || extra) return false;
            return ParseInspectRequest(line, request);
        }
    }
    return false;
}
static InspectResponse Error(const InspectRequest& request, const char* message) {
    InspectResponse r; r.request = request; r.pid = GetCurrentProcessId(); r.error = message;
    r.sampledAt = EpochMilliseconds(); r.sampledTick = GetTickCount64(); return r;
}
unsigned __stdcall InspectorBridge::Worker(void* context) {
    auto& s = *static_cast<State*>(context);
    try {
        while (WaitForSingleObject(s.stop,0) == WAIT_TIMEOUT) {
            ResetEvent(s.ioEvent); OVERLAPPED op{}; op.hEvent = s.ioEvent; DWORD n = 0;
            bool connected = ConnectNamedPipe(s.pipe,&op) != FALSE;
            if (!connected) {
                const auto error = GetLastError();
                if (error == ERROR_PIPE_CONNECTED) connected = true;
                else if (error == ERROR_IO_PENDING) connected = CompleteIO(s.pipe,s.stop,op,INFINITE,n);
            }
            if (!connected) {
                if (WaitForSingleObject(s.stop,10) == WAIT_OBJECT_0) break;
                DisconnectNamedPipe(s.pipe); continue;
            }
            InspectRequest request;
            if (!ReadRequest(s.pipe,s.stop,s.ioEvent,request)) { DisconnectNamedPipe(s.pipe); continue; }
            request.addresses = s.cheatEngine;
            auto job = std::make_shared<InspectJob>();
            job->request = request; job->deadline = GetTickCount64() + 650;
            bool queued = false;
            AcquireSRWLockExclusive(&s.lock);
            if (s.pending && s.pending->stage.load() != InspectJob::Dispatched) s.pending.reset();
            if (!s.pending && job->done) { s.pending = job; queued = true; }
            ReleaseSRWLockExclusive(&s.lock);
            InspectResponse response = Error(request, "Inspector busy");
            bool connectedPeer = true;
            if (queued) {
                while (GetTickCount64() < job->deadline && job->stage.load(std::memory_order_acquire) != InspectJob::Ready) {
                    HANDLE waits[] = {s.stop,job->done};
                    if (WaitForMultipleObjects(2,waits,FALSE,10) != WAIT_TIMEOUT) break;
                    DWORD extra = 0;
                    if (!PeekNamedPipe(s.pipe,nullptr,0,nullptr,&extra,nullptr) || extra) { connectedPeer = false; break; }
                }
                if (connectedPeer && job->stage.load(std::memory_order_acquire) == InspectJob::Ready) response = std::move(job->response);
                else response = Error(request,"Inspector request expired");
                auto expected = InspectJob::Queued;
                job->stage.compare_exchange_strong(expected,InspectJob::Expired);
                AcquireSRWLockExclusive(&s.lock);
                if (s.pending == job && job->stage.load() != InspectJob::Dispatched) s.pending.reset();
                ReleaseSRWLockExclusive(&s.lock);
            }
            if (connectedPeer && WaitForSingleObject(s.stop,0) == WAIT_TIMEOUT) {
                auto json = EncodeInspectResponse(response,GetTickCount64());
                if (s.cheatEngine) { // CE pipe objects read fixed sizes: uint32 LE length, then JSON
                    const auto length = static_cast<uint32_t>(json.size());
                    json.insert(0, reinterpret_cast<const char*>(&length), sizeof(length));
                }
                ResetEvent(s.ioEvent); OVERLAPPED write{}; write.hEvent = s.ioEvent;
                bool sent = WriteFile(s.pipe,json.data(),static_cast<DWORD>(json.size()),&n,&write) != FALSE;
                if (!sent && GetLastError() == ERROR_IO_PENDING) sent = CompleteIO(s.pipe,s.stop,write,250,n);
                if (sent && n == json.size()) for (int i = 0; i < 25 && WaitForSingleObject(s.stop,10) == WAIT_TIMEOUT; ++i) {
                    if (!PeekNamedPipe(s.pipe,nullptr,0,nullptr,nullptr,nullptr)) break;
                }
            }
            DisconnectNamedPipe(s.pipe);
        }
    } catch (...) { /* No exceptions cross the CRT thread boundary. */ }
    s.running.store(false);
    return 0;
}
}
