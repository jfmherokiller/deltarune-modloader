# The RValue Type System

Everything a GameMaker game manipulates at runtime — variables, array elements, function
arguments, return values, struct members — is an **`RValue`**: a 16-byte tagged union.
This is the single most important data structure in the runtime.

## Struct layout (declared in IDB)

```c
struct RValue {            // sizeof = 16
    union {                // +0  (8 bytes) payload
        double      val;   //      REAL / BOOL stored as double
        void       *ptr;   //      STRING(RefString*) / ARRAY / OBJECT / PTR
        int         v32;   //      INT32 / REF
        long long   v64;   //      INT64 / raw pointer bits
    };
    unsigned int flags;    // +8  ownership / GC flags (bit 3 = owns method/ptr)
    int          kind;     // +12 type tag; only low 24 bits used (& 0xFFFFFF)
};
```

The `kind` field is masked with `0xFFFFFF` everywhere; the top byte carries auxiliary
flags. The sentinel value `0xFFFFFF` in the low 24 bits means **UNSET** (a declared but
never-assigned variable), distinct from `UNDEFINED` (5).

## RValueKind enum (declared in IDB)

Derived verbatim from `RValue_GetKindName` (`0x1401BCBE0`), which returns each name:

| Value | Name | Payload | Notes |
|---:|---|---|---|
| 0 | `VALUE_REAL` | `double val` | The default numeric type |
| 1 | `VALUE_STRING` | `RefString* ptr` | Ref-counted; `ptr[0]` = `char*` data |
| 2 | `VALUE_ARRAY` | array header `ptr` | Ref-counted GML array |
| 3 | `VALUE_PTR` | `void* ptr` | Raw pointer (buffers, etc.) |
| 4 | `VALUE_VEC3` | `ptr` | |
| 5 | `VALUE_UNDEFINED` | — | `undefined` |
| 6 | `VALUE_OBJECT` | struct/method `ptr` | Distinguishes *method* vs *struct* via `sub_14000CB10` |
| 7 | `VALUE_INT32` | `int v32` | |
| 8 | `VALUE_VEC4` | `ptr` | |
| 9 | `VALUE_VEC44` | `ptr` | matrix-ish |
| 10 (0xA) | `VALUE_INT64` | `long long v64` | |
| 11 (0xB) | `VALUE_ACCESSOR` | | |
| 12 (0xC) | `VALUE_NULL` | — | |
| 13 (0xD) | `VALUE_BOOL` | `double val` | Stored as 0.0/1.0 |
| 14 (0xE) | `VALUE_ITERATOR` | | |
| 15 (0xF) | `VALUE_REF` | `int v32` | Asset/instance reference id |
| 0xFFFFFF | (UNSET) | — | Declared-but-unassigned sentinel |

## The `YYGet*` argument accessors

A GML built-in receives its arguments as an `RValue* args` array. To extract a typed C
value it calls one of these accessors with `(args, index)`. Each performs type coercion
and, on incompatible types, calls `YYError` with a function-tagged message. All were
identified by their **unique error strings** (100% reliable).

| Address | Name | Returns | Error tag | Coercion summary |
|---|---|---|---|---|
| 0x1401BF360 | `YYGetReal` | `double` | YYGR | REAL/BOOL→val; STRING→parse; OBJECT→coerce; INT32/REF→v32; INT64→v64 |
| 0x1401BEDE0 | `YYGetFloat` | `double` | YYGF | Like YYGetReal but via `float` cast |
| 0x1401BEF50 | `YYGetInt32` | `int` | YYGI32 | REAL/BOOL→trunc; STRING→parse; INT*→raw |
| 0x1401BF650 | `YYGetUint32` | `unsigned` | YYGU32 | As int32, unsigned |
| 0x1401BECE0 | `YYGetBool` | `bool` | YYGB | REAL→`val>0.5`; PTR/OBJECT→`!=0`; INT→`>0`; UNDEFINED→false |
| 0x1401BF490 | `YYGetString` | `const char*` | YYGS | STRING→data ptr; numerics→serialized temp string |
| 0x1401BF270 | `YYGetPtr` | `void*` | "expecting a Pointer" | Only kind 3 (PTR) |
| 0x1401BF2D0 | `YYGetPtrOrInt` | `__int64` | "Number or Pointer" | REAL/PTR/INT32/INT64/REF payload |
| 0x14012E520 | `YYGetStruct` | `RValue*` | "expecting a struct (object)" | Only kind 6 (OBJECT) |

Common coercion details (from the decompiled switch statements):
- **STRING → number**: only treated as numeric if the first char is a digit (or `-`
  followed by a digit for `YYGetFloat`); otherwise it flows to `RValue_ToNumber`.
- The error message format is
  `"%s argument %d incorrect type (%s) expecting a <T>"`, where `%s#1` is the current
  built-in's name (from the global execution context `qword_1408D02A8`, read via
  `Func_GetName` / `RValue_GetKindName`) and `%d` is `index+1` (1-based for users).

## Conversion: `RValue_ToNumber` (`0x1400097D0`)

`int RValue_ToNumber(RValue* dst, RValue* src, char asInt64)` — normalizes any RValue
into a numeric RValue written to `dst`. Returns `0`=ok, `1`=fail, `2`=special. Behavior:
- REAL/BOOL → copy payload, set kind 0.
- STRING → numeric parse supporting: leading/trailing whitespace skipping, decimal,
  `0x`/`0X` hex (digit table `byte_1405F40A8`), and the literals `Infinity`,
  `+Infinity`, `-Infinity` (emits IEEE ±inf bit patterns `0x7FF0…`/`0xFFF0…`). Hex
  values outside int32 range (or `asInt64`) yield an INT64 (kind 10); else REAL.
- OBJECT (kind 6) → if it is a **boxed number** (field `+124 == 1`), reads the int at
  field `+180`; otherwise recurses through an accessor (`sub_140009C20`).
- INT32/REF/INT64/NULL → converted to REAL.

## Lifetime: `RValue_Free` (`0x140002C40`)

`RValue_Free(RValue* v)` releases the **heap payload** of an RValue (not the 16-byte
struct itself), dispatched by kind:
- **STRING (1)**: decrements the RefString refcount (`*(int*)(p+8)`); at zero, frees the
  char buffer (`sub_1401BEC70`) and the header (`sub_14038F1F0(p,16)`).
- **ARRAY (2)**: releases the array object (`sub_14007E0F0` / `sub_14007E150`).
- **PTR (3) with `flags & 8`**: invokes the bound method/object cleanup via its
  vtable slot `(**vtbl)(vtbl, 1)`.

The idiom `if (((1 << (kind & 0x1F)) & 0x46) != 0) RValue_Free(v);` recurs throughout
the codebase. The mask `0x46` = bits {1,2,6} = exactly the heap-owning kinds STRING,
ARRAY, OBJECT — a fast "does this need freeing?" test.

## Serialization: `RValue_AppendToString` (`0x1401BDA30`)

`void RValue_AppendToString(char** pCur, void** pBuf, int* pCap, RValue* v)` — the
recursive value→text formatter that powers `string()`, `YYGetString` on numerics, and
debug output. Per kind:
- REAL → integer-valued numbers print as `%lld`, otherwise `%.2f`; non-finite → `inf`.
- STRING → raw text, wrapped in `"`…`"` only when nested inside an array/struct
  (`dword_1408C9EB8` nesting counter).
- ARRAY → `[ a,b,c ]` with recursive elements; recursion guard prints
  `"Warning: recursive array found"`.
- OBJECT → calls the value's GML `toString` method if present, else recurses members,
  else `null`; recursion guard prints `"Warning: recursive struct found"`.
- PTR → `%p` or `null`; INT64 → `%lld`; REF → `ref %d`; UNDEFINED → `undefined`.

The buffer auto-grows via `sub_140390200` (realloc, doubling). An UNSET value raises
`YYError("STRING argument is unset")`.

## Function registration context

A built-in's name (used in error messages) is fetched from the current execution
context global `qword_1408D02A8` through `Func_GetName` (`0x1401BC740`), which returns
the name or the fallback string `"Unknown Function"`.
