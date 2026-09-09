"""CPU evidence gates: numeric errors, signed zeros, receipts, and epochs."""
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
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


class EnhancementComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def plane(rgb=0, alpha=1):
        values = np.full((1, 2, 4), rgb, dtype="<f2")
        values[..., 3] = alpha
        return values

    def chain_fixture(self, role, rgb_values=(1, 1), source_offset=0):
        directory = self.root / role
        directory.mkdir()
        request = {"frames": 3, "samples": 2, "warmup": 1, "capture_first": 1, "capture_count": 2,
                   "fixed_dt": 1 / 60, "output_width": 2, "output_height": 1, "dlss_mode": 3,
                   "animations": False, "camera_input": False, "fg": False, "rr": False,
                   "require_core": role != "pure_sr"}
        complete = {"completed": True, "gpu_complete": True, "frames": 3, "sr_evaluations": 3,
                    "warmup": 1, "samples": 2, "captures": 2}
        epoch = {"epoch": 1, "next_epoch_frame": 0, "SR_reset_next_frame": True, "jitter_restart": True}
        for name, data in (("requested", request), ("completed", complete), ("epoch-start", epoch)):
            (directory / (name + ".json")).write_text(json.dumps(data))
        rows = []
        for index in range(3):
            row = {"host_frame": index, "epoch": 1, "epoch_frame": index, "epoch_frame_one_based": index + 1,
                   "source_frame": index + source_offset, "warmup": index == 0, "scene": "fixture",
                   "scene_time": index / 60, "camera": [0, 1, 0, 1, 1, 0], "fov": 60,
                   "render_width": 2, "render_height": 1, "output_width": 2, "output_height": 1,
                   "dlss_mode": 3, "jitter": [index / 4, 0], "reset": index == 0, "sr_sequence": index + 1,
                   "sr_result": 0, "gpu_complete": True, "capture": index > 0, "planes": []}
            if index > 0:
                frame_dir = directory / f"frame-{index:03d}"
                frame_dir.mkdir()
                for name, values in (("input_color", self.plane(.25)),
                                     ("sr_output", self.plane(rgb_values[index - 1]))):
                    values.tofile(frame_dir / (name + ".raw"))
                    row["planes"].append({"name": name, "width": 2, "height": 1, "bytes_per_pixel": 8,
                                          "format": "RGBA16_FLOAT_LE", "packed_bytes": 16})
            rows.append(row)
        (directory / "frames.jsonl").write_text("\n".join(json.dumps(row) for row in rows))
        return directory

    def triplet(self):
        return [self.chain_fixture(role, values, source_offset=index * 3) for index, (role, values) in
                enumerate((("pure_sr", (0, 0)), ("fp8", (1, 2)), ("candidate", (0, 2))))]

    @staticmethod
    def edit_history(path, edit):
        file = path / "frames.jsonl"
        rows = [json.loads(line) for line in file.read_text().splitlines()]
        edit(rows)
        file.write_text("\n".join(json.dumps(row) for row in rows))

    def test_identity_and_extinguished_effect_are_distinguished(self):
        s, f = self.plane(4), self.plane(5)
        for candidate, expected in ((f, (1, 0, 1)), (s, (0, 1, 0))):
            metrics = compare.enhancement_statistics(s, f, candidate)["metrics"]
            self.assertEqual(tuple(metrics[key] for key in
                                   ("effect_gain", "residual_nrmse", "residual_rms_ratio")), expected)
            self.assertEqual(metrics["reference_residual_rms"], 1)

    def test_projection_detects_orthogonal_change_and_alpha_is_excluded(self):
        s, f, c = self.plane(0, np.nan), self.plane(0, np.inf), self.plane(0, -np.inf)
        f[..., 0] = 1
        c[..., :2] = .5
        metrics = compare.enhancement_statistics(s, f, c)["metrics"]
        self.assertAlmostEqual(metrics["effect_gain"], .5)
        self.assertAlmostEqual(metrics["residual_nrmse"], math.sqrt(.5))
        self.assertAlmostEqual(metrics["residual_rms_ratio"], math.sqrt(.5))
        self.assertAlmostEqual(metrics["reference_residual_rms"], math.sqrt(1 / 3))
        json.dumps(metrics, allow_nan=False)

    def test_zero_reference_energy_is_undefined_without_epsilon(self):
        result = compare.enhancement_statistics(self.plane(), self.plane(), self.plane(1))
        self.assertEqual(result["metrics"]["normalization_status"], "undefined_zero_reference_residual")
        for name in ("effect_gain", "residual_nrmse", "residual_rms_ratio"):
            self.assertIsNone(result["metrics"][name])
        self.assertEqual(result["metrics"]["error_rms"], 1)
        json.dumps(result["metrics"], allow_nan=False)

    def test_nonfinite_rgb_and_shape_mismatch_are_refused(self):
        for bad in (np.nan, np.inf, -np.inf):
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, "NaN or infinity"):
                compare.enhancement_statistics(self.plane(), self.plane(1), self.plane(bad))
        with self.assertRaisesRegex(ValueError, "shape mismatch"):
            compare.enhancement_statistics(self.plane(), self.plane(1), np.zeros((2, 1, 4)))

    def test_temporal_error_change_uses_signed_errors(self):
        s, f = self.plane(), self.plane(1)
        # Opposite signed errors have equal error magnitude, but their change is two.
        result = compare.enhancement_statistics(s, f, self.plane(2), previous=(s, f, s))
        self.assertEqual(result["temporal"]["error_change_rms"], 2)
        self.assertEqual(result["temporal"]["reference_residual_change_rms"], 0)
        self.assertEqual(result["temporal"]["candidate_residual_change_rms"], 2)

    def test_full_capture_report_pools_energy_and_ignores_preparation_tokens(self):
        report = compare.build_enhancement_report(self.triplet())
        self.assertTrue(report["checks_passed"])
        self.assertFalse(report["quality_qualified"])
        self.assertFalse(report["pipeline_roles_verified"])
        self.assertAlmostEqual(report["pooled_metrics"]["effect_gain"], .8)
        self.assertAlmostEqual(report["pooled_metrics"]["residual_nrmse"], math.sqrt(.2))
        self.assertEqual(report["comparisons"][0]["temporal"]["status"], "undefined_no_previous_frame")
        self.assertEqual(report["comparisons"][1]["temporal"]["error_change_rms"], 1)
        json.dumps(report, allow_nan=False)

    def test_warmup_history_mismatch_refuses_identical_capture_inputs(self):
        paths = self.triplet()
        self.edit_history(paths[2], lambda rows: rows[0].update(jitter=[.25, .5]))
        with self.assertRaisesRegex(ValueError, "history mismatch"):
            compare.build_enhancement_report(paths)

    def test_input_identity_and_raw_byte_count_are_enforced(self):
        paths = self.triplet()
        self.plane(.5).tofile(paths[2] / "frame-001/input_color.raw")
        with self.assertRaisesRegex(ValueError, "raw identity mismatch"):
            compare.build_enhancement_report(paths)
        self.plane(.25).tofile(paths[2] / "frame-001/input_color.raw")
        (paths[2] / "frame-001/sr_output.raw").write_bytes(b"bad")
        with self.assertRaisesRegex(ValueError, "shape/format/byte count mismatch"):
            compare.build_enhancement_report(paths)

    def test_passthrough_cannot_be_declared_pure_sr(self):
        paths = self.triplet()
        request_file = paths[0] / "requested.json"
        request = json.loads(request_file.read_text())
        request["require_core"] = True
        request_file.write_text(json.dumps(request))
        with self.assertRaisesRegex(ValueError, "passthrough is not pure SR"):
            compare.build_enhancement_report(paths)

    def test_missing_epoch_or_incomplete_history_is_refused(self):
        paths = self.triplet()
        self.edit_history(paths[1], lambda rows: rows.pop(0))
        with self.assertRaisesRegex(ValueError, "incomplete frame history"):
            compare.build_enhancement_report(paths)
        (paths[0] / "epoch-start.json").write_text("{}")
        with self.assertRaisesRegex(ValueError, "history boundary"):
            compare.build_enhancement_report(paths)

    def test_shared_reset_has_no_cross_reset_temporal_measurement(self):
        paths = self.triplet()
        for path in paths:
            self.edit_history(path, lambda rows: rows[2].update(reset=True))
        report = compare.build_enhancement_report(paths)
        temporal = report["comparisons"][1]["temporal"]
        self.assertEqual(temporal["status"], "undefined_reset_or_nonconsecutive_frames")
        self.assertIsNone(temporal["error_change_rms"])

    def test_enhancement_cli_writes_strict_json_and_failure_is_explicit(self):
        paths = self.triplet()
        output = self.root / "report.json"
        command = [sys.executable, str(Path(compare.__file__)), "--enhancement", *map(str, paths),
                   "--output", str(output)]
        success = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(success.returncode, 0, success.stderr)
        report = json.loads(output.read_text())
        self.assertEqual(len(report["comparisons"]), 2)
        self.assertFalse(report["cuda_initialized"])
        self.assertEqual(report["gpu_launches"], 0)
        self.edit_history(paths[1], lambda rows: rows[0].update(scene_time=float("nan")))
        failed = self.root / "failed.json"
        command[-1] = str(failed)
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        report = json.loads(failed.read_text())
        self.assertFalse(report["analysis_completed"])
        self.assertIn("ValueError", report["error"])

    def add_motion_guides(self, path):
        request_file = path / "requested.json"
        request = json.loads(request_file.read_text())
        request.update(motion=True, motion_period_frames=360, motion_lateral_extent_m=.4)
        request_file.write_text(json.dumps(request))
        def edit(rows):
            for row in rows:
                index = row["epoch_frame"]
                row.update(motion=True, motion_frame=max(index - 1, 0), motion_phase=max(index - 1, 0),
                           guide_capture_boundary="post-SR original renderer depth/MV resources; after timing end",
                           scene_time=float(np.float32(max(index - 1, 0) / 60)),
                           camera=[0, float(np.float32(1.8)), index / 180, 1, float(np.float32(1.8)), index / 180])
                if row["capture"]:
                    for name, values, format_name in (
                        ("depth", np.array([[[1], [0xffffff]]], dtype="<u4"), "D24_UNORM_LE_X8_ZERO"),
                        ("motion_vectors", np.array([[[1, -2], [-.5, .25]]], dtype="<f2"), "RG16_FLOAT_LE")):
                        values.tofile(path / f"frame-{index:03d}" / (name + ".raw"))
                        plane = {"name": name, "width": 2, "height": 1, "bytes_per_pixel": 4,
                                 "format": format_name, "packed_bytes": 8}
                        if name == "depth":
                            plane.update(depth_bits=24, stencil_captured=False)
                        row["planes"].append(plane)
        self.edit_history(path, edit)

    def test_motion_actual_float_metadata_and_shared_guides_are_compared(self):
        paths = self.triplet()
        for path in paths:
            self.add_motion_guides(path)
        report = compare.build_enhancement_report(paths)
        self.assertTrue(report["checks_passed"])
        self.assertEqual(report["comparisons"][1]["temporal"]["status"], "measured_unwarped")
        for name in compare.CHAIN_GUIDE_PLANES:
            self.assertTrue(report["guide_coverage"][name]["all_frames_compared"])
            receipts = report["comparisons"][0]["guides"][name]["receipts"]
            self.assertEqual(len(receipts), 3)
            self.assertEqual(len({item["sha256"] for item in receipts}), 1)
        self.edit_history(paths[2], lambda rows: rows[0].update(motion_phase=42))
        with self.assertRaisesRegex(ValueError, "history mismatch"):
            compare.build_enhancement_report(paths)

    def test_motion_request_mismatch_is_refused(self):
        paths = self.triplet()
        for path in paths:
            self.add_motion_guides(path)
        request_file = paths[1] / "requested.json"
        request = json.loads(request_file.read_text())
        request["motion_lateral_extent_m"] = .8
        request_file.write_text(json.dumps(request))
        with self.assertRaisesRegex(ValueError, "conditions mismatch"):
            compare.build_enhancement_report(paths)

    def test_partial_guide_coverage_is_explicit_and_never_claimed_equal(self):
        paths = self.triplet()
        for path in paths:
            self.add_motion_guides(path)
        self.edit_history(paths[2], lambda rows: rows[1].update(
            planes=[plane for plane in rows[1]["planes"] if plane["name"] != "depth"]))
        report = compare.build_enhancement_report(paths)
        guide = report["comparisons"][0]["guides"]["depth"]
        self.assertEqual(guide["status"], "not_compared_missing_roles")
        self.assertEqual(guide["roles_present"], ["pure_sr", "fp8"])
        self.assertFalse(report["guide_coverage"]["depth"]["all_frames_compared"])
        self.assertEqual(report["guide_coverage"]["depth"]["compared_frames"], 1)

    def test_guide_raw_mismatches_and_invalid_storage_are_refused(self):
        paths = self.triplet()
        for path in paths:
            self.add_motion_guides(path)
        cases = (("depth", np.array([[[2], [0xffffff]]], dtype="<u4"), "depth raw identity mismatch"),
                 ("motion_vectors", np.ones((1, 2, 2), dtype="<f2"), "motion_vectors raw identity mismatch"),
                 ("depth", np.full((1, 2, 1), 0xff000001, dtype="<u4"), "zero unused byte"),
                 ("motion_vectors", np.full((1, 2, 2), np.nan, dtype="<f2"), "NaN or infinity"))
        for name, values, message in cases:
            raw = paths[2] / "frame-001" / (name + ".raw")
            original = raw.read_bytes()
            with self.subTest(name=name, message=message):
                try:
                    values.tofile(raw)
                    with self.assertRaisesRegex(ValueError, message):
                        compare.build_enhancement_report(paths)
                finally:
                    raw.write_bytes(original)

    @staticmethod
    def set_preparation_receipts(path, target, actual):
        for name in ("requested.json", "epoch-start.json", "completed.json"):
            file = path / name
            receipt = json.loads(file.read_text())
            if target is not None:
                receipt["fixed_preparation_frames"] = target
            if name != "requested.json":
                receipt["preparation_frames"] = actual
            file.write_text(json.dumps(receipt))

    def test_preparation_counts_are_reported_without_internal_state_equivalence(self):
        paths = self.triplet()
        for target in (None, 0, 64):
            for path, actual in zip(paths, (1, 3, 6) if target != 64 else (64, 64, 64)):
                self.set_preparation_receipts(path, target, actual)
            report = compare.build_enhancement_report(paths)
            self.assertTrue(report["checks_passed"])
            self.assertFalse(report["internal_state_equivalence_verified"])
            self.assertEqual([run["preparation"]["epoch_preparation_frames"] for run in report["runs"]],
                             [64, 64, 64] if target == 64 else [1, 3, 6])
            self.assertTrue(all(run["preparation"]["fixed_count_verified"] is (target == 64) for run in report["runs"]))

    def test_fixed_preparation_target_mismatch_is_refused(self):
        paths = self.triplet()
        for path in paths:
            self.set_preparation_receipts(path, 64, 64)
        file = paths[2] / "epoch-start.json"
        receipt = json.loads(file.read_text())
        receipt["fixed_preparation_frames"] = 63
        file.write_text(json.dumps(receipt))
        with self.assertRaisesRegex(ValueError, "fixed_preparation_frames mismatch"):
            compare.build_enhancement_report(paths)
        self.set_preparation_receipts(paths[2], 65, 65)
        with self.assertRaisesRegex(ValueError, "conditions mismatch"):
            compare.build_enhancement_report(paths)

    def test_fixed_preparation_actual_65_is_refused(self):
        paths = self.triplet()
        for path in paths:
            self.set_preparation_receipts(path, 64, 64)
        for name in ("epoch-start.json", "completed.json"):
            with self.subTest(receipt=name):
                self.set_preparation_receipts(paths[2], 64, 64)
                file = paths[2] / name
                receipt = json.loads(file.read_text())
                receipt["preparation_frames"] = 65
                file.write_text(json.dumps(receipt))
                with self.assertRaisesRegex(ValueError, "actual preparation_frames"):
                    compare.build_enhancement_report(paths)

    def test_fixed_preparation_invalid_or_missing_target_is_refused(self):
        paths = self.triplet()
        for path in paths:
            self.set_preparation_receipts(path, 64, 64)
        file = paths[2] / "completed.json"
        for value in (-1, 64.0, True, None):
            with self.subTest(value=value):
                receipt = json.loads(file.read_text())
                if value is None:
                    receipt.pop("fixed_preparation_frames", None)
                else:
                    receipt["fixed_preparation_frames"] = value
                file.write_text(json.dumps(receipt))
                with self.assertRaisesRegex(ValueError, "invalid fixed_preparation_frames"):
                    compare.build_enhancement_report(paths)


if __name__ == "__main__":
    unittest.main()
