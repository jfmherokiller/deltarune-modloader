// Loopback TCP relay for Wine/Proton; see TcpRelay.hpp. Data-only: no YYTK dependency.
#include <winsock2.h>
#include <ws2tcpip.h>
#include "TcpRelay.hpp"
#include "InspectorBridge.hpp"
#include <bcrypt.h>
#include <process.h>
#include <string_view>

namespace RunnerEditor {
namespace {
constexpr DWORD ReadTimeoutMs = 500, PipeTimeoutMs = 1000;
constexpr size_t HeaderMax = 96; // "RUNNER-RELAY/1 " + 64 hex + " inspect"
constexpr std::string_view Magic = "RUNNER-RELAY/1 ";

std::wstring Env(const wchar_t* name) {
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    std::wstring value(size, L'\0');
    size = GetEnvironmentVariableW(name, value.data(), size);
    value.resize(size);
    return value;
}
// Unix path -> Wine DOS path (e.g. /run/user/1000 -> Z:\run\user\1000).
std::wstring DosPath(const std::wstring& unixPath) {
    using Fn = WCHAR* (CDECL*)(LPCSTR);
    auto convert = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_dos_file_name"));
    if (!convert || unixPath.empty() || unixPath[0] != L'/') return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, unixPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, unixPath.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);
    WCHAR* dos = convert(utf8.c_str());
    if (!dos) return {};
    std::wstring result(dos);
    HeapFree(GetProcessHeap(), 0, dos);
    return result;
}
bool EnsureDirectory(const std::wstring& dir) {
    if (CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
    const auto slash = dir.find_last_of(L'\\');
    if (slash == std::wstring::npos || slash < 3 || !EnsureDirectory(dir.substr(0, slash))) return false;
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
// Wine hides HOME, XDG_RUNTIME_DIR and similar Unix-only variables from the Win32
// environment block, but the Unix process environment is still in /proc/self/environ.
std::wstring UnixEnv(const wchar_t* name) {
    if (auto value = Env(name); !value.empty()) return value;
    const auto path = DosPath(L"/proc/self/environ");
    HANDLE file = path.empty() ? INVALID_HANDLE_VALUE
        : CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string block;
    char chunk[4096]; DWORD n = 0;
    while (block.size() < (1u << 20) && ReadFile(file, chunk, sizeof(chunk), &n, nullptr) && n) block.append(chunk, n);
    CloseHandle(file);
    const int wanted = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
    if (wanted <= 1) return {};
    std::string key(static_cast<size_t>(wanted - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, name, -1, key.data(), wanted, nullptr, nullptr);
    key += '=';
    for (size_t at = 0; at < block.size();) {
        size_t end = block.find('\0', at);
        if (end == std::string::npos) end = block.size();
        if (block.compare(at, key.size(), key) == 0) {
            const std::string value = block.substr(at + key.size(), end - at - key.size());
            const int chars = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
            std::wstring wide(static_cast<size_t>(chars > 0 ? chars : 0), L'\0');
            if (chars > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), chars);
            return wide;
        }
        at = end + 1;
    }
    return {};
}
// "<unix pid> <start time>" of this Wine process from /proc/self/stat (fields 1 and 22).
// Lets the Linux client tell a live game from a stale entry: wineserver keeps an exited
// process's listening sockets open, so "port accepts connections" proves nothing.
std::string UnixIdentity() {
    const auto path = DosPath(L"/proc/self/stat");
    HANDLE file = path.empty() ? INVALID_HANDLE_VALUE
        : CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    char text[1024]{}; DWORD n = 0;
    ReadFile(file, text, sizeof(text) - 1, &n, nullptr);
    CloseHandle(file);
    const std::string stat(text, n);
    const auto close = stat.rfind(')'); // comm may contain spaces
    if (close == std::string::npos) return {};
    const std::string pid = stat.substr(0, stat.find(' '));
    size_t at = close + 2;
    for (int field = 3; field < 22 && at != std::string::npos; ++field) at = stat.find(' ', at) + 1;
    if (at == 0 || at == std::string::npos) return {};
    const std::string start = stat.substr(at, stat.find(' ', at) - at);
    if (pid.empty() || start.empty() || pid.find_first_not_of("0123456789") != std::string::npos ||
        start.find_first_not_of("0123456789") != std::string::npos) return {};
    return ",\"unixPid\":" + pid + ",\"unixStart\":\"" + start + "\"";
}
std::wstring RegistryDirectory() {
    std::wstring candidates[3];
    // Not XDG_RUNTIME_DIR: Steam's pressure-vessel container gives the game a private one.
    // The home directory (and so the cache directory) is shared with the host.
    candidates[0] = UnixEnv(L"RUNNER_STUDIO_RUNTIME_DIR");
    if (auto cache = UnixEnv(L"XDG_CACHE_HOME"); !cache.empty()) candidates[1] = cache + L"/runner-studio";
    if (auto home = UnixEnv(L"HOME"); !home.empty()) candidates[2] = home + L"/.cache/runner-studio";
    for (const auto& unixDir : candidates) {
        if (unixDir.empty()) continue;
        auto dos = DosPath(unixDir);
        while (!dos.empty() && dos.back() == L'\\') dos.pop_back();
        if (!dos.empty() && EnsureDirectory(dos)) return dos;
        if (!candidates[0].empty()) return {}; // an explicit directory is never silently replaced
    }
    return {};
}
bool WriteAll(const std::wstring& path, const std::string& text) {
    const auto temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temporary.c_str()); return false;
    }
    return true;
}
bool ConstantTimeEqual(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}
// Overlapped pipe I/O bounded by `deadline` and the stop event; buffers outlive cancellation.
bool PipeIo(HANDLE pipe, HANDLE stop, HANDLE event, bool write, char* data, DWORD size, uint64_t deadline, DWORD& count) {
    ResetEvent(event);
    OVERLAPPED op{}; op.hEvent = event; count = 0;
    BOOL done = write ? WriteFile(pipe, data, size, &count, &op) : ReadFile(pipe, data, size, &count, &op);
    if (done) return true;
    if (GetLastError() != ERROR_IO_PENDING) return false;
    const uint64_t now = GetTickCount64();
    HANDLE waits[] = {stop, event};
    if (now < deadline && WaitForMultipleObjects(2, waits, FALSE, static_cast<DWORD>(deadline - now)) == WAIT_OBJECT_0 + 1)
        return GetOverlappedResult(pipe, &op, &count, FALSE) != FALSE;
    CancelIoEx(pipe, &op);
    GetOverlappedResult(pipe, &op, &count, TRUE);
    return false;
}
HANDLE OpenPipe(const std::wstring& name) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) return pipe;
        if (GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(name.c_str(), 300)) break;
    }
    return INVALID_HANDLE_VALUE;
}
}

bool RunningUnderWine() {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version");
}

struct TcpRelay::State {
    SOCKET listener = INVALID_SOCKET;
    HANDLE stop = nullptr, worker = nullptr, ioEvent = nullptr;
    bool wsa = false;
    unsigned short port = 0;
    std::string token;
    std::wstring registry;
    const char* failure = "";
};

TcpRelay::TcpRelay() : state_(new State) {}
TcpRelay::~TcpRelay() { if (Stop()) delete state_; }
unsigned short TcpRelay::Port() const { return state_->port; }
std::string TcpRelay::Token() const { return state_->token; }
std::wstring TcpRelay::RegistryFile() const { return state_->registry; }
const char* TcpRelay::Failure() const { return state_->failure; }

static void Cleanup(SOCKET& listener, HANDLE& stop, HANDLE& ioEvent, bool& wsa) {
    if (listener != INVALID_SOCKET) closesocket(listener);
    listener = INVALID_SOCKET;
    if (ioEvent) CloseHandle(ioEvent);
    if (stop) CloseHandle(stop);
    ioEvent = stop = nullptr;
    if (wsa) WSACleanup();
    wsa = false;
}

bool TcpRelay::Start() {
    auto& s = *state_;
    if (s.worker) return false;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { s.failure = "WSAStartup"; return false; }
    s.wsa = true;
    unsigned char random[32]{};
    s.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    s.ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    s.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int length = sizeof(address);
    BOOL exclusive = TRUE;
    if (!s.stop || !s.ioEvent || s.listener == INVALID_SOCKET ||
        setsockopt(s.listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0 ||
        bind(s.listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(s.listener, 4) != 0 ||
        getsockname(s.listener, reinterpret_cast<sockaddr*>(&address), &length) != 0 ||
        BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        s.failure = "loopback listener or token";
        Cleanup(s.listener, s.stop, s.ioEvent, s.wsa); return false;
    }
    s.port = ntohs(address.sin_port);
    static const char hex[] = "0123456789abcdef";
    s.token.clear();
    for (unsigned char byte : random) { s.token += hex[byte >> 4]; s.token += hex[byte & 15]; }
    SecureZeroMemory(random, sizeof(random));
    const auto dir = RegistryDirectory();
    const DWORD pid = GetCurrentProcessId();
    const auto file = dir.empty() ? std::wstring{} : dir + L"\\RunnerEditor-" + std::to_wstring(pid) + L".json";
    const std::string json = "{\"protocol\":1,\"pid\":" + std::to_string(pid) + ",\"port\":" + std::to_string(s.port) +
        ",\"token\":\"" + s.token + "\"" + UnixIdentity() + "}\n";
    if (file.empty() || !WriteAll(file, json)) {
        s.failure = file.empty() ? "no registry directory (RUNNER_STUDIO_RUNTIME_DIR, XDG_CACHE_HOME, HOME)" : "registry write";
        Cleanup(s.listener, s.stop, s.ioEvent, s.wsa); return false; }
    s.registry = file;
    s.worker = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, Worker, &s, 0, nullptr));
    if (!s.worker) {
        DeleteFileW(s.registry.c_str()); s.registry.clear();
        Cleanup(s.listener, s.stop, s.ioEvent, s.wsa); return false;
    }
    return true;
}

bool TcpRelay::Stop() {
    auto& s = *state_;
    if (!s.worker) return true;
    SetEvent(s.stop);
    if (WaitForSingleObject(s.worker, 1500) != WAIT_OBJECT_0) return false;
    CloseHandle(s.worker); s.worker = nullptr;
    if (!s.registry.empty()) DeleteFileW(s.registry.c_str());
    s.registry.clear();
    Cleanup(s.listener, s.stop, s.ioEvent, s.wsa);
    return true;
}

static void Serve(SOCKET client, HANDLE stop, HANDLE ioEvent, const std::string& token) {
    const DWORD timeout = ReadTimeoutMs;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    // Header line, then (inspect only) exactly one request line. Nothing else may follow.
    char bytes[HeaderMax + InspectorMaxRequest + 2]{};
    size_t used = 0, header = std::string_view::npos;
    const uint64_t readDeadline = GetTickCount64() + ReadTimeoutMs;
    bool inspect = false, complete = false;
    while (!complete && used < sizeof(bytes) && GetTickCount64() < readDeadline) {
        const int n = recv(client, bytes + used, static_cast<int>(sizeof(bytes) - used), 0);
        if (n <= 0) return;
        used += static_cast<size_t>(n);
        std::string_view view(bytes, used);
        if (header == std::string_view::npos) {
            header = view.find('\n');
            if (header == std::string_view::npos) { if (used > HeaderMax) return; continue; }
            std::string_view line = view.substr(0, header);
            if (line.size() > HeaderMax || line.substr(0, Magic.size()) != Magic) return;
            line.remove_prefix(Magic.size());
            const auto space = line.find(' ');
            if (space == std::string_view::npos || !ConstantTimeEqual(line.substr(0, space), token)) return;
            const auto channel = line.substr(space + 1);
            if (channel == "inspect") inspect = true;
            else if (channel != "snapshot") return;
        }
        const size_t end = inspect ? view.find('\n', header + 1) : header;
        if (end == std::string_view::npos) continue;
        if (end + 1 != used) return; // trailing bytes: refuse rather than forward a second command
        complete = true;
    }
    if (!complete) return;
    const DWORD pid = GetCurrentProcessId();
    HANDLE pipe = OpenPipe(inspect ? InspectorPipeName(pid) : PipeName(pid));
    if (pipe == INVALID_HANDLE_VALUE) return;
    const uint64_t deadline = GetTickCount64() + PipeTimeoutMs;
    DWORD count = 0;
    bool ok = true;
    if (inspect) {
        const DWORD length = static_cast<DWORD>(used - header - 1);
        ok = PipeIo(pipe, stop, ioEvent, true, bytes + header + 1, length, deadline, count) && count == length;
    }
    const size_t limit = inspect ? InspectorMaxResponse : MaxMessageBytes;
    std::string response;
    char chunk[8192];
    while (ok && response.size() <= limit && response.find('\n') == std::string::npos) {
        if (!PipeIo(pipe, stop, ioEvent, false, chunk, sizeof(chunk), deadline, count) || !count) break;
        response.append(chunk, count);
    }
    CloseHandle(pipe);
    const auto newline = response.find('\n');
    if (newline == std::string::npos || newline + 1 > limit) return;
    response.resize(newline + 1);
    for (size_t sent = 0; sent < response.size();) {
        const int n = send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
    shutdown(client, SD_SEND);
}

unsigned __stdcall TcpRelay::Worker(void* context) {
    auto& s = *static_cast<State*>(context);
    try {
        while (WaitForSingleObject(s.stop, 0) == WAIT_TIMEOUT) {
            fd_set readable; FD_ZERO(&readable); FD_SET(s.listener, &readable);
            timeval wait{0, 100000};
            const int ready = select(0, &readable, nullptr, nullptr, &wait);
            if (ready < 0) { WaitForSingleObject(s.stop, 50); continue; }
            if (ready == 0) continue;
            SOCKET client = accept(s.listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            Serve(client, s.stop, s.ioEvent, s.token);
            closesocket(client);
        }
    } catch (...) { /* No exceptions cross the CRT thread boundary. */ }
    return 0;
}
}
