// Synthetic real Win32 pipe/lifecycle tests; never load/contact a game.
#include "../source/InspectorBridge.hpp"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <aclapi.h>
#include <vector>
using namespace RunnerEditor;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static HANDLE Connect() {
    const auto name = InspectorPipeName(GetCurrentProcessId());
    const auto end = GetTickCount64() + 2000;
    do {
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) return h;
        Sleep(2);
    } while (GetTickCount64() < end);
    CHECK(false); return INVALID_HANDLE_VALUE;
}
static void Send(HANDLE h, const std::string& text) {
    DWORD n = 0;
    CHECK(WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &n, nullptr));
    CHECK(n == text.size());
}
static std::string Read(HANDLE h) {
    std::string text;
    char data[8192]; DWORD n;
    while (ReadFile(h, data, sizeof(data), &n, nullptr) && n) {
        text.append(data,n);
        if (text.back() == '\n') break;
    }
    CHECK(text.size() <= InspectorMaxResponse);
    return text;
}
static std::shared_ptr<InspectJob> Take(InspectorBridge& b) {
    const auto end = GetTickCount64() + 500;
    do { auto j = b.Take(); if (j) return j; Sleep(1); } while (GetTickCount64() < end);
    CHECK(false); return {};
}
static InspectResponse Fixture(const InspectRequest& request) {
    InspectResponse r; r.request = request; r.pid = GetCurrentProcessId();
    r.sampledAt = EpochMilliseconds(); r.sampledTick = GetTickCount64();
    r.hasRoom = true; r.room = "SYNTHETIC_INSPECTOR_TEST";
    if (request.offset == 0) {
        InspectItem item;
        if (request.kind == InspectKind::Instances) { item.id = 100001; item.object = "obj_SYNTHETIC"; }
        else { item.name = "SYNTHETIC_exact_integer"; item.type = "int64"; item.value = "9223372036854775807"; item.builtin = request.kind == InspectKind::Instance; }
        r.items.push_back(item);
    }
    return r;
}
int main(int argc, char** argv) {
    InspectorBridge b;
    CHECK(b.Start());
    InspectorBridge duplicate;
    CHECK(!duplicate.Start());
    if (argc == 2 && std::string(argv[1]) == "--serve-synthetic") {
        std::atomic<bool> finish{false};
        std::thread sampler([&] { while (!finish) { if (auto job = b.Take()) b.Complete(job, Fixture(job->request)); Sleep(1); } });
        puts("SYNTHETIC_INSPECTOR_READY - fixture only, NOT a game"); fflush(stdout);
        char command[16]{}; fgets(command, sizeof(command), stdin);
        finish = true; sampler.join(); CHECK(b.Stop()); return 0;
    }
    HANDLE c = Connect();
    PSECURITY_DESCRIPTOR descriptor=nullptr; PACL acl=nullptr;
    CHECK(GetSecurityInfo(c,SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&descriptor)==ERROR_SUCCESS);
    CHECK(acl && acl->AceCount==1);
    void* rawAce=nullptr; CHECK(GetAce(acl,0,&rawAce));
    auto ace=static_cast<ACCESS_ALLOWED_ACE*>(rawAce); CHECK(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE);
    HANDLE token=nullptr; CHECK(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token));
    DWORD size=0; GetTokenInformation(token,TokenUser,nullptr,0,&size);
    std::vector<unsigned char> tokenData(size);
    CHECK(GetTokenInformation(token,TokenUser,tokenData.data(),size,&size));
    CHECK(EqualSid(&ace->SidStart,reinterpret_cast<TOKEN_USER*>(tokenData.data())->User.Sid));
    LocalFree(descriptor); CloseHandle(token);
    Send(c, "instance 42 "); Sleep(10); CHECK(!b.Take()); Send(c, "0\n");
    auto job = Take(b);
    CHECK(job->request.target == 42 && job->request.kind == InspectKind::Instance);
    CHECK(!b.Take());
    b.Complete(job, Fixture(job->request));
    auto text = Read(c); CHECK(text.find("\"target\":42") != std::string::npos); CloseHandle(c);
    // Invalid/oversized/multiple commands cannot reach the callback.
    for (const std::string command : {std::string("eval 0\n"), std::string(129, 'a'), std::string("globals 0\ninstances 0\n")}) {
        c = Connect(); Send(c,command); CHECK(Read(c).empty()); CHECK(!b.Take()); CloseHandle(c);
    }
    // Deadline expires before callback: never dispatch late work.
    c = Connect(); Send(c,"globals 0\n"); Sleep(750);
    CHECK(!b.Take()); text = Read(c);
    CHECK(text.find("\"error\":\"Inspector request expired\"") != std::string::npos);
    CHECK(text.find("\"items\":[],\"hasMore\":false") != std::string::npos); CloseHandle(c);
    // A timed-out dispatched job blocks new dispatch until completed; no stack lifetime hazards.
    c = Connect(); Send(c,"globals 0\n"); job = Take(b); Sleep(750); Read(c); CloseHandle(c);
    c = Connect(); Send(c,"instances 0\n"); text = Read(c);
    CHECK(text.find("Inspector busy") != std::string::npos); CHECK(!b.Take()); CloseHandle(c);
    b.Complete(job, Fixture(job->request));
    c = Connect(); Send(c,"instances 0\n"); job = Take(b); b.Complete(job,Fixture(job->request));
    text = Read(c); CHECK(text.find("obj_SYNTHETIC") != std::string::npos); CloseHandle(c);
    // Closing a queued request cancels it before a later callback.
    c = Connect(); Send(c,"globals 0\n"); Sleep(10); CloseHandle(c); Sleep(40); CHECK(!b.Take());
    // A nonreading peer and a large response cannot stall shutdown.
    c = Connect(); Send(c,"globals 0\n"); job=Take(b);
    auto large=Fixture(job->request); large.items.clear();
    for(int i=0;i<64;++i) large.items.push_back({std::to_string(i),"string",std::string(512,'x')});
    b.Complete(job,std::move(large)); Sleep(10);
    CHECK(b.Stop()); CloseHandle(c);
    for (int i = 0; i < 20; ++i) {
        InspectorBridge idle; CHECK(idle.Start());
        const auto start = GetTickCount64(); CHECK(idle.Stop()); CHECK(GetTickCount64()-start < 1500);
    }
    InspectorBridge slow; CHECK(slow.Start()); c = Connect(); Send(c,"globals ");
    const auto start = GetTickCount64(); CHECK(slow.Stop()); CHECK(GetTickCount64()-start < 1500); CloseHandle(c);
    InspectorBridge parked; CHECK(parked.Start()); c = Connect(); Send(c,"globals 0\n"); job = Take(parked);
    CHECK(parked.Stop()); parked.Complete(job,Fixture(job->request)); CloseHandle(c);
    // Cheat Engine pipe: separate name, address-bearing jobs, uint32 length-prefixed frame.
    {
        InspectorBridge editor; CHECK(editor.Start());
        InspectorBridge ce(true); CHECK(ce.Start());
        const auto ceName = CheatEnginePipeName(GetCurrentProcessId());
        CHECK(ceName != InspectorPipeName(GetCurrentProcessId()));
        HANDLE h = CreateFileW(ceName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE);
        Send(h,"globals 0\n"); auto cj = Take(ce); CHECK(cj->request.addresses); CHECK(!editor.Take());
        auto fixture = Fixture(cj->request); fixture.items[0].address = 0xABCDEF; ce.Complete(cj, fixture);
        uint32_t length = 0; DWORD got = 0;
        CHECK(ReadFile(h,&length,4,&got,nullptr) && got == 4 && length > 0 && length <= InspectorMaxResponse);
        std::string body(length,'\0'); DWORD at = 0;
        while (at < length && ReadFile(h,body.data()+at,length-at,&got,nullptr) && got) at += got;
        CHECK(at == length && body.back() == '\n');
        CHECK(body.find("\"addresses\":true") != std::string::npos && body.find("0xABCDEF") != std::string::npos);
        CloseHandle(h); CHECK(ce.Stop()); CHECK(editor.Stop());
    }
    puts("SYNTHETIC inspector transport tests passed: fragmented/invalid/oversized frames, expiry, single-flight, recovery, cancellation");
}
