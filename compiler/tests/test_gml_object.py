#!/usr/bin/env python3
"""Runtime linking of GmlObjectCompiler output (compiler/tools/gml_object.py, gmlo_link.hpp).

    python3 -m unittest discover -s compiler/tests -v

compiler/fixtures/runtime_link holds five snippets compiled against the vanilla launcher
data.win, and the bytes the in-game linker (RunnerCompiler link mode) produced for them in a live
run. The tests check that the native linker rewrote exactly the relocated operand words, and that
an offline link with the ids it chose reproduces its bytes. Set DR_LAUNCHER_WIN to the vanilla
launcher data.win to also check the ids against the data.win load-order rules.
"""
import os, struct, sys, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import bytecode_fixup as fx  # noqa: E402
import gml_object as go  # noqa: E402

FIXTURE = os.path.join(HERE, "..", "fixtures", "runtime_link")
# Link order of the live run (link.txt): ids for new names are handed out in this order.
ORDER = ["rcl_globals", "rcl_debug", "rcl_script", "rcl_instance", "rcl_strings"]


def load_pairs():
    pairs = []
    for stem in ORDER:
        obj = go.load(os.path.join(FIXTURE, "objects", stem + ".gmlo"))
        with open(os.path.join(FIXTURE, "linked", stem + ".bin"), "rb") as f:
            pairs.append((stem, obj, f.read()))
    return pairs


class RuntimeLinkTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pairs = load_pairs()
        cls.ids = {}
        for _, obj, linked in cls.pairs:
            for key, value in go.runtime_ids(obj, linked).items():
                cls.ids.setdefault(key, value)

    def test_parse(self):
        for stem, obj, linked in self.pairs:
            with self.subTest(stem=stem):
                self.assertEqual(obj.name, "gml_GlobalScript_" + stem)
                self.assertEqual(len(obj.code), len(linked))
                self.assertTrue(obj.relocs)

    def test_only_relocated_words_change(self):
        for stem, obj, linked in self.pairs:
            operands = {r.offset + 4 for r in obj.relocs}
            for at in range(0, len(obj.code), 4):
                if at not in operands:
                    with self.subTest(stem=stem, offset=at):
                        self.assertEqual(obj.code[at:at + 4], linked[at:at + 4])

    def test_reloc_opcodes(self):
        # var: push/pushloc/pushglb/pushbltn/pop (0xC0-0xC3, 0x45); func: call (0xD9) or
        # push.i of a function; string: push.s (0xC0, type 6).
        allowed = {"var": {0xC0, 0xC1, 0xC2, 0xC3, 0x45}, "func": {0xD9, 0xC0}, "string": {0xC0}}
        for stem, obj, _ in self.pairs:
            for r in obj.relocs:
                word = struct.unpack_from("<I", obj.code, r.offset)[0]
                with self.subTest(stem=stem, reloc=r):
                    self.assertIn(word >> 24, allowed[r.kind])
                    if r.kind == "string":
                        self.assertEqual((word >> 16) & 0xF, 6)

    def test_offline_link_reproduces_runtime_bytes(self):
        builtin_vars = {n: v for (k, n), v in self.ids.items() if k == "var" and v < fx.USER_ID_BASE}
        user_vars = {n: v - fx.USER_ID_BASE for (k, n), v in self.ids.items() if k == "var" and v >= fx.USER_ID_BASE}
        funcs = {n: v for (k, n), v in self.ids.items() if k == "func"}
        resolver = fx.Resolver(builtin_vars, funcs, user_vars)
        for stem, obj, linked in self.pairs:
            with self.subTest(stem=stem):
                self.assertEqual(go.link(obj, resolver, lambda s: self.ids[("string", s)]), linked)

    def test_expected_ids(self):
        # Built-ins and the script call match the ids the launcher's own bytecode uses
        # (compiler/fixtures/runtime_bytecode/launcher/links.json).
        self.assertEqual(self.ids[("var", "x")], 0)
        self.assertEqual(self.ids[("var", "i")], fx.USER_ID_BASE + 49)
        self.assertEqual(self.ids[("func", "abs")], 200)
        self.assertEqual(self.ids[("func", "@@NewGMLArray@@")], 2253)
        self.assertEqual(self.ids[("func", "array_length")], 170)
        self.assertEqual(self.ids[("func", "gml_Script_strlen")], fx.USER_ID_BASE + 81)

    def test_new_names_get_consecutive_slots(self):
        # Names the game had never seen get the runner's next free slot, in link order.
        # Every user variable in the snippets except the loop counter "i" is new to the launcher.
        new = []
        for _, obj, _ in self.pairs:
            for r in obj.relocs:
                if r.kind == "var" and r.name != "i" and self.ids[("var", r.name)] >= fx.USER_ID_BASE and r.name not in new:
                    new.append(r.name)
        self.assertEqual(len(new), 6)
        slots = [self.ids[("var", n)] for n in new]
        self.assertEqual(slots, list(range(slots[0], slots[0] + len(slots))), dict(zip(new, slots)))


@unittest.skipUnless(os.environ.get("DR_LAUNCHER_WIN"), "set DR_LAUNCHER_WIN to the vanilla launcher data.win")
class LoadOrderTest(unittest.TestCase):
    """The ids the runtime linker chose against the data.win: existing names and strings keep the
    load-order ids, new names and strings come after everything the data.win defines."""

    @classmethod
    def setUpClass(cls):
        from compare_runtime_bytecode import chunks, cstring, load_data
        cls.d = load_data(os.environ["DR_LAUNCHER_WIN"])
        start, _ = chunks(cls.d)["STRG"]
        count = struct.unpack_from("<I", cls.d, start)[0]
        cls.strings = {}
        for i in range(count):
            cls.strings.setdefault(cstring(cls.d, struct.unpack_from("<I", cls.d, start + 4 + 4 * i)[0] + 4), i)
        cls.string_count = count
        cls.slots = fx.user_variable_slots(cls.d)
        cls.scripts = fx.script_indices(cls.d)
        cls.ids = {}
        for _, obj, linked in load_pairs():
            for key, value in go.runtime_ids(obj, linked).items():
                cls.ids.setdefault(key, value)

    def test_strings(self):
        appended = []
        for (kind, name), value in self.ids.items():
            if kind != "string":
                continue
            if name in self.strings:
                self.assertEqual(value, self.strings[name], name)
            else:
                appended.append(value)
        self.assertEqual(sorted(appended), list(range(self.string_count, self.string_count + len(appended))))

    def test_variables(self):
        for (kind, name), value in self.ids.items():
            if kind != "var" or value < fx.USER_ID_BASE:
                continue
            with self.subTest(name=name):
                if name in self.slots:
                    self.assertEqual(value, fx.USER_ID_BASE + self.slots[name])
                else:   # at or after the data.win's names (the game may add some at runtime first)
                    self.assertGreaterEqual(value, fx.USER_ID_BASE + len(self.slots))

    def test_scripts(self):
        for (kind, name), value in self.ids.items():
            if kind == "func" and value >= fx.USER_ID_BASE:
                self.assertEqual(value, fx.USER_ID_BASE + self.scripts[name], name)


if __name__ == "__main__":
    unittest.main()
