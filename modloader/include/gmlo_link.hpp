// gmlo_link.hpp - runtime linker for GML linkable objects (.gmlo), VM and YYC runners.
//
// compiler/GmlObjectCompiler turns GML into a .gmlo: Underanalyzer bytecode whose reference
// operands are left open, plus a relocation table (var / func / string, by name). This header
// does at run time what the runner's loader does to the CODE chunk (compiler/RUNTIME_COMPILER.md,
// "Load-time operand fixup"), but against the live runner instead of data.win:
//   var     operand = (reference-type bits) | id. Built-ins: YYTK GetBuiltinVariableIndex.
//           Everything else (self/global/local): YYTK GetVariableSlot, which is the runner's
//           find-or-allocate on its one variable-name table (Code_Variable_FindAlloc_Slot_From_Name).
//   func    operand = GetNamedRoutineIndex (built-in index, or 100000 + script index).
//   string  operand = index into the runner's string table. Strings the game already has keep
//           their index; new ones are appended (RunnerStrings below).
// On YYC one more rewrite is needed: a call.i to a compiled script faults (the VM expects the
// script's CCode to hold bytecode), so the call is lowered to script_execute(<id>, args...).
// The linked code is wrapped in a hand-built CCode/VMBuffer and run through YYTK
// ExecuteCodeObject (the runner's own Code_Execute).
//
// Everything runs on the game thread (from a YYTK callback). Linked code objects are never
// freed: VMBuffer::Prepare caches the threaded form of a buffer keyed by its bytecode address,
// so freeing a buffer and getting the same address back would run stale code. Link once, run
// many times.
#pragma once
#include <YYToolkit/YYTK_Shared.hpp>
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace gmlo
{
	using namespace Aurie;
	using namespace YYTK;

	struct Reloc
	{
		uint32_t offset = 0;      // byte offset of the instruction; its operand word is offset + 4
		std::string kind;         // "var", "func", "string"
		std::string name;         // variable / function name, or the string's content
		int instType = 0;         // var: compile-time instance type (-1 self, -5 global, -6 built-in, -7 local)
	};

	struct Object
	{
		std::string name;
		uint32_t locals = 0, args = 0;
		std::vector<uint32_t> code;
		std::vector<Reloc> relocs;
	};

	inline std::string Unescape(const std::string& s)
	{
		std::string o;
		for (size_t i = 0; i < s.size(); i++)
		{
			if (s[i] != '\\' || i + 1 == s.size()) { o += s[i]; continue; }
			char c = s[++i];
			o += c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c;
		}
		return o;
	}

	// Parses the text form written by GmlObjectCompiler (GmlObject.ToText).
	inline bool Parse(const std::string& text, Object& obj, std::string& err)
	{
		size_t pos = 0;
		bool header = false, end = false;
		while (pos < text.size() && !end)
		{
			size_t nl = text.find('\n', pos);
			std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
			pos = nl == std::string::npos ? text.size() : nl + 1;
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty()) continue;
			size_t sp = line.find(' ');
			std::string key = line.substr(0, sp), rest = sp == std::string::npos ? "" : line.substr(sp + 1);
			if (key == "GMLO") { header = rest == "1"; if (!header) { err = "unsupported GMLO version " + rest; return false; } }
			else if (!header) { err = "missing GMLO header"; return false; }
			else if (key == "name") obj.name = rest;
			else if (key == "locals") obj.locals = (uint32_t)strtoul(rest.c_str(), nullptr, 10);
			else if (key == "args") obj.args = (uint32_t)strtoul(rest.c_str(), nullptr, 10);
			else if (key == "code")
			{
				const char* p = rest.c_str();
				char* e = nullptr;
				for (;;)
				{
					while (*p == ' ') p++;
					if (!*p) break;
					obj.code.push_back((uint32_t)strtoul(p, &e, 16));
					if (e == p) { err = "bad code word"; return false; }
					p = e;
				}
			}
			else if (key == "reloc")
			{
				// reloc <offset> var <inst type> <name> | reloc <offset> func <name> | reloc <offset> string <escaped text>
				Reloc r;
				size_t a = rest.find(' '), b = a == std::string::npos ? a : rest.find(' ', a + 1);
				if (b == std::string::npos) { err = "bad reloc: " + line; return false; }
				r.offset = (uint32_t)strtoul(rest.c_str(), nullptr, 10);
				r.kind = rest.substr(a + 1, b - a - 1);
				std::string tail = rest.substr(b + 1);
				if (r.kind == "var")
				{
					size_t c = tail.find(' ');
					if (c == std::string::npos) { err = "bad var reloc: " + line; return false; }
					r.instType = atoi(tail.substr(0, c).c_str());
					r.name = tail.substr(c + 1);
				}
				else if (r.kind == "func" || r.kind == "string") r.name = Unescape(tail);
				else { err = "unknown reloc kind " + r.kind; return false; }
				obj.relocs.push_back(r);
			}
			else if (key == "end") end = true;
			// other keys (local, strings, ...) are informational
		}
		if (!end) { err = "missing end"; return false; }
		for (auto& r : obj.relocs)
			if (r.offset % 4 || r.offset / 4 + 1 >= obj.code.size()) { err = "reloc outside code"; return false; }
		return true;
	}

	// ---- bytecode helpers (bytecode 15+ encoding) -------------------------------------------

	constexpr uint8_t kOpCall = 0xD9, kOpPush = 0xC0;
	constexpr uint32_t kTypeInt32 = 2, kTypeString = 6;
	constexpr uint32_t kPushI32 = 0xC0020000;   // push.i <imm32>
	constexpr uint32_t kConvIV = 0x07520000;    // conv.i.v
	constexpr uint32_t kConvSV = 0x07560000;    // conv.s.v
	constexpr uint32_t kRetV = 0x9C050000;      // ret.v
	constexpr int kScriptIdBase = 100000;

	// Operand size per data type, the runner's own table (VMBuffer::Prepare).
	inline uint32_t InstrWords(uint32_t w)
	{
		static const uint8_t size[16] = { 8, 4, 4, 8, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0 };
		return 1 + ((w & 0x40000000) ? size[(w >> 16) & 0xF] / 4 : 0);
	}

	inline bool IsBranch(uint32_t w)
	{
		uint8_t op = (uint8_t)(w >> 24);
		return op == 0xB6 || op == 0xB7 || op == 0xB8 || op == 0xBA || op == 0xBB;
	}

	inline int32_t BranchWords(uint32_t w)   // 23-bit signed word offset
	{
		int32_t o = (int32_t)(w & 0x7FFFFF);
		return (w & 0x400000) ? o - 0x800000 : o;
	}

	// ---- the runner's string table ------------------------------------------------------------
	//
	// push.s <i> reads a uint32 table: chars = image + table[i] + 4, where image is the loaded
	// data.win (FORM header at offset 0) and table[i] is the file offset of STRG entry i (its
	// length prefix). Both pointers live in the runner's .data; they are found by value, not by
	// address: the image pointer is the one whose target starts with a FORM header, the table
	// pointer the one whose target holds STRG's offsets. New strings are appended by moving the
	// table to a bigger copy; their text is placed less than 4 GB above the image, because the
	// table entries are 32-bit offsets from it.
	class RunnerStrings
	{
	public:
		std::string error;

		bool Ready() const { return m_TableSlot != nullptr; }
		uint32_t Count() const { return m_Count; }
		const void* Image() const { return m_Image; }
		uint32_t** TableSlot() const { return m_TableSlot; }

		bool Discover()
		{
			if (m_TableSlot) return true;
			HMODULE exe = GetModuleHandleW(nullptr);
			auto* dos = (IMAGE_DOS_HEADER*)exe;
			auto* nt = (IMAGE_NT_HEADERS64*)((uint8_t*)exe + dos->e_lfanew);
			auto* sec = IMAGE_FIRST_SECTION(nt);
			std::vector<std::pair<uint64_t*, uint64_t*>> data;   // writable, initialized/uninitialized data
			for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
				if ((sec[i].Characteristics & IMAGE_SCN_MEM_WRITE) && !(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
				{
					auto* b = (uint64_t*)((uint8_t*)exe + sec[i].VirtualAddress);
					data.push_back({ b, b + sec[i].Misc.VirtualSize / 8 });
				}
			// 1. the data.win image: a pointer to "FORM" <size> whose STRG chunk parses.
			for (auto& [b, e] : data)
				for (auto* p = b; p < e && !m_Image; p++)
					if (Readable(*p, 8) && *(uint32_t*)*p == 0x4D524F46 /* FORM */ && ParseStrg((const uint8_t*)*p))
						m_Image = (const uint8_t*)*p;
			if (!m_Image) { error = "data.win image not found"; return false; }
			// 2. the table: a pointer to uint32[] starting with STRG's first offsets.
			for (auto& [b, e] : data)
				for (auto* p = b; p < e && !m_TableSlot; p++)
					if (*p != (uint64_t)m_Image && Readable(*p, 32) && memcmp((void*)*p, m_StrgOffsets, 32) == 0)
						m_TableSlot = (uint32_t**)p;
			if (!m_TableSlot) { error = "string table not found"; return false; }
			m_Capacity = m_Count;
			return true;
		}

		// Runtime index of `s`: an existing string's index, or a new appended entry.
		bool Intern(const std::string& s, uint32_t& index)
		{
			if (!Discover()) return false;
			if (m_Index.empty())
				for (uint32_t i = 0; i < m_Count; i++)
				{
					const uint8_t* e = m_Image + (*m_TableSlot)[i];
					m_Index.emplace(std::string((const char*)e + 4, *(const uint32_t*)e), i);   // first wins
				}
			if (auto it = m_Index.find(s); it != m_Index.end()) { index = it->second; return true; }
			uint8_t* text = Alloc((uint32_t)s.size() + 5);
			if (!text) { error = "no memory within 4 GB above the data.win image"; return false; }
			*(uint32_t*)text = (uint32_t)s.size();
			memcpy(text + 4, s.data(), s.size());
			text[4 + s.size()] = 0;
			if (m_Count == m_Capacity)
			{
				uint32_t cap = m_Capacity * 2 + 16;
				auto* grown = new (std::nothrow) uint32_t[cap];
				if (!grown) { error = "out of memory"; return false; }
				memcpy(grown, *m_TableSlot, m_Count * 4ull);
				*m_TableSlot = grown;   // the old table (inside the image, or our previous copy) stays valid
				m_Capacity = cap;
			}
			(*m_TableSlot)[m_Count] = (uint32_t)(text - m_Image);
			index = m_Count++;
			m_Index.emplace(s, index);
			return true;
		}

	private:
		const uint8_t* m_Image = nullptr;
		uint32_t** m_TableSlot = nullptr;
		uint32_t m_Count = 0, m_Capacity = 0;
		uint32_t m_StrgOffsets[8] = {};
		std::unordered_map<std::string, uint32_t> m_Index;
		uint8_t* m_Pool = nullptr;
		size_t m_PoolLeft = 0;

		// Most scanned qwords are not pointers; the last region queried is cached.
		uint64_t m_RegionLo = 0, m_RegionHi = 0;
		bool m_RegionOk = false;
		bool Readable(uint64_t addr, size_t len)
		{
			if (addr < 0x10000 || addr > 0x7FFFFFFF0000ull) return false;
			if (addr < m_RegionLo || addr >= m_RegionHi)
			{
				MEMORY_BASIC_INFORMATION mbi;
				if (!VirtualQuery((void*)addr, &mbi, sizeof(mbi))) return false;
				m_RegionLo = (uint64_t)mbi.BaseAddress;
				m_RegionHi = m_RegionLo + mbi.RegionSize;
				m_RegionOk = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
					(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
			}
			return m_RegionOk && addr + len <= m_RegionHi;
		}

		bool ParseStrg(const uint8_t* form)
		{
			uint32_t size = *(const uint32_t*)(form + 4);
			if (size < 16 || !Readable((uint64_t)form, size + 8ull)) return false;
			for (uint32_t o = 8; o + 8 <= size + 8;)
			{
				uint32_t len = *(const uint32_t*)(form + o + 4);
				if (memcmp(form + o, "STRG", 4) == 0)
				{
					uint32_t n = *(const uint32_t*)(form + o + 8);
					if (n < 8) return false;
					m_Count = n;
					memcpy(m_StrgOffsets, form + o + 12, sizeof(m_StrgOffsets));
					return true;
				}
				o += 8 + len;
			}
			return false;
		}

		uint8_t* Alloc(uint32_t n)
		{
			n = (n + 7) & ~7u;
			if (n > m_PoolLeft)
			{
				size_t want = n > 0x10000 ? (n + 0xFFFF) & ~0xFFFFull : 0x10000;
				uint64_t lo = (uint64_t)m_Image, hi = lo + 0xFFFF0000ull - want;
				MEMORY_BASIC_INFORMATION mbi;
				m_Pool = nullptr;
				for (uint64_t a = lo; a < hi && VirtualQuery((void*)a, &mbi, sizeof(mbi)); a = (uint64_t)mbi.BaseAddress + mbi.RegionSize)
				{
					uint64_t start = ((uint64_t)mbi.BaseAddress + 0xFFFF) & ~0xFFFFull;
					if (mbi.State == MEM_FREE && start + want <= (uint64_t)mbi.BaseAddress + mbi.RegionSize && start + want <= hi)
						if ((m_Pool = (uint8_t*)VirtualAlloc((void*)start, want, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)))
							break;
				}
				if (!m_Pool) return nullptr;
				m_PoolLeft = want;
			}
			uint8_t* r = m_Pool;
			m_Pool += n;
			m_PoolLeft -= n;
			return r;
		}
	};

	// ---- linker ----------------------------------------------------------------------------

	// CCode / VMBuffer fields Code_Execute and VM::Exec read (same offsets on the 2024 VM and
	// YYC runners; see RUNTIME_COMPILER.md "YYC: running linked bytecode").
	constexpr size_t kCCodeSize = 0xB8, kCCodeKind = 0x10, kCCodeVmBuffer = 0x68, kCCodeName = 0x80,
		kCCodeIndex = 0x88, kCCodeCompiledFn = 0x90, kCCodeLocals = 0xA0, kCCodeArgs = 0xA4;
	constexpr size_t kVmBufferSize = 0x30, kVmBufferLength = 0x08, kVmBufferCode = 0x18, kVmBufferPrepared = 0x20;

	struct Linked
	{
		std::string name;
		void* ccode = nullptr;        // runner CCode layout, pass to ExecuteCodeObject
		uint32_t* code = nullptr;     // linked bytecode (owned by the VMBuffer, never freed)
		size_t words = 0;
		int loweredScriptCalls = 0;   // YYC: call.i <script> rewritten to script_execute
		int newStrings = 0;           // strings appended to the runner's table
	};

	class Linker
	{
	public:
		explicit Linker(YYTK::YYTKInterface* yytk) : m_Yytk(yytk) { m_Yyc = yytk->IsYYCRunner(); }

		bool IsYYC() const { return m_Yyc; }
		RunnerStrings& Strings() { return m_Strings; }

		// Links `obj` against the running game. On failure returns nullptr and sets `err`.
		Linked* Link(const Object& obj, std::string& err)
		{
			std::vector<uint32_t> code = obj.code;
			std::vector<Reloc> relocs = obj.relocs;
			auto* out = new Linked();
			out->name = obj.name;

			// 1. function ids (needed first: scripts are lowered on YYC).
			std::map<std::string, int> funcIds;
			for (auto& r : relocs)
				if (r.kind == "func" && !funcIds.count(r.name))
				{
					int id = -1;
					if (!AurieSuccess(m_Yytk->GetNamedRoutineIndex(r.name.c_str(), &id)) || id < 0)
					{ err = "unresolved function " + r.name; delete out; return nullptr; }
					funcIds[r.name] = id;
				}
			// 2. YYC: call.i <script> -> push.i <script id>; conv.i.v; call.i script_execute(argc + 1)
			if (m_Yyc)
			{
				std::vector<uint32_t> calls;   // word index of each script call
				for (auto& r : relocs)
					if (r.kind == "func" && (uint8_t)(code[r.offset / 4] >> 24) == kOpCall && funcIds[r.name] >= kScriptIdBase)
						calls.push_back(r.offset / 4);
				if (!calls.empty())
				{
					int scriptExecute = -1;
					if (!AurieSuccess(m_Yytk->GetNamedRoutineIndex("script_execute", &scriptExecute)) || scriptExecute < 0)
					{ err = "script_execute not found"; delete out; return nullptr; }
					if (!LowerScriptCalls(code, relocs, calls, funcIds, scriptExecute, err)) { delete out; return nullptr; }
					out->loweredScriptCalls = (int)calls.size();
				}
			}
			// 3. operands
			RValue global;
			{
				CInstance* g = nullptr;
				m_Yytk->GetGlobalInstance(&g);
				global = RValue(g);
			}
			for (auto& r : relocs)
			{
				uint32_t& op = code[r.offset / 4 + 1];
				if (r.kind == "func")
				{
					if (op == 0) op = (uint32_t)funcIds[r.name];   // lowered calls already hold script_execute
				}
				else if (r.kind == "var")
				{
					int32_t id = -1;
					size_t builtin = 0;
					if (r.instType != -7 && AurieSuccess(m_Yytk->GetBuiltinVariableIndex(r.name, builtin)))
						id = (int32_t)builtin;
					else if (!AurieSuccess(m_Yytk->GetVariableSlot(global, r.name.c_str(), id)) || id < 0)
					{ err = "no variable slot for " + r.name; delete out; return nullptr; }
					op = (op & 0xF8000000u) | ((uint32_t)id & 0x07FFFFFFu);
				}
				else if (r.kind == "string")
				{
					uint32_t before = m_Strings.Count(), index = 0;
					if (!m_Strings.Intern(r.name, index)) { err = "string table: " + m_Strings.error; delete out; return nullptr; }
					if (m_Strings.Count() != before) out->newStrings++;
					op = index;
				}
			}
			out->code = new uint32_t[code.size()];
			memcpy(out->code, code.data(), code.size() * 4);
			out->words = code.size();
			out->ccode = MakeCCode(out->code, code.size(), obj);
			return out;
		}

		// Runs linked code as `self` (and `other`); SEH-guarded. Returns false on a fault or a
		// false Code_Execute, with `err` set.
		bool Run(Linked* l, CInstance* self, CInstance* other, RValue& result, std::string& err)
		{
			AurieStatus st = AURIE_SUCCESS;
			DWORD ex = SehRun(l->ccode, self, other, &result, &st);
			if (ex) { char b[64]; snprintf(b, sizeof(b), "exception 0x%08lX", ex); err = b; return false; }
			if (!AurieSuccess(st)) { err = std::string("ExecuteCodeObject: ") + AurieStatusToString(st); return false; }
			return true;
		}

		// Runs `push.s index; conv.s.v; ret.v` and returns the string the VM produced.
		bool ReadRunnerString(uint32_t index, std::string& value, std::string& err)
		{
			Object o;
			o.name = "gml_Script_gmlo_strcheck";
			o.code = { 0xC0060000, index, kConvSV, kRetV };
			auto* code = new uint32_t[o.code.size()];
			memcpy(code, o.code.data(), o.code.size() * 4);
			Linked l{ o.name, MakeCCode(code, o.code.size(), o), code, o.code.size() };
			CInstance* g = nullptr;
			m_Yytk->GetGlobalInstance(&g);
			RValue r;
			if (!Run(&l, g, g, r, err)) return false;
			value = r.ToString();
			return true;
		}

	private:
		YYTK::YYTKInterface* m_Yytk;
		bool m_Yyc = false;
		RunnerStrings m_Strings;

		static void* MakeCCode(uint32_t* code, size_t words, const Object& obj)
		{
			auto* vmbuf = new (std::align_val_t(16)) unsigned char[kVmBufferSize]();
			*(int32_t*)(vmbuf + kVmBufferLength) = (int32_t)(words * 4);
			*(void**)(vmbuf + kVmBufferCode) = code;
			auto* cc = new (std::align_val_t(16)) unsigned char[kCCodeSize]();
			auto* name = new char[obj.name.size() + 1];
			memcpy(name, obj.name.c_str(), obj.name.size() + 1);
			*(int32_t*)(cc + kCCodeKind) = 1;
			*(void**)(cc + kCCodeVmBuffer) = vmbuf;
			*(const char**)(cc + kCCodeName) = name;
			*(int32_t*)(cc + kCCodeIndex) = -1;
			*(void**)(cc + kCCodeCompiledFn) = nullptr;   // YYC: null selects the VM path
			*(int32_t*)(cc + kCCodeLocals) = (int32_t)obj.locals;
			*(int32_t*)(cc + kCCodeArgs) = (int32_t)obj.args;
			return cc;
		}

		DWORD SehRun(void* cc, CInstance* self, CInstance* other, RValue* result, AurieStatus* st)
		{
			__try { *st = m_Yytk->ExecuteCodeObject(self, other, (CCode*)cc, *result, 0); return 0; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
		}

		// Inserts 3 words before each script call and fixes branch offsets and relocation offsets.
		static bool LowerScriptCalls(std::vector<uint32_t>& code, std::vector<Reloc>& relocs,
			const std::vector<uint32_t>& calls, std::map<std::string, int>& funcIds, int scriptExecute, std::string& err)
		{
			constexpr uint32_t kInsert = 3;
			std::vector<uint32_t> starts;   // instruction start words
			for (uint32_t i = 0; i < code.size(); i += InstrWords(code[i])) starts.push_back(i);
			std::vector<uint32_t> shift(code.size() + 1, 0);   // insertions strictly before word i
			{
				uint32_t s = 0; size_t c = 0;
				std::vector<uint32_t> sorted = calls;
				std::sort(sorted.begin(), sorted.end());
				for (uint32_t i = 0; i <= code.size(); i++)
				{
					shift[i] = s;
					if (c < sorted.size() && sorted[c] == i) { s += kInsert; c++; }
				}
			}
			auto isCall = [&](uint32_t i) { return std::find(calls.begin(), calls.end(), i) != calls.end(); };
			std::map<uint32_t, int> callScript;   // call word -> script id
			for (auto& r : relocs)
				if (r.kind == "func" && isCall(r.offset / 4)) callScript[r.offset / 4] = funcIds[r.name];
			std::vector<uint32_t> out;
			out.reserve(code.size() + calls.size() * kInsert);
			for (uint32_t i : starts)
			{
				uint32_t n = InstrWords(code[i]);
				uint32_t w = code[i];
				if (isCall(i))
				{
					out.push_back(kPushI32);
					out.push_back((uint32_t)callScript[i]);
					out.push_back(kConvIV);
					uint32_t argc = (w & 0xFFFF) + 1;
					out.push_back((w & 0xFFFF0000u) | argc);
					out.push_back((uint32_t)scriptExecute);
					continue;
				}
				if (IsBranch(w) && (w & 0xFFFFFF) != 0xF00000)
				{
					int64_t target = (int64_t)i + BranchWords(w);
					if (target < 0 || target > (int64_t)code.size()) { err = "branch outside code"; return false; }
					int64_t now = (int64_t)i + shift[i];
					int64_t to = target + shift[target];   // lands on the inserted push.i of a lowered call
					w = (w & 0xFF800000u) | ((uint32_t)(to - now) & 0x7FFFFFu);
				}
				out.push_back(w);
				for (uint32_t k = 1; k < n; k++) out.push_back(code[i + k]);
			}
			for (auto& r : relocs)
			{
				uint32_t i = r.offset / 4;
				r.offset = (i + shift[i] + (isCall(i) ? kInsert : 0)) * 4;
			}
			code.swap(out);
			return true;
		}
	};
}
