"""Synthetic CPU study of depth rejection for a four-tap residual estimator.

This is not imported by the renderer. Callers supply scalar residuals and the
already-computed baseline photometric weights on ONE aligned grid; no textures,
SRV indices, colour, alpha, interpolation across R/N/O, or history are accessed.
The baseline is the current D estimator's 0.40 centre + up to four 0.15 taps.

Only synthetic positive view-Z with an explicit common-scale provenance is
admitted. Hardware/NDC/inverse depth must never be relabelled as view-Z. The
provenance marker documents fixture construction; it cannot certify real engine
data. A future adapter must independently prove the frame, mapping and scale.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Sequence


CENTER_WEIGHT = 0.40
MAX_TAP_WEIGHT = 0.15
SYNTHETIC_VIEW_Z = "synthetic-verified-common-view-z"


@dataclass(frozen=True)
class FrameDomain:
    frame_id: int
    generation: int
    viewport: tuple[int, int, int, int]  # Source-grid x, y, width, height.
    route: str


@dataclass(frozen=True)
class ResidualTap:
    x: int  # Local coordinates within the identical residual/depth viewport.
    y: int
    residual: float
    weight: float = MAX_TAP_WEIGHT  # Existing spatial/photometric weight.


@dataclass(frozen=True)
class DepthGuide:
    values: tuple[tuple[float, ...], ...]
    domain: FrameDomain
    encoding: str
    scale_id: str | None  # One common positive scale; does not imply metres.
    provenance: str | None


@dataclass(frozen=True)
class Estimate:
    value: float
    baseline: float
    neighbor_weights: tuple[float, ...]
    depth_used: bool
    reason: str


def _finite(value: float) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _validate_residuals(center: float, taps: Sequence[ResidualTap]) -> None:
    if not _finite(center) or len(taps) > 4:
        raise ValueError("A finite centre and at most four baseline taps are required")
    if any(not _finite(t.residual) or not _finite(t.weight) or
           not 0.0 <= t.weight <= MAX_TAP_WEIGHT for t in taps):
        raise ValueError("Baseline residuals must be finite and tap weights in [0, 0.15]")


def _average(center: float, taps: Sequence[ResidualTap], weights: tuple[float, ...]) -> float:
    if not any(weights):
        return center  # Preserve the exact centre, without a multiply/divide round trip.
    numerator, denominator = CENTER_WEIGHT * center, CENTER_WEIGHT
    for tap, weight in zip(taps, weights):
        numerator += weight * tap.residual
        denominator += weight
    return numerator / denominator


def baseline_average(center: float, taps: Sequence[ResidualTap]) -> float:
    """Unmodified weighted fallback; supplied weights already include luma guards."""
    _validate_residuals(center, taps)
    return _average(center, taps, tuple(t.weight for t in taps))


def _validate_domain(domain: FrameDomain, positions: Sequence[tuple[int, int]]) -> None:
    integers = (domain.frame_id, domain.generation, *domain.viewport)
    if len(domain.viewport) != 4 or any(type(v) is not int for v in integers):
        raise ValueError("Frame, generation and viewport must use integer identities")
    x, y, width, height = domain.viewport
    if domain.frame_id <= 0 or domain.generation <= 0 or x < 0 or y < 0 or width <= 0 or height <= 0:
        raise ValueError("A known frame/generation and nonempty viewport are required")
    if not isinstance(domain.route, str) or not domain.route.strip():
        raise ValueError("A route identity is required")
    if any(type(px) is not int or type(py) is not int or
           not (0 <= px < width and 0 <= py < height) for px, py in positions):
        raise ValueError("Samples must already be mapped to the shared local viewport")


def _depth_fallback(depth: DepthGuide | None, domain: FrameDomain) -> str | None:
    if depth is None:
        return "depth-missing"
    for field in ("frame_id", "generation", "viewport", "route"):
        if getattr(depth.domain, field) != getattr(domain, field):
            return field.replace("_", "-") + "-mismatch"
    if depth.encoding != "positive-view-z":
        return "depth-encoding-unverified"
    if depth.provenance != SYNTHETIC_VIEW_Z:
        return "depth-provenance-unverified"
    if not isinstance(depth.scale_id, str) or not depth.scale_id.strip():
        return "depth-scale-unverified"
    width, height = domain.viewport[2:]
    if len(depth.values) != height or any(len(row) != width for row in depth.values):
        return "depth-grid-mismatch"
    # Conservative whole-guide admission is intentional for this offline study.
    # Invalid samples are not converted to far-plane values or zero motion.
    if any(not _finite(z) or z <= 0.0 for row in depth.values for z in row):
        return "depth-invalid"
    return None


def interpolate_residual(
    *,
    center: tuple[int, int],
    center_residual: float,
    taps: Sequence[ResidualTap],
    domain: FrameDomain,
    depth: DepthGuide | None,
    relative_depth_limit: float = 0.02,
) -> Estimate:
    """Reject neighbours across a synthetic depth gap; otherwise retain baseline.

    Relative gap = abs(z - centre_z) / max(z, centre_z), evaluated using the
    equivalent 1 - min/max form. A common positive depth scaling cancels out.
    The threshold is a study parameter, not a validated surface/material test.
    Invalid residual kernels are rejected; missing/untrusted depth falls back.
    """
    taps = tuple(taps)
    _validate_domain(domain, (center, *((t.x, t.y) for t in taps)))
    if not _finite(relative_depth_limit) or not 0.0 <= relative_depth_limit < 1.0:
        raise ValueError("Relative depth limit must be finite and in [0, 1)")
    baseline = baseline_average(center_residual, taps)
    original_weights = tuple(t.weight for t in taps)
    reason = _depth_fallback(depth, domain)
    if reason is not None:
        return Estimate(baseline, baseline, original_weights, False, reason)
    assert depth is not None
    center_z = depth.values[center[1]][center[0]]
    weights = []
    for tap in taps:
        z = depth.values[tap.y][tap.x]
        gap = 1.0 - min(z, center_z) / max(z, center_z)
        weights.append(tap.weight if gap <= relative_depth_limit else 0.0)
    weights = tuple(weights)
    value = _average(center_residual, taps, weights)
    return Estimate(value, baseline, weights, True, "guided" if any(weights) else "center-only")
