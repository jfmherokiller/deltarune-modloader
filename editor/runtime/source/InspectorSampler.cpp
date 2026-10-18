#include "InspectorSampler.hpp"
#include "InspectorValue.hpp"
#include "RuntimeBridge.hpp"
#include <YYToolkit/YYTK_Shared.hpp>
#include <algorithm>
#include <cstring>
#include <iterator>
namespace RunnerEditor {
using namespace YYTK;
using namespace Aurie;
static_assert(VALUE_REAL == 0 && VALUE_STRING == 1 && VALUE_ARRAY == 2 && VALUE_PTR == 3 &&
    VALUE_UNDEFINED == 5 && VALUE_OBJECT == 6 && VALUE_INT32 == 7 && VALUE_INT64 == 10 &&
    VALUE_NULL == 12 && VALUE_BOOL == 13 && VALUE_REF == 15, "Review RValue display tags for new YYTK ABI");
static constexpr const char* Builtins[] = {
    "id", "object_index", "x", "y", "depth", "visible", "persistent", "sprite_index",
    "image_index", "image_speed", "image_xscale", "image_yscale", "image_angle", "image_alpha",
    "image_blend", "speed", "direction", "hspeed", "vspeed"
};
struct Budget {
    LARGE_INTEGER started{}, frequency{};
    Budget() { QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&started); }
    bool Expired() const { LARGE_INTEGER now{}; QueryPerformanceCounter(&now); return (now.QuadPart-started.QuadPart)*1000 >= frequency.QuadPart*4; }
};
static uint64_t Bits(const RValue& value) { uint64_t bits; memcpy(&bits,&value.m_i64,sizeof(bits)); return bits; }
static bool Integer(const RValue& value, int32_t& n, bool allowReference = false) { return InspectInteger(value.m_Kind,Bits(value),n,allowReference); }
static std::string Text(const char* text) {
    if (!text) return {};
    return InspectText(std::string_view(text,strnlen_s(text,InspectorTextBytes)));
}
static InspectItem Display(const char* name, const RValue& value, bool builtin) {
    auto item = DisplayInspectValue(value.m_Kind,Bits(value));
    item.name = Text(name); item.builtin = builtin;
    item.kind = static_cast<uint32_t>(value.m_Kind) & 0x0ffffff;
    // Dynamic members are live runner slots (StructGetMember/InternalGetYYVarRef);
    // builtins are copies from GetBuiltin and have no addressable RValue.
    if (!builtin) item.address = reinterpret_cast<uint64_t>(&value);
    // Never use general RValue::ToString/GetKindName: they can enter YYGetString
    // or dereference unknown object types. Only a proven STRING tag takes this path.
    if (value.m_Kind == VALUE_STRING) {
        const char* text = value.ToCString();
        if (!text) throw "Invalid string";
        item.value = Text(text);
    }
    return item;
}
static void Fail(InspectResponse& r, const char* error) { r.items.clear(); r.hasMore = false; r.error = error; }
static void Truncate(InspectResponse& r) {
    if (r.items.empty()) Fail(r,"Sampling budget exhausted before page progress");
    else r.hasMore = true;
}
static bool IsBuiltin(const char* name) {
    return std::any_of(std::begin(Builtins),std::end(Builtins),[name](const char* n) { return strcmp(n,name)==0; });
}
static bool Push(InspectResponse& r, InspectItem item) {
    if (std::any_of(r.items.begin(),r.items.end(),[&](const InspectItem& old) { return old.name == item.name; })) {
        Fail(r,"Duplicate or truncated member name; refresh the page"); return false;
    }
    r.items.push_back(std::move(item)); return true;
}
static void Members(YYTKInterface* y, CInstance* instance, InspectResponse& r, Budget& budget) {
    uint32_t position = 0;
    if (r.request.kind == InspectKind::Instance) for (const char* name : Builtins) {
        if (budget.Expired()) { Truncate(r); return; }
        if (position++ < r.request.offset) continue;
        RValue value;
        if (!AurieSuccess(y->GetBuiltin(name,instance,NULL_INDEX,value))) { Fail(r,"Builtin instance property unavailable"); return; }
        if (!Push(r,Display(name,value,true))) return;
    }
    bool stopped = false;
    uint32_t visited = 0;
    // The supported API internally collects names before invoking callbacks. Its
    // allocation/engine time cannot be preempted by this plugin; see README.
    const auto status = y->EnumInstanceMembers(RValue(instance),[&](const char* name,RValue* value) {
        if (budget.Expired() || ++visited > 1000000) { Truncate(r); stopped = true; return true; }
        if (!name || !value) { Fail(r,"Invalid enumerated member"); stopped = true; return true; }
        if (r.request.kind == InspectKind::Instance && IsBuiltin(name)) return false;
        if (position++ < r.request.offset) return false;
        if (r.items.size() == InspectorPageSize) { r.hasMore = true; stopped = true; return true; }
        if (!Push(r,Display(name,*value,false))) { stopped = true; return true; }
        return false;
    });
    // OBJECT_NOT_FOUND is the documented full traversal outcome, including empty
    // instances; SUCCESS requires our callback to have explicitly stopped it.
    if (!stopped && status != AURIE_OBJECT_NOT_FOUND) Fail(r,"Member enumeration unavailable or incomplete");
    if (stopped && !AurieSuccess(status)) Fail(r,"Member enumeration failed");
    // YYTK also uses OBJECT_NOT_FOUND for a failed first member lookup. With no
    // callbacks there is no evidence of a complete empty enumeration: fail closed.
    if (!stopped && !visited && r.items.empty() && r.error.empty()) Fail(r,"No members exposed; empty or unsupported enumeration");
}
static CInstance* Resolve(YYTKInterface* y, int32_t id) {
    CInstance* instance = nullptr;
    if (!AurieSuccess(y->GetInstanceObject(id,instance)) || !instance) return nullptr;
    RValue identity; int32_t actual = -1;
    if (!AurieSuccess(y->GetBuiltin("id",instance,NULL_INDEX,identity)) || !Integer(identity,actual,true) || actual != id) return nullptr;
    return instance;
}
static void Instances(YYTKInterface* y, InspectResponse& r, Budget& budget) {
    RValue total; int32_t count = 0;
    if (!AurieSuccess(y->CallBuiltinEx(total,"instance_number",nullptr,nullptr,{RValue(-3)})) || !Integer(total,count) || count < 0) {
        Fail(r,"instance_number(all) unavailable or invalid"); return;
    }
    uint32_t index = r.request.offset;
    while (index < static_cast<uint32_t>(count) && r.items.size() < InspectorPageSize) {
        if (budget.Expired()) { Truncate(r); return; }
        RValue found, object, name; int32_t id = -1;
        if (!AurieSuccess(y->CallBuiltinEx(found,"instance_find",nullptr,nullptr,{RValue(-3),RValue(static_cast<int32_t>(index))})) || !Integer(found,id,true) || id < 0) {
            Fail(r,"instance_find(all) returned an invalid instance; refresh"); return;
        }
        auto instance = Resolve(y,id);
        if (!instance) { Fail(r,"Instance no longer exists; refresh"); return; }
        if (!AurieSuccess(y->GetBuiltin("object_index",instance,NULL_INDEX,object)) ||
            !AurieSuccess(y->CallBuiltinEx(name,"object_get_name",nullptr,nullptr,{object})) || name.m_Kind != VALUE_STRING) {
            Fail(r,"Object name unavailable"); return;
        }
        if (std::any_of(r.items.begin(),r.items.end(),[id](const InspectItem& item) { return item.id==id; })) { Fail(r,"Duplicate instance ID; refresh"); return; }
        InspectItem item; item.id = id; item.object = Text(name.ToCString()); item.address = reinterpret_cast<uint64_t>(instance);
        r.items.push_back(std::move(item)); ++index;
    }
    r.hasMore = index < static_cast<uint32_t>(count);
}
static void Sample(YYTKInterface* y, InspectResponse& r) {
    Budget budget;
    RValue room, roomName;
    if (AurieSuccess(y->GetBuiltin("room",nullptr,NULL_INDEX,room)) &&
        AurieSuccess(y->CallBuiltinEx(roomName,"room_get_name",nullptr,nullptr,{room})) && roomName.m_Kind == VALUE_STRING) {
        const char* text = roomName.ToCString();
        if (text) { r.room=Text(text); r.hasRoom=true; }
    }
    if (budget.Expired()) { Truncate(r); return; }
    if (r.request.kind == InspectKind::Instances) { Instances(y,r,budget); return; }
    CInstance* instance = nullptr;
    if (r.request.kind == InspectKind::Globals) {
        if (!AurieSuccess(y->GetGlobalInstance(&instance)) || !instance) { Fail(r,"Global namespace unavailable"); return; }
    } else {
        instance = Resolve(y,r.request.target);
        if (!instance) { Fail(r,"Instance no longer exists"); return; }
    }
    Members(y,instance,r,budget);
}
// Keep SEH in a destructor-free frame, as in the established generic probe.
static bool Protected(YYTKInterface* y, InspectResponse* r) {
    __try { Sample(y,*r); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SampleInspector(YYTKInterface* y, const InspectRequest& request, InspectResponse& response) {
    response = {}; response.request=request; response.addresses=request.addresses; response.pid=GetCurrentProcessId();
    response.sampledAt=EpochMilliseconds(); response.sampledTick=GetTickCount64();
    bool safe = true;
    try { safe = y && Protected(y,&response); }
    catch (...) { safe = false; }
    if (!safe) Fail(response,"Inspector disabled after an unsafe runner read");
    return safe;
}
}

