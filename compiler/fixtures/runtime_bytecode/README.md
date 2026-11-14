# Runtime bytecode fixtures (ROADMAP 41)

Paired bytecode for seven small GML snippets: the bytes Underanalyzer emitted (as stored in the
data.win the game was served) and the same buffer in the runner's memory after its CODE-chunk
load fixup. These are the input for deriving the load-time operand fixup (ROADMAP 4,
`compiler/RUNTIME_COMPILER.md`).

All snippets are our own code. No game bytecode is stored here.

## Layout

| Path | What |
|---|---|
| `mod/RuntimeBytecodeFixtures/code/*.gml` | The snippets, as a loose mod for the launcher (root `data.win`). |
| `launcher/offline/<entry>.bin` | Pre-fixup bytes, cut from the served data.win (Underanalyzer output, as written by UndertaleModLib). |
| `launcher/offline/offline.tsv` | Per entry: file offset of the buffer, CODE length, locals, args, child offset, number of differing words. |
| `launcher/runtime/<entry>.bin` | Post-fixup bytes, copied live from `CCode->VMBuffer->bytecode`. |
| `launcher/runtime/capture.tsv` | Per entry: VMBuffer length, VMBuffer `+0x0c`/`+0x10`, CCode/VMBuffer/bytecode addresses, CCode script index, locals, args, flags. |
| `launcher/listings/<entry>.txt` | Source, then every instruction word side by side (file, runtime). Differing words are marked `*`. |
| `launcher/run.log` | The loader and RunnerCompiler log lines from the capture run. |

Each snippet is a global script with one function, so the data.win holds two code entries per
snippet: `gml_GlobalScript_rcfx_<x>` (the parent, which only defines the function) and
`gml_Script_rcfx_<x>` (the function, child offset 4). The runner gives both entries a VMBuffer
that points at the same whole parent buffer. The two runtime dumps are byte-identical, and the
child's start offset is not applied in the VMBuffer. Both are kept because their CCode metadata
differs (locals/args).

## Snippets

| Entry | Covers |
|---|---|
| `rcfx_globals` | `global.` read, write and compound assignment (`pushglb`, `pop` to global). |
| `rcfx_instance` | Built-in instance variables (`x`, `y`, `image_alpha`), a new self variable, an `other.` write. |
| `rcfx_locals` | `var` declarations, a named argument, a `for` loop over a local (branches). |
| `rcfx_builtins` | Built-in function calls (`abs`, `floor`, `string_length`, `show_debug_message`, `string`, `max`) and a built-in read (`room_speed`). |
| `rcfx_scripts` | Calls to game scripts (`strlen`, `scr_wrap`) and to another fixture function (`rcfx_locals`). |
| `rcfx_strings` | String constants, including one used twice, and string concatenation. |
| `rcfx_arrays` | Array literal, local array write and read, `global.` array, self array, `array_length`. |

Words that differ, all fixtures (`python3 compiler/tools/compare_runtime_bytecode.py <served> <capture dir>`):
operand words of `pop` (58), `push` (42), `call` (36), `pushloc` (22), `pushglb` (2), `pushbltn` (2).
Opcode words, branch offsets and `pushi` immediates are unchanged.

## How the dumps were obtained

Game: DELTARUNE (VM runner), exe md5 `7bf3cccc2e54481ced3a149e1a083684`, launcher data.win md5
`83a5a14f9b92a20f21fb9ec6c8528469`. Run under Proton (proton-cachyos-slr) on Linux, 2026-11-13,
with the repo's loader, YYToolkit v5 and the RunnerCompiler spike built from this commit.

1. **Served data.win.** The loader compiled the fixture mod into the launcher's data.win with
   LooseModCompiler and served 2,956,674 bytes (`run.log`). The same build run offline produces
   byte-identical output (md5 `5d960c65c30591d19e2fb06d55092616`, deterministic across two
   runs, same size as served):
   ```sh
   : > served.raw
   DR_MOD_DIRS=compiler/fixtures/runtime_bytecode/mod/RuntimeBytecodeFixtures DR_OUT_PIPE=$PWD/served.raw \
       compiler/build/linux-x64/LooseModCompiler load <game>/data.win
   ```
   `served.raw` is an 8-byte length followed by the data.win. The tools accept it as is.
2. **Runtime.** The RunnerCompiler plugin's named capture (`capture.txt` with the prefixes
   `gml_GlobalScript_rcfx_` and `gml_Script_rcfx_`) walked the runner's CCode list on the first
   `EVENT_OBJECT_CALL` (352 CCode objects, 14 matched) and copied each VMBuffer. The game then
   kept running at chapter select (3000 steps) until it was killed. It did not crash.
3. **Pairing.**
   ```sh
   python3 compiler/tools/runtime_bytecode_fixtures.py served.raw <game>/mods/aurie/runnercompiler/capture \
       compiler/fixtures/runtime_bytecode/launcher --source compiler/fixtures/runtime_bytecode/mod/RuntimeBytecodeFixtures/code
   ```

The full steps, including install and restore, are in `compiler/RUNTIME_COMPILER.md`
("Live run and fixtures").

Observed in this run: the runtime bytecode pointer minus the buffer's file offset is the same
for all seven buffers (`0x3180060`). The runner fixes the operands in place inside its loaded
copy of data.win.
