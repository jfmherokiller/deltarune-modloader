// SYNTHETIC adapter tests against a minimal fake interface, not live-engine proof.
#include <Windows.h>
#include <YYToolkit/YYTK_Shared.hpp>
#include "../source/InspectorSampler.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
using namespace RunnerEditor;
using namespace YYTK;
using namespace Aurie;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
struct Fake : YYTKInterface {
    CInstance global{0}, live{100001};
    std::vector<std::pair<std::string,RValue>> members;
    int reads = 0, callbacks = 0, count = 1;
    bool dead = false, slow = false, failEnum = false, fault = false;
    AurieStatus GetGlobalInstance(CInstance** p) override { *p=&global; return AURIE_SUCCESS; }
    AurieStatus GetInstanceObject(int32_t id,CInstance*& p) override {
        p = !dead && id == live.id ? &live : nullptr; return p ? AURIE_SUCCESS : AURIE_OBJECT_NOT_FOUND;
    }
    AurieStatus GetBuiltin(const char* name,CInstance* p,int,RValue& r) override {
        ++reads; r=RValue(1);
        if (std::string(name)=="id") r=RValue(p->id);
        if (fault) RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
        return AURIE_SUCCESS;
    }
    AurieStatus CallBuiltinEx(RValue& r,const char* name,CInstance*,CInstance*,const std::vector<RValue>& args) override {
        std::string n(name);
        if (n=="room_get_name" || n=="object_get_name") { r.m_Kind=VALUE_STRING; r.m_Pointer=const_cast<char*>("SYNTHETIC_name"); }
        else if(n=="instance_number") { CHECK(args.size()==1 && args[0].m_i64==-3); r=RValue(count); }
        else if(n=="instance_find") { CHECK(args.size()==2 && args[0].m_i64==-3); r=RValue(100001); }
        else return AURIE_OBJECT_NOT_FOUND;
        return AURIE_SUCCESS;
    }
    AurieStatus EnumInstanceMembers(RValue,std::function<bool(const char*,RValue*)> fn) override {
        if(failEnum) return AURIE_ACCESS_DENIED;
        for (auto& entry : members) { ++callbacks; if(slow) Sleep(6); if(fn(entry.first.c_str(),&entry.second)) return AURIE_SUCCESS; }
        return AURIE_OBJECT_NOT_FOUND;
    }
};
int main() {
    Fake f; InspectResponse r;
    RValue exact; exact.m_Kind=VALUE_INT64; exact.m_i64=9223372036854775807LL;
    f.members.push_back({"exact",exact});
    CHECK(SampleInspector(&f,{InspectKind::Globals,-1,0},r));
    CHECK(r.error.empty() && r.items.size()==1 && !r.hasMore);
    CHECK(r.items[0].name=="exact" && r.items[0].value=="9223372036854775807");
    CHECK(!r.items[0].builtin && r.sampledAt && r.hasRoom);
    CHECK(r.items[0].address == reinterpret_cast<uint64_t>(&f.members[0].second) && r.items[0].kind == VALUE_INT64);
    f.members.clear();
    CHECK(SampleInspector(&f,{InspectKind::Globals,-1,0},r));
    CHECK(!r.error.empty() && r.items.empty() && !r.hasMore);
    for(int i=0;i<100;++i) f.members.push_back({"member"+std::to_string(i),RValue(i)});
    CHECK(SampleInspector(&f,{InspectKind::Globals,-1,0},r));
    CHECK(r.error.empty() && r.items.size()==64 && r.hasMore);
    CHECK(SampleInspector(&f,{InspectKind::Globals,-1,64},r));
    CHECK(r.error.empty() && r.items.size()==36 && !r.hasMore && r.items[0].name=="member64");
    CHECK(SampleInspector(&f,{InspectKind::Globals,-1,1000000},r));
    CHECK(r.items.empty() && !r.hasMore);
    CHECK(SampleInspector(&f,{InspectKind::Instance,100001,0},r));
    CHECK(r.error.empty() && !r.items.empty() && r.items[0].name=="id" && r.items[0].builtin);
    f.dead=true;
    CHECK(SampleInspector(&f,{InspectKind::Instance,100001,0},r));
    CHECK(!r.error.empty() && r.items.empty() && !r.hasMore); f.dead=false;
    CHECK(SampleInspector(&f,{InspectKind::Instances,-1,0},r));
    CHECK(r.error.empty() && r.items.size()==1 && r.items[0].id==100001 && r.items[0].object=="SYNTHETIC_name");
    CHECK(r.items[0].address == reinterpret_cast<uint64_t>(&f.live));
    f.count=2;
    CHECK(SampleInspector(&f,{InspectKind::Instances,-1,0},r));
    CHECK(!r.error.empty() && r.items.empty() && !r.hasMore); f.count=1;
    f.failEnum=true;
    CHECK(SampleInspector(&f,{InspectKind::Globals,-1,0},r));
    CHECK(!r.error.empty() && r.items.empty() && !r.hasMore); f.failEnum=false;
    f.slow=true;
    CHECK(SampleInspector(&f,{InspectKind::Globals,-1,64},r));
    CHECK(!r.error.empty() && r.items.empty() && !r.hasMore); f.slow=false;
    f.fault=true;
    CHECK(!SampleInspector(&f,{InspectKind::Instance,100001,0},r));
    CHECK(!r.error.empty() && r.items.empty() && !r.hasMore);
    puts("SYNTHETIC sampler tests passed: enumeration contract, pagination, builtins, destroyed IDs, duplicate IDs, budget, SEH");
}
