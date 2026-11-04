"""Run the real F8 driver's filesystem/launch blocks without a Windows GPU.

AST extraction executes the production Python statements unchanged. The child
compiler and renderer are explicit seams; no C++, WARP, NGX, pixel-quality or
full archive-acceptance success is claimed by these tests. Real filesystem IO
covers repeat/parallel invocations, stale outputs and negative-control isolation.
"""
from contextlib import redirect_stdout
from concurrent.futures import ThreadPoolExecutor
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
import ast
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = Path(os.environ.get(
    "NR_F8_DRIVER_UNDER_TEST", ROOT / "scripts/validate-f8-controller.py"))
SOURCE = DRIVER.read_text(encoding="utf-8-sig")
TREE = ast.parse(SOURCE, filename=str(DRIVER))


def assigns(node, name):
    return isinstance(node, ast.Assign) and any(
        isinstance(child, ast.Name) and child.id == name
        for target in node.targets for child in ast.walk(target))


def execute(nodes, namespace):
    module = ast.Module(body=nodes, type_ignores=[])
    exec(compile(module, str(DRIVER), "exec"), namespace)


START = next(i for i, node in enumerate(TREE.body)
             if isinstance(node, ast.Assign) and any(
                 isinstance(child, ast.Constant)
                 and child.value == "test-output/f8-gpu-v4"
                 for child in ast.walk(node)))
END = next(i for i, node in enumerate(TREE.body)
           if i > START and assigns(node, "cpp") and assigns(node, "exe"))
OUTPUT_BLOCK = TREE.body[START:END + 1]
SELECT = next(node for node in TREE.body if assigns(node, "latest"))
NEGATIVE = next(node for node in TREE.body if isinstance(node, ast.If)
                and ast.unparse(node.test) == "args.negative_control")
MUTATIONS = next(node for node in NEGATIVE.body if assigns(node, "mutations"))
LOOP = next(node for node in NEGATIVE.body if isinstance(node, ast.For))


class F8AttemptIsolation(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="nr-f8-attempt-test-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def namespace(self, gpu=False):
        namespace = {"Path": Path, "tempfile": tempfile,
                     "root": self.root, "args": SimpleNamespace(
                         gpu=gpu, compiler="controlled-test-compiler")}
        execute([node for node in TREE.body if isinstance(node, ast.FunctionDef)],
                namespace)
        return namespace

    def begin(self, gpu=False):
        namespace = self.namespace(gpu)
        with redirect_stdout(StringIO()):
            execute(OUTPUT_BLOCK, namespace)
        return namespace

    def select(self, namespace):
        execute([SELECT], namespace)
        return namespace["latest"]

    def run_negatives(self, namespace, *, compile_error=False, exit_code=1):
        calls = []
        namespace.update(native_png=b"exact synthetic PNG fixture bytes", results=[])
        execute([MUTATIONS], namespace)
        namespace["harness"] = "\n".join(
            old for old, new in namespace["mutations"].values())
        (namespace["out"] / "native-display.png").write_bytes(namespace["native_png"])

        def child(command, **kwargs):
            calls.append((command, kwargs))
            if command[0] == namespace["args"].compiler:
                if compile_error:
                    raise subprocess.CalledProcessError(2, command)
                return subprocess.CompletedProcess(command, 0)
            return subprocess.CompletedProcess(command, exit_code, "", "FAIL: controlled assertion\n")

        # This suite verifies driver orchestration. Real process/log behavior is
        # independently exercised by test_f8_process_evidence.py.
        namespace["subprocess"] = SimpleNamespace(run=child)
        namespace["run_logged"] = lambda command, directory, label, inputs=None, **kw: child(command, **kw)
        execute([LOOP], namespace)
        return [(cmd, kw) for cmd, kw in calls
                if cmd[0] != namespace["args"].compiler]

    def test_repeated_cpu_invocations_are_disjoint(self):
        first, second = self.begin(), self.begin()
        self.assertNotEqual(first["out"], second["out"])
        self.assertNotEqual(first["cpp"], second["cpp"])
        self.assertNotEqual(first["exe"], second["exe"])

    def test_repeated_gpu_invocations_are_disjoint(self):
        first, second = self.begin(True), self.begin(True)
        self.assertNotEqual(first["out"], second["out"])
        self.assertNotEqual(first["cpp"], second["cpp"])
        self.assertNotEqual(first["exe"], second["exe"])

    def test_existing_artifact_roots_are_preserved(self):
        for gpu, leaf in ((False, "f8-controller-v4"), (True, "f8-gpu-v4")):
            with self.subTest(gpu=gpu):
                ns = self.begin(gpu)
                self.assertEqual(ns["out"].parent, self.root / "test-output" / leaf)

    def test_cpu_gpu_namespaces_are_separate(self):
        self.assertNotEqual(self.begin()["out"], self.begin(True)["out"])

    def test_parallel_setup_is_atomic(self):
        def start(_):
            ns = self.namespace()
            # Avoid redirecting the process-global stdout from worker threads.
            ns["print"] = lambda *args, **kwargs: None
            execute(OUTPUT_BLOCK, ns)
            return ns["out"]
        with ThreadPoolExecutor(max_workers=8) as pool:
            outputs = list(pool.map(start, range(32)))
        self.assertEqual(len(set(outputs)), 32)
        self.assertTrue(all(p.is_dir() for p in outputs))

    def test_setup_preserves_old_failure_evidence(self):
        first = self.begin()
        path = first["out"] / "old-failure.log"
        path.write_bytes(b"keep original failure")
        second = self.begin()
        self.assertEqual(path.read_bytes(), b"keep original failure")
        self.assertNotEqual(first["out"], second["out"])

    def test_each_attempt_starts_without_result_children(self):
        first = self.begin()
        (first["out"] / "run-123").mkdir()
        second = self.begin()
        self.assertEqual(list(second["out"].glob("run-*")), [])

    def test_single_current_directory_is_accepted(self):
        ns = self.begin()
        run = ns["out"] / "run-123"
        run.mkdir()
        self.assertEqual(self.select(ns), run)

    def test_missing_current_directory_is_explicit_failure(self):
        with self.assertRaises(RuntimeError):
            self.select(self.begin())

    def test_future_dated_old_run_cannot_win_selection(self):
        first = self.begin()
        old = first["out"] / "run-old"
        old.mkdir()
        os.utime(old, (4070908800, 4070908800))
        second = self.begin()
        new = second["out"] / "run-current"
        new.mkdir()
        self.assertEqual(self.select(second), new)
        self.assertTrue(old.is_dir())

    def test_old_run_is_not_a_fallback_for_missing_current_result(self):
        first = self.begin()
        (first["out"] / "run-old").mkdir()
        with self.assertRaises(RuntimeError):
            self.select(self.begin())

    def test_multiple_result_directories_are_rejected(self):
        ns = self.begin()
        (ns["out"] / "run-one").mkdir()
        (ns["out"] / "run-two").mkdir()
        with self.assertRaises(RuntimeError):
            self.select(ns)

    def test_run_named_regular_file_is_rejected(self):
        ns = self.begin()
        (ns["out"] / "run-file").write_bytes(b"not a directory")
        with self.assertRaises(RuntimeError):
            self.select(ns)

    def make_link(self, path, target):
        try:
            path.symlink_to(target, target_is_directory=True)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlink privilege unavailable: {exc}")

    def test_link_to_old_result_is_rejected(self):
        ns = self.begin()
        old = self.root / "old-result"
        old.mkdir()
        self.make_link(ns["out"] / "run-link", old)
        with self.assertRaises(RuntimeError):
            self.select(ns)

    def test_broken_result_link_is_rejected(self):
        ns = self.begin()
        self.make_link(ns["out"] / "run-link", self.root / "missing")
        with self.assertRaises(RuntimeError):
            self.select(ns)

    def test_all_nine_negative_controls_get_separate_output(self):
        ns = self.begin()
        calls = self.run_negatives(ns)
        paths = [Path(cmd[1]) for cmd, _ in calls]
        self.assertEqual(len(paths), 9)
        self.assertEqual(len(set(paths)), 9)
        self.assertTrue(all(p != ns["out"] and p.parent == ns["out"] for p in paths))
        self.assertTrue(all(p.is_dir() for p in paths))

    def test_negative_controls_receive_identical_screenshot_bytes(self):
        ns = self.begin()
        calls = self.run_negatives(ns)
        for cmd, kwargs in calls:
            with self.subTest(command=cmd):
                self.assertEqual((Path(cmd[1]) / "native-display.png").read_bytes(),
                                 ns["native_png"])
                self.assertTrue(kwargs["capture_output"])
                self.assertTrue(kwargs["text"])

    def test_negative_pid_reuse_cannot_hit_positive_result(self):
        ns = self.begin()
        positive = ns["out"] / "run-123"
        positive.mkdir()
        marker = positive / "receipt.txt"
        marker.write_text("positive evidence")
        for cmd, _ in self.run_negatives(ns):
            # All simulated children deliberately have the same PID.
            (Path(cmd[1]) / "run-123").mkdir()
        self.assertEqual(marker.read_text(), "positive evidence")
        self.assertEqual(self.select(ns), positive)

    def test_negative_receipt_names_its_private_directory(self):
        ns = self.begin()
        calls = self.run_negatives(ns)
        for receipt, (cmd, _) in zip(ns["results"], calls):
            self.assertEqual(receipt["output_directory"], Path(cmd[1]).name)
            self.assertEqual(receipt["exit_code"], 1)
            self.assertEqual(receipt["assertion"], "FAIL: controlled assertion")

    def test_negative_compile_error_cannot_count_as_rejection(self):
        ns = self.begin()
        with self.assertRaises(subprocess.CalledProcessError):
            self.run_negatives(ns, compile_error=True)
        self.assertEqual(ns["results"], [])

    def test_negative_unexpected_success_cannot_count_as_rejection(self):
        ns = self.begin()
        with self.assertRaises(AssertionError):
            self.run_negatives(ns, exit_code=0)
        self.assertEqual(ns["results"], [])

    def test_negative_crash_cannot_count_as_expected_rejection(self):
        ns = self.begin()
        with self.assertRaises(AssertionError):
            self.run_negatives(ns, exit_code=-11)
        self.assertEqual(ns["results"], [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
