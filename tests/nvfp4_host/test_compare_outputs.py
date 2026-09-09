"""CPU evidence gates: numeric errors, signed zeros, receipts, and epochs."""
import importlib.util
import json
import math
from pathlib import Path
import tempfile
import unittest

import numpy as np

spec = importlib.util.spec_from_file_location("compare_outputs", Path(__file__).with_name("compare_outputs.py"))
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class OutputComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.counter = 0

    def tearDown(self):
        self.temp.cleanup()

    def run_fixture(self, values=None, mode="optimized", warmup=1, block=0, epoch=False,
                    preparation=0, expected_hash=None, final_frame_adjust=0):
        values = np.zeros((1, 2, 4), dtype="<f2") if values is None else np.asarray(values, dtype="<f2")
        directory = self.root / f"run-{self.counter:02d}"
        self.counter += 1
        directory.mkdir()
        host = {"width": values.shape[1], "height": values.shape[0], "frames": 2,
                "warmup": warmup, "mode": mode, "input": "synthetic-static-fp16", "game": False, "sr": False}
        if epoch:
            host.update(history_epoch="reset-after-candidate-ready-v1", preparation_frames=preparation)
        (directory / "host.json").write_text(json.dumps(host))
        values.tofile(directory / "nr_output.f16")
        rows = []
        for index in range(preparation):
            rows.append({"frame": 0, "submission": index, "phase": "preparing", "warmup": True,
                         "gpu_complete": True, "replaced_pairs": 0})
        for index in range(warmup + 2):
            row = {"frame": index, "warmup": index < warmup, "gpu_complete": True,
                   "replaced_pairs": 8 if mode in ("fused", "optimized") else 0}
            if epoch:
                row.update(submission=preparation + index, phase="warmup" if index < warmup else "measured")
            rows.append(row)
        rows[-1]["frame"] += final_frame_adjust
        (directory / "timing.jsonl").write_text("\n".join(json.dumps(row) for row in rows))
        meta = {"width": host["width"], "height": host["height"], "frames": 2, "warmup": warmup,
                "completed": True, "inputs": {"fixture/host.exe": "host-v1", "fixture/nr_host.cpp": "input-v1"}}
        entry = {"block": block, "mode": mode, "directory": directory.name,
                 "output_sha256": expected_hash or compare.sha256(directory / "nr_output.f16")}
        return compare.Run(directory / "host.json", self.root / "experiment.json", meta, entry)

    def test_exact_fused_contract_has_zero_error_and_infinite_psnr(self):
        a, b = self.run_fixture(), self.run_fixture(mode="fused")
        result = compare.compare_pair(a, b)
        self.assertTrue(result["exact_contract_met"])
        self.assertEqual(result["different_bytes"], 0)
        self.assertEqual(result["metrics"]["A"]["rmse"], 0)
        self.assertIsNone(result["rgb"]["psnr_db"])
        self.assertTrue(result["rgb"]["psnr_infinite"])

    def test_known_rgb_metrics_peak_and_alpha_exclusion(self):
        a = self.run_fixture(mode="fp8")
        values = np.zeros((1, 2, 4), np.float16)
        values[0, 0, 0], values[..., 3] = 1, 10
        b = self.run_fixture(values, "fused")
        result = compare.compare_pair(a, b, peak=2)
        self.assertEqual(result["status"], "measurement_only")
        self.assertFalse(result["quality_qualified"])
        self.assertAlmostEqual(result["metrics"]["R"]["rmse"], math.sqrt(.5))
        self.assertEqual(result["metrics"]["R"]["mae"], .5)
        self.assertEqual(result["metrics"]["R"]["max_abs_error"], 1)
        self.assertAlmostEqual(result["metrics"]["R"]["p99_abs_error"], .99)
        self.assertAlmostEqual(result["rgb"]["mse"], 1 / 6)
        self.assertAlmostEqual(result["rgb"]["psnr_db"], 10 * math.log10(24))

    def test_signed_zero_is_numerically_equal_but_fails_byte_contract(self):
        a = self.run_fixture()
        values = np.zeros((1, 2, 4), np.float16)
        values[0, 0, 0] = -0.0
        b = self.run_fixture(values, "fused")
        result = compare.compare_pair(a, b)
        self.assertEqual(result["numeric_different_elements"], 0)
        self.assertEqual(result["different_fp16_encodings"], 1)
        self.assertEqual(result["different_bytes"], 1)
        self.assertFalse(result["exact_contract_met"])

    def test_warmup_difference_makes_exact_contract_inconclusive(self):
        a = self.run_fixture(warmup=1)
        b = self.run_fixture(mode="fused", warmup=2)
        result = compare.compare_pair(a, b)
        self.assertTrue(result["byte_exact"])
        self.assertEqual(result["conditions"]["status"], "different")
        self.assertIn("warmup", result["conditions"]["differences"])
        self.assertIsNone(result["exact_contract_met"])

    def test_nonfinite_is_invalid_without_silent_finite_only_metrics(self):
        a = self.run_fixture()
        values = np.zeros((1, 2, 4), np.float16)
        values[0, 0, 0] = np.inf
        b = self.run_fixture(values, "fused")
        result = compare.compare_pair(a, b)
        self.assertFalse(result["all_finite"])
        self.assertEqual(result["metrics"]["R"]["nonfinite_pairs"], 1)
        self.assertIsNone(result["metrics"]["R"]["rmse"])
        self.assertIsNone(result["rgb"]["psnr_db"])
        json.dumps(result, allow_nan=False)

    def test_shape_mismatch_is_explicit(self):
        a = self.run_fixture()
        b = self.run_fixture(np.zeros((2, 1, 4), np.float16), "fused")
        result = compare.compare_pair(a, b)
        self.assertEqual(result["status"], "incompatible_dimensions")
        self.assertIsNone(result["metrics"])

    def test_receipt_hash_and_timing_failures_remain_invalid(self):
        a = self.run_fixture(expected_hash="wrong")
        b = self.run_fixture(mode="fused", final_frame_adjust=1)
        report, _ = compare.build_report([a, b])
        self.assertFalse(report["all_data_valid"])
        self.assertFalse(report["checks_passed"])
        self.assertIn("SHA256", a.issues[0])
        self.assertFalse(b.timing["valid"])

    def test_same_mode_hash_drift_is_measured_and_fails_stability(self):
        a = self.run_fixture(block=0)
        values = np.zeros((1, 2, 4), np.float16)
        values[0, 0, 2] = .125
        b = self.run_fixture(values, block=1)
        report, _ = compare.build_report([a, b], peak=4)
        group = report["same_mode_stability"][0]["groups"][0]
        self.assertEqual(group["status"], "different_outputs")
        self.assertEqual(group["distinct_hash_comparisons_vs_first"][0]["numeric_different_elements"], 1)
        self.assertEqual(group["distinct_hash_comparisons_vs_first"][0]["rgb"]["psnr_peak"], 4)
        self.assertFalse(report["same_mode_stability_passed"])
        self.assertFalse(report["checks_passed"])

    def test_preparation_length_does_not_split_unified_epoch(self):
        a = self.run_fixture(epoch=True, preparation=1)
        b = self.run_fixture(mode="fused", epoch=True, preparation=5)
        result = compare.compare_pair(a, b)
        self.assertTrue(a.timing["valid"] and b.timing["valid"])
        self.assertEqual(a.timing["epoch_rows"], 3)
        self.assertEqual(b.timing["preparation_rows"], 5)
        self.assertEqual(result["conditions"]["status"], "matched")
        self.assertTrue(result["exact_contract_met"])

    def test_legacy_and_readiness_epochs_are_distinguished(self):
        a = self.run_fixture()
        b = self.run_fixture(mode="fused", epoch=True, preparation=1)
        result = compare.compare_pair(a, b)
        self.assertIn("history_epoch", result["conditions"]["differences"])
        self.assertIsNone(result["exact_contract_met"])

    def test_discovery_deduplicates_experiment_and_direct_host_inputs(self):
        a = self.run_fixture()
        b = self.run_fixture(mode="fused")
        meta = dict(a.experiment, runs=[a.entry, b.entry])
        path = self.root / "experiment.json"
        path.write_text(json.dumps(meta))
        runs = compare.discover([self.root, a.host_path, b.directory])
        self.assertEqual(len(runs), 2)
        report, _ = compare.build_report(runs)
        self.assertTrue(report["checks_passed"])
        self.assertTrue(report["all_required_exact_contracts_passed"])

    def test_three_mode_auto_keeps_measurement_and_exact_contract_separate(self):
        runs = [self.run_fixture(mode=mode) for mode in ("passthrough", "optimized", "fused")]
        report, _ = compare.build_report(runs)
        self.assertEqual(report["baseline_mode"], "passthrough")
        self.assertEqual(len(report["comparisons"]), 3)
        self.assertEqual(sum(row["purpose"] == "byte_exact_contract" for row in report["comparisons"]), 1)
        self.assertEqual(sum(row["purpose"] == "difference_measurement" for row in report["comparisons"]), 2)
        self.assertTrue(report["all_required_exact_contracts_passed"])
        self.assertFalse(report["quality_qualified"])

    def test_input_identity_difference_is_explicit(self):
        a = self.run_fixture()
        b = self.run_fixture(mode="fused")
        b.conditions["input"] = "different-input-sequence"
        result = compare.compare_pair(a, b)
        self.assertIn("input", result["conditions"]["differences"])
        self.assertIsNone(result["exact_contract_met"])


if __name__ == "__main__":
    unittest.main()
