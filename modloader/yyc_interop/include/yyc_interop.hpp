// yyc_interop.hpp - call and hook compiled GML by name in YYC GameMaker games.
//
// The IL2CPP-interop equivalent for YYC (see docs/yyc_il2cpp.md). Every compiled script has
// the same native ABI, so no per-script code generation is needed:
//
//   RValue& script(CInstance* self, CInstance* other, RValue& result, int argc, RValue** argv);
//   void    event (CInstance* self, CInstance* other);          // gml_Object_<obj>_<Event>_<n>
//
// Names are resolved at run time from the runner's own YYGMLFuncs table, never from fixed
// addresses, so a mod keeps working across game patches as long as the GML name still exists.
// Script-to-script calls in YYC are direct `call`s, so hooks are inline detours (Aurie
// MmCreateHook) on the function entry, which catch every call path.
//
// Header-only. Needs YYToolkit v5 (YYTK_Shared.hpp) and its Aurie headers.
#pragma once
#include <YYToolkit/YYTK_Shared.hpp>
#include <windows.h>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yyc
{
	using namespace YYTK;

	// One entry of the runner's static script/event table (YYTK's YYGMLFuncs).
	struct Function
	{
		const char* name;
		void* code;            // PFUNC_YYGMLScript for scripts, PFUNC_YYGML for object events
	};

	namespace detail
	{
		inline bool InExeText(const void* p)
		{
			static uintptr_t lo = 0, hi = 0;
			if (!lo)
			{
				auto base = (uintptr_t)GetModuleHandleW(nullptr);
				auto nt = (const IMAGE_NT_HEADERS64*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
				auto s = IMAGE_FIRST_SECTION(nt);
				for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
					if (!strncmp((const char*)s->Name, ".text", 8))
					{
						lo = base + s->VirtualAddress;
						hi = lo + s->Misc.VirtualSize;
					}
			}
			return (uintptr_t)p >= lo && (uintptr_t)p < hi;
		}

		inline bool Readable(const void* p, size_t n)
		{
			MEMORY_BASIC_INFORMATION mbi;
			if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
				return false;
			if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
				return false;
			return (uintptr_t)p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
		}

		inline bool IsEntry(const YYGMLFuncs* e)
		{
			return Readable(e, sizeof(*e)) && Readable(e->m_Name, 5) &&
				!strncmp(e->m_Name, "gml_", 4) && InExeText((void*)e->m_RawFunction);
		}
	}

	// Name -> entry for every compiled script and object event, built once from the static
	// YYGMLFuncs array. Any script's CScript::m_Functions points into that array, so walk
	// outwards from there.
	class Registry
	{
	public:
		bool Init(YYTKInterface* yytk)
		{
			if (!m_ByName.empty())
				return true;
			CScript* s = nullptr;
			for (int i = 0; i < 64 && !m_Table; i++)
				if (AurieSuccess(yytk->GetScriptData(i, s)) && s && detail::IsEntry(s->m_Functions))
					m_Table = s->m_Functions;
			if (!m_Table)
				return false;   // VM game, or a runner whose CScript layout differs
			const YYGMLFuncs* lo = m_Table;
			while (detail::IsEntry(lo - 1)) lo--;
			const YYGMLFuncs* hi = m_Table;
			while (detail::IsEntry(hi + 1)) hi++;
			for (const YYGMLFuncs* e = lo; e <= hi; e++)
				m_ByName.emplace(e->m_Name, Function{ e->m_Name, (void*)e->m_RawFunction });
			return true;
		}

		size_t Count() const { return m_ByName.size(); }

		// Accepts the full name ("gml_Script_foo", "gml_Object_oPlayer_Step_0") or a bare
		// script name ("foo").
		const Function* Find(std::string_view name) const
		{
			auto it = m_ByName.find(std::string(name));
			if (it == m_ByName.end() && name.substr(0, 4) != "gml_")
				it = m_ByName.find("gml_Script_" + std::string(name));
			return it == m_ByName.end() ? nullptr : &it->second;
		}

		template <class Fn>
		void ForEach(Fn&& fn) const
		{
			for (const auto& [name, f] : m_ByName)
				fn(f);
		}

	private:
		const YYGMLFuncs* m_Table = nullptr;
		std::unordered_map<std::string, Function> m_ByName;
	};

	inline Registry& Functions()
	{
		static Registry r;
		return r;
	}

	// Calls a compiled script directly (no GML lookup per call). Returns false if not found.
	inline bool CallScript(std::string_view name, CInstance* self, CInstance* other,
		RValue& result, std::vector<RValue> args = {})
	{
		const Function* f = Functions().Find(name);
		if (!f || !strncmp(f->name, "gml_Object_", 11))
			return false;
		std::vector<RValue*> argv;
		argv.reserve(args.size());
		for (auto& a : args)
			argv.push_back(&a);
		reinterpret_cast<PFUNC_YYGMLScript>(f->code)(self, other, result,
			static_cast<int>(argv.size()), argv.data());
		return true;
	}

	// Inline detour on a compiled script or event. `Original` receives the trampoline; call it
	// from the detour to run the game's code. The hook id is the GML name.
	template <class T>
	inline Aurie::AurieStatus Hook(Aurie::AurieModule* module, std::string_view name, T detour, T* original)
	{
		const Function* f = Functions().Find(name);
		if (!f)
			return Aurie::AURIE_OBJECT_NOT_FOUND;
		PVOID trampoline = nullptr;
		Aurie::AurieStatus s = Aurie::MmCreateHook(module, f->name, f->code,
			reinterpret_cast<PVOID>(detour), &trampoline);
		if (Aurie::AurieSuccess(s) && original)
			*original = reinterpret_cast<T>(trampoline);
		return s;
	}

	inline Aurie::AurieStatus Unhook(Aurie::AurieModule* module, std::string_view name)
	{
		const Function* f = Functions().Find(name);
		return f ? Aurie::MmRemoveHook(module, f->name) : Aurie::AURIE_OBJECT_NOT_FOUND;
	}
}
