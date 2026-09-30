"""Unit tests for the bounded Windows staging-directory publication helper."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "prepare_native_bridge_publish", ROOT / "scripts/prepare-native-bridge.py"
)
PREPARE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(PREPARE)


def _permission(code: int) -> PermissionError:
    error = PermissionError(f"simulated Windows error {code}")
    error.winerror = code
    return error


class PreparePublishTests(unittest.TestCase):
    def _paths(self):
        temp = tempfile.TemporaryDirectory(prefix="prepare-publish-")
        root = Path(temp.name)
        source = root / "staging" / "candidate"
        source.mkdir(parents=True)
        (source / "source.txt").write_text("fixture\n", encoding="utf-8")
        return temp, source, root / "published"

    def test_transient_windows_sharing_errors_retry_with_bounded_delays(self):
        temp, source, output = self._paths()
        self.addCleanup(temp.cleanup)
        calls = []
        delays = []

        def move(src, dst):
            calls.append((src, dst))
            if len(calls) < 3:
                raise _permission(32)

        with mock.patch.object(PREPARE.os, "name", "nt"):
            PREPARE.publish_stage(source, output, rename=move, sleep=delays.append)

        self.assertEqual(len(calls), 3)
        self.assertEqual(delays, [0.05, 0.1])
        self.assertLess(sum(delays), 2.0)
        self.assertTrue(all(src == source.resolve() for src, _ in calls))
        self.assertTrue(all(dst == output.resolve() for _, dst in calls))
        self.assertTrue(source.exists(), "the fake move must not be emulated by deletion")

    def test_terminal_or_non_windows_errors_are_immediate(self):
        temp, source, output = self._paths()
        self.addCleanup(temp.cleanup)
        delays = []
        calls = []

        def terminal(src, dst):
            calls.append(1)
            raise _permission(1)

        with mock.patch.object(PREPARE.os, "name", "nt"):
            with self.assertRaises(PermissionError):
                PREPARE.publish_stage(source, output, rename=terminal, sleep=delays.append)
        self.assertEqual(calls, [1])
        self.assertEqual(delays, [])

        calls.clear()

        def ordinary(src, dst):
            calls.append(1)
            raise OSError("terminal filesystem failure")

        with mock.patch.object(PREPARE.os, "name", "nt"):
            with self.assertRaises(OSError):
                PREPARE.publish_stage(source, output, rename=ordinary, sleep=delays.append)
        self.assertEqual(calls, [1])
        self.assertEqual(delays, [])

        calls.clear()
        with mock.patch.object(PREPARE.os, "name", "posix"):
            with self.assertRaises(PermissionError):
                PREPARE.publish_stage(source, output, rename=terminal, sleep=delays.append)
        self.assertEqual(calls, [1])
        self.assertEqual(delays, [])

    def test_destination_appearing_after_transient_failure_is_never_overwritten(self):
        temp, source, output = self._paths()
        self.addCleanup(temp.cleanup)
        calls = []
        delays = []

        def move(src, dst):
            calls.append(1)
            dst.mkdir()
            raise _permission(33)

        with mock.patch.object(PREPARE.os, "name", "nt"):
            with self.assertRaisesRegex(ValueError, "Output appeared"):
                PREPARE.publish_stage(source, output, rename=move, sleep=delays.append)
        self.assertEqual(calls, [1])
        self.assertEqual(delays, [])
        self.assertTrue(output.is_dir())
        self.assertTrue(source.exists())

    def test_retry_exhaustion_reraises_final_native_error(self):
        temp, source, output = self._paths()
        self.addCleanup(temp.cleanup)
        failure = _permission(5)
        calls = []
        delays = []

        def exhausted(src, dst):
            calls.append(1)
            raise failure

        with mock.patch.object(PREPARE.os, "name", "nt"):
            with self.assertRaises(PermissionError) as caught:
                PREPARE.publish_stage(source, output, rename=exhausted, sleep=delays.append)
        self.assertIs(caught.exception, failure)
        self.assertEqual(len(calls), 6)
        self.assertEqual(delays, [0.05, 0.1, 0.2, 0.4, 0.5])
        self.assertLess(sum(delays), 2.0)
        self.assertTrue(source.exists())
        self.assertFalse(output.exists())

    def test_path_boundaries_are_checked_before_every_move(self):
        temp, source, output = self._paths()
        self.addCleanup(temp.cleanup)
        calls = []

        def move(src, dst):
            calls.append(1)

        outside_parent = output / "nested" / "final"
        with self.assertRaisesRegex(ValueError, "inside the output parent"):
            PREPARE.publish_stage(source, outside_parent, rename=move)
        self.assertEqual(calls, [])

        nested_output = source / "published"
        with self.assertRaisesRegex(ValueError, "inside the staging source"):
            PREPARE.publish_stage(source, nested_output, rename=move)
        self.assertEqual(calls, [])

        output.mkdir()
        with self.assertRaisesRegex(ValueError, "refusing to overwrite"):
            PREPARE.publish_stage(source, output, rename=move)
        self.assertEqual(calls, [])

    def test_all_three_prepare_layers_use_the_helper(self):
        base = (ROOT / "scripts/prepare-native-bridge.py").read_text(encoding="utf-8")
        legacy = (ROOT / "scripts/prepare-native-bridge-045.py").read_text(encoding="utf-8")
        multi = (ROOT / "scripts/prepare-multi-api-beta.py").read_text(encoding="utf-8")
        self.assertIn("publish_stage(stage, output)", base)
        self.assertIn("legacy.publish_stage(stage, output)", legacy)
        self.assertIn("m.legacy.publish_stage(stage, output)", multi)


if __name__ == "__main__":
    unittest.main()
