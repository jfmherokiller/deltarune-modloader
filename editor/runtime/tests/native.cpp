// SYNTHETIC native transport/lifecycle tests. Never load or contact a game.
#include <winsock2.h>
#include <Windows.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include "../source/RuntimeBridge.hpp"
#include "../source/TcpRelay.hpp"
int main(int argc, char** argv) {
  using namespace RunnerEditor;
  RuntimeBridge bridge;
  assert(bridge.Start());
  Snapshot value{};
  value.hasRoom = true;
  strcpy_s(value.room, "SYNTHETIC_NATIVE_TEST\"\\\n");
  value.events = 123;
  value.sampledAt = EpochMilliseconds();
  value.sampledTick = GetTickCount64();
  bridge.Publish(value);
  if (argc == 2 && strcmp(argv[1], "--serve-synthetic") == 0) {
    // Under Wine the Linux-side Node client reaches the pipe through the relay;
    // print the Windows PID because the harness only knows the Unix one.
    TcpRelay relay;
    const bool relayed = RunningUnderWine() && relay.Start();
    printf("SYNTHETIC_READY pid=%lu relay=%d - test data, NOT a game\n", GetCurrentProcessId(), relayed ? 1 : 0); fflush(stdout);
    char command[16]{};
    fgets(command, sizeof(command), stdin); // test harness stdin ONLY; never part of pipe protocol
    if (relayed) assert(relay.Stop());
    assert(bridge.Stop());
    return 0;
  }
  const std::wstring name = PipeName(GetCurrentProcessId());
  HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  assert(client != INVALID_HANDLE_VALUE);
  char bytes[4096]{}; DWORD read = 0;
  assert(ReadFile(client, bytes, sizeof(bytes) - 1, &read, nullptr));
  const std::string json(bytes, read);
  assert(json.find("\"protocol\":1") != std::string::npos);
  assert(json.find("SYNTHETIC_NATIVE_TEST\\\"\\\\\\n") != std::string::npos);
  assert(json.find("\"gold\"") == std::string::npos);
  assert(json.find("\"chapter\"") == std::string::npos);
  assert(json.find("\"ageMs\":") != std::string::npos);
  assert(json.find("\"runnerType\":\"unknown\"") != std::string::npos);
  CloseHandle(client);
  assert(bridge.Stop());
  for (auto [type, text] : {std::pair{RunnerType::VM, "vm"}, std::pair{RunnerType::YYC, "yyc"}}) {
    assert(bridge.Start());
    value.runnerType = type; bridge.Publish(value);
    client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(client != INVALID_HANDLE_VALUE);
    assert(ReadFile(client, bytes, sizeof(bytes) - 1, &read, nullptr));
    assert(std::string(bytes, read).find(std::string("\"runnerType\":\"") + text + "\"") != std::string::npos);
    CloseHandle(client);
    assert(bridge.Stop());
  }
  value.runnerType = RunnerType::Unknown;
  assert(bridge.Start());
  client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  assert(client != INVALID_HANDLE_VALUE);
  assert(ReadFile(client, bytes, sizeof(bytes) - 1, &read, nullptr));
  assert(json.back() == '\n');
  CloseHandle(client);
  assert(bridge.Stop());
  assert(bridge.Start());
  value.inspectVariables = true; bridge.Publish(value);
  client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  assert(client != INVALID_HANDLE_VALUE);
  assert(ReadFile(client, bytes, sizeof(bytes) - 1, &read, nullptr));
  assert(std::string(bytes,read).find("\"inspectVariables\":true") != std::string::npos);
  CloseHandle(client); assert(bridge.Stop());
  // Idle ConnectNamedPipe cancellation must not need a client to unblock it.
  for (int i = 0; i < 20; ++i) {
    RuntimeBridge idle;
    assert(idle.Start());
    const auto start = GetTickCount64();
    assert(idle.Stop());
    assert(GetTickCount64() - start < 1500);
  }
  // A client that never reads/writes must not prevent bounded stop.
  RuntimeBridge slow;
  assert(slow.Start()); slow.Publish(value);
  client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  assert(client != INVALID_HANDLE_VALUE);
  Sleep(20);
  assert(slow.Stop());
  CloseHandle(client);
  puts("SYNTHETIC native tests passed: encoding, local pipe, 20 idle cancellations, slow client stop");
}
