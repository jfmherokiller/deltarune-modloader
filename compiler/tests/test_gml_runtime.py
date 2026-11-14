#!/usr/bin/env python3
"""gmlrt::Runtime smoke fixtures (modloader/include/gml_runtime.hpp, ROADMAP 4/41).

    python3 -m unittest discover -s compiler/tests -v

compiler/fixtures/runtime_gml holds one GML snippet (gmlrt_smoke.gml) that RunnerCompiler's link
mode compiled in the game (RuntimeExports.CompileObject), linked and ran 1000 times with
Runtime::CompileAndRun on a VM runner (DELTARUNE launcher) and a YYC runner (Dwarf Eats Mountain).
objects/ is the compiler output for each game's data.win, linked/ the bytes the game ran, logs/ the
link.log of each run. The tests check that one source gave the same unlinked code on both runners,
that the in-game linker changed only relocated operand words, that an offline link with the ids it
chose reproduces its bytes, and that every side-effect check in the logs passed.
"""
import os, re, struct, sys, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import bytecode_fixup as fx  # noqa: E402
import gml_object as go  # noqa: E402

FIXTURE = os.path.join(HERE, "..", "fixtures", "runtime_gml")
RUNNERS = {"vm": "vm_deltarune_launcher.log", "yyc": "yyc_dwarf_eats_mountain.log"}


def load(kind):
    obj = go.load(os.path.join(FIXTURE, "objects", f"gmlrt_smoke.{kind}.gmlo"))
    with open(os.path.join(FIXTURE, "linked", f"gmlrt_smoke.{kind}.bin"), "rb") as f:
        return obj, f.read()


class RuntimeGmlTest(unittest.TestCase):
    def test_same_code_on_both_runners(self):
        # Only push.s operands (compile-time STRG indices, relinked anyway) may differ.
        (vm, _), (yyc, _) = load("vm"), load("yyc")
        self.assertEqual(len(vm.code), len(yyc.code))
        self.assertEqual([(r.offset, r.kind, r.name) for r in vm.relocs], [(r.offset, r.kind, r.name) for r in yyc.relocs])
        strings = {r.offset + 4 for r in vm.relocs if r.kind == "string"}
        for at in range(0, len(vm.code), 4):
            if at not in strings:
                self.assertEqual(vm.code[at:at + 4], yyc.code[at:at + 4], at)

    def test_only_relocated_words_change(self):
        for kind in RUNNERS:
            obj, linked = load(kind)
            self.assertEqual(len(obj.code), len(linked), kind)   # no script calls, so nothing lowered on YYC
            operands = {r.offset + 4 for r in obj.relocs}
            for at in range(0, len(obj.code), 4):
                if at not in operands:
                    with self.subTest(kind=kind, offset=at):
                        self.assertEqual(obj.code[at:at + 4], linked[at:at + 4])

    def test_offline_link_reproduces_runtime_bytes(self):
        for kind in RUNNERS:
            obj, linked = load(kind)
            ids = go.runtime_ids(obj, linked)
            builtin_vars = {n: v for (k, n), v in ids.items() if k == "var" and v < fx.USER_ID_BASE}
            user_vars = {n: v - fx.USER_ID_BASE for (k, n), v in ids.items() if k == "var" and v >= fx.USER_ID_BASE}
            funcs = {n: v for (k, n), v in ids.items() if k == "func"}
            resolver = fx.Resolver(builtin_vars, funcs, user_vars)
            with self.subTest(kind=kind):
                self.assertEqual(go.link(obj, resolver, lambda s: ids[("string", s)]), linked)

    def test_runner_specific_ids(self):
        # The same names resolve to different ids per runner: they must come from the live runner.
        vm = go.runtime_ids(*load("vm"))
        yyc = go.runtime_ids(*load("yyc"))
        self.assertEqual(vm[("func", "abs")], 200)              # launcher fixture id (links.json)
        self.assertEqual(vm[("var", "room_speed")], 136)
        self.assertEqual(yyc[("var", "room_speed")], 139)       # Dwarf Eats Mountain's runner
        self.assertNotEqual(vm[("func", "show_debug_message")], yyc[("func", "show_debug_message")])

    def test_live_checks_passed(self):
        for kind, log in RUNNERS.items():
            with open(os.path.join(FIXTURE, "logs", log)) as f:
                text = f.read()
            with self.subTest(kind=kind):
                self.assertIn(f"link: {kind.upper()} runner", text)
                self.assertIn("compiled + linked", text)
                self.assertEqual(len(re.findall(r"check PASS", text)), 5)
                self.assertNotIn("FAIL", text)
                self.assertIn("0 failure(s)", text)


if __name__ == "__main__":
    unittest.main()
