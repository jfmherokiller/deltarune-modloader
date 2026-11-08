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
//
// One set of hooks per process: the first plugin whose gm::Init runs installs the hooks and
// publishes them as the Aurie interface "GmScriptHooks"; every later plugin's gm::Init finds
// that interface and registers its observers and redirects there. Without this, each plugin
// would hook the same routine again and the hooks would see each other's redirects in load
// order.
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
	constexpr const char* InterfaceName = "GmScriptHooks";

	// The shared hook set (see above). Strings are NUL-terminated; empty name = every script.
	struct ScriptHooksInterface : Aurie::AurieInterfaceBase
	{
		virtual Runner GetRunner() = 0;
		virtual Aurie::AurieStatus Observe(const char* name, Observer fn, void* user) = 0;
		virtual Aurie::AurieStatus Redirect(const char* name, const char* replacement) = 0;
	};

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
			ScriptHooksInterface* shared = nullptr;   // the process-wide hook set
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

	namespace detail
	{
		inline Aurie::AurieStatus LocalObserve(std::string_view name, Observer fn, void* user)
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
		inline Aurie::AurieStatus LocalRedirect(std::string_view name, std::string_view replacement)
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

		struct LocalHooks final : ScriptHooksInterface
		{
			Aurie::AurieStatus Create() override { return Aurie::AURIE_SUCCESS; }
			void Destroy() override {}
			void QueryVersion(short& major, short& minor, short& patch) override { major = 1; minor = 0; patch = 0; }
			Runner GetRunner() override { return S().runner; }
			Aurie::AurieStatus Observe(const char* name, Observer fn, void* user) override
			{
				return LocalObserve(name ? name : "", fn, user);
			}
			Aurie::AurieStatus Redirect(const char* name, const char* replacement) override
			{
				return LocalRedirect(name ? name : "", replacement ? replacement : "");
			}
		};
		inline LocalHooks& Local() { static LocalHooks h; return h; }
	}

	inline Runner GetRunner() { return detail::S().runner; }

	// True when this plugin installed the hooks; false when it uses another plugin's.
	inline bool OwnsHooks() { return detail::S().shared == &detail::Local(); }

	inline Aurie::AurieStatus Init(Aurie::AurieModule* module, YYTKInterface* yytk)
	{
		detail::State& s = detail::S();
		if (s.shared)
			return Aurie::AURIE_SUCCESS;
		Aurie::AurieInterfaceBase* existing = nullptr;
		if (AurieSuccess(Aurie::ObGetInterface(InterfaceName, existing)) && existing)
		{
			s.shared = static_cast<ScriptHooksInterface*>(existing);
			s.runner = s.shared->GetRunner();
			return Aurie::AURIE_SUCCESS;
		}
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
		}
		else
		{
			st = yyc::vm::Init(module, yytk);
			if (!AurieSuccess(st))
				return st;
			s.runner = Runner::VM;
		}
		s.shared = &detail::Local();
		Aurie::ObCreateInterface(module, s.shared, InterfaceName);
		return Aurie::AURIE_SUCCESS;
	}

	// Calls `fn` before every call of `name`, or of every script when `name` is empty (VM only).
	inline Aurie::AurieStatus Observe(std::string_view name, Observer fn, void* user = nullptr)
	{
		auto* h = detail::S().shared;
		return h ? h->Observe(std::string(name).c_str(), fn, user) : Aurie::AURIE_MODULE_INITIALIZATION_FAILED;
	}

	// Calls of `name` run `replacement` instead; an empty replacement removes the redirect.
	inline Aurie::AurieStatus Redirect(std::string_view name, std::string_view replacement)
	{
		auto* h = detail::S().shared;
		return h ? h->Redirect(std::string(name).c_str(), std::string(replacement).c_str())
			: Aurie::AURIE_MODULE_INITIALIZATION_FAILED;
	}
}
