#include "InspectorValue.hpp"
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
namespace RunnerEditor {
InspectItem DisplayInspectValue(uint32_t kind, uint64_t bits) {
    InspectItem item;
    switch (kind) {
    case 0: {
        item.type = "real";
        const auto number = std::bit_cast<double>(bits);
        if (std::isnan(number)) item.value = "NaN";
        else if (!std::isfinite(number)) item.value = number < 0 ? "-Infinity" : "Infinity";
        else { char text[64]; const auto r = std::to_chars(text,text+sizeof(text),number); item.value.assign(text,r.ptr); }
        break;
    }
    case 1: item.type = "string"; item.value = "[string unavailable]"; break;
    case 2: item.type = "array"; item.value = "[array]"; break;
    case 3: item.type = "pointer"; item.value = "[opaque]"; break;
    case 5: item.type = "undefined"; item.value = "undefined"; break;
    case 6: item.type = "object"; item.value = "[struct/object]"; break;
    case 7: item.type = "int32"; item.value = std::to_string(std::bit_cast<int32_t>(static_cast<uint32_t>(bits))); break;
    case 10: item.type = "int64"; item.value = std::to_string(std::bit_cast<int64_t>(bits)); break;
    case 12: item.type = "null"; item.value = "null"; break;
    case 13: item.type = "bool"; item.value = std::bit_cast<double>(bits) != 0.0 ? "true" : "false"; break;
    case 15: item.type = "reference"; item.value = "[opaque reference]"; break;
    case 0x0ffffff: item.type = "unset"; item.value = "undefined"; break;
    default: item.type = "unsupported"; item.value = "[unsupported]"; break;
    }
    return item;
}
bool InspectInteger(uint32_t kind, uint64_t bits, int32_t& value, bool allowReference) {
    int64_t n;
    if (kind == 7 || (kind == 15 && allowReference)) n = std::bit_cast<int32_t>(static_cast<uint32_t>(bits));
    else if (kind == 10) n = std::bit_cast<int64_t>(bits);
    else if (kind == 0) {
        const double d = std::bit_cast<double>(bits);
        if (!std::isfinite(d) || d < -2147483648.0 || d > 2147483647.0 || std::trunc(d) != d) return false;
        n = static_cast<int64_t>(d);
    } else return false;
    if (n < (std::numeric_limits<int32_t>::min)() || n > (std::numeric_limits<int32_t>::max)()) return false;
    value = static_cast<int32_t>(n); return true;
}
}

