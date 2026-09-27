"""Real Qt widget checks with simulated process memory; no game required."""
import contextlib
import io
import os
import unittest
from unittest.mock import Mock, patch

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
try:
    from PyQt6.QtWidgets import QApplication, QMainWindow, QToolBar
except ImportError:
    QApplication = None

import deltarune_stats_pince as dr


@unittest.skipIf(QApplication is None, 'PyQt6 unavailable')
class GuardCheckboxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.win = QMainWindow()
        self.trainer = dr.Trainer()
        self.trainer.mem = Mock(pid=123, writable=True)
        self.trainer.mem.write_f64.return_value = True
        self.trainer.r = Mock(name_ok=True)
        self.trainer.r.global_obj.return_value = 5000
        self.trainer.r.map_by_name.return_value = {'inv': 1000}
        self.pid = patch.object(dr, '_pince_attached_pid', return_value=123)
        self.pid_mock = self.pid.start()
        self.quiet = contextlib.redirect_stdout(io.StringIO())
        self.quiet.__enter__()
        self.trainer._install_guard_controls(self.win)

    def tearDown(self):
        self.trainer._remove_guard_controls()
        self.win.close()
        self.win.deleteLater()
        self.app.processEvents()
        self.quiet.__exit__(None, None, None)
        self.pid.stop()

    def test_checkbox_controls_guard_and_failed_enable_is_unchecked(self):
        box = self.trainer._guard_checkbox
        box.click()
        self.assertTrue(self.trainer._damage_guard)
        self.trainer.mem.write_f64.assert_called_with(1000, dr.GUARD_FRAMES)
        box.click()
        self.assertFalse(self.trainer._damage_guard)
        self.trainer.mem.write_f64.assert_called_with(1000, 0)
        self.trainer.mem.write_f64.return_value = False
        box.click()
        self.assertFalse(box.isChecked())
        self.assertIn('Could not', self.trainer._guard_status.text())

    def test_refresh_reflects_disarm_and_pid_change(self):
        box = self.trainer._guard_checkbox
        box.click()
        self.trainer.r.map_by_name.return_value = {}
        self.trainer._guard_tick()
        self.trainer._sync_guard_controls()
        self.assertFalse(box.isChecked())
        self.trainer.r.map_by_name.return_value = {'inv': 1000}
        box.click()
        self.pid_mock.return_value = 456
        self.trainer._sync_guard_controls()
        self.assertFalse(box.isChecked())
        self.assertFalse(box.isEnabled())
        self.assertFalse(self.trainer._damage_guard)

    def test_click_rechecks_pid_before_writing(self):
        self.pid_mock.return_value = 456
        self.trainer._guard_checkbox.click()
        self.trainer.mem.write_f64.assert_not_called()
        self.assertFalse(self.trainer._guard_checkbox.isChecked())

    def test_install_is_idempotent_and_removal_disables_guard(self):
        self.trainer._install_guard_controls(self.win)
        self.assertEqual(len(self.win.findChildren(QToolBar)), 1)
        self.trainer._guard_checkbox.click()
        timer = self.trainer._guard_timer
        self.trainer._remove_guard_controls()
        self.assertFalse(timer.isActive())
        self.assertFalse(self.trainer._damage_guard)
        self.assertIsNone(self.trainer._guard_checkbox)
        self.trainer.mem.write_f64.assert_called_with(1000, 0)
