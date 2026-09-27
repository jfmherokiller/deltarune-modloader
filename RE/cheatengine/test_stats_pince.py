"""Offline regressions; no game process, PINCE, or memory writes required."""
import contextlib
import io
import unittest
import types
from unittest.mock import Mock, patch

import deltarune_stats_pince as dr


class MemoryChecks(unittest.TestCase):
    def guard_trainer(self):
        trainer = dr.Trainer()
        trainer.mem = Mock(writable=True)
        trainer.mem.write_f64.return_value = True
        trainer.r = Mock(name_ok=True)
        trainer.r.global_obj.return_value = 5000
        trainer.r.map_by_name.return_value = {'inv': 1000}
        return trainer

    def test_guard_reresolves_and_disable_resets_current_slot(self):
        trainer = self.guard_trainer()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(trainer.no_damage())
            trainer.mem.write_f64.assert_called_with(1000, dr.GUARD_FRAMES)
            trainer.r.map_by_name.return_value = {'inv': 2000}
            self.assertTrue(trainer._guard_tick())
            trainer.mem.write_f64.assert_called_with(2000, dr.GUARD_FRAMES)
            self.assertTrue(trainer.no_damage(False))
        trainer.mem.write_f64.assert_called_with(2000, 0)
        self.assertFalse(trainer._damage_guard)

    def test_guard_refuses_fallback_missing_variable_and_write_failure(self):
        for case in ('fallback', 'missing', 'read-only', 'failed-write'):
            trainer = self.guard_trainer()
            if case == 'fallback': trainer.r.name_ok = False
            if case == 'missing': trainer.r.map_by_name.return_value = {}
            if case == 'read-only': trainer.mem.writable = False
            if case == 'failed-write': trainer.mem.write_f64.return_value = False
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertFalse(trainer.no_damage(), case)
            self.assertFalse(trainer._damage_guard, case)
            if case in ('fallback', 'missing', 'read-only'):
                trainer.mem.write_f64.assert_not_called()

    def test_guard_disarms_on_replaced_global_or_failed_refresh(self):
        for case in ('new-global', 'write-failed'):
            trainer = self.guard_trainer()
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertTrue(trainer.no_damage())
                trainer.mem.write_f64.reset_mock()
                if case == 'new-global': trainer.r.global_obj.return_value = 6000
                else: trainer.mem.write_f64.return_value = False
                self.assertFalse(trainer._guard_tick())
            self.assertFalse(trainer._damage_guard)
            if case == 'new-global': trainer.mem.write_f64.assert_not_called()

    def test_guard_stop_resets_timer_before_closing_memory(self):
        trainer = self.guard_trainer()
        mem = trainer.mem
        with contextlib.redirect_stdout(io.StringIO()):
            trainer.no_damage()
            trainer.stop()
        self.assertEqual(mem.method_calls[-2:],
                         [unittest.mock.call.write_f64(1000, 0), unittest.mock.call.close()])
        self.assertFalse(trainer._damage_guard)

    def test_pince_rejects_another_pid(self):
        trainer = dr.Trainer()
        trainer.mem = Mock(pid=10)
        trainer.specs = [{'desc': 'Gold'}]
        win = Mock()
        lib = types.ModuleType('libpince')
        lib.debugcore = types.SimpleNamespace(currentpid=20)
        lib.typedefs = Mock()
        with patch.dict(dr.sys.modules, libpince=lib), patch.object(dr, '_pince_window', return_value=win):
            with contextlib.redirect_stdout(io.StringIO()):
                trainer.to_pince()
        win.add_entry_to_addresstable.assert_not_called()

    def test_inventory_capacity_uses_live_bounds(self):
        resolver = Mock()
        resolver.map_by_name.return_value = {'weapon': 1000, 'pocketitem': 2000}
        for capacity in (13, 48):
            resolver.arr_elem_addr.side_effect = lambda rv, i: (
                rv + 16 * i if i < (capacity if rv == 1000 else 72) else None)
            specs = dr.build_specs_by_name(resolver)
            weapons = [s for s in specs if s['pince']['var'] == 'weapon']
            pocket = [s for s in specs if s['pince']['var'] == 'pocketitem']
            self.assertEqual(len(weapons), capacity)
            self.assertEqual(len(pocket), 72)
            self.assertEqual(weapons[-1]['pince']['index'], capacity - 1)

    def test_double_write_checks_kind_and_short_write(self):
        mem = dr.Mem.__new__(dr.Mem)
        mem.fd, mem.writable = 123, True
        mem.u32 = Mock(return_value=2)
        with patch.object(dr.os, "pwrite", return_value=8) as write:
            self.assertFalse(mem.write_f64(1000, 90))
            write.assert_not_called()
            mem.u32.return_value = 0x1000000  # upper tag bits are masked
            self.assertTrue(mem.write_f64(1000, 90))
            write.return_value = 4
            self.assertFalse(mem.write_f64(1000, 90))
            mem.writable = False
            self.assertFalse(mem.write_f64(1000, 90))

    def test_array_bounds_and_element_kind(self):
        mem = Mock()
        mem.u64.side_effect = lambda a: {1000: 2000, 2000 + dr.ARR_ELEMS: 3000}.get(a)
        mem.i32.return_value = 4
        mem.u32.side_effect = lambda a: {1012: 2, 3028: 0, 3044: 2}.get(a)
        resolver = dr.Resolver(mem, 0)
        self.assertEqual(resolver.arr_elem_addr(1000, 1), 3016)
        self.assertIsNone(resolver.arr_elem_addr(1000, 2))
        self.assertIsNone(resolver.arr_elem_addr(1000, 4))
        self.assertIsNone(resolver.arr_elem_addr(1000, -1))

    def test_fallback_refuses_ambiguous_scalars(self):
        resolver = Mock()
        resolver.iter_vars.side_effect = lambda: iter([(1000, 1, 0), (2000, 2, 0)])
        resolver.kind_of.return_value = 0
        resolver.mem.f64.return_value = 0
        with contextlib.redirect_stdout(io.StringIO()), patch.dict(dr.CFG, gold=0, tension=0):
            self.assertEqual(dr.build_specs_by_value(resolver), [])
            resolver.mem.f64.side_effect = lambda a: 0 if a == 1000 else 50
            specs = dr.build_specs_by_value(resolver)
        self.assertEqual(len(specs), 2)
        self.assertEqual(specs[0]['get']({}), 1000)

    def test_manual_writes_resolve_again_and_failed_freeze_is_not_armed(self):
        trainer = dr.Trainer()
        trainer.mem = Mock()
        trainer._snapshot = lambda: {}
        trainer.specs = [{'desc': 'HP Kris', 'get': lambda snap: 2000}]
        trainer.addr = {'HP Kris': 1000}
        with contextlib.redirect_stdout(io.StringIO()):
            trainer.set('HP Kris', 90)
            trainer.mem.write_f64.assert_called_with(2000, 90)
            trainer.mem.write_f64.return_value = False
            trainer.freeze('HP Kris', 90)
        self.assertEqual(trainer.frozen, {})


if __name__ == '__main__':
    unittest.main()
