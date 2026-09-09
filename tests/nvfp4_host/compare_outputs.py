#!/usr/bin/env python3
"""Compare owned Feature18 FP16 readbacks without GPU work or raw-file edits.

Inputs are experiment directories/experiment.json files or host directories/
host.json files. The host writes tightly packed little-endian RGBA FP16.
Fused versus optimized has a byte-exact contract. Comparisons with original
FP8 measure numerical differences only; they never qualify image quality.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re

import numpy as np

CHANNELS = ("R", "G", "B", "A")
CONDITION_FIELDS = ("width", "height", "frames", "warmup", "input", "game", "sr")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def basename(path):
    return str(path).replace("\\", "/").rsplit("/", 1)[-1]


def normalized_receipts(meta):
    # Directory relocation does not change a recorded input's identity.
    return sorted(({"name": basename(name), "sha256": value}
                   for name, value in meta.get("inputs", {}).items()),
                  key=lambda item: (item["name"], item["sha256"]))


def timing_summary(path, host):
    if not path.is_file():
        return {"available": False, "valid": None, "issues": ["timing.jsonl missing"]}
    all_rows = [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    preparing = [row for row in all_rows if row.get("phase") == "preparing"]
    rows = [row for row in all_rows if row.get("phase") != "preparing"]
    expected = host["frames"] + host["warmup"]
    issues = []
    if "history_epoch" in host:
        if any(row.get("phase") not in ("preparing", "warmup", "measured") for row in all_rows):
            issues.append("epoch-aware timing contains an unknown phase")
        if len(preparing) != host.get("preparation_frames"):
            issues.append("preparation timing count disagrees with host metadata")
        if [row.get("submission") for row in all_rows] != list(range(len(all_rows))):
            issues.append("submission indices are incomplete")
        if any(row.get("phase") != ("warmup" if index < host["warmup"] else "measured")
               for index, row in enumerate(rows)):
            issues.append("timing phase disagrees with the fixed measurement epoch")
    if len(rows) != expected:
        issues.append(f"timing rows {len(rows)} != frames + warmup {expected}")
    if [row.get("frame") for row in rows] != list(range(expected)):
        issues.append("timing frame indices are not the complete expected sequence")
    if any(row.get("gpu_complete") is not True for row in all_rows):
        issues.append("one or more timing rows lack GPU completion")
    if any(row.get("warmup") is not (index < host["warmup"]) for index, row in enumerate(rows)):
        issues.append("timing warmup flags disagree with host metadata")
    return {"available": True, "valid": not issues, "issues": issues,
            "rows": len(all_rows), "epoch_rows": len(rows), "preparation_rows": len(preparing),
            "warmup_rows": sum(row.get("warmup") is True for row in rows),
            "measured_rows": sum(row.get("warmup") is False for row in rows),
            "first_frame_index": rows[0].get("frame") if rows else None,
            "last_frame_index": rows[-1].get("frame") if rows else None,
            "first_replacement_frame": next((row.get("frame") for row in rows if row.get("replaced_pairs", 0) > 0), None),
            "first_eight_pair_frame": next((row.get("frame") for row in rows if row.get("replaced_pairs") == 8), None),
            "first_eight_pair_submission": next((row.get("submission") for row in all_rows if row.get("replaced_pairs") == 8), None),
            "measured_replaced_pairs": sorted({row.get("replaced_pairs", 0) for row in rows if row.get("warmup") is False}),
            "sha256": sha256(path)}


class Run:
    def __init__(self, host_path, experiment_path=None, experiment=None, entry=None):
        self.host_path = host_path.resolve()
        self.directory = self.host_path.parent
        self.raw_path = self.directory / "nr_output.f16"
        self.host = read_json(self.host_path)
        self.experiment_path = experiment_path.resolve() if experiment_path else None
        self.experiment = experiment or {}
        self.entry = entry or {}
        self.mode = self.host.get("mode", "unknown")
        self.block = self.entry.get("block")
        if self.block is None:
            match = re.fullmatch(r"block-(\d+)-.*", self.directory.name)
            self.block = int(match.group(1)) if match else None
        self.issues = []
        for name in ("width", "height", "frames", "warmup"):
            value = self.host.get(name)
            if type(value) is not int or value < (0 if name == "warmup" else 1):
                raise ValueError(f"{self.host_path}: invalid {name}")
        self.width, self.height = self.host["width"], self.host["height"]
        self.shape = (self.height, self.width, 4)
        expected_bytes = self.width * self.height * 8
        if self.raw_path.stat().st_size != expected_bytes:
            raise ValueError(f"{self.raw_path}: expected {expected_bytes} tightly packed RGBA FP16 bytes, "
                             f"found {self.raw_path.stat().st_size}")
        self.output_hash = sha256(self.raw_path)
        self.expected_hash = self.entry.get("output_sha256")
        if self.expected_hash and self.output_hash != self.expected_hash:
            self.issues.append("raw output SHA256 does not match experiment receipt")
        if self.experiment_path:
            for name in ("width", "height", "frames", "warmup"):
                if self.experiment.get(name) != self.host[name]:
                    self.issues.append(f"experiment and host disagree on {name}")
            if self.entry.get("mode") != self.mode:
                self.issues.append("experiment and host disagree on mode")
            if self.experiment.get("completed") is not True:
                self.issues.append("experiment is incomplete")
        self.timing = timing_summary(self.directory / "timing.jsonl", self.host)
        if self.timing["valid"] is False:
            self.issues.extend(self.timing["issues"])
        self.receipts = normalized_receipts(self.experiment)
        self.control_receipts = [item for item in self.receipts if
                                 item["name"].lower().endswith((".exe", ".dll")) or item["name"] == "nr_host.cpp"]
        self.conditions = {name: self.host.get(name) for name in CONDITION_FIELDS}
        self.conditions["history_epoch"] = self.host.get("history_epoch", "not_recorded")
        self.conditions["expected_final_frame_index"] = self.host["warmup"] + self.host["frames"] - 1
        self.conditions["observed_final_frame_index"] = self.timing.get("last_frame_index")
        self.conditions["input_and_runtime_receipts"] = self.control_receipts or None
        self.array = np.memmap(self.raw_path, mode="r", dtype="<f2", shape=self.shape)
        self.finite = {}
        for index, name in enumerate(CHANNELS):
            channel = self.array[..., index]
            finite = np.isfinite(channel)
            count = int(np.count_nonzero(finite))
            self.finite[name] = {"finite_elements": count, "nonfinite_elements": int(channel.size - count),
                                 "min": float(np.min(channel[finite])) if count else None,
                                 "max": float(np.max(channel[finite])) if count else None}
        self.all_finite = all(item["nonfinite_elements"] == 0 for item in self.finite.values())
        if not self.all_finite:
            self.issues.append("raw output contains nonfinite FP16 elements")
        self.id = str(self.directory)

    def describe(self):
        return {"id": self.id, "directory": str(self.directory), "mode": self.mode, "block": self.block,
                "experiment": str(self.experiment_path) if self.experiment_path else None,
                "host_json_sha256": sha256(self.host_path), "output": str(self.raw_path),
                "shape_hwc": list(self.shape), "dtype": "little-endian float16", "layout": "interleaved RGBA",
                "output_bytes": self.raw_path.stat().st_size, "output_sha256": self.output_hash,
                "receipt_output_sha256": self.expected_hash,
                "receipt_hash_matches": self.output_hash == self.expected_hash if self.expected_hash else None,
                "all_finite": self.all_finite, "channels": self.finite, "conditions": self.conditions,
                "timing": self.timing, "preparation_frames": self.host.get("preparation_frames"),
                "source_commit": self.experiment.get("source_commit"),
                "source_dirty": self.experiment.get("source_dirty"), "input_receipts": self.receipts,
                "data_valid": not self.issues, "issues": self.issues}


def discover(paths):
    runs, seen = [], set()

    def add(host, experiment_path=None, meta=None, entry=None):
        key = str(host.resolve())
        if key not in seen:
            runs.append(Run(host, experiment_path, meta, entry))
            seen.add(key)

    for requested in paths:
        path = requested.resolve()
        if path.is_dir():
            path = path / ("experiment.json" if (path / "experiment.json").is_file() else "host.json")
        if path.name == "experiment.json":
            meta = read_json(path)
            for entry in meta.get("runs", []):
                directory = (path.parent / entry["directory"]).resolve()
                if not directory.is_relative_to(path.parent):
                    raise ValueError(f"Run directory escapes experiment: {entry['directory']}")
                add(directory / "host.json", path, meta, entry)
        elif path.name == "host.json":
            parent = path.parent.parent / "experiment.json"
            meta = read_json(parent) if parent.is_file() else {}
            entries = [entry for entry in meta.get("runs", [])
                       if (parent.parent / entry["directory"]).resolve() == path.parent]
            if len(entries) > 1:
                raise ValueError(f"Duplicate experiment run: {path.parent}")
            add(path, parent if entries else None, meta if entries else None, entries[0] if entries else None)
        else:
            raise ValueError(f"Expected an experiment.json or host.json input: {path}")
    if len(runs) < 2:
        raise ValueError("At least two distinct host readbacks are required")
    return runs


def condition_comparison(a, b):
    differences, unknown = {}, []
    for name in a.conditions:
        av, bv = a.conditions[name], b.conditions[name]
        if av is None or bv is None:
            unknown.append(name)
        elif av != bv:
            differences[name] = {"baseline": av, "candidate": bv}
    status = "different" if differences else "unknown" if unknown else "matched"
    return {"status": status, "differences": differences, "unknown_fields": unknown,
            "note": "Matching recorded conditions does not establish an image-quality reference."}


def compare_pair(baseline, candidate, peak=1.0, purpose="auto", pairing="explicit"):
    if not math.isfinite(peak) or peak <= 0:
        raise ValueError("PSNR peak must be positive and finite")
    exact = purpose == "exact" or (purpose == "auto" and {baseline.mode, candidate.mode} == {"fused", "optimized"})
    conditions = condition_comparison(baseline, candidate)
    result = {"baseline": baseline.id, "candidate": candidate.id, "baseline_mode": baseline.mode,
              "candidate_mode": candidate.mode, "baseline_block": baseline.block, "candidate_block": candidate.block,
              "pairing": pairing, "purpose": "byte_exact_contract" if exact else "difference_measurement",
              "dimensions_match": baseline.shape == candidate.shape, "conditions": conditions,
              "baseline_sha256": baseline.output_hash, "candidate_sha256": candidate.output_hash,
              "all_finite": baseline.all_finite and candidate.all_finite,
              "quality_qualified": False, "exact_contract_met": None}
    if baseline.shape != candidate.shape:
        result.update({"status": "incompatible_dimensions", "metrics": None, "byte_exact": False})
        return result
    result["byte_exact"] = baseline.output_hash == candidate.output_hash
    metrics, sums, numeric_different, encoded_different = {}, [], 0, 0
    for index, name in enumerate(CHANNELS):
        a = baseline.array[..., index].astype(np.float64)
        b = candidate.array[..., index].astype(np.float64)
        finite = np.isfinite(a) & np.isfinite(b)
        different = int(np.count_nonzero(a != b))
        bits = int(np.count_nonzero(baseline.array[..., index].view("<u2") != candidate.array[..., index].view("<u2")))
        numeric_different += different
        encoded_different += bits
        valid = bool(finite.all())
        entry = {"elements": int(a.size), "numeric_different_elements": different,
                 "different_fp16_encodings": bits, "nonfinite_pairs": int(a.size - np.count_nonzero(finite)),
                 "rmse": None, "mae": None, "max_abs_error": None, "p99_abs_error": None}
        if valid:
            delta = b - a
            absolute = np.abs(delta)
            mse = float(np.mean(delta * delta, dtype=np.float64))
            entry.update({"rmse": math.sqrt(mse), "mae": float(np.mean(absolute, dtype=np.float64)),
                          "max_abs_error": float(np.max(absolute)),
                          "p99_abs_error": float(np.quantile(absolute, .99, method="linear"))})
            if index < 3:
                sums.append(mse)
        metrics[name] = entry
    different_bytes = int(np.count_nonzero(baseline.array.view(np.uint8) != candidate.array.view(np.uint8)))
    rgb_mse = sum(sums) / 3 if len(sums) == 3 else None
    infinite = rgb_mse == 0 if rgb_mse is not None else False
    rgb_psnr = None if rgb_mse is None or infinite else 20 * math.log10(peak) - 10 * math.log10(rgb_mse)
    result.update({"metrics": metrics, "numeric_different_elements": numeric_different,
                   "different_fp16_encodings": encoded_different, "different_bytes": different_bytes,
                   "rgb": {"elements": baseline.width * baseline.height * 3, "mse": rgb_mse,
                           "rmse": math.sqrt(rgb_mse) if rgb_mse is not None else None,
                           "psnr_peak": peak, "psnr_db": rgb_psnr, "psnr_infinite": infinite,
                           "psnr_definition": "10*log10(peak^2 / mean((candidate_RGB-baseline_RGB)^2)); raw values, no clipping"}})
    # Hash equality is verified against a direct full-byte comparison.
    if result["byte_exact"] != (different_bytes == 0):
        raise ValueError("Output changed during comparison or SHA256/direct bytes disagree")
    if exact:
        if conditions["status"] != "matched" or baseline.issues or candidate.issues:
            result["status"] = "exact_contract_inconclusive"
        else:
            result["exact_contract_met"] = result["byte_exact"] and result["all_finite"]
            result["status"] = "exact_contract_passed" if result["exact_contract_met"] else "exact_contract_failed"
    else:
        result["status"] = "measurement_only" if conditions["status"] == "matched" else "measurement_with_condition_differences"
    return result


def pair_runs(runs, baseline_mode, candidate_modes):
    baselines = [run for run in runs if run.mode == baseline_mode]
    if not baselines:
        raise ValueError(f"Baseline mode absent: {baseline_mode}")
    pairs, unmatched = [], []
    for candidate in runs:
        if candidate.mode not in candidate_modes:
            continue
        possible = [base for base in baselines if base is not candidate and base.block == candidate.block]
        local = [base for base in possible if base.experiment_path and base.experiment_path == candidate.experiment_path]
        if local:
            possible, kind = local, "same_experiment_and_block"
        elif possible:
            kind = "corresponding_block_across_inputs"
        elif len(baselines) == 1 and baselines[0] is not candidate:
            possible, kind = baselines, "single_baseline_reused"
        else:
            unmatched.append({"candidate": candidate.id, "reason": "no unambiguous corresponding baseline block"})
            continue
        pairs.extend((base, candidate, kind) for base in possible)
    return pairs, unmatched


def stability(runs, peak=1.0):
    output = []
    for mode in sorted({run.mode for run in runs}):
        groups = {}
        for run in runs:
            if run.mode == mode:
                key = json.dumps(run.conditions, sort_keys=True, separators=(",", ":"))
                groups.setdefault(key, []).append(run)
        checks = []
        for group in groups.values():
            hashes = sorted({run.output_hash for run in group})
            known = all(value is not None for value in group[0].conditions.values())
            reference = group[0]
            representatives = {}
            for run in group[1:]:
                if run.output_hash != reference.output_hash:
                    representatives.setdefault(run.output_hash, run)
            checks.append({"conditions": group[0].conditions, "recorded_conditions_complete": known,
                           "runs": [run.id for run in group], "blocks": [run.block for run in group],
                           "observation_count": len(group), "distinct_hashes": hashes,
                           "status": "single_observation" if len(group) < 2 else
                                     "stable" if len(hashes) == 1 else "different_outputs",
                           "preparation_observations": [{"run": run.id,
                               "first_replacement_frame": run.timing.get("first_replacement_frame"),
                               "first_eight_pair_frame": run.timing.get("first_eight_pair_frame")} for run in group],
                           "distinct_hash_comparisons_vs_first": [compare_pair(reference, run, peak=peak, purpose="measure",
                               pairing="same_mode_same_recorded_conditions") for run in representatives.values()],
                           "all_data_valid": all(not run.issues for run in group)})
        output.append({"mode": mode, "condition_variants": len(groups), "groups": checks,
                       "note": "Different warmup, frame count, input identity, or runtime receipts form separate groups."})
    return output


def preview(pairs, destination, peak, gain, limit=6):
    from PIL import Image, ImageDraw
    selected = [(a, b, kind) for a, b, kind in pairs if a.shape == b.shape][:limit]
    if not selected:
        raise ValueError("No dimension-compatible pairs for preview")
    destination.mkdir(parents=True, exist_ok=False)
    width, height, label = 320, 180, 42
    sheet = Image.new("RGB", (width * 3, (height + label) * len(selected) + 42), "#202020")
    draw = ImageDraw.Draw(sheet)
    draw.text((8, 8), f"Technical readback: linear clamp / peak={peak:g}; absolute RGB error x{gain:g}", fill="white")

    def sample(run):
        ys = np.linspace(0, run.height - 1, height).astype(np.int64)
        xs = np.linspace(0, run.width - 1, width).astype(np.int64)
        return run.array[ys[:, None], xs[None, :], :3].astype(np.float32)

    for row, (a, b, _) in enumerate(selected):
        x, y = sample(a), sample(b)
        arrays = (x / peak, y / peak, np.abs(y - x) / peak * gain)
        labels = (f"Baseline: {a.mode}, block {a.block}", f"Candidate: {b.mode}, block {b.block}",
                  f"Absolute RGB difference x{gain:g}")
        for column, (values, caption) in enumerate(zip(arrays, labels)):
            bad = ~np.isfinite(values).all(axis=2)
            pixels = np.rint(np.clip(np.nan_to_num(values), 0, 1) * 255).astype(np.uint8)
            pixels[bad] = [255, 0, 255]
            top = 42 + row * (height + label)
            sheet.paste(Image.fromarray(pixels), (column * width, top))
            draw.text((column * width + 6, top + height + 5), caption, fill="white")
            draw.text((column * width + 6, top + height + 20), f"{a.width} x {a.height}; RGB only", fill="#bbbbbb")
    path = destination / "contact-sheet.png"
    sheet.save(path)
    return {"file": str(path.resolve()), "sha256": sha256(path), "pairs_shown": len(selected),
            "display": "Nearest sampled raw RGB / peak, clamped to [0,1]; no gamma/color transform; nonfinite shown magenta",
            "difference_gain": gain, "quality_qualified": False}


def build_report(runs, baseline_mode=None, candidate_modes=None, peak=1.0, purpose="auto"):
    available = {run.mode for run in runs}
    baseline_mode = baseline_mode or ("fp8" if "fp8" in available else "passthrough" if "passthrough" in available
                                      else "optimized" if "optimized" in available else runs[0].mode)
    candidates = set(candidate_modes) if candidate_modes else available - {baseline_mode}
    if not candidates and len(available) == 1:
        pairs, unmatched = [], []
    else:
        missing = candidates - available
        if missing:
            raise ValueError(f"Candidate modes absent: {sorted(missing)}")
        pairs, unmatched = pair_runs(runs, baseline_mode, candidates)
        # A three-mode readout needs both the numerical FP8 baseline and the
        # independent fused/unfused exact contract in the same report.
        if purpose == "auto" and {"fused", "optimized"}.issubset(candidates | {baseline_mode}):
            exact_pairs, exact_unmatched = pair_runs(runs, "optimized", {"fused"})
            present = {frozenset((a.id, b.id)) for a, b, _ in pairs}
            pairs.extend((a, b, kind) for a, b, kind in exact_pairs if frozenset((a.id, b.id)) not in present)
            unmatched.extend(exact_unmatched)
    comparisons = [compare_pair(a, b, peak, purpose, kind) for a, b, kind in pairs]
    exact = [row for row in comparisons if row["purpose"] == "byte_exact_contract"]
    stability_results = stability(runs, peak)
    repeated = [group for item in stability_results for group in item["groups"] if group["observation_count"] >= 2]
    stable = all(group["status"] == "stable" for group in repeated) if repeated else None
    report = {"schema": 1, "scope": "CPU analysis of owned signed Feature18 final readbacks",
              "analysis_completed": True, "cuda_initialized": False, "gpu_launches": 0,
              "quality_qualified": False, "auto_qualified": False, "game_quality_qualified": False,
              "baseline_mode": baseline_mode, "candidate_modes": sorted(candidates), "purpose": purpose,
              "psnr_peak": peak, "metric_units": "raw FP16 output values; float64 error accumulation",
              "p99_definition": "99th percentile of per-channel absolute error, linear interpolation",
              "psnr_infinite_encoding": "psnr_db=null and psnr_infinite=true means zero RGB MSE (+infinity dB)",
              "measurement_note": "Original FP8 is a numerical baseline, not image-quality ground truth. No quality threshold is applied.",
              "runs": [run.describe() for run in runs], "comparisons": comparisons,
              "unmatched_candidates": unmatched, "same_mode_stability": stability_results,
              "same_mode_stability_passed": stable,
              "all_data_valid": all(not run.issues for run in runs),
              "all_required_exact_contracts_passed": all(row["exact_contract_met"] is True for row in exact) if exact else None}
    report["checks_passed"] = bool(report["all_data_valid"] and not unmatched and
        all(row["dimensions_match"] for row in comparisons) and
        report["same_mode_stability_passed"] is not False and
        (report["all_required_exact_contracts_passed"] is not False))
    return report, pairs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", type=Path, nargs="+")
    parser.add_argument("--baseline-mode")
    parser.add_argument("--candidate-modes", help="Comma-separated modes; default: all modes except baseline")
    parser.add_argument("--purpose", choices=("auto", "exact", "measure"), default="auto")
    parser.add_argument("--peak", type=float, default=1.0, help="RGB PSNR/display peak; explicitly set for HDR")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preview-dir", type=Path)
    parser.add_argument("--difference-gain", type=float, default=8.0)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output exists; choose a new report path")
    if args.preview_dir and args.preview_dir.exists():
        parser.error("Preview directory exists; choose a new path")
    if not math.isfinite(args.peak) or args.peak <= 0:
        parser.error("--peak must be positive and finite")
    if not math.isfinite(args.difference_gain) or args.difference_gain <= 0:
        parser.error("--difference-gain must be positive and finite")
    try:
        runs = discover(args.inputs)
        report, pairs = build_report(runs, args.baseline_mode,
                                     args.candidate_modes.split(",") if args.candidate_modes else None,
                                     args.peak, args.purpose)
        if args.preview_dir:
            report["preview"] = preview(pairs, args.preview_dir, args.peak, args.difference_gain)
    except (ValueError, OSError, KeyError, ImportError) as error:
        report = {"schema": 1, "analysis_completed": False, "checks_passed": False,
                  "quality_qualified": False, "cuda_initialized": False, "gpu_launches": 0,
                  "error": f"{type(error).__name__}: {error}"}
    report["analyzer_sha256"] = sha256(Path(__file__))
    report["requested_inputs"] = [str(path.resolve()) for path in args.inputs]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps({"analysis_completed": report["analysis_completed"], "checks_passed": report["checks_passed"],
                      "comparisons": len(report.get("comparisons", [])),
                      "exact_contracts_passed": report.get("all_required_exact_contracts_passed"),
                      "quality_qualified": False, "output": str(args.output.resolve()), "error": report.get("error")}))
    if not report["checks_passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
