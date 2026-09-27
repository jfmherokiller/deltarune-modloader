# TODO / Workflow

Durable notes so work can be picked up later. Two parallel tracks:

- **Track A** — extend the static RE of `DELTARUNE.exe`: more function names, more types.
- **Track B** — exercise the newly-installed Linux RE tools, aimed at improving the
  cheat/mod tooling.

Read `CONTRIBUTING.md` first for paths, conventions, and the tool inventory. Everything below
assumes those.

---

## State of the analysis (2026-09-04)

### 2026-09-06 follow-up

- Linux FakePDB user plugin loaded automatically in an isolated idalib process using
  the idalib virtualenv; JSON + native PDB export succeeded on a temporary IDB copy.
  Reference IDB/EXE/PDB unchanged. Details in `01_methodology.md`.
- Linux/PINCE now has a **[DR] No damage** checkbox in a DELTARUNE trainer toolbar,
  added by the usual `dr.start()` setup; Python `dr.no_damage(True/False)` also works.
  Fourteen offline tests pass, including real Qt interactions with simulated memory.
  The toggle maintains `global.inv` by name.
  This guards normal damage checks, **not all scripted damage**: Chapter 2 teacup
  hazards reset the timer before calling damage. Chapters 1-5 `scr_damage` all
  contain the guard; full cross-chapter coverage and live testing remain open.
- Keep Frida available for chapter instrumentation: log damage entry, inv resets,
  and HP writes; establish an in-process guard for bypasses without editing data.win.

- Linux trainer: refuse ambiguous scalar fallback matches; check REAL tags and full
  writes; resolve immediately before manual set/freeze; reject initial freeze write
  failures and PINCE PID mismatches; omit invalid PINCE array rows.
- Chapter 2 GML confirms Noelle's id 4 and startup HP 90, 48 weapon/armor slots,
  and 72 pocket-item slots. Python trainer now covers those inventory capacities
  while respecting shorter live arrays. Findings in [07_globals_and_variable_storage.md](07_globals_and_variable_storage.md).
- Six offline regression tests pass (`python3 -B -m unittest discover -s cheatengine
  -p 'test_*.py' -v`). This pass has not validated the changed trainer in a live game.
  Host `--once` reported no running DELTARUNE process.
- Chapter GML: full dumps for 1/2 (1,293/5,840 files); Chapter 3 partial bulk output
  plus targeted scripts (1,188); Chapters 4/5 four targeted scripts each. All five
  directories have source hashes and explicit scope in `extraction.json`.
  Chapters 3-5 full dumps remain unfinished. Extraction commands and limitations:
  [01_methodology.md](01_methodology.md#chapter-gml-extraction-2026-09-06).
- Host access: workspace is exposed through SSHFS at `/tmp/mount`; run process-memory
  tooling on `<user>@<linux-host>`, where the workspace is `<workspace>/RE`.
  Use `ssh -F /dev/null` if the local system SSH configuration fails ownership checks.

Done (see `00_README.md` index):

- Runtime identified = GameMaker VC_Runner, GM `2022.0`, imagebase `0x140000000`.
- `RValue` struct + `RValueKind` (16 kinds) declared in the IDB.
- 1,876 `gml_*` builtins auto-named from `Function_Add` call sites (2,514 sites, 0 fails);
  catalogued in `gml_builtins_manifest.json` + `family_stats.txt`.
- Core primitives named: `YYGet*` accessors, `Function_Add`, `YYError`/`GML_HandleError`,
  RValue coerce/free/serialize.
- `ds_*` subsystem internals (bank/handle, ds_map, ds_list) — `05_data_structures.md`.
- Global object graph + `YYObjectBase` variable storage + `VarHashMap` + `GMArray`;
  `global.hp[]` etc. proven against the GML dump — `07_globals_and_variable_storage.md`.
- **2026-09-17 (Track A P1–P7 pass — "finish all the static RE"):** `YYObjectBase` full
  20-field struct + RTTI-confirmed class hierarchy (`CInstanceBase` ← `YYObjectBase` ←
  `CInstance`/`CScriptRef`/`CWeakRef`/`GCObjectContainer`), `CInstanceBase` confirmed
  vtable-only (no fields), `CScriptRef` struct declared; instance manager cracked
  (`g_pInstanceManager`'s pointer-keyed hashmap, `Instance_UpdateObjectTypeLinks`'s
  568-xref mystery resolved — [08_instances.md](08_instances.md)); all 351 builtin
  variable getter/setter functions read from their 4 registrars' literal call sites and
  renamed, accessor signature confirmed — `07_globals_and_variable_storage.md`; **the
  entire `data.win` loader chunk-dispatch table completed** — every tag now has a
  confirmed handler, inline, or no-op disposition (20 chunk handlers decompiled total
  across both passes), 2 mixups corrected against raw disassembly (`STRG` is a true
  no-op, `TPAG` is inline not a `sub_140072560` call), 2 previously-undocumented tags
  found (`LANG` — a fully reverse-engineered embedded localization chunk — and `PSPS`),
  the PNG-decode/texture-upload primitive found (the concrete image-override hook
  point) — [09_datawin_loader.md](09_datawin_loader.md); `layer_*` subsystem internals
  traced as a template for the rest — `04_builtin_functions.md`; GGPO rollback's real
  online path confirmed disabled in this build (`"operagx"`-only) —
  `04_builtin_functions.md`. Full detail in each pass's TODO.md entry above.

Explicitly **not** done / flagged in the docs:

- ~~`sub_140082340` (`Instance_UpdateObjectTypeLinks`, 568 xrefs) — "needs further
  analysis"~~ — **P3 done 2026-09-17**, see below and [08_instances.md](08_instances.md).
- ~~`YYObjectBase` only partially mapped (offsets +8, +32, +72, +120, +124)~~ — **P2
  done 2026-09-17**, full 20-field struct declared + RTTI-confirmed class hierarchy,
  see below.
- ~~`g_pInstanceManager` internals (+136 list head), `sub_14007DBB0`~~ — **P3 done
  2026-09-17**, see above and [08_instances.md](08_instances.md).
- GGPO rollback subsystem (18 `rollback_*` builtins + `GGPO_Log`) — untouched.
- `data.win` loader / room-loading path in the runner — **P6 done 2026-09-17**, see
  below and [09_datawin_loader.md](09_datawin_loader.md). Main call chain, the
  universal file-read primitive, and every chunk-tag handler are documented (including
  a previously-undocumented `LANG` localization chunk). Deeper-level open ends remain
  (sprite-frame wiring, a few shared/generic per-chunk workers, `TPAG`'s lazy
  per-entry parse, room-loading's own internal structure) — see the doc's open-ends
  section — but the chunk-dispatch layer itself is fully mapped.
- Media Foundation video path, Steamworks plumbing — noted only.
- `g_BuiltinVarAccessors` table (`0x14067F188`) — the `x`/`y`/`image_index`/… getter/setter
  pairs are not individually named.

---

## Track A — static RE: functions & types

### Session setup (every time, before writing)

1. **Back up the IDB**:
   `cp <steam-library>/steamapps/common/DELTARUNE/DELTARUNE.exe.i64{,.bak-<date>}`
   (a known-good `DELTARUNE.exe.i64.bak-20260904` already exists).
2. Open headless via idalib:
   `idapro.open_database(".../DELTARUNE.exe.i64", run_auto_analysis=False)`.
   Verify Hex-Rays is available.
3. Do **reads** freely. Batch **writes** (renames, types,
   comments). Save the database once at the end of the session.
4. If the worker left loose `DELTARUNE.exe.id0/.id1/.id2/.nam/.til` from a previous
   unclean exit and no session is running, they're safe to delete (they mirror the
   `.i64`); never delete them while a session is live.
5. After a meaningful batch, regenerate `DELTARUNE.pdb` with FakePDB on Linux or Windows
   so dynamic tools pick up the new names. Linux headless export is verified; see
   [01_methodology.md](01_methodology.md#linux-fakepdb-headless-verification-2026-09-06).

### Conventions (from `01_methodology.md` — do not drift)

- Names come from evidence (string literals, registration args, unique error strings),
  never from behavior guesses. Unsure → leave `sub_xxxxxxxx`, add a `// needs analysis`
  comment.
- `gml_` prefix for GameMaker builtins; descriptive C-style names for runtime primitives.
- Quote hex verbatim from IDA. No manual base math.
- Every durable finding → fold into the owning `0X_*.md` doc (and the manifest if a
  builtin/alias changes).

### Passes (pick one per session, work it end to end)

- [~] **P1 — high-xref `sub_*` sweep.** `list_funcs(filter="sub_*")`, rank by xref count
      (`func_profile` / `xrefs_to`). For the top N: `decompile`, identify from
      strings/callees/callers, `rename`, `set_type`, comment. Target the 100 most-xref'd
      unnamed functions first.
      - **2026-09-04:** named the 27 `Function_Add` callers (the builtin registrars) +
        the master dispatcher `Register_AllBuiltinFunctions` (`0x1401FFD90`). Method:
        `func_profile(include_lists)` → read each function's builtin name-strings → name
        `Register_<Family>_Functions`. Table in `04_builtin_functions.md`. IDB saved.
      - **2026-09-04 (cont.):** `survey_binary` top-xref list → named the unnamed
        primitives on it: `stdstring_Tidy_deallocate` (`0x140012060`, 417 xrefs, MSVC
        std::string cleanup), `GetCurrentInstance` (`0x1401D15F0`, 399), plus
        `PushCurrentInstance` + `g_InstanceContextStack*`, `Variable_AddBuiltin`
        (`0x1400422D0`, 224) + `g_BuiltinVarTable`/`Count`/`NameMap`, `HashMap_Insert`
        (`0x140042070`, generic Robin-Hood). Commented (not named) `sub_140064760`
        (210 xrefs, 64-slot state dirty-cache, purpose TBC). Doc: `07_...md`. IDB saved.
      - Next unnamed high-xref: `sub_1401FFD10` (WinMain init), `sub_1400B7520`
        (`Register_AllBuiltinFunctions`' caller), `sub_1400E2F00`/`sub_1400E2F20`
        (HashMap hash/compare), `sub_1401CE600` (instance-context stack init).
        Then continue down `survey_binary`'s list / page `func_profile` by caller_count.
- [x] **P2 — finish `YYObjectBase`.** 2026-09-17: found the real base constructor
      `YYObjectBase_Construct` (`0x14007C4A0`, identified by its own
      `YYObjectBase::vftable` RTTI assignment, not just inferred) and read every field
      it initializes — a full 20-field struct (vtable, members, parent, hashmap, count/
      capacity, debug type-index, slot id, kind, several still-unconfirmed zeroed
      fields) declared in the IDB as `struct YYObjectBase`, applied to
      `Variable_GetValue`/`Object_FindVariableSlot`/`YYObjectBase_Construct`'s
      signatures — confirmed Hex-Rays improved (`object->kind`/`object->parent`/
      `object->members` now render by name; a dropped second parameter on
      `Object_FindVariableSlot` was recovered). **Bonus, user-suggested**: the binary
      retains full MSVC RTTI (287 vtable symbols total). Walked the actual
      `RTTIClassHierarchyDescriptor`/`RTTIBaseClassArray`/`type_info` chain (decoded by
      script, not by hand) and got a *confirmed, not inferred* class hierarchy:
      `CInstanceBase` (root) ← `YYObjectBase` ← `{CInstance, CScriptRef, CWeakRef,
      GCObjectContainer}`. Also spotted (not yet explored) GGPO rollback backend
      classes in the same RTTI symbol list — relevant to P7 later. Full writeup:
      [07_globals_and_variable_storage.md](07_globals_and_variable_storage.md). Renames
      + struct + signatures saved to the IDB. Not done: `CInstanceBase`'s own fields
      (if any — may be a vtable-only abstract base, unconfirmed), and the
      `CInstance`/`CScriptRef`/`CWeakRef`/`GCObjectContainer`-specific fields beyond the
      shared base (partially seen in the `CScriptRef` ctor, not yet formalized as a
      struct — natural next step, was in the Type backlog already).
- [x] **P3 — instance manager.** 2026-09-17: `g_pInstanceManager+136` is a Robin-Hood
      hashmap keyed by pointer value — the live-instance existence/lookup set.
      `Instance_RegisterWithManager` (renamed from `sub_14007DBB0`, ~40 call sites)
      inserts a new instance and calls `Instance_UpdateObjectTypeLinks` (the former
      568-xref unnamed function) to maintain per-object-type linked lists in
      `g_ObjectTypeTable`. Found two more compiled instantiations of the same
      generic Robin-Hood insert algorithm as `HashMap_Insert`:
      `InstancePool_HashMap_Insert` and `ObjectTypeTable_HashMap_Insert` (both
      renamed). `Instance_UpdateObjectTypeLinks` itself is debug-only (gated on
      `byte_1405FCF10`), explaining its high xref count (called on every
      type-reassignment path, not doing 568 different things). Full writeup:
      [08_instances.md](08_instances.md). Renames + comments saved to the IDB. Open
      ends: the hashmap header's exact field layout, `sub_1401CE1E0` (the actual
      link/unlink splice), and 4 live-window counters used for a validity check.
- [x] **P4 — builtin variable accessors.** 2026-09-04: found the registration side —
      `Variable_AddBuiltin` (`0x1400422D0`) + `g_BuiltinVarTable` (`0x14067F180`,
      32-byte entries {nameId, getter, setter, writable}, count `g_BuiltinVarTableCount`).
      `g_BuiltinVarTable+8` == the `0x14067F188` accessor table. Documented in `07_...md`.
      **2026-09-17: done.** `Variable_AddBuiltin` has only 4 callers (registration is
      flat calls in 4 registrar functions, not a data table) — read every
      `(name, getter, setter)` triple straight from their literal arguments and renamed
      all 351 getter/setter functions (`BuiltinVar_Get_<name>`/`BuiltinVar_Set_<name>`)
      plus the 4 registrars (`Register_BuiltinVars_{Environment,Instance,Platform,
      Rollback}` — the `Rollback` one is 6 `rollback_*` vars, a head start on unstarted
      P7). Full table + 5 found name-aliases (US/UK spelling pairs etc.):
      [07_globals_and_variable_storage.md](07_globals_and_variable_storage.md).
- [x] **P5 — subsystem internals.** 2026-09-17: swept `layer_*` (145, the largest
      family) — `gml_layer_create`/`gml_layer_x` both resolve a "current layer-list
      owner" via a `-1`-sentinel + id→owner lookup table + fallback idiom, then walk a
      linked list of internal layer-node records at `owner+376` (by id via
      `Layer_FindById`, renamed from `sub_14002D610`, or by name via a linear scan).
      Established this as the general pattern for the remaining families
      (`physics`/`draw`/`audio`) rather than exhaustively sweeping all of them — full
      writeup: [04_builtin_functions.md](04_builtin_functions.md#subsystem-internals-layer_-track-a-p5-2026-09-17).
- [x] **P6 — `data.win` loader.** Find where the runner mmaps/parses `data.win` (FORM/
      chunk reader: `GEN8`, `OPTN`, `FUNC`, `VARI`, `CODE`, `STRG`, …). Documents how
      content reaches the VM. Doc: [09_datawin_loader.md](09_datawin_loader.md).
      **All chunk tags now resolved as of 2026-09-17** — see the entries below.
      - **2026-09-17:** Traced and named the full call chain: `RunnerLoadGame`
        (`0x1400B9230`, resolves path — embedded/bundled > `-game <path>` cmdline arg >
        default search — then reads + FORM/MROF-validates) → `LoadGameData`
        (`0x1400B9830`) → `LoadGameData_ProcessChunks` (`0x1400B7E50`, the FORM
        chunk-tag dispatch loop). Decoded the full chunk-tag→handler table (all magic
        constants via script, not by hand) — `GEN8`/`OPTN`/`SPRT`/`SOND`/`BGND`/`PATH`/
        `SCPT`/`FONT`/`SHDR`/`TMLN`/`OBJT`/`ROOM`/`TPAG`/`FUNC`/`STRG`/`TXTR`/`AUDO`/
        `ACRV`/`SEQN`/`TAGS` map to named-but-undecompiled `sub_*` handlers; `CODE` is
        handled inline (the GML bytecode operand-fixup pass, not a sub-call); `VARI`
        is deliberately a no-op here (the real var-name table comes from
        `qword_1406A9DD8`, built separately after the chunk loop); `EXTN`/`STAT`/`HELP`/
        `RASP` are recognized-but-ignored; `GMEN`/`GLOB` aren't in UndertaleModTool's
        public chunk list, flagged for further analysis. Also found and named the
        runner's **universal file-read primitive** — `ReadEntireFile`
        (`0x140092C40`, plain `_wfopen`+`fread`) via wrappers `ReadEntireFile_Bundle`
        (`0x140091A40`, 27 call sites) / `ReadEntireFile_Plain` (`0x140091BD0`, 24 call
        sites) — used for `data.win`/`options.ini`/gamepad configs/`gml_file_copy`/etc,
        not just game data. **This is the recommended hook point for whole-file
        overrides** (simpler than per-chunk-type asset substitution, which needs the
        individual handlers above decompiled first — not yet done). All renames +
        comments saved to the IDB. Full writeup incl. open ends:
        [09_datawin_loader.md](09_datawin_loader.md).
      - **RESOLVED 2026-09-17**: the "doesn't progress past chapter 1" blocker below was
        a test-rig bug (incomplete file copy), not a real blocker — see the retracted
        entry further down in Track B. The test rig now runs chapter 1 stably; a live
        `ReadEntireFile` hook against a real `-game <chapter>` load is unblocked.
      - **2026-09-17, continued (finish-all-static-RE pass)**: decompiled 8 more
        per-chunk handlers — `GEN8`, `SPRT`, `SCPT`, `OBJT`, `ROOM`, `FUNC`, `TXTR`,
        `AUDO`, `SEQN` (renamed `LoadGameData_ProcessChunk_<TAG>`), and found the actual
        PNG-decode/texture-upload primitive (`Texture_DecodePNGAndUpload`,
        `0x1400728B0`) — the concrete hook point for image/texture override, one level
        more precise than the whole-file `ReadEntireFile` override already proven.
        **Correction**: the earlier table's `sub_140123AA0` = `STRG` mapping was wrong
        — decompiling it showed it's actually `SEQN` (allocates
        `CSequenceInstance`-shaped records, registers with the instance manager in
        debug builds — a plain string table would never do that). `STRG`'s real handler
        is now flagged unconfirmed, needs re-deriving from raw disassembly.
      - **2026-09-17, continued again (all remaining chunk handlers decompiled)**:
        decompiled the last 11 unhandled tags — `FONT`, `OPTN`, `SOND`, `BGND`, `PATH`,
        `SHDR`, `TMLN`, `ACRV`, `TAGS`, `TGIN`, `FEDS` (all renamed
        `LoadGameData_ProcessChunk_<TAG>`). Cross-checked the *entire* dispatch against
        raw disassembly (capstone) rather than trusting Hex-Rays' if-chain rendering,
        which resolved two more mixups and found two previously-undocumented tags:
        **`STRG` is a confirmed pure no-op** (no handler at all — the earlier "needs
        re-deriving" flag is now closed for real, there's nothing to find); **`TPAG` is
        an inline pointer-stash** (`g_TPAGChunkPtr`), not a call to `sub_140072560` —
        that call is actually `EMBI`'s handler, now disambiguated; new tag **`PSPS`**
        (silent no-op, purpose unknown); new tag **`LANG`** — a fully-decompiled,
        previously-undocumented embedded localization chunk (language list + per-key
        translated strings, on-disk layout fully reverse-engineered), independent of
        the loose `lang_en.json`/`lang_ja.json` files Track B already overrides — not
        yet confirmed whether the engine actually consumes it at runtime. Every chunk
        tag in the dispatch now has a confirmed disposition — decompiled handler,
        confirmed inline, or confirmed no-op. Full table + the `LANG` writeup + open
        ends (deeper: `sub_1400DDB50` sprite-frame wiring, a few shared/generic
        workers, `LANG`'s runtime consumer): [09_datawin_loader.md](09_datawin_loader.md).
- [x] **P8 — script invocation / VM hooking (new, 2026-09-17).** User clarified the
      mod-loader goal is primarily **script replacement or hooking the VM**, not asset
      override — this pass targets that directly. Found `CCode` (one 184-byte instance
      per compiled script, RTTI-confirmed class, global linked list) and `VMBuffer`
      (48-byte class holding the actual fixed-up bytecode pointer + length) — both
      declared as structs in the IDB. Traced the full invocation chain:
      `gml_script_execute`/`_ext` → (id≥100000) → `Script_Exists` → `Script_InvokeById`
      (`0x1400BB020`) → `g_ScriptCodeTable[id-100000]` (a `CCode*`) →
      `Script_InvokeCode` → `Code_Execute` (`0x1401BBC90` — decompile fails, matches a
      raw-byte-confirmed C++ SEH funclet table, i.e. GML `try`/`catch`, not a giant
      opcode switch). **Live-hook-tested same day, and this corrected the
      recommendation**: hooked both `Script_InvokeById` and `Code_Execute`
      simultaneously against the scratch rig — `Script_InvokeById` fired **zero**
      times in ~10s of normal chapter-select UI activity (it only catches explicit
      `script_execute()`/`script_execute_ext()` GML builtin calls, rarely used),
      while `Code_Execute` fired **52 times** in the same window (the real
      general-purpose "run this compiled code" entry, reached via indirect/vtable
      calls invisible to static analysis, not just its 2 static xrefs). **`Code_Execute`
      is the confirmed, real hook point for whole-script/whole-event replacement** —
      superseding `Script_InvokeById`. Not yet done: resolve a human-readable name per
      `Code_Execute` call (the name-resolution trick was proven on `Script_InvokeById`
      instead, the wrong function) — not yet exploited, redirection not yet attempted.
      **Signature confirmed by cross-referencing YYToolkit's own source**: `bool
      ExecuteIt(CInstance* self, CInstance* other, CCode* CodeObject, RValue*
      Arguments, int Flags)` — YYToolkit hooks this exact function (their name:
      `ExecuteIt`) via `MmCreateHook`, the same Aurie primitive this project already
      proved hangs under Wine ([Aurie#22](https://github.com/AurieFramework/Aurie/issues/22))
      — fully explaining why script hooking never worked via Aurie/YYToolkit here,
      independent of DELTARUNE or the target function itself.
      - **DONE, live, end-to-end, same day**: corrected `CCode`'s name field (`+128`,
        not `+120` — the real tell is the debug-branch `strncmp` against
        `"gml_Script"`/`"gml_GlobalScript"`); reading it from the `ExecuteIt` hook
        resolved a real, human-readable name for **every** call, matching this
        project's own GML dump naming exactly (`gml_Object_obj_ui_choice_Step_0`,
        `gml_GlobalScript_scr_wrap`, etc). Then **live-tested an actual redirect**:
        retargeted `gml_Object_obj_ui_version_Draw_0` (draws the "DELTARUNE v23"
        version text) to `gml_Object_obj_screen_start_Draw_0` by swapping the
        `CCode*` before calling through to the real `ExecuteIt`. **Result: GameMaker's
        own runtime error dialog confirmed it in its own words** — "ERROR in ... Draw
        Event for object obj_ui_version: Variable obj_ui_version.init(...) not set
        before reading it. at gml_Object_obj_screen_start_Draw_0" — i.e. the game's
        own diagnostics named the substituted code as what was actually running.
        **Full, live, conclusive proof that GML script/event replacement works via
        this hook.**
      - **Follow-up same day: clean redirect with zero error dialog.** Dumped both
        candidates' real GML via `utmtcli dump` instead of guessing this time —
        `obj_ui_version`'s own `Create_0` only *writes* instance vars (literals +
        `get_version()`), never reads anything, so it's safe to run in place of
        `Draw_0` on the same instance. Redirected `gml_Object_obj_ui_version_Draw_0`
        → `gml_Object_obj_ui_version_Create_0`: fired repeatedly with **zero error
        dialogs**, version text visibly disappeared (screenshot-confirmed), rest of
        the screen unaffected. General lesson: check real GML source before picking a
        substitution pair — safe if the substitute never reads an instance variable
        the target's instance doesn't already have; same-object substitutions are
        inherently safer than cross-object ones. Redirect left disabled in the
        scratch rig's default build afterward (re-enable in `mod_test/aurie_build/
        proxy/dllmain.c` to reproduce either test). The literal per-*instruction*
        opcode loop (if separate from `Code_Execute` at all) is still not
        individually located — not needed, since whole-script replacement is now
        proven without it. Full writeup:
        [10_vm_and_scripts.md](10_vm_and_scripts.md).
- [x] **P7 — GGPO rollback.** 2026-09-17: decompiled `gml_rollback_create_game` —
      confirmed the real online path (`g_RollbackSessionType==2`, "operagx" target) is
      **disabled in this Steam build** (`YYError("Multiplayer rollback is only
      supported in the operagx target.")`); only single-player and local-synctest
      session types actually construct a backend here. Backend class hierarchy
      confirmed via RTTI (`Backend` ← `Peer2PeerBackend`/`SinglePlayerBackend`/
      `SpectatorBackend`/`SyncTestBackend`/`UdpBackend`, found during P2's RTTI sweep).
      `g_RollbackBackend`/`g_RollbackSessionType`/`g_RollbackPlayerCount` renamed. Full
      writeup: [04_builtin_functions.md](04_builtin_functions.md#ggpo-rollback-track-a-p7-2026-09-17).
      Deeper GGPO internals (save-state snapshotting, prediction, `GGPO_Log`) not
      pursued further — third-party library code, not DELTARUNE-specific, and netplay
      is confirmed out of scope for this build.

### Type backlog

- [x] `YYObjectBase` (full) — done 2026-09-17, see P2 above
- [x] `CInstanceBase` — done 2026-09-17: confirmed **zero data members** (found its
      actual ctor/dtor pair, `CInstanceBase_Construct`/`_Destruct`, ctor is a single
      `*this = &vftable`) — pure vtable-defining abstract base. See
      [07_globals_and_variable_storage.md](07_globals_and_variable_storage.md).
- [x] `CScriptRef` — done 2026-09-17: `struct CScriptRef` declared (base fields +
      12 more from `+0x88`–`+0xD8`, incl. two `RValue`-shaped embedded slots) and
      applied to its renamed constructor `CScriptRef_Construct` (`0x14007C030`).
- [x] builtin-var accessor fn signature — done 2026-09-17: confirmed
      `char (__fastcall*)(YYObjectBase *self, unsigned int flags, RValue *value)`,
      declared as `PFN_BuiltinVarAccessor`, applied to the `x` getter/setter as worked
      examples (not all 351, mechanical/low-value to do exhaustively). Bonus: found
      `CInstance`'s own `x` field lives at `self+232`.
- [x] `data.win` chunk header struct — done 2026-09-17:
      `struct DataWinChunkHeader { char tag[4]; unsigned int length; }` declared in the
      IDB. Per-chunk *payload* layouts remain a bigger, open-ended item (would mean
      fully reconstructing every asset type's on-disk format) — not attempted.
- [ ] `CInstance` / `CWeakRef` / `GCObjectContainer`-specific fields beyond the shared
      `YYObjectBase` base (confirmed direct subclasses via RTTI; only `CInstance+232`=`x`
      is known so far) · `YYRoom` / live-room-instance structs (the `ROOM` chunk's
      0x218-byte on-disk asset record is not the same object `layer_*` builtins operate
      on — see P5's note in
      [04_builtin_functions.md](04_builtin_functions.md#subsystem-internals-layer_-track-a-p5-2026-09-17)).
      Genuinely open-ended (each is its own multi-session reversing effort); natural
      next steps whenever static RE work resumes, not blocking anything currently
      planned.

---

## Track B — play with the other RE tools

Ordered by payoff for the cheat/mod tooling. The game has **no native build** — every
dynamic approach runs `DELTARUNE.exe` through `wine`/Proton.

- [~] **2026-09-16: Aurie Framework / YYToolkit under Wine — investigated for a
      BepInEx-style "load unpacked assets" loader.** Goal: a native DLL injected into
      `DELTARUNE.exe` that patches the runtime to load loose GML/sprite/sound assets
      instead of only from `data.win`. Scratch test rig at `<workspace>/mod_test/`
      (exe + chapter1 `data.win` + launcher `data.win` + `mus/`, copied — never the real
      install), dedicated `WINEPREFIX=~/.wine-deltarune-mod` (plain wine 11.17, not
      Proton). Teardown: `mod_test/cleanup.sh` (`--dry-run` to preview, `--keep-build` to
      keep the compiled plugin/proxy DLLs).
      - Prior art found: `github.com/AurieFramework/{Aurie,YYToolkit}` — a real,
        actively-maintained BepInEx-equivalent for GameMaker. Aurie = generic Windows
        code-injection + hooking framework; YYToolkit (YYTK) = GameMaker-specific layer
        on top (GML variable access, script/function hooking).
      - **Aurie's own installer (`AuriePatcher.exe`) is broken under Wine.** Root cause
        (read straight from `AuriePatcher/source/main.cpp`, not just the wiki): it
        statically patches the target exe's entry point to run a tiny embedded trampoline
        (`ArProcessInitialize`) that manually walks `PEB->Ldr->InLoadOrderModuleList` and
        resolves `ntdll!LdrLoadDll` by hand (deliberately avoiding the CRT/normal loader,
        per the source's own comments) to load `AurieCore.dll` before jumping to the real
        original entry point. Under Wine this null-pointer-jumps immediately
        (`Unhandled page fault ... execute access to 0000000000000000`) before any game
        or plugin code runs. The wiki's Wine section (`wine ./mods/AurieLoader.exe game.exe
        --debug`) describes an older IFEO-proxy architecture that no longer matches the
        current source — there is no `AurieLoader.exe` in the current repo or releases.
      - **Workaround found: standard DLL-proxy injection works fine.** Built a minimal
        `version.dll` (forwards all 16 real exports to a renamed `version_orig.dll` via
        genuine PE export forwarders; `DllMain`/`DLL_PROCESS_ATTACH` calls
        `LoadLibraryW` on `mods/native/AurieCore.dll`) — confirmed via a log line that
        `AurieCore.dll` loads successfully this way, and with **only** `AurieCore.dll`
        present (no YYTK) the game boots completely normally (real window, an "Aurie
        Framework Log" console appears, no errors) — verified against a vanilla no-Aurie
        control run in the identical prefix to rule out generic missing-dependency causes.
      - ~~**YYToolkit's own init breaks early GML VM startup.**~~ **RETRACTED
        2026-09-17 — this was wrong, root-caused below.** Originally attributed a GML
        runtime error (`ds_map_find_value argument 1 incorrect type (undefined)
        expecting a Number (YYGI32)` in `gml_Script_scr_84_get_lang_string`, called from
        `scr_ascii_input_names` → `scr_84_init_localization` → `obj_initializer2`'s
        Create event) to YYToolkit's init, since it appeared with `YYToolkit.dll` loaded
        and not in a shorter AurieCore-only run. **Reproduced 2026-09-17 in a
        completely vanilla run with zero Aurie/YYTK involvement** — proves it was never
        YYTK-specific. Actual cause: our scratch test copy of `chapter1_windows/`
        only had `data.win` (plus a `lang/` folder added mid-session) — missing
        `audiogroup1.dat` and the loose `.ogg` files that live next to `data.win` in the
        real install. The launcher hands off to chapter 1 fine either way, but chapter 1
        prints `Failed to load audiogroup1.dat` and then explicitly calls
        `game_end(254)`, which is what sends it back to the chapter-select screen —
        this is what looked like "looping." **Fixed** by copying the *entire*
        `chapter1_windows/` directory (not just `data.win`) into the test rig; a vanilla
        run then sits stably in Chapter 1's main loop indefinitely (`Audio group 1 ->
        Loaded`, no errors, 20+ seconds observed with a steady process count) — **and
        then eventually calls `game_end(254)` anyway, deliberately, on a clean run**.
        Traced this in the GML dump: `obj_DEVICE_FAILURE`'s `Step_0`
        (`code/chapter1/gml_Object_DEVICE_FAILURE_Step_0.gml`) is DELTARUNE's
        well-known intentional opening gag — a fake "device failure" screen that plays
        `AUDIO_DARKNESS.ogg` and calls `ossafe_game_end()` once it finishes or after a
        ~68s timer (`DARK_WAIT >= 2040` @ 30 steps/sec), by design — you're meant to
        relaunch the game afterward to continue into the real opening
        (`obj_chapter_continue`'s `Alarm_1` is a bare `game_end();`, the
        chapter-completion equivalent). **This is not a bug at all** — repeatedly
        launching the exe without knowing this is what produces the "infinite loop"
        appearance. Nothing further to fix here; the test rig is healthy.
      - The
        earlier AurieCore-only run's "no errors" result was very likely just a shorter
        observation window that didn't reach the chapter-1 transition, not evidence of
        anything Aurie-related — don't read anything into it either way. **Lesson**: any
        future dynamic test must copy full chapter directories (`cp -r
        chapterN_windows`), not hand-picked files, or spurious failures will look
        like real bugs.
      - **`MmCreateHook` (Aurie's own inline-hook primitive, backed by vendored
        SafetyHook) hangs/never returns under Wine, on *any* target** — reproduced with a
        from-scratch plugin (no YYTK at all) hooking `Function_Add` (`0x1401BAA10`) *and*
        with a trivial control target (`kernel32!GetTickCount`): both cases reach the log
        line immediately before the `MmCreateHook` call and never reach the line after it.
        This means the problem is in AurieCore's hooking engine itself under Wine, not in
        YYTK, not in anything DELTARUNE-specific. **This breaks the core premise of using
        Aurie as the hooking substrate for the loose-asset-loader idea** — injection works
        (proxy-DLL method), but the one primitive actually needed (intercepting an engine
        call to substitute a loose asset) does not.
      - Toolchain notes for reproducing/continuing: MinGW-w64 (already installed) builds
        Aurie-ABI-compatible plugin DLLs fine on x64 (Windows has one calling convention
        on x64, unlike x86) — no `msvc-wine` needed. Aurie ships no import lib; generate
        one from the release `AurieCore.dll` with `pefile` (dump exports → `.def`) +
        `x86_64-w64-mingw32-dlltool -d file.def -l libAurieCore.a -D AurieCore.dll` — no
        `gendef` needed either. Case-sensitivity gotcha: Aurie/YYTK headers
        `#include <Windows.h>` (capital), which fails on Linux's case-sensitive FS against
        mingw's lowercase `windows.h` — fixed with a one-line shim header of that name on
        the include path. `UNREFERENCED_PARAMETER(P)` from `<winnt.h>` (`{(P)=(P);}`)
        doesn't compile against a `const fs::path&` under libstdc++ — redefine it to
        `((void)(P))` after including `windows.h`. `CreateCallback`'s `PVOID Routine` arg
        needs `-fpermissive` to accept a typed function pointer without a cast (MSVC
        allows this by default, GCC doesn't).
      - **RESOLVED same session: vendored MinHook instead of Aurie's own hooking.**
        Pulled `github.com/TsudaKageyu/minhook` source directly (`include/MinHook.h` +
        `src/{buffer,hook,trampoline}.c` + `src/hde/hde64.{c,h}`, `table64.h`,
        `pstdint.h` — x64-only, skip the `hde32`/`table32` files), built as a static lib
        with plain `x86_64-w64-mingw32-gcc` (its repo even ships a MinGW build dir, a
        good sign). `MH_Initialize`/`MH_CreateHook`/`MH_EnableHook` all report `MH_OK`
        and — control-tested by hooking `kernel32!Sleep` from an Aurie-loaded plugin —
        **the hook actually fires** (50/50 calls logged in each of two live game
        processes), unlike Aurie's own `MmCreateHook` which just hangs. Confirms the
        earlier `MmCreateHook` failure is specific to AurieCore's own (SafetyHook-backed)
        hooking engine, not a Wine-hooking-is-impossible problem in general.
      - **Second gotcha found and fixed: an Aurie-loaded plugin is too *late* for a
        startup-only hook.** The same `Sleep` control plugin hooked via
        `MdMapFolder`-loaded `mods/aurie/*.dll` caught `Sleep` fine (called continuously
        all game long) but caught **zero** `Function_Add` calls even though
        `MH_CreateHook`/`MH_EnableHook` both still reported `MH_OK`. Root cause: Aurie's
        module scan/load appears to happen off the main thread (the safe pattern to
        avoid `LoadLibrary`-from-`DllMain` loader-lock issues), so it reliably loses the
        race against `Function_Add`'s one-time startup burst (~2,500 calls, all within
        milliseconds) even though it wins easily against anything called throughout the
        game's lifetime. **Fix**: install startup-critical hooks synchronously inside the
        proxy DLL's *own* `DllMain` (before ever calling `LoadLibrary` on `AurieCore.dll`
        or anything else) rather than via an Aurie-loaded plugin.
      - **Milestone reached: full, verified `Function_Add` capture under Wine.** With the
        hook moved into the proxy DLL's `DllMain`, a single game process logged **2,537**
        `Function_Add` calls (vs. the documented 2,514 call sites — close enough; minor
        count differences don't matter, the content is unambiguously correct), with real
        builtin names, real in-image function addresses, and correct `argc` — spot
        checked `abs`, `sin`, `show_debug_message`, `ds_map_create`, and the
        `rollback_*` (GGPO) family, all matching `04_builtin_functions.md` /
        `gml_builtins_manifest.json`. This is a working, reusable, Wine-compatible
        native-code-injection + hooking pipeline for `DELTARUNE.exe`, entirely
        independent of Aurie's own broken pieces (its installer, its `MmCreateHook`) and
        of YYToolkit.
      - **Recipe for next session** (all pieces proven, not yet packaged as a reusable
        tool): (1) a `version.dll` proxy next to the target exe forwarding its 16 real
        exports (PE export-forwarder `.def` entries) to a renamed `version_orig.dll`;
        (2) link vendored MinHook statically into that same proxy DLL; (3) in
        `DllMain`/`DLL_PROCESS_ATTACH`, compute target addresses as
        `GetModuleHandleA(NULL) + RVA` (image loads unrelocated at `0x140000000` under
        this Wine setup, matching the Proton behavior already documented in
        `07_...md`) and install any startup-critical hooks *there*, synchronously;
        (4) still `LoadLibraryW` `AurieCore.dll` afterward if Aurie's module-loading
        conveniences are wanted for non-timing-sensitive, later-installed hooks (its
        injection story is fine — only its own hooking primitive and YYToolkit are
        broken). Two DELTARUNE.exe processes run briefly during the launcher→chapter
        handoff (same command line, `-game data.win launcher switch_-1 returning_0`,
        confirmed via `wmctrl`/`pgrep` — a previously-undocumented fact about the
        runner's own process model, worth folding into `02_binary_overview.md` if this
        work continues); log files should be tagged by PID (`GetCurrentProcessId()`) to
        avoid two processes racing to truncate the same file.
      - ~~**2026-09-17 follow-up: injected test rig doesn't progress past chapter 1**~~
        **RESOLVED same day — was a test-rig bug, not an Aurie/injection problem.** A
        vanilla (zero Aurie/YYTK) run hit the *exact same* apparent "loop," which
        proved it couldn't be our injection. Real cause: `chapter1_windows/` in the
        scratch rig only had `data.win` (+ a `lang/` folder added mid-diagnosis) —
        missing `audiogroup1.dat` and loose companion `.ogg` files present in the real
        install. Chapter 1 loads, prints `Failed to load audiogroup1.dat`, then calls
        `game_end(254)` — not a crash or a Wine/Aurie bug. Fixed by `cp -r`-ing the
        whole `chapterN_windows/` directory instead of just `data.win`; a vanilla run
        then sits stably in Chapter 1's main loop indefinitely — **and separately,
        `game_end(254)` also fires on a fully clean run, deliberately: it's DELTARUNE's
        own intentional opening "device failure" gag** (`obj_DEVICE_FAILURE`, see the
        detailed note earlier in this same 2026-09-16 block), not something to fix
        further. See the retracted-conclusion entry earlier in this same block (the
        "YYToolkit breaks GML startup" claim) — same missing-files root cause, same fix,
        plus this additional by-design behavior on top.
      - **2026-09-17: "load unpacked files" verified end-to-end against the real,
        unmodified game.** First tried Frida per-user-suggestion for read-only
        observation against the unmodified exe (cleaner than injecting our own DLL) —
        hit two environment obstacles worth recording so they aren't re-discovered
        blind: (1) `frida -p <pid>` attach failed outright with
        `ptrace_scope=1` (Linux hardening restricting `ptrace` to direct-child
        processes only — this system's default, user relaxed it to `0` via
        `sudo sysctl kernel.yama.ptrace_scope=0` for this session, reversible with the
        same command set back to `1`); (2) even with that fixed, native-realm attach
        crashes Frida's own injection bootstrapper with SIGSEGV against this
        wine-hosted process, and `--realm emulated` (the mode meant for exactly this
        translated-process scenario) is unsupported by this pipx-installed Frida
        17.17.0 build (`unable to handle emulated processes due to build
        configuration`). Spawn-mode (`frida -f wine -- DELTARUNE.exe`) resumes the
        process but the agent gets torn down immediately after
        ("Process terminated"), likely lost across wine's internal process-image setup.
        **Verdict: this Frida install cannot reliably instrument this Wine-hosted
        binary; don't retry the same approach without a different Frida build/version.**
        Fell back to the already-proven proxy-DLL + MinHook technique instead (works
        reliably here, see the entry above) and pointed it at `ReadEntireFile` for real:
        hooked it, logged the path of every file the (fully unmodified, vanilla)
        `data.win`/config/audio content, then live-substituted `chapter1_windows/
        lang/lang_en.json` for the game's own `lang_ja.json` (different file, verified
        by the resulting buffer size matching the *substitute* — 700,438 bytes — not
        the original 542,800) and the game accepted it and kept running normally, no
        crash. **This is full, live proof that a whole-file override at
        `ReadEntireFile` works exactly as `09_datawin_loader.md` predicted.** Also
        discovered live (previously undocumented): the runner reads
        `%LOCALAPPDATA%\DELTARUNE\hiscore.dat`, `dr.ini`, and a per-chapter-folder
        `true_config.ini` this same way — worth folding into a future doc pass. One
        asset type does **not** go through `ReadEntireFile`: the loose `.ogg` audio
        files (tried overriding `AUDIO_INTRONOISE.ogg` first — hook never fired for it
        despite the file clearly existing and being played) — audio almost certainly
        streams through a separate reader, not the whole-file-preload primitive. Needs
        its own decompilation pass if audio override is wanted later.
      - **Not yet done**: per-asset-type substitution (replacing one sprite/script
        *inside* an otherwise-normal `data.win`, or the audio streaming path above)
        needs the individual chunk handlers in `09_datawin_loader.md` decompiled first —
        bigger lift, not started. Whole-file override (a full alternate `data.win`,
        `options.ini`, localization file, etc.) is done and proven.
      - **2026-09-17, plan corrected using the completed chunk-table pass**: the
        earlier assumption that `TPAG` was the place to hook for per-sprite override
        was wrong — decompiling it showed `TPAG` is a lazy pointer-stash consumed only
        by `TXTR`'s own housekeeping. The **real** per-sprite-frame override hook is
        `SPRT_PopulateFrames` (`0x1400DDB50`), which resolves each frame's
        texture-page-item reference directly from the `SPRT` entry via the universal
        `unk_1406A9DE8+offset` pattern (see `09_datawin_loader.md`'s asset-override
        section). **Revised recipe for a real single-sprite override plugin**: (1) the
        proven proxy-DLL + MinHook pipeline (Track B, above) already gives reliable
        Wine-compatible hooking; (2) hook `SPRT_PopulateFrames` instead of anything
        `TPAG`-related, matching by resolved sprite name (available via the chunk's
        name-offset field, same as every other chunk type) and substituting a loose
        image's resolved texture-page-item pointer for the named sprite's frame entries
        at `self+56`; (3) for a coarser but simpler win, `Texture_DecodePNGAndUpload`
        (`0x1400728B0`) still overrides an entire shared texture page's pixels with one
        hook, no per-sprite name-matching needed — better first target if surgical
        single-sprite substitution isn't required. Neither hook has actually been
        installed/tested yet — this is a location fix, not a completed feature.
      - **CONFIRMED live same day**: extended the proxy DLL (`mod_test/aurie_build/
        proxy/dllmain.c`) with a `Texture_DecodePNGAndUpload` hook alongside the
        existing `ReadEntireFile` one — always substitutes a 64×64 solid-magenta PNG
        for whatever texture-page bytes the runner decoded. Rebuilt cleanly with the
        existing MinGW + vendored-MinHook toolchain, ran against the scratch rig: hook
        fired 3/3 times (one per `TXTR` entry in the launcher's `data.win`), and
        **every sprite/button/cursor on the chapter-select screen rendered solid
        magenta** (screenshot taken, confirms visually — not just log-inferred), game
        stayed up with no crash. **Whole-texture-page override is proven working, not
        just located.** `SPRT_PopulateFrames`'s per-sprite hook (the surgical,
        single-sprite alternative) is still unexploited — natural next step if
        per-asset (vs. whole-page) precision is wanted. Test build kept at
        `mod_test/aurie_build/version_texdecode_test.dll` initially, but **removed
        entirely later the same day** — the magenta override was unconditional and
        made the UI unreadable if the build ran again by accident; the finding is
        already fully documented above. `dllmain.c`/`version.dll` carry no texture
        hook going forward.
      - **2026-09-17: root-caused and FIXED AT THE SOURCE the "clicking Yes always
        loops back to the chapter-select prompt" bug** (the user specifically asked
        for the real launcher flow to work, not just a workaround — this was chased
        down to an actual fix, not left as a bypass). Symptom: launching via the
        normal launcher flow (`wine DELTARUNE.exe`, click "Yes" on "Would you like to
        start from Chapter 1?") relaunches the game but lands right back on the same
        prompt, every time. **Root cause, isolated in three steps, each confirmed
        with live hooks**:
        1. `ReadEntireFile` hook showed the self-relaunched child
           (`DELTARUNE.exe -game data.win launcher switch_-1 returning_0` — bare
           `data.win`, no `chapter1_windows\` prefix) reliably loads
           `mod_test\data.win` (2,953,218 bytes, the *launcher's own* data) instead of
           `chapter1_windows\data.win` (13,072,876 bytes, real chapter 1 content) —
           reproduced across multiple separate relaunches, not a one-off glitch.
           `chapter1_windows/data.win` itself is verified byte-identical to the real
           Steam install and reads perfectly at the OS level — not corrupted.
        2. A `CreateProcessW` hook showed the game *does* pass a correct-looking
           `lpCurrentDirectory` (`...\mod_test/chapter1_windows`) — but with a
           **mixed separator** (backslashes throughout, one bare `/` right before the
           chapter folder). Normalizing it to all-backslash and re-testing live
           **did not fix the bug** — ruled out as the cause.
        3. Checked `/proc/<pid>/cwd` on the live relaunched child: its actual Unix
           working directory **is** correctly `chapter1_windows/` — yet
           `_wfopen("data.win")` still resolved against the parent's directory
           regardless. This means the bug is *below* the `CreateProcess`/cwd layer
           entirely — most likely Wine's own Windows-side per-process
           current-directory tracking not being correctly initialized from
           `lpCurrentDirectory` for a freshly spawned child, independent of the real
           Unix `chdir` it also performs. Not something fixable by correcting
           parameters at the `CreateProcess` call site.
        **The actual fix**: since this project's proxy DLL loads via `DELTARUNE.exe`'s
        import table — before the CRT's own `argv` parsing and before `WinMain` —
        hook `kernel32!GetCommandLineW` and rewrite the bare `data.win` to
        `chapter1_windows\data.win` in the relaunch's own command line *before the
        game ever reads it*, sidestepping the broken cwd propagation entirely.
        Scoped narrowly (only rewrites when the command line contains both
        `-game data.win` and `launcher`, i.e. specifically the "start from chapter 1"
        relaunch). **Verified live end-to-end through the real, unmodified "click
        Yes" flow** (not the external workaround): `GetCommandLineW` hook logged the
        rewrite firing, `ReadEntireFile` confirmed the child then loaded
        `chapter1_windows\data.win` (the real 13 MB file), and a screenshot confirmed
        the actual chapter 1 save-select screen (`CHAPTER 1` / save slots /
        `CHAPTER SELECT` / `END PROGRAM`) — a full, clean chapter boot via the
        genuine relaunch mechanism, zero looping, zero external `-game` override
        needed. Implementation in `mod_test/aurie_build/proxy/dllmain.c`
        (`HookedGetCommandLineW`).
        **Generalized to any chapter, same day, per user request — then verified
        live on an actual different chapter.** v1 hooked `GetCommandLineW` and
        hardcoded `chapter1_windows` — worked, but only for that one case. v2 folded
        the fix directly into the `CreateProcessW` hook and extracted just the last
        path component of `lpCurrentDirectory` (e.g. `chapter1_windows`) — more
        general, but broke on the *other* relaunch shape the user's own in-game
        testing turned up: "return to launcher" passes
        `lpCurrentDirectory="...\mod_test/../"` (a relative `..`, not a bare
        `chapterN_windows` name), which v2's naive last-component extraction didn't
        handle, and the user's live retry of that path **crashed**. **v3 (current)**:
        instead of pattern-matching a directory name, fully **resolve**
        `lpCurrentDirectory` into a clean absolute path via `GetFullPathNameW`
        (correctly collapses `..`/`.`/mixed separators — pure string arithmetic, not
        subject to whatever Wine bug breaks the *actual* cwd application — verified
        standalone: `"...\mod_test/../"` → `"<data-drive>\DeltaRune"`,
        correctly), then splice that **absolute path** in front of the bare
        `data.win` argument. Zero knowledge of `switch_N`'s numeric meaning, zero
        chapter-name pattern-matching, needed. Re-verified live end-to-end, twice:
        (1) the "click Yes → chapter 1" flow still works with v3; (2) **the user
        used the in-game "Chapter Select" menu to jump to chapter 3 directly** — the
        "return to launcher" hop's `lpCurrentDirectory` resolved to the *wrong*
        absolute directory (one level too high — see the open question below) but
        the game evidently recovers from that on its own (likely via its own
        documented default-search fallback, `sub_1400BA000`,
        [09_datawin_loader.md](09_datawin_loader.md)), reached the launcher's
        `CHAPTER_SELECT` screen successfully, and picking chapter 3 there produced
        `cwd_param="...\mod_test/chapter3_windows"` → correctly resolved and spliced
        in with **no code changes and no chapter-specific logic at all** — window
        title changed to "DELTARUNE Chapter 3", screenshot confirmed the real
        Chapter 3 save-select screen (`Ch 2 Files` copy-forward option, its own art,
        `DELTARUNE v0.0.105`). (Scratch-rig note: this required `cp -r`-ing
        `chapter3_windows/` — 146 MB — from the real install into `mod_test/` first;
        the rig only had chapter 1 before.) Current implementation:
        `HookedCreateProcessW` in `mod_test/aurie_build/proxy/dllmain.c` (the
        `GetCommandLineW` hook was removed in v2, fully superseded).
        **Open question, not blocking**: the "return to launcher" hop's own
        `lpCurrentDirectory` (`"...\mod_test/../"`, i.e. computed as "wherever I
        currently am, go up one level") resolves to `mod_test`'s *parent*
        (`<workspace>`, which has no `data.win` at all) rather than
        `mod_test` itself — meaning **the game's own internal notion of "current
        directory" was already wrong** (stuck at `mod_test`, not
        `mod_test\chapter1_windows`, despite the chapter 1 boot itself having
        succeeded) by the time it computed this later transition. This points at a
        deeper version of the same root cause: Wine's Windows-side per-process cwd
        state (whatever `GetCurrentDirectoryW` would report to the game) likely
        never actually updates from `lpCurrentDirectory` at child-process creation,
        so *every* subsequent relative computation the game itself makes is built on
        a stale base. Not fixed — the game's own fallback search happens to paper
        over it for now. A more thorough fix would have the proxy DLL's `DllMain`
        call `SetCurrentDirectoryW` explicitly in the child (using the resolved
        target, handed over via an environment variable set in the parent's
        `CreateProcessW` hook) so the game's *own* internal cwd tracking stays
        correct across multiple hops, not just the one file-open call this fix
        currently patches around. Flagged as a follow-up, not attempted.
        **Bonus, separately confirmed same session**: the proxy DLL does not reliably
        win Wine's native-vs-builtin `version.dll` resolution on every relaunch
        (`MH_CreateHook`/hook installation calls didn't even run for one observed
        child process — it loaded Wine's builtin `version.dll` instead of ours) —
        always pass `WINEDLLOVERRIDES="version=n,b"` explicitly on launch from now on
        rather than relying on default DLL search order, which was previously
        assumed reliable but isn't. The direct `-game chapter1_windows/data.win`
        external-launch form documented earlier this session still works as a
        simpler fallback if the DLL-based fix isn't in place for some reason, but is
        no longer the primary recommendation now that the general fix exists.
- [ ] **Frida + wine — live `var_id`↔name map (highest value).**
  - Launch: `WINEDEBUG=-all wine "<steam-library>/steamapps/common/DELTARUNE/DELTARUNE.exe"`
    (or launch a chapter, or via Steam). Then `frida -n DELTARUNE.exe` or attach to the
    wine process; `frida-ps -a` to find it.
  - Hooks (addresses from the docs; rebase to the live module base):
    - `Function_Add` `0x1401BAA10` — log `(name, func, argc)` live; diff vs
      `gml_builtins_manifest.json`.
    - `Variable_GetBuiltinId` `0x1400423C0` + `Variable_GetValue` `0x1400426B0` — record
      `name → var_id` for every `global.*` access. This is the name table that is *not*
      in the exe; dump it to `cheatengine/global_vars.json`.
    - `YYError` `0x1401D7B10` — capture runtime errors during play.
  - Deliverable: `cheatengine/global_vars.json` (name→var_id→last value), then rewrite
    `deltarune_stats.lua` (and/or a Frida trainer) to resolve by name with zero fragile
    offsets.
- [x] **PINCE / Linux port of the CE stat script.** `cheatengine/deltarune_stats_pince.py`
      — pure `/proc/<pid>/mem` (no ptrace/gdb, no root needed here), same by-name /
      by-value resolve + freeze/set as the Lua. Runs standalone (auto-finds the pid,
      REPL) or `import`ed inside PINCE's Libpince Engine (reuses `debugcore.currentpid`).
      Inside PINCE it auto-adds one `[DR] <stat>` row per entry to the address table via
      `MainForm.add_entry_to_addresstable` — scalars as an absolute address, array
      elements as a PINCE `PointerChainRequest(rv, [0x90, 16*i])` that PINCE re-walks
      each refresh (`dr.to_pince()` / `dr.from_pince()`; `dr.stop()` removes them).
      2026-09-04: verified live vs a running Proton copy — by-name mode, 84 entries all
      correct, pointer-chain rows match the direct walk. Under Proton the PE loads at
      its preferred base `0x140000000` (no ASLR slide); RVAs from `07_...md` used as-is.
      Also verified inside a real PINCE `MainForm` (gdb-attached): `[DR]` rows added,
      PINCE's own Freeze checkbox held a pointer-chain row across an external memory
      perturbation, `dr.stop()` cleaned up. Fixed a bug found live: character id **4 =
      Noelle** (joins Ch2) wasn't in `PARTY` (only 1-3 were), so her HP/stats rows were
      silently skipped — confirmed via `global.char=[1,4,0]`, `global.hp[4]=2`,
      `global.maxhp[4]=90` matching the on-screen "2/90" bar. Docs: `cheatengine/README.md`.
      - **2026-09-17: new `list_instances()` feature using this session's static RE
        findings.** Uses the just-decompiled `g_pInstanceManager` hashmap
        ([08_instances.md](08_instances.md), P3) plus the newly-confirmed
        `CInstance.x`/`.y` offsets (`+232`/`+236`, P4 type backlog) to enumerate
        **every live GML instance in the room** — a real capability upgrade beyond
        the fixed global-variable stat list, which only ever covered party/inventory
        stats. **Live-testing this immediately caught a real bug in the static
        write-up**: `g_pInstanceManager+136` is a *pointer to* the hashmap header,
        not the header inline — reading it as inline gives a bogus multi-million
        "capacity" and a `NULL` slots pointer; dereferencing it once first gives
        sane values (`capacity=1024`/`count=399`/`mask=1023`/valid slots ptr) and
        plausible on-screen positions for real instances. Corrected in both the IDB
        comment and `08_instances.md`. Filters by `YYObjectBase.kind` (`1` = real
        `CInstance`); does *not* identify which GML object an instance is —
        `typeIndex` is confirmed debug-build-only and unreliable in this shipped
        build, a genuine dead end, not just unexplored. Docs:
        `cheatengine/README.md#live-instance-listing-list_instances-added-2026-09-17`.
- [ ] **UndertaleModTool CLI — dump all 5 chapters.** `code/` is ~chapter 1 only. For
      each `chapterN_windows/data.win`:
      `utmtcli dump <data.win> --code --strings ... < /dev/null` (check `utmtcli dump --help`).
      Land them under `code/chapterN/` (keep the flat filename scheme). Also try a UTMT
      C# mod script (infinite HP / all items) as a cleaner persistent cheat than memory
      editing.
- [x] **Native Windows mod loader** (2026-09-27) — `modloader/` (see its README). Port of
      the Wine proxy: `version.dll` proxy (forwards to System32, no renamed copy), MinHook on
      `ReadEntireFile` + `ExecuteIt`, `mods\<Mod>\<relative path>` file overrides (last mod
      wins), `mods\<Mod>\redirects.txt` script redirects, 16-byte prologue check so an
      updated exe runs unmodded instead of crashing, CreateProcessW fix only under Wine.
      Built with MSVC 2022 (`build.bat`). **Verified live on the Steam install:** hooks OK,
      `obj_ui_version` Draw→Create redirect active, `options.ini` + Ch1 `lang_en.json`
      overrides served (542,800 → 542,809 bytes), chapter relaunch from chapter select
      works natively with no fix. Logs in `modloader/test_logs/`. Not yet visually
      confirmed on screen. Wine build of the new loader untested.
- [x] **Aurie + YYToolkit running natively via the modloader** (2026-09-27). The loader
      parks the main thread at the exe entry point and loads `mods\native\AurieCore.dll`,
      which then boots `mods\aurie\YYToolkit.dll` plus plugins. No AuriePatcher, exe
      unmodified. Three fixes were needed:
      (1) **Chapter "Play" loop:** AurieCore `SetCurrentDirectoryW(exe dir)` made the
      relative `-game data.win` resolve to the launcher `data.win`. The loader now
      restores the working directory before the entry point continues. Verified: chapter
      select → Play → `chapter2_windows\data.win` (66,949,110 B) → `room_intro_ch2`.
      (2) **YYTK `Failed to locate room data!`:** DELTARUNE's `F_RoomInstanceClear` calls
      a non-inlined `Room_Data` (`0x1400B6330`, renamed in IDB; array `g_RoomArray_Items`
      `0x1405FC130`, count `0x1405FC128`). Patched YYTK Zeus
      (`modloader/third_party/yytk_patch`).
      (3) YYTK `GetInstanceMember(global)` crashes in `YYGetString`; the plugin uses
      `variable_global_get` instead. EVENT_FRAME never fires in YYTK 5.0.0 (no Stage 3
      hook call). The example plugin `modloader/yytk_plugin` logs room, chapter and gold
      live.
      - [x] Upstream the Room_Data fix: https://github.com/AurieFramework/YYToolkit/pull/85
      - [ ] Re-test the whole stack under Wine (Aurie's MmCreateHook hung there before).
      - [ ] Port the old `mod_test/aurie_build` plugin ideas to the v5 headers.
- [x] **Cleanup** (2026-09-27): deleted the whole `mod_test/` Wine rig (800 MB: proxy
      prototype, v4 ExamplePlugin/ourplugin, version_*_test.dll experiments,
      control_test game copy, stale v4 YYToolkit/AurieCore) plus root
      `proxy_log.txt` / `readfile_hook_log.624.txt`. All working code is in `modloader/`.
      `mod_test/` paths in this file are historical.
- [x] **Aurie console hidden** (2026-09-27): AurieCore `AllocConsole()`s in every
      process, and each chapter is a new process, so each switch popped a console. The
      loader hooks `AllocConsole` and returns TRUE without creating one;
      `aurie_console=1` in `mods\modloader.ini` restores it. `aurie.log` is unaffected.
- [x] **Loose-GML script replacement (big goal: no xdelta)** — working 2026-09-27.
      Mods ship `mods\<Mod>\chapterN_windows\code\<gml_Name>.gml` (decompiled GML, UTMT
      naming). When the game reads that chapter's `data.win` (`ReadEntireFile` hook), the
      loader runs UTMT's compiler (`mods\tools\utmt\UndertaleModCli.exe` +
      `mods\tools\ImportGMLFolder.csx`, hidden window). It gets the patched bytes back
      over a named pipe, into a buffer from the runner's own allocator (`sub_14038F430`,
      prologue-checked). **Nothing is written to disk and vanilla data.win is untouched.**
      Compile errors fall back to vanilla; details go to `mods\modloader.gml.log`.
      **Verified live:** Local Multiplayer Ch2 = 216 loose .gml, compiled in about 4 s.
      Screenshot shows "CHAPTER 2 LOCAL MULTIPLAYER" / "Version 6" / "Mod Created by:
      Yomsman" on the file menu. Converter: `modloader/tools/xdelta_to_gml.py` (xdelta →
      changed code entries only). All 5 chapters converted (79/216/437/560/1520 files, 25 MB),
      and all compile. Only Ch2 has been checked in game.
      - [ ] Asset changes are not carried: the multiplayer mod drops a few unused
            sprites/objects in Ch2–5, which is harmless, but Ch5 also adds 1 embedded
            texture + 1 sound. Needs loose asset import (sprites/sounds) next to the GML.
      - [ ] Per-script runtime swap (follow-up, per user): compile a single .gml and
            replace that `CCode`'s `VMBuffer` live, with no full-file recompile. First
            needs runtime registration of new variable/string ids (IDA).
      - [ ] Cache the compiled result keyed by (vanilla hash + .gml mtimes), kept in
            memory or in `%TEMP%` if you're OK with a temp file, to skip the ~4 s
            compile on relaunch.
      - Upstream: YYToolkit PR #85 (Room_Data fix) opened 2026-09-27 from
        `jfmherokiller/YYToolkit` `fix/vm-room-data-non-inlined`.
      - Test case: Local Multiplayer v19 (gamebanana 601376), 5 xdeltas for 1.05. Its
        base checksums match the installed Steam files. Ch2 diff (UTMT CLI 0.9.2.0
        dump, vanilla vs patched): **216 code entries changed** (193 object events +
        23 scripts), 0 added, 2 unused scripts removed, **+255 variables, +252
        strings**, no asset changes seen.
      - Compiler: UTMT's (`UndertaleModLib.Compiler.CodeImportGroup`). Headless
        import script is `modloader/tools/ImportGMLFolder.csx`. Recompiling those 216
        decompiled files against vanilla Ch2 succeeds in about 6 s with 0 errors.
- [ ] **radare2 / rizin — second opinion + scripting practice.**
  - `r2 -A DELTARUNE.exe`; `afl~gml_`, `axt @ sym.Function_Add`, `pdf`, play with ESIL.
  - Script a standalone xref-count ranking to seed Track A/P1 without IDA.
  - `radiff2` / `rz-diff` old vs new `DELTARUNE.exe` when the game updates.
- [ ] **Ghidra headless — coverage diff.** `analyzeHeadless` import `DELTARUNE.exe`, run
      a script to export all function names; compare against the IDB's coverage to find
      functions Ghidra recognises that IDA left as `sub_*` (and vice-versa). Optionally
      import `DELTARUNE.pdb` so Ghidra carries the IDA names.
- [ ] **x64dbg via wine + `DELTARUNE.pdb`.** Live debugging with symbol names — set
      breakpoints on `scr_damage` / `Variable_GetValue`, watch HP writes.
- [ ] **ImHex — pattern files.** One for `data.win` (GameMaker FORM/chunk layout); one
      for `RValue` / `YYObjectBase` to overlay on a wine process memory dump.
- [ ] **binwalk / 7z** — sanity-scan `data.win` and the chapter blobs for embedded
      archives / textures.

---

## Handy references

- Backup IDB: `<steam-library>/steamapps/common/DELTARUNE/DELTARUNE.exe.i64.bak-20260904`
- idalib session id used so far: `deltarune`
- Key addrs: `Function_Add` `0x1401BAA10` · `Variable_GetValue` `0x1400426B0` ·
  `Variable_GetBuiltinId` `0x1400423C0` · `YYError` `0x1401D7B10` ·
  `g_BuiltinVarAccessors` `0x14067F188` · `g_pGlobalObject` `0x1406A9DC0` ·
  `sub_140082340` (instance mgr, needs analysis)
- FakePDB (regen PDB): Windows IDA → `Edit → FakePDB → Generate .PDB file`;
  plugin at `<windows-drive>/Program Files/IDA Professional 9.1/plugins/fakepdb/`
