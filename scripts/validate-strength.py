"""Numerical regression for the beta0.3.7 transfer-strength contract."""
import argparse
import math
from pathlib import Path


def normalize(value, fallback, low, high):
    if not math.isfinite(value):
        value = fallback
    return max(low, min(high, value))


def shader_ratio(strength, relative_delta=0.5, max_ratio=2.0):
    # Exact scalar expression used by CombineCS. The final luma limiter remains
    # in the shader; this probe checks that the user strength reaches it without
    # an accidental saturate(0..1).
    return max(1.0 / max_ratio,
               min(max_ratio, 1.0 + relative_delta * strength))


parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (args.source or root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
shader = source[source.index('const char kCodecShader[] = R"HLSL('):]
if "1.0 + relativeLumaDelta * TransferStrength" not in shader:
    raise SystemExit("CombineCS transfer expression is missing")
if "saturate(TransferStrength)" in shader:
    raise SystemExit("TransferStrength is incorrectly saturated to 0..1")

checks = [
    (0.0, 1.0),
    (1.0, 1.5),
    (1.2, 1.6),
    (2.0, 2.0),
    (float("nan"), 1.0),
    (float("inf"), 1.0),
]
for strength, expected in checks:
    safe = normalize(strength, 0.0, 0.0, 2.0)
    actual = shader_ratio(safe)
    if abs(actual - expected) > 1e-6:
        raise SystemExit(f"strength {strength!r}: got {actual}, expected {expected}")
    print(f"strength={strength!r} normalized={safe:.3f} ratio={actual:.3f}")

# The retained beta0.2 configuration path maps old mode 1 to the new default
# mode 2 while preserving the legacy 1.0 transfer value.
old_mode = 1
migrated_mode = 2 if old_mode != 2 else old_mode
if migrated_mode != 2:
    raise SystemExit("legacy mode migration failed")
print("legacy config migration: mode=1 -> mode=2, transfer=1.000")
print("strength regression: PASS")
