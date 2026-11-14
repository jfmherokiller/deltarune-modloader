#!/usr/bin/env python3
"""Load-time operand fixup (compiler/tools/bytecode_fixup.py) against the live-captured fixtures.

    python3 -m unittest discover -s compiler/tests -v

The fixture test needs only the committed files (compiler/fixtures/runtime_bytecode/launcher).
Set DR_SERVED_WIN to the served data.win (DR_OUT_PIPE output, see RUNTIME_COMPILER.md) to also
re-derive the relocations and ids from the data.win itself.
"""
import json, os, struct, sys, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import bytecode_fixup as fx  # noqa: E402

FIXTURE = os.path.join(HERE, "..", "fixtures", "runtime_bytecode", "launcher")


def load_links():
    with open(os.path.join(FIXTURE, "links.json"), encoding="utf-8") as f:
        return json.load(f)


class FixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.links = load_links()
        cls.pairs = list(fx.fixture_entries(FIXTURE))

    def relocs(self, name):
        return [fx.Reloc.from_json(r) for r in self.links["entries"][name]["relocs"]]

    def test_all_fixtures_present(self):
        self.assertEqual(len(self.pairs), 14)
        self.assertEqual({n for n, *_ in self.pairs}, set(self.links["entries"]))

    def test_fixup_reproduces_runtime_bytes(self):
        resolver = fx.resolver_from_links(self.links)
        for name, _, offline, runtime in self.pairs:
            with self.subTest(entry=name):
                self.assertNotEqual(offline, runtime)
                self.assertEqual(fx.apply_fixup(offline, self.relocs(name), resolver), runtime)

    def test_only_relocated_operand_words_differ(self):
        for name, _, offline, runtime in self.pairs:
            operand_words = {r.offset + 4 for r in self.relocs(name)}
            differing = {i for i in range(0, len(offline), 4) if offline[i:i + 4] != runtime[i:i + 4]}
            with self.subTest(entry=name):
                self.assertEqual(differing, operand_words)

    def test_child_entry_shares_parent_buffer_and_relocations(self):
        for name, _, offline, runtime in self.pairs:
            if name.startswith("gml_Script_"):
                parent = name.replace("gml_Script_", "gml_GlobalScript_")
                with self.subTest(entry=name):
                    self.assertEqual(self.links["entries"][name]["relocs"], self.links["entries"][parent]["relocs"])

    def test_id_spaces(self):
        resolver = fx.resolver_from_links(self.links)
        for name, entry in self.links["variables"].items():
            ident = resolver.var_id(name)
            self.assertEqual(ident >= fx.USER_ID_BASE, "slot" in entry, name)
        for name, entry in self.links["functions"].items():
            ident = resolver.func_id(name)
            self.assertEqual(ident >= fx.USER_ID_BASE, "script_index" in entry, name)
            self.assertEqual(name.startswith("gml_Script_"), "script_index" in entry, name)


class RuleTest(unittest.TestCase):
    def test_variable_keeps_reference_type_bits(self):
        code = struct.pack("<IIII", 0x45550000, 0x8000048F, 0xC005FFFB, 0x00000040)
        out = fx.apply_fixup(code, [fx.Reloc(0, "var", "v", -1), fx.Reloc(8, "var", "w", -5)],
                             fx.Resolver(user_vars={"v": 7, "w": 8}))
        self.assertEqual(struct.unpack("<IIII", out), (0x45550000, 0x80000000 | 100007, 0xC005FFFB, 100008))

    def test_function_word_is_replaced_whole(self):
        code = struct.pack("<II", 0xD9020002, 0x000000AC)
        out = fx.apply_fixup(code, [fx.Reloc(0, "func", "method")], fx.Resolver(builtin_funcs={"method": 162}))
        self.assertEqual(struct.unpack("<II", out), (0xD9020002, 162))
        out = fx.apply_fixup(code, [fx.Reloc(0, "func", "gml_Script_f")], fx.Resolver(scripts={"gml_Script_f": 239}))
        self.assertEqual(struct.unpack("<II", out)[1], 100239)

    def test_new_user_variable_takes_next_slot(self):
        resolver = fx.Resolver(user_vars={"a": 0, "b": 1})
        self.assertEqual(resolver.var_id("b"), 100001)
        self.assertEqual(resolver.var_id("new_name"), 100002)
        self.assertEqual(resolver.var_id("new_name"), 100002)

    def test_unknown_function_raises(self):
        with self.assertRaises(KeyError):
            fx.Resolver().func_id("nope")


@unittest.skipUnless(os.environ.get("DR_SERVED_WIN"), "set DR_SERVED_WIN to the served data.win")
class ServedDataWinTest(unittest.TestCase):
    def test_links_rederive_from_data_win(self):
        d = fx.load_data(os.environ["DR_SERVED_WIN"])
        self.assertEqual(fx.build_links(d, FIXTURE), load_links())

    def test_chain_resolution_relinks_every_entry(self):
        d = fx.load_data(os.environ["DR_SERVED_WIN"])
        pairs = list(fx.fixture_entries(FIXTURE))
        relocs = {n: fx.relocations_from_data_win(d, off, len(b)) for n, off, b, _ in pairs}
        resolver = fx.Resolver.from_data_win(d, *fx.observed_builtins((relocs[n], rt) for n, _, _, rt in pairs))
        for name, _, offline, runtime in pairs:
            with self.subTest(entry=name):
                self.assertEqual(fx.apply_fixup(offline, relocs[name], resolver), runtime)


if __name__ == "__main__":
    unittest.main()
