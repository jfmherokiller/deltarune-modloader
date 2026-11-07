#pragma once
#include "RuntimeBridge.hpp"
#include <string>
namespace RunnerEditor {
// True inside Wine/Proton (ntdll exports wine_get_version).
bool RunningUnderWine();
// Wine/Proton only. Native Linux tools cannot open Wine named pipes, so this exposes
// the existing RunnerEditor-<pid> / RunnerInspector-<pid> pipes on 127.0.0.1 behind a
// random 256-bit token. The port and token are published in a Unix-side registry file:
//   $RUNNER_STUDIO_RUNTIME_DIR, else $XDG_CACHE_HOME/runner-studio,
//   else $HOME/.cache/runner-studio  ->  RunnerEditor-<windows pid>.json
// (XDG_RUNTIME_DIR is private inside Steam's pressure-vessel container; the home is shared.)
// Request: "RUNNER-RELAY/1 <token> snapshot\n" or "RUNNER-RELAY/1 <token> inspect\n<line>\n".
// The relay is a pipe client only: the pipe servers keep doing all parsing/validation,
// so it adds no command surface. The Cheat Engine pipe is not relayed.
class TcpRelay {
public:
    TcpRelay();
    ~TcpRelay();
    TcpRelay(const TcpRelay&) = delete;
    TcpRelay& operator=(const TcpRelay&) = delete;
    bool Start(); // listener + registry file; false leaves nothing behind
    bool Stop();  // <=1500ms join; removes the registry file
    unsigned short Port() const;
    std::string Token() const;
    std::wstring RegistryFile() const; // empty when not published
    const char* Failure() const;       // step that made Start() fail, for logs
private:
    struct State;
    State* state_;
    static unsigned __stdcall Worker(void* context);
};
}
