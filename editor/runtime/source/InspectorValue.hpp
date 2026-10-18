#pragma once
#include "InspectorProtocol.hpp"
namespace RunnerEditor {
// Tagged RValue payload, copied without engine conversions. Only VALUE_STRING
// is converted separately by the game-thread adapter; other pointers stay opaque.
InspectItem DisplayInspectValue(uint32_t kind, uint64_t bits);
bool InspectInteger(uint32_t kind, uint64_t bits, int32_t& value, bool allowReference = false);
}
