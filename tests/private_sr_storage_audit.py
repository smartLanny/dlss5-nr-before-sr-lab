#!/usr/bin/env python3
"""Research audit: FLOAT32 arithmetic -> binary16 carrier -> FLOAT32 decode.

Not a renderer, HLSL compiler or DLSS test. Formulas independently transcribed
from this repository PR #259 at 1d125828 and the public wilsjo2 v0.8.4 source.
Optional --scalar-header compiles the actual PR #259 production scalar helper
and feeds it the binary16-rounded carrier; its Git blob is checked first.
No network, vendor binaries, GPU, game files or configuration are accessed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile

EXPECTED_SCALAR_BLOB = "740e4a943866622fea0b354b6f80e73ec9b4fb6f"

def f32(x: float) -> float:
    return struct.unpack("<f", struct.pack("<f", x))[0]

def f16(x: float) -> float:
    return struct.unpack("<e", struct.pack("<e", x))[0]

def affine(delta: float, span: float) -> dict[str, float]:
    d, b = f32(delta), f32(span)
    core = max(-b, min(b, d))
    carrier = f32(0.5 + f32(0.5 * f32(core / b)))
    tail = f32(d - core)
    stored = f16(carrier)
    decoded = f32(f32(f32(f32(2.0 * stored) - 1.0) * b) + tail)
    return {"carrier_f32": carrier, "carrier_f16": stored,
            "tail_f32": tail, "decoded": decoded}

def rational(delta: float, scale: float) -> dict[str, float]:
    d, k = f32(delta), f32(scale)
    carrier = f32(0.5 + f32(f32(0.5 * d) / f32(k + abs(d))))
    stored = f16(carrier)
    signed = max(-0.999, min(0.999, f32(2.0 * stored - 1.0)))
    decoded = f32(f32(k * signed) / f32(1.0 - abs(signed)))
    return {"carrier_f32": carrier, "carrier_f16": stored,
            "tail_f32": 0.0, "decoded": decoded}

def production_check(header: Path, compiler: str) -> str:
    raw = header.read_bytes()
    blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
    if blob != EXPECTED_SCALAR_BLOB:
        raise ValueError("Scalar source identity differs from reviewed PR #259; re-audit first")
    harness = r'''#include "scalar.hlsli"
#include <cstdio>
int main() {
    float d, carrier, span;
    while (std::scanf("%f %f %f", &d, &carrier, &span) == 3) {
        const auto packed = nr_edit_math::NrEditPack(d, span);
        const auto value = nr_edit_math::NrEditUnpack(carrier, packed.tail, span);
        std::printf("%.9g %.9g %.9g\n", packed.carrier, packed.tail, value);
    }
    const float cleared = nr_edit_math::NrEditAccumulate(0.f, .02f, 1.f, .08f, 0.f, 0.f);
    return cleared == 0.f ? 0 : 2;
}
'''
    with tempfile.TemporaryDirectory(prefix="nr-carrier-audit-") as directory:
        root = Path(directory)
        (root / "scalar.hlsli").write_bytes(raw)
        (root / "main.cpp").write_text(harness, encoding="utf-8")
        executable = root / "probe"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-ffp-contract=off", str(root / "main.cpp"), "-o", str(executable)],
                       check=True, capture_output=True, text=True, timeout=30)
        samples = [(d, b) for d in (0., 1e-4, -1e-4, 1e-3, -1e-3, 8., -8.)
                   for b in (4., 1/64)]
        payload = "".join(f"{d:.17g} {affine(d,b)['carrier_f16']:.17g} {b:.17g}\n"
                          for d, b in samples)
        result = subprocess.run([str(executable)], input=payload, capture_output=True,
                                text=True, check=True, timeout=10)
        lines = result.stdout.splitlines()
        if len(lines) != len(samples):
            raise AssertionError("Production helper returned an unexpected row count")
        for line, (d, b) in zip(lines, samples):
            got = tuple(map(float, line.split()))
            expected = affine(d, b)
            want = (expected["carrier_f32"], expected["tail_f32"], expected["decoded"])
            if any(not math.isclose(a, z, rel_tol=2e-7, abs_tol=2e-9)
                   for a, z in zip(got, want)):
                raise AssertionError(f"Production/mirror mismatch at delta={d}, span={b}")
    return f"passed: {len(samples)} pack/unpack samples and RR zero-neighborhood clamp"

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Optional JSON result outside tracked source")
    parser.add_argument("--scalar-header", type=Path, help="Reviewed PR #259 nr_edit_scalar.hlsli")
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--require-small-edit-survival", action="store_true",
                        help="Expected to exit 1 for the reviewed B=4+FP16 design")
    args = parser.parse_args()
    samples = [0., 1e-5, -1e-5, 1e-4, -1e-4, 1e-3, -1e-3, 1e-2, -1e-2, 8., -8.]
    rows = []
    for d in samples:
        for name, method, scale in (("affine_B4", affine, 4.),
                                    ("affine_B1over64", affine, 1/64),
                                    ("rational_k1over64", rational, 1/64)):
            value = method(d, scale)
            rows.append({"delta": d, "scheme": name, **value,
                         "absolute_error": abs(value["decoded"]-d)})
    checks = 0
    for row in rows:
        if not all(math.isfinite(row[key]) for key in
                   ("carrier_f32", "carrier_f16", "tail_f32", "decoded")):
            raise AssertionError("Non-finite tested result")
        checks += 1
        if row["delta"] == 0 and row["decoded"] != 0:
            raise AssertionError("Scalar zero-edit identity broken")
    for d in (1e-4, -1e-4):
        old = affine(d, 4.)
        if old["carrier_f16"] != .5 or old["tail_f32"] != 0 or old["decoded"] != 0:
            raise AssertionError("Expected B=4 FP16 loss was not reproduced")
        for method in (affine, rational):
            recovered = method(d, 1/64)["decoded"]
            if recovered*d <= 0 or abs(recovered-d) > 1e-5:
                raise AssertionError("Small-scale comparison failed")
        checks += 3
    report = {
        "scope": "CPU storage and scalar equations only; not DLSS or GPU quality",
        "reviewed_pr259": "1d1258282419d291004d38d545a8d5fc935a1c31",
        "reviewed_scalar_blob": EXPECTED_SCALAR_BLOB,
        "arithmetic": "binary32 rounded operations, binary16 round-to-nearest storage",
        "equation_checks": checks,
        "production_helper": production_check(args.scalar_header, args.cxx)
                             if args.scalar_header else "not-run; equations only",
        "small_edits_survive_reviewed_B4_FP16": False,
        "shader_compile": "not-run", "gpu_execution": "not-run", "dlss_runtime": "not-run",
        "rows": rows,
        "notes": ["Tail preserves clipping overflow, not in-range FP16 carrier rounding.",
                  "B=1/64 is a comparison, not a shipping recommendation.",
                  "Rational inverse can amplify near-endpoint error; test HDR extremes separately.",
                  "RR exact current-neighborhood clipping is not Magpie accumulation+."]
    }
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 1 if args.require_small_edit_survival else 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, AssertionError, subprocess.SubprocessError) as error:
        raise SystemExit(f"Audit failed: {error}")
