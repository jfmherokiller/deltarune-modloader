// Synthetic protocol tests: no YYTK or game process.
#include "../source/InspectorProtocol.hpp"
#include "../source/InspectorValue.hpp"
#include <bit>
#include <limits>
#include <cstdio>
#include <cstdlib>
using namespace RunnerEditor;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main() {
    InspectRequest r;
    CHECK(ParseInspectRequest("globals 0\n", r));
    CHECK(r.kind == InspectKind::Globals && r.offset == 0 && r.target == -1);
    CHECK(ParseInspectRequest("instances 1000000\n", r));
    CHECK(r.kind == InspectKind::Instances && r.offset == 1000000);
    CHECK(ParseInspectRequest("instance 2147483647 64\n", r));
    CHECK(r.kind == InspectKind::Instance && r.target == 2147483647 && r.offset == 64);
    CHECK(ParseInspectRequest("instance 0 0\n", r));
    for (auto text : {"", "globals 0", "globals -1\n", "globals +1\n", "Globals 0\n", " globals 0\n", "globals  0\n", "globals 0 \n", "globals 0\r\n", "globals 1000001\n", "globals 999999999999999999999999999\n", "instances\t0\n", "instance 2147483648 0\n", "instance -1 0\n", "instance 1\n", "instance 1 0 0\n", "eval 0\n", "set 0\n", "globals 0\ninstances 0\n"}) CHECK(!ParseInspectRequest(text, r));
    CHECK(!ParseInspectRequest(std::string("globals 0\0\n", 11), r));
    CHECK(!ParseInspectRequest(std::string(129, '0'), r));
    CHECK(DisplayInspectValue(10, 9223372036854775807ULL).value == "9223372036854775807");
    CHECK(DisplayInspectValue(10, 9223372036854775808ULL).value == "-9223372036854775808");
    CHECK(DisplayInspectValue(7, 0xffffffff).value == "-1");
    CHECK(DisplayInspectValue(0, std::bit_cast<uint64_t>(1.25)).value == "1.25");
    CHECK(DisplayInspectValue(13, std::bit_cast<uint64_t>(1.0)).value == "true");
    CHECK(DisplayInspectValue(13, 0).value == "false");
    CHECK(DisplayInspectValue(2, 0x12345678).value == "[array]");
    CHECK(DisplayInspectValue(6, 0x12345678).value == "[struct/object]");
    CHECK(DisplayInspectValue(3, 0x12345678).value == "[opaque]");
    CHECK(DisplayInspectValue(500, 0x12345678).value == "[unsupported]");
    CHECK(DisplayInspectValue(0, std::bit_cast<uint64_t>(std::numeric_limits<double>::infinity())).value == "Infinity");
    int32_t integer = 0;
    CHECK(InspectInteger(0, std::bit_cast<uint64_t>(42.0),integer) && integer == 42);
    CHECK(!InspectInteger(0, std::bit_cast<uint64_t>(42.5),integer));
    CHECK(!InspectInteger(0, std::bit_cast<uint64_t>(std::numeric_limits<double>::infinity()),integer));
    CHECK(!InspectInteger(10, 2147483648ULL, integer));
    CHECK(!InspectInteger(15, 100001, integer));
    CHECK(InspectInteger(15, 100001, integer,true) && integer == 100001);
    InspectResponse sample;
    sample.pid = 123; sample.sampledAt = 123456; sample.sampledTick = 100;
    sample.items.push_back({"exact", "int64", "9223372036854775807"});
    auto json = EncodeInspectResponse(sample, 120);
    CHECK(json.find("\"protocol\":2") != std::string::npos);
    CHECK(json.find("\"target\":null") != std::string::npos);
    CHECK(json.find("\"value\":\"9223372036854775807\"") != std::string::npos);
    CHECK(json.find("\"ageMs\":20") != std::string::npos);
    CHECK(json.find("\"source\":\"dynamic\",\"readOnly\":true") != std::string::npos);
    CHECK(json.find("\"room\":null,\"error\":null") != std::string::npos);
    CHECK(json.back() == '\n');
    sample.request = {InspectKind::Instance, 42, 2};
    sample.items[0].builtin = true;
    sample.hasRoom = true; sample.room = "quote\" slash\\ newline\n";
    json = EncodeInspectResponse(sample, 90);
    CHECK(json.find("\"target\":42") != std::string::npos);
    CHECK(json.find("\"ageMs\":0") != std::string::npos);
    CHECK(json.find("quote\\\" slash\\\\ newline\\n") != std::string::npos);
    CHECK(InspectText(std::string(511, 'a') + "\xe2\x82\xac").size() == 511);
    CHECK(InspectText("\xff").find('\xff') == std::string::npos);
    CHECK(InspectText("\xed\xa0\x80").find('\xed') == std::string::npos);
    sample.items.clear();
    for (int i = 0; i < 65; ++i) sample.items.push_back({std::to_string(i) + std::string(510, '\1'), "string", std::string(512, '\2')});
    json = EncodeInspectResponse(sample, 120);
    CHECK(json.size() <= InspectorMaxResponse);
    CHECK(json.find("\"hasMore\":true") != std::string::npos);
    CHECK(json.find("\"items\":[]") == std::string::npos);
    sample.request = {InspectKind::Instances, -1, 0}; sample.items.clear();
    InspectItem instance; instance.id = 123; instance.object = "obj_test";
    sample.items.push_back(instance);
    json = EncodeInspectResponse(sample, 120);
    CHECK(json.find("\"items\":[{\"id\":123,\"object\":\"obj_test\"}]") != std::string::npos);
    sample.items.push_back(instance);
    json = EncodeInspectResponse(sample,120);
    CHECK(json.find("\"items\":[],\"hasMore\":false") != std::string::npos);
    CHECK(json.find("Duplicate") != std::string::npos);
    sample.items.clear(); sample.hasMore=true;
    json = EncodeInspectResponse(sample,120);
    CHECK(json.find("Sampling budget exhausted") != std::string::npos);
    sample.error="explicit failure"; sample.items.push_back(instance);
    json = EncodeInspectResponse(sample,120);
    CHECK(json.find("\"items\":[],\"hasMore\":false") != std::string::npos);
    // CE mode: addresses + raw kinds only when the response is flagged.
    sample = {}; sample.request = {InspectKind::Globals, -1, 0}; sample.pid = 7;
    InspectItem slot; slot.name = "gold"; slot.type = "real"; slot.value = "5"; slot.kind = 0; slot.address = 0x1234abcdULL;
    InspectItem builtinSlot; builtinSlot.name = "x"; builtinSlot.type = "real"; builtinSlot.value = "1"; builtinSlot.builtin = true;
    sample.items = {slot, builtinSlot};
    json = EncodeInspectResponse(sample, 0);
    CHECK(json.find("address") == std::string::npos && json.find("1234abcd") == std::string::npos);
    sample.addresses = true;
    json = EncodeInspectResponse(sample, 0);
    CHECK(json.find("\"addresses\":true") != std::string::npos);
    CHECK(json.find("\"kind\":0,\"address\":\"0x1234ABCD\"") != std::string::npos);
    CHECK(json.find("\"name\":\"x\",\"type\":\"real\",\"value\":\"1\",\"source\":\"builtin\",\"readOnly\":true,\"kind\":0,\"address\":null") != std::string::npos);
    sample.request = {InspectKind::Instances, -1, 0}; sample.items.clear();
    InspectItem inst; inst.id = 100001; inst.object = "obj_a"; inst.address = 0x7ff000ULL; sample.items.push_back(inst);
    json = EncodeInspectResponse(sample, 0);
    CHECK(json.find("{\"id\":100001,\"object\":\"obj_a\",\"address\":\"0x7FF000\"}") != std::string::npos);
    puts("SYNTHETIC inspector protocol/serialization tests passed");
}
