from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tests" / "motion_slot_selection_cpu.cpp"
HEADER = ROOT / "src" / "nr_motion_slot_selection.h"


class MotionSlotSelectionCpu(unittest.TestCase):
    def _compile(self, cxx: str, include_root: Path, output: Path) -> None:
        command = [
            cxx,
            "-std=c++20",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(include_root),
            str(SOURCE),
            "-o",
            str(output),
        ]
        result = subprocess.run(command, cwd=ROOT, text=True,
                                capture_output=True)
        if result.returncode != 0:
            self.fail("compile failed:\n" + result.stdout + result.stderr)

    def test_production_header_positive_and_old_strategy_negative(self):
        cxx = os.environ.get("CXX")
        if not cxx:
            self.fail("CXX must point to the configured host C++ compiler")

        with tempfile.TemporaryDirectory(prefix="motion-slot-selection-") as raw:
            temp = Path(raw)
            positive = temp / "motion-slot-selection-positive.exe"
            self._compile(cxx, ROOT / "src", positive)
            passed = subprocess.run([str(positive)], cwd=ROOT, text=True,
                                    capture_output=True)
            print("positive stdout:", passed.stdout.strip())
            print("positive stderr:", passed.stderr.strip())
            self.assertEqual(passed.returncode, 0,
                             passed.stdout + passed.stderr)
            self.assertIn("MOTION_SLOT_SELECTION_PASS: 5 scenarios passed",
                          passed.stdout)

            production_header = HEADER.read_text(encoding="utf-8")
            complete = """    for (std::size_t i = 0; i < count; ++i) {
        if (same_external(slots[i]) && complete_binding(slots[i]))
            return &slots[i];
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (same_external(slots[i]) && can_write(slots[i]))
            return &slots[i];
    }
"""
            old = """    (void)complete_binding;
    (void)can_write;
    for (std::size_t i = 0; i < count; ++i) {
        if (same_external(slots[i])) return &slots[i];
    }
"""
            self.assertEqual(production_header.count(complete), 1)
            old_header = production_header.replace(complete, old, 1)
            old_include = temp / "old"
            old_include.mkdir()
            (old_include / HEADER.name).write_text(old_header, encoding="utf-8")
            negative = temp / "motion-slot-selection-old.exe"
            self._compile(cxx, old_include, negative)
            rejected = subprocess.run([str(negative)], cwd=ROOT, text=True,
                                      capture_output=True)
            print("negative stdout:", rejected.stdout.strip())
            print("negative stderr:", rejected.stderr.strip())
            self.assertNotEqual(rejected.returncode, 0,
                                "old first-external strategy unexpectedly passed")
            self.assertIn("OLD_STRATEGY_ORACLE", rejected.stdout + rejected.stderr)


if __name__ == "__main__":
    unittest.main()
