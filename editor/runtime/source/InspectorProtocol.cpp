#include "InspectorProtocol.hpp"
#include <cstdio>
#include <set>
namespace RunnerEditor {
std::string InspectText(std::string_view input) {
    std::string out;
    for (size_t i = 0; i < input.size() && out.size() < InspectorTextBytes;) {
        const auto c = static_cast<unsigned char>(input[i]);
        size_t n = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        bool valid = n && i + n <= input.size();
        for (size_t j = 1; valid && j < n; ++j) valid = (static_cast<unsigned char>(input[i+j]) & 0xc0) == 0x80;
        if (valid && n >= 3) {
            const auto b = static_cast<unsigned char>(input[i+1]);
            valid = !(c == 0xe0 && b < 0xa0) && !(c == 0xed && b >= 0xa0) && !(c == 0xf0 && b < 0x90) && !(c == 0xf4 && b >= 0x90);
        }
        if (!valid) { out += '?'; ++i; continue; }
        if (out.size() + n > InspectorTextBytes) break;
        out.append(input.substr(i, n)); i += n;
    }
    return out;
}
static std::string Quote(std::string_view value) {
    const auto text = InspectText(value);
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c == '\n') out += "\\n";
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
static std::string Hex(uint64_t value) {
    if (!value) return "null";
    char text[24]; snprintf(text, sizeof(text), "\"0x%llX\"", static_cast<unsigned long long>(value));
    return text;
}
std::string EncodeInspectResponse(const InspectResponse& v, uint64_t nowTick) {
    const bool addresses = v.addresses || v.request.addresses;
    const char* kind = v.request.kind == InspectKind::Globals ? "globals" : v.request.kind == InspectKind::Instances ? "instances" : "instance";
    std::string prefix = std::string("{\"protocol\":2,") + (addresses ? "\"addresses\":true," : "") + "\"pid\":" + std::to_string(v.pid) + ",\"kind\":\"" + kind + "\",\"target\":" +
        (v.request.kind == InspectKind::Instance ? std::to_string(v.request.target) : "null") + ",\"offset\":" + std::to_string(v.request.offset) + ",\"items\":[";
    std::string error = v.error;
    bool more = v.hasMore;
    if (error.empty() && more && v.items.empty()) error = "Sampling budget exhausted before page progress";
    std::string items;
    size_t count = 0;
    std::set<std::string> names;
    std::set<int32_t> ids;
    if (error.empty()) for (const auto& item : v.items) {
        std::string encoded;
        if (v.request.kind == InspectKind::Instances) encoded = "{\"id\":" + std::to_string(item.id) + ",\"object\":" + Quote(item.object) + (addresses ? ",\"address\":" + Hex(item.address) : "") + "}";
        else encoded = "{\"name\":" + Quote(item.name) + ",\"type\":" + Quote(item.type.substr(0, 64)) + ",\"value\":" + Quote(item.value) + ",\"source\":\"" + (item.builtin ? "builtin" : "dynamic") + "\",\"readOnly\":true" +
            (addresses ? ",\"kind\":" + std::to_string(item.kind) + ",\"address\":" + Hex(item.builtin ? 0 : item.address) : "") + "}";
        // Reserve ample tail room for worst-case escaped room and error strings.
        if (count == InspectorPageSize || prefix.size() + items.size() + encoded.size() + 7000 > InspectorMaxResponse) { more = true; break; }
        if (v.request.kind == InspectKind::Instances) {
            if (item.id < 0) { error="Invalid instance ID"; break; }
            if (!ids.insert(item.id).second) { error="Duplicate instance ID"; break; }
        } else if (!names.insert(InspectText(item.name)).second) { error="Duplicate member name"; break; }
        if (count++) items += ',';
        items += encoded;
    }
    if (!error.empty()) { items.clear(); more = false; }
    return prefix + items + "],\"hasMore\":" + (more ? "true" : "false") + ",\"sampledAt\":" + std::to_string(v.sampledAt) +
        ",\"ageMs\":" + std::to_string(nowTick >= v.sampledTick ? nowTick - v.sampledTick : 0) +
        ",\"room\":" + (v.hasRoom ? Quote(v.room) : "null") + ",\"error\":" + (error.empty() ? "null" : Quote(error)) + "}\n";
}
}
namespace RunnerEditor {
static bool Decimal(std::string_view text, uint32_t maximum, uint32_t& result) {
    if (text.empty()) return false;
    uint32_t n = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<uint32_t>(c - '0');
        if (n > (maximum - digit) / 10) return false;
        n = n * 10 + digit;
    }
    result = n;
    return true;
}
bool ParseInspectRequest(std::string_view line, InspectRequest& result) {
    if (line.empty() || line.size() > InspectorMaxRequest || line.back() != '\n') return false;
    line.remove_suffix(1);
    InspectRequest parsed;
    if (line.starts_with("globals ")) { line.remove_prefix(8); parsed.kind = InspectKind::Globals; }
    else if (line.starts_with("instances ")) { line.remove_prefix(10); parsed.kind = InspectKind::Instances; }
    else if (line.starts_with("instance ")) {
        line.remove_prefix(9); parsed.kind = InspectKind::Instance;
        const auto space = line.find(' ');
        if (space == std::string_view::npos) return false;
        uint32_t id;
        if (!Decimal(line.substr(0, space), 2147483647, id)) return false;
        parsed.target = static_cast<int32_t>(id);
        line.remove_prefix(space + 1);
    } else return false;
    if (!Decimal(line, 1000000, parsed.offset)) return false;
    result = parsed;
    return true;
}
}
