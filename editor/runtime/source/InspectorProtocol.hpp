#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace RunnerEditor {
constexpr size_t InspectorMaxRequest = 128, InspectorMaxResponse = 65536, InspectorPageSize = 64, InspectorTextBytes = 512;
enum class InspectKind { Globals, Instances, Instance };
struct InspectRequest {
    InspectKind kind = InspectKind::Globals;
    int32_t target = -1;
    uint32_t offset = 0;
    bool addresses = false; // set only by the Cheat Engine pipe, never parsed from the wire
};
bool ParseInspectRequest(std::string_view line, InspectRequest& result);
struct InspectItem {
    std::string name, type, value;
    bool builtin = false;
    int32_t id = 0;
    std::string object;
    uint32_t kind = 0;    // raw RValue kind (CE mode only)
    uint64_t address = 0; // RValue slot or CInstance* (CE mode only); 0 = none
};
struct InspectResponse {
    InspectRequest request;
    uint32_t pid = 0;
    std::vector<InspectItem> items;
    bool hasMore = false, hasRoom = false, addresses = false;
    std::string room, error;
    uint64_t sampledAt = 0, sampledTick = 0;
};
// UTF-8 validation/truncation, never split code points or disclose pointer payloads.
std::string InspectText(std::string_view input);
std::string EncodeInspectResponse(const InspectResponse& value, uint64_t nowTick);
}
