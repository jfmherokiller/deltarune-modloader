// vm_hooks.hpp - observe and redirect GML script calls by name on VM (bytecode) runners.
//
// On a VM runner a script-to-script call does not go through Code_Execute: the interpreter's
// call handler (DoCall) pushes a new frame with an internal routine and keeps running in the
// same interpreter loop. Every bytecode script call - `call.i`, method calls, `script_execute`
// - reaches that routine with the callee's CScript*:
//
//   RValue* VM_PushFrame(CScript* script, int argc, RValue* sp, VMExec* vm, YYObjectBase* locals);
//
// One inline hook on it gives per-script observers and redirects. The routine is located at
// run time from the debug-trace string it alone references ("script call = %s\n"), not from
// a fixed address. The same routine exists in YYC runners (the VM is linked there too), but
// compiled YYC scripts never reach it; use yyc_interop.hpp's Hook() there.
//
// Limits, by construction: observers run before the callee starts (there is no return
// point to hook, the callee runs later in the same interpreter loop), and a redirect swaps
// which script runs, so it cannot change arguments or wrap the result.
//
// Header-only. Call vm::Init() once from the game thread (e.g. the first EVENT_FRAME).
// Observe/Redirect must also be called from the game thread.
#pragma once
#include <YYToolkit/YYTK_Shared.hpp>
#include <windows.h>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yyc::vm
{
	using namespace YYTK;

	struct CallInfo
	{
		CScript* script;      // callee (after any redirect)
		const char* name;     // callee name as the runner stores it, e.g. "gml_Script_scr_foo"
		CInstance* self;      // caller's self/other; the callee runs with the same pair
		CInstance* other;
		int argc;
	};
	using Observer = void (*)(const CallInfo& call, void* user);

	namespace detail
	{
		// VMExec fields read here. VMExec is internal to the runner and not in YYToolkit; these
		// offsets come from DoCall in DELTARUNE (GM 2024.x) and are the ones DoCall itself uses
		// for its own gml_method(self, other, ...) call.
		constexpr size_t VMExec_Self = 0x28, VMExec_Other = 0x30;

		using PFN_PushFrame = RValue* (*)(CScript*, int, RValue*, void*, void*);

		struct Watch
		{
			std::vector<std::pair<Observer, void*>> observers;
			CScript* redirect = nullptr;
		};

		struct State
		{
			PFN_PushFrame original = nullptr;
			void* target = nullptr;
			std::unordered_map<std::string, CScript*> byName;
			std::unordered_map<CScript*, Watch> watches;
			std::vector<std::pair<Observer, void*>> all;   // observers of every call
		};
		inline State& S() { static State s; return s; }

		inline bool Section(const char* name, uint8_t*& lo, uint8_t*& hi)
		{
			auto base = (uint8_t*)GetModuleHandleW(nullptr);
			auto nt = (const IMAGE_NT_HEADERS64*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
			auto s = IMAGE_FIRST_SECTION(nt);
			for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
				if (!strncmp((const char*)s->Name, name, 8))
				{
					lo = base + s->VirtualAddress;
					hi = lo + s->Misc.VirtualSize;
					return true;
				}
			return false;
		}

		// Start of the one function in .text whose code takes the address of `text` (a
		// NUL-terminated string in .rdata) with a RIP-relative LEA. nullptr if none or several.
		inline void* FunctionReferencing(const char* text)
		{
			uint8_t *rlo, *rhi, *tlo, *thi;
			if (!Section(".rdata", rlo, rhi) || !Section(".text", tlo, thi))
				return nullptr;
			size_t n = strlen(text) + 1;
			const uint8_t* str = nullptr;
			for (uint8_t* p = rlo; p + n <= rhi; p++)
				if (*p == (uint8_t)text[0] && !memcmp(p, text, n) && (p == rlo || p[-1] == 0))
				{
					if (str) return nullptr;   // ambiguous string
					str = p;
				}
			if (!str)
				return nullptr;
			void* found = nullptr;
			for (uint8_t* p = tlo; p + 7 <= thi; p++)
			{
				// REX.W/REX.WR 8D /r with ModRM mod=00 rm=101 (RIP-relative)
				if ((p[0] != 0x48 && p[0] != 0x4C) || p[1] != 0x8D || (p[2] & 0xC7) != 0x05)
					continue;
				if (p + 7 + *(const int32_t*)(p + 3) != str)
					continue;
				DWORD64 image = 0;
				auto fe = RtlLookupFunctionEntry((DWORD64)p, &image, nullptr);
				if (!fe)
					continue;
				void* start = (void*)(image + fe->BeginAddress);
				// Chained unwind info (cold parts of a function) points back to the primary entry.
				auto ui = (const uint8_t*)(image + fe->UnwindData);
				while (ui[0] & (UNW_FLAG_CHAININFO << 3))
				{
					size_t codes = (ui[2] + 1) & ~1u;
					auto chained = (const RUNTIME_FUNCTION*)(ui + 4 + codes * 2);
					start = (void*)(image + chained->BeginAddress);
					ui = (const uint8_t*)(image + chained->UnwindData);
				}
				if (found && found != start)
					return nullptr;
				found = start;
			}
			return found;
		}

		inline RValue* HkPushFrame(CScript* script, int argc, RValue* sp, void* vm, void* locals)
		{
			State& s = S();
			if (script && vm)
			{
				// Observers belong to the script that actually runs, as on YYC, where a redirect
				// jumps straight into the replacement's (possibly observed) entry.
				auto it = s.watches.find(script);
				if (it != s.watches.end() && it->second.redirect)
				{
					script = it->second.redirect;
					it = s.watches.find(script);
				}
				if (!s.all.empty() || it != s.watches.end())
				{
					CallInfo call{ script, script->m_Name,
						*(CInstance**)((uint8_t*)vm + VMExec_Self),
						*(CInstance**)((uint8_t*)vm + VMExec_Other), argc };
					for (auto& [fn, user] : s.all)
						fn(call, user);
					if (it != s.watches.end())
						for (auto& [fn, user] : it->second.observers)
							fn(call, user);
				}
			}
			return s.original(script, argc, sp, vm, locals);
		}
	}

	// Finds the frame-push routine, builds the name table from YYToolkit's script list and
	// installs the hook. Safe to call again after success.
	inline Aurie::AurieStatus Init(Aurie::AurieModule* module, YYTKInterface* yytk)
	{
		detail::State& s = detail::S();
		if (s.original)
			return Aurie::AURIE_SUCCESS;
		CScript* script = nullptr;
		for (int i = 0; AurieSuccess(yytk->GetScriptData(i, script)); i++)
			if (script && script->m_Name)
				s.byName.emplace(script->m_Name, script);
		if (s.byName.empty())
			return Aurie::AURIE_OBJECT_NOT_FOUND;
		s.target = detail::FunctionReferencing("script call = %s\n");
		if (!s.target)
			return Aurie::AURIE_MODULE_INTERNAL_ERROR;
		PVOID trampoline = nullptr;
		Aurie::AurieStatus st = Aurie::MmCreateHook(module, "VM_PushFrame", s.target,
			reinterpret_cast<PVOID>(&detail::HkPushFrame), &trampoline);
		if (AurieSuccess(st))
			s.original = reinterpret_cast<detail::PFN_PushFrame>(trampoline);
		return st;
	}

	inline void* PushFrameAddress() { return detail::S().target; }
	inline size_t ScriptCount() { return detail::S().byName.size(); }

	// Accepts "gml_Script_foo" or a bare "foo".
	inline CScript* Find(std::string_view name)
	{
		auto& m = detail::S().byName;
		auto it = m.find(std::string(name));
		if (it == m.end() && name.substr(0, 4) != "gml_")
			it = m.find("gml_Script_" + std::string(name));
		return it == m.end() ? nullptr : it->second;
	}

	template <class Fn>
	inline void ForEach(Fn&& fn)
	{
		for (const auto& [name, script] : detail::S().byName)
			fn(name, script);
	}

	// Calls `fn` before every call of `name`, or of every script when `name` is empty.
	inline Aurie::AurieStatus Observe(std::string_view name, Observer fn, void* user = nullptr)
	{
		if (!fn)
			return Aurie::AURIE_INVALID_PARAMETER;
		if (name.empty())
		{
			detail::S().all.emplace_back(fn, user);
			return Aurie::AURIE_SUCCESS;
		}
		CScript* script = Find(name);
		if (!script)
			return Aurie::AURIE_OBJECT_NOT_FOUND;
		detail::S().watches[script].observers.emplace_back(fn, user);
		return Aurie::AURIE_SUCCESS;
	}

	// Runs `replacement` whenever `name` is called (same arguments, self and other).
	// Observers of `replacement` then fire; observers of `name` do not. Redirects are not
	// chained. An empty replacement removes the redirect.
	inline Aurie::AurieStatus Redirect(std::string_view name, std::string_view replacement)
	{
		CScript* script = Find(name);
		CScript* with = replacement.empty() ? nullptr : Find(replacement);
		if (!script || (!replacement.empty() && !with))
			return Aurie::AURIE_OBJECT_NOT_FOUND;
		detail::S().watches[script].redirect = with;
		return Aurie::AURIE_SUCCESS;
	}
}
