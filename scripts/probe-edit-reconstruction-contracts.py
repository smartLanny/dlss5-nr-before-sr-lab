#!/usr/bin/env python3
"""Independent CPU examples for the RC10-review implementation contracts.

No third-party implementation is copied or loaded. Coefficients below are
illustrative, deliberately not target shader immediates or proposed defaults.
This is not the production compositor, an emulator, a GPU/NGX run, or a quality
benchmark. The supplied private-material audit is documented separately.

Run: python scripts/probe-edit-reconstruction-contracts.py --output <new.json>
Tests: python -m unittest discover -s tests -p test_edit_reconstruction_contracts.py
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
from typing import Sequence

LUMA = (0.2126, 0.7152, 0.0722)


def finite(value: float, name: str) -> float:
    value = float(value)
    if not math.isfinite(value):
        raise ValueError(f"{name} must be finite")
    return value


def smooth(value: float) -> float:
    t = min(1.0, max(0.0, value))
    return t * t * (3.0 - 2.0 * t)


@dataclass(frozen=True)
class ExampleEnvelope:
    """Illustrative normalized domain only; not nits, PQ or saved NR settings."""

    peak_low: float = 0.001
    peak_high: float = 0.02
    gain_begin: float = 2.0
    gain_end: float = 8.0

    def __post_init__(self) -> None:
        values = (self.peak_low, self.peak_high, self.gain_begin, self.gain_end)
        if not all(math.isfinite(x) for x in values):
            raise ValueError("Envelope values must be finite")
        if not 0 <= self.peak_low < self.peak_high:
            raise ValueError("Require 0 <= peak_low < peak_high")
        if not 0 < self.gain_begin < self.gain_end:
            raise ValueError("Require 0 < gain_begin < gain_end")


def chroma_confidence(peak: float, gain: float,
                      envelope: ExampleEnvelope = ExampleEnvelope(),
                      enabled: bool = True) -> float:
    """Attenuate only when BOTH the low-peak and high-gain risks are present."""
    peak, gain = finite(peak, "peak"), finite(gain, "gain")
    if peak < 0 or gain <= 0:
        raise ValueError("Require a nonnegative peak and positive gain")
    if not enabled:
        return 1.0
    safe_peak = smooth((peak - envelope.peak_low) /
                       (envelope.peak_high - envelope.peak_low))
    # Subtract logs instead of forming a potentially overflowing gain ratio.
    stops = math.log2(gain) - math.log2(envelope.gain_begin)
    span = math.log2(envelope.gain_end) - math.log2(envelope.gain_begin)
    safe_gain = 1.0 - smooth(stops / span)
    return max(safe_peak, safe_gain)


def luminance(rgb: Sequence[float]) -> float:
    if len(rgb) != 3:
        raise ValueError("Expected three RGB components")
    values = tuple(finite(x, "RGB") for x in rgb)
    if any(x < 0 for x in values):
        raise ValueError("This example does not define a signed-RGB domain")
    return math.fsum(a * b for a, b in zip(values, LUMA))


def at_target(rgb: Sequence[float], target: float) -> tuple[float, ...]:
    """Only the ideal same-target endpoints; no transfer/gamut/store model."""
    y = luminance(rgb)
    target = finite(target, "target")
    if target < 0 or (y == 0 and target > 0):
        raise ValueError("Cannot recover a positive color direction from black")
    if target == 0:
        return (0.0, 0.0, 0.0)
    result = tuple(float(c) * (target / y) for c in rgb)
    if not all(math.isfinite(c) for c in result):
        raise ValueError("Endpoint overflow")
    return result


def blend_at_target(source: Sequence[float], candidate: Sequence[float],
                    target: float, confidence: float) -> tuple[float, ...]:
    confidence = finite(confidence, "confidence")
    if not 0 <= confidence <= 1:
        raise ValueError("Confidence must lie in [0, 1]")
    original_at_target = at_target(source, target)
    candidate_at_target = at_target(candidate, target)
    return tuple((1 - confidence) * a + confidence * b
                 for a, b in zip(original_at_target, candidate_at_target))


def guided_edit(edits: Sequence[float], guides: Sequence[float], center: float,
                px: float = 0.5, py: float = 0.5,
                spatial_strength: float = 2.0, guide_strength: float = 2.0,
                minimum_joint_weight: float = 1e-6) -> tuple[float, bool]:
    """Illustrative normalized 4x4 scalar-edit filter, not texture/codec math."""
    if len(edits) != 16 or len(guides) != 16:
        raise ValueError("Expected exactly sixteen paired local samples")
    edits = tuple(finite(x, "edit") for x in edits)
    guides = tuple(finite(x, "guide") for x in guides)
    values = (center, px, py, spatial_strength, guide_strength, minimum_joint_weight)
    if not all(math.isfinite(x) for x in values):
        raise ValueError("Filter parameters must be finite")
    if not 0 <= px <= 1 or not 0 <= py <= 1:
        raise ValueError("The example models a fractional position in [0, 1]")
    if spatial_strength < 0 or guide_strength < 0 or minimum_joint_weight <= 0:
        raise ValueError("Invalid weight parameters")
    spatial, joint = [], []
    for i, (y, x) in enumerate((y, x) for y in (-1, 0, 1, 2)
                              for x in (-1, 0, 1, 2)):
        ws = 2.0 ** (-spatial_strength * ((x - px)**2 + (y - py)**2))
        difference = guides[i] - center
        # Multiplication may reach inf for hostile finite input; exp2(-inf)=0.
        wr = 1.0 if guide_strength == 0 else 2.0 ** (-guide_strength * difference * difference)
        spatial.append(ws)
        joint.append(ws * wr)
    fallback = math.fsum(joint) <= minimum_joint_weight
    weights = spatial if fallback else joint
    total = math.fsum(weights)
    if total <= 0:
        raise ValueError("No valid spatial support")
    value = math.fsum((w / total) * e for w, e in zip(weights, edits))
    if not math.isfinite(value):
        raise ValueError("Weighted edit overflow")
    return value, fallback


def floored_ratio_counterexample() -> dict[str, float | str]:
    """Scalar algebra of the separately supplied reference, NOT RC10 or ours."""
    source, candidate, floor, limit = 0.0001, 0.01, 1 / 512, 2.0
    ratio = (candidate + floor) / (source + floor)
    bounded_ratio = min(limit, max(1 / limit, ratio))
    color_endpoint = candidate * bounded_ratio / ratio
    luma_endpoint = source * bounded_ratio
    return {
        "source": "separate reference formula; not target or production code",
        "scope": "ideal neutral-grey algebra; no complete hue/gamut/GPU emulation",
        "source_value": source,
        "candidate_value": candidate,
        "floor": floor,
        "intermediate_bounded_ratio": bounded_ratio,
        "final_color_endpoint_gain": color_endpoint / source,
        "final_luma_endpoint_gain": luma_endpoint / source,
    }


def report() -> dict[str, object]:
    e = ExampleEnvelope()
    edits = [1.0 if x <= 0 else -1.0
             for y in (-1, 0, 1, 2) for x in (-1, 0, 1, 2)]
    guides = [0.0 if x <= 0 else 2.0
              for y in (-1, 0, 1, 2) for x in (-1, 0, 1, 2)]
    return {
        "scope": "independent illustrative CPU math, not a plugin patch",
        "parameters_are_production_defaults": False,
        "parameters_are_exact_target_immediates": False,
        "confidence_table": [
            {"peak": peak, "weights": {str(g): chroma_confidence(peak, g, e)
                                       for g in (1, 2, 4, 8)}}
            for peak in (0.0001, 0.02)
        ],
        "same_target_luminance_examples": [
            {"confidence": c, "rgb": blend_at_target((0.4, 0.3, 0.2), (0.2, 0.1, 0.3), 0.15, c)}
            for c in (0, 0.25, 0.5, 0.75, 1)
        ],
        "guidance_examples": {
            "split_luminance_edge": guided_edit(edits, guides, 0),
            "equal_luminance_not_separable": guided_edit(edits, [0.0] * 16, 0),
            "invalid_support_spatial_fallback": guided_edit(edits, [100.0] * 16, 0),
        },
        "separate_reference_counterexample": floored_ratio_counterexample(),
        "hlsl_compiled": False,
        "gpu_executed": False,
        "ngx_executed": False,
        "game_verified": False,
        "measured_quality_or_fps_gain": False,
        "production_runtime_modified": False,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    payload = json.dumps(report(), ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive open: rerunning must not rewrite a previous validation receipt.
    with args.output.open("x", encoding="utf-8") as stream:
        stream.write(payload)
    print(payload, end="")


if __name__ == "__main__":
    main()
