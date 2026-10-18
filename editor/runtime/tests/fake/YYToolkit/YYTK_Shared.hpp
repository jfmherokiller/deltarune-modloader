#pragma once
// Deliberately synthetic, minimal engine seam. Does not load YYTK or emulate runner ownership.
#include <cstdint>
#include <functional>
#include <vector>
namespace Aurie {
enum AurieStatus { AURIE_SUCCESS, AURIE_OBJECT_NOT_FOUND, AURIE_INVALID_PARAMETER, AURIE_ACCESS_DENIED };
inline bool AurieSuccess(AurieStatus s) { return s == AURIE_SUCCESS; }
}
namespace YYTK {
enum RValueType : uint32_t { VALUE_REAL=0, VALUE_STRING=1, VALUE_ARRAY=2, VALUE_PTR=3, VALUE_UNDEFINED=5, VALUE_OBJECT=6, VALUE_INT32=7, VALUE_INT64=10, VALUE_NULL=12, VALUE_BOOL=13, VALUE_REF=15 };
constexpr int NULL_INDEX = -1;
struct CInstance { int id = 0; };
struct RValue {
    union { int32_t m_i32; int64_t m_i64; double m_Real; void* m_Pointer; CInstance* m_Instance; };
    uint32_t m_Flags = 0;
    RValueType m_Kind = VALUE_UNDEFINED;
    RValue() : m_i64(0) {}
    RValue(int32_t n) : m_i64(n), m_Kind(VALUE_INT64) {}
    RValue(CInstance* p) : m_Instance(p), m_Kind(VALUE_OBJECT) {}
    const char* ToCString() const { return static_cast<const char*>(m_Pointer); }
};
struct YYTKInterface {
public:
    virtual ~YYTKInterface() = default;
    virtual Aurie::AurieStatus GetGlobalInstance(CInstance**) = 0;
    virtual Aurie::AurieStatus GetInstanceObject(int32_t,CInstance*&) = 0;
    virtual Aurie::AurieStatus GetBuiltin(const char*,CInstance*,int,RValue&) = 0;
    virtual Aurie::AurieStatus CallBuiltinEx(RValue&,const char*,CInstance*,CInstance*,const std::vector<RValue>&) = 0;
    virtual Aurie::AurieStatus EnumInstanceMembers(RValue,std::function<bool(const char*,RValue*)>) = 0;
};
}
