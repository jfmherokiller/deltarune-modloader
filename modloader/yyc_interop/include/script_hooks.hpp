// script_hooks.hpp - one script-hook API for both GameMaker runner types.
//
//   gm::Init(module, yytk);                     // once, on the game thread
//   gm::Observe("scr_foo", fn, user);           // fn runs before every call of scr_foo
//   gm::Observe("", fn, user);                  // ... before every script call
//   gm::Redirect("scr_foo", "scr_bar");         // calls of scr_foo run scr_bar instead
//
// Backends, chosen at run time from the GML built-in code_is_compiled():
//   VM  - vm_hooks.hpp: one hook on the interpreter's frame-push routine.
//   YYC - yyc_interop.hpp: inline detours on the compiled script functions. Observers use a
//         fixed pool of detour thunks (MaxYycObserved scripts); observe-all is not available
//         on YYC, because it would mean detouring every compiled script.
//
// Scripts only (gml_Script_*). Object events go through Code_Execute on both runners and are
// already covered by YYToolkit's EVENT_OBJECT_CALL.
#pragma once
#include <yyc_interop.hpp>
#include <vm_hooks.hpp>
#include <array>
#include <utility>

namespace gm
{
	using namespace YYTK;
	using CallInfo = yyc::vm::CallInfo;
	using Observer = yyc::vm::Observer;
	enum class Runner { Unknown, VM, YYC };

	constexpr size_t MaxYycObserved = 64;

	namespace detail
	{
		struct Slot
		{
			const char* name = nullptr;
			void* code = nullptr;
			PFUNC_YYGMLScript original = nullptr;
			std::vector<std::pair<Observer, void*>> observers;
		};
		struct State
		{
			Runner runner = Runner::Unknown;
			Aurie::AurieModule* module = nullptr;
			std::array<Slot, MaxYycObserved> slots;
			size_t used = 0;
		};
		inline State& S() { static State s; return s; }

		template <size_t I>
		RValue& Thunk(CInstance* self, CInstance* other, RValue& result, int argc, RValue** argv)
		{
			Slot& slot = S().slots[I];
			CallInfo call{ nullptr, slot.name, self, other, argc };
			for (auto& [fn, user] : slot.observers)
				fn(call, user);
			return slot.original(self, other, result, argc, argv);
		}

		template <size_t... I>
		constexpr std::array<PFUNC_YYGMLScript, sizeof...(I)> MakeThunks(std::index_sequence<I...>)
		{
			return { &Thunk<I>... };
		}
		inline const auto& Thunks()
		{
			static const auto t = MakeThunks(std::make_index_sequence<MaxYycObserved>{});
			return t;
		}

		inline const yyc::Function* FindYycScript(std::string_view name)
		{
			const yyc::Function* f = yyc::Functions().Find(name);
			return f && !strncmp(f->name, "gml_Script_", 11) ? f : nullptr;
		}
	}

	inline Runner GetRunner() { return detail::S().runner; }

	inline Aurie::AurieStatus Init(Aurie::AurieModule* module, YYTKInterface* yytk)
	{
		detail::State& s = detail::S();
		if (s.runner != Runner::Unknown)
			return Aurie::AURIE_SUCCESS;
		RValue compiled;
		Aurie::AurieStatus st = yytk->CallBuiltinEx(compiled, "code_is_compiled", nullptr, nullptr, {});
		if (!AurieSuccess(st))
			return st;
		s.module = module;
		if (compiled.ToBoolean())
		{
			if (!yyc::Functions().Init(yytk))
				return Aurie::AURIE_OBJECT_NOT_FOUND;
			s.runner = Runner::YYC;
			return Aurie::AURIE_SUCCESS;
		}
		st = yyc::vm::Init(module, yytk);
		if (AurieSuccess(st))
			s.runner = Runner::VM;
		return st;
	}

	inline Aurie::AurieStatus Observe(std::string_view name, Observer fn, void* user = nullptr)
	{
		detail::State& s = detail::S();
		if (s.runner == Runner::VM)
			return yyc::vm::Observe(name, fn, user);
		if (s.runner != Runner::YYC)
			return Aurie::AURIE_MODULE_INITIALIZATION_FAILED;
		if (!fn)
			return Aurie::AURIE_INVALID_PARAMETER;
		if (name.empty())
			return Aurie::AURIE_NOT_IMPLEMENTED;
		const yyc::Function* f = detail::FindYycScript(name);
		if (!f)
			return Aurie::AURIE_OBJECT_NOT_FOUND;
		for (size_t i = 0; i < s.used; i++)
			if (s.slots[i].code == f->code)
			{
				s.slots[i].observers.emplace_back(fn, user);
				return Aurie::AURIE_SUCCESS;
			}
		if (s.used == MaxYycObserved)
			return Aurie::AURIE_INSUFFICIENT_MEMORY;
		detail::Slot& slot = s.slots[s.used];
		slot.name = f->name;
		slot.code = f->code;
		slot.observers.emplace_back(fn, user);
		PVOID trampoline = nullptr;
		Aurie::AurieStatus st = Aurie::MmCreateHook(s.module, f->name, f->code,
			reinterpret_cast<PVOID>(detail::Thunks()[s.used]), &trampoline);
		if (!AurieSuccess(st))
		{
			slot = {};
			return st;
		}
		slot.original = reinterpret_cast<PFUNC_YYGMLScript>(trampoline);
		s.used++;
		return Aurie::AURIE_SUCCESS;
	}

	// On YYC a script that is redirected cannot also be observed (both are detours on its entry).
	inline Aurie::AurieStatus Redirect(std::string_view name, std::string_view replacement)
	{
		detail::State& s = detail::S();
		if (s.runner == Runner::VM)
			return yyc::vm::Redirect(name, replacement);
		if (s.runner != Runner::YYC)
			return Aurie::AURIE_MODULE_INITIALIZATION_FAILED;
		const yyc::Function* f = detail::FindYycScript(name);
		if (!f)
			return Aurie::AURIE_OBJECT_NOT_FOUND;
		std::string id = std::string("redirect:") + f->name;
		if (replacement.empty())
			return Aurie::MmRemoveHook(s.module, id.c_str());
		const yyc::Function* with = detail::FindYycScript(replacement);
		if (!with)
			return Aurie::AURIE_OBJECT_NOT_FOUND;
		PVOID trampoline = nullptr;
		return Aurie::MmCreateHook(s.module, id.c_str(), f->code, with->code, &trampoline);
	}
}
