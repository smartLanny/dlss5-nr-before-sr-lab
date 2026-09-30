"""Compile and execute the real RR depth projection header.

This is a host-side contract test.  It deliberately exercises the production
``Resolve``/``Convert`` functions instead of copying their implementation into
Python.  It does not create a D3D device or claim WARP/NVIDIA/game coverage.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def _compiler() -> Path:
    configured = os.environ.get("RR_DEPTH_TEST_CXX") or os.environ.get("CXX")
    candidates: list[str] = []
    if configured:
        candidates.append(configured)
    toolchain = os.environ.get("DLSS5_LLVM_MINGW_ROOT")
    if toolchain:
        candidates.append(
            str(Path(toolchain) / "bin" / "x86_64-w64-mingw32-clang++.exe")
        )
    candidates.extend(("clang++", "g++", "c++"))
    for candidate in candidates:
        path = Path(candidate)
        resolved = path if path.is_file() else shutil.which(candidate)
        if resolved:
            # Keep the invoked name: clang++ is often a symlink to clang and
            # selects C++ mode from argv[0], so resolving it would link as C.
            return Path(resolved).absolute()
    raise RuntimeError(
        "No C++ compiler found; set RR_DEPTH_TEST_CXX to clang++ or g++."
    )


HARNESS = r'''
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

#include "src/nr_rr_depth_projection.h"

using nr_rr_depth::Convert;
using nr_rr_depth::Projection;
using nr_rr_depth::Resolve;

using Matrix = std::array<float, 16>;

static int failures = 0;

void Check(bool condition, const char *label) {
    if (!condition) {
        std::printf("FAIL %s\n", label);
        ++failures;
    }
}

void Close(float actual, float expected, float tolerance, const char *label) {
    Check(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          label);
}

Matrix NormalFinite(float near_z, float far_z) {
    Matrix m{};
    m[0] = 1.7f;
    m[5] = 2.3f;
    m[10] = far_z / (far_z - near_z);
    m[11] = 1.0f;
    m[14] = -near_z * far_z / (far_z - near_z);
    return m;
}

Matrix NormalInfinite(float near_z) {
    Matrix m{};
    m[0] = 1.7f;
    m[5] = 2.3f;
    m[10] = 1.0f;
    m[11] = 1.0f;
    m[14] = -near_z;
    return m;
}

Matrix ReverseFinite(float near_z, float far_z) {
    Matrix m{};
    m[0] = 1.7f;
    m[5] = 2.3f;
    m[10] = -near_z / (far_z - near_z);
    m[11] = 1.0f;
    m[14] = near_z * far_z / (far_z - near_z);
    return m;
}

Matrix ReverseInfinite(float near_z) {
    Matrix m{};
    m[0] = 3.73205137f;
    m[5] = 6.634758f;
    m[10] = 0.0f;
    m[11] = 1.0f;
    m[14] = near_z;
    return m;
}

void CheckFiniteProjection(const Matrix &m, bool inverted, float near_z,
                           float far_z, const char *label) {
    Projection p{};
    Check(Resolve(m.data(), inverted, p), label);
    Close(Convert(p, near_z), inverted ? 1.0f : 0.0f, 2.0e-5f,
          "near endpoint");
    Close(Convert(p, far_z), inverted ? 0.0f : 1.0f, 2.0e-5f,
          "far endpoint");
    const float mid = std::sqrt(near_z * far_z);
    const float expected = p.a + p.b / mid;
    Close(Convert(p, mid), std::fmax(0.0f, std::fmin(1.0f, expected)),
          2.0e-5f, "finite midpoint");
}

void CheckInfiniteProjection(const Matrix &m, bool inverted, float near_z,
                             const char *label) {
    Projection p{};
    Check(Resolve(m.data(), inverted, p), label);
    Close(Convert(p, near_z), inverted ? 1.0f : 0.0f, 2.0e-5f,
          "infinite near endpoint");
    const float very_far = 1.0e30f;
    const float d = Convert(p, very_far);
    Check(std::isfinite(d), "infinite far remains finite");
    Check(inverted ? d < 1.0e-5f : d > 1.0f - 1.0e-5f,
          "infinite far orientation");
}

int main() {
    constexpr float near_z = 0.25f;
    constexpr float far_z = 100.0f;

    CheckFiniteProjection(NormalFinite(near_z, far_z), false, near_z, far_z,
                          "normal finite projection");
    CheckFiniteProjection(ReverseFinite(near_z, far_z), true, near_z, far_z,
                          "reverse finite projection");
    CheckInfiniteProjection(NormalInfinite(near_z), false, near_z,
                            "normal infinite projection");
    CheckInfiniteProjection(ReverseInfinite(near_z), true, near_z,
                            "reverse infinite projection");

    // F8/NTE fixture: flags=67, m10=0, m11=1, m14=10.  The projection
    // direction comes from the matrix; the flag value is not treated as a
    // hardware-depth declaration.
    const unsigned nte_create_flags = 67u;
    Check(nte_create_flags == 67u, "NTE flags fixture retained");
    const Matrix nte = ReverseInfinite(10.0f);
    Projection nte_projection{};
    Check(!Resolve(nte.data(), false, nte_projection),
          "NTE reverse matrix rejects normal DepthInverted");
    Check(Resolve(nte.data(), true, nte_projection),
          "NTE reverse matrix derives inverted direction");
    Close(Convert(nte_projection, 10.0f), 1.0f, 2.0e-5f,
          "NTE near depth");
    Close(Convert(nte_projection, 20.0f), 0.5f, 2.0e-5f,
          "NTE 10/z depth");

    // Off-axis projection terms affect clip X/Y only and must not invalidate
    // a scalar Z map.  The four z/w x/y terms remain zero.
    Matrix jitter = NormalFinite(near_z, far_z);
    jitter[8] = 0.03125f;
    jitter[9] = -0.046875f;
    Projection jitter_projection{};
    Check(Resolve(jitter.data(), false, jitter_projection),
          "off-axis jitter is accepted");
    Close(Convert(jitter_projection, near_z), 0.0f, 2.0e-5f,
          "jitter near endpoint");

    // The production conversion is fail-closed for invalid source samples and
    // clamps finite out-of-range values to the declared far/near endpoint.
    Projection normal{};
    Check(Resolve(NormalFinite(near_z, far_z).data(), false, normal),
          "normal for invalid samples");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Close(Convert(normal, 0.0f), 1.0f, 0.0f, "zero maps to normal far");
    Close(Convert(normal, -1.0f), 1.0f, 0.0f, "negative Z maps to normal far");
    Close(Convert(normal, nan), 1.0f, 0.0f, "NaN maps to normal far");
    Close(Convert(normal, inf), 1.0f, 0.0f, "positive infinity maps to normal far");
    Close(Convert(normal, near_z * 0.5f), 0.0f, 2.0e-5f,
          "near underflow clamps to normal near");
    Close(Convert(normal, far_z * 2.0f), 1.0f, 2.0e-5f,
          "far overflow clamps to normal far");

    Projection reverse{};
    Check(Resolve(ReverseFinite(near_z, far_z).data(), true, reverse),
          "reverse for invalid samples");
    Close(Convert(reverse, 0.0f), 0.0f, 0.0f, "zero maps to reverse far");
    Close(Convert(reverse, -1.0f), 0.0f, 0.0f, "negative Z maps to reverse far");
    Close(Convert(reverse, nan), 0.0f, 0.0f, "NaN maps to reverse far");
    Close(Convert(reverse, inf), 0.0f, 0.0f, "positive infinity maps to reverse far");
    Close(Convert(reverse, near_z * 0.5f), 1.0f, 2.0e-5f,
          "near underflow clamps to reverse near");
    Close(Convert(reverse, far_z * 2.0f), 0.0f, 2.0e-5f,
          "far overflow clamps to reverse far");

    // Invalid projection contracts: null/non-finite, oblique depth, an
    // orthographic W row, bad focal rows, and a nonzero constant W.
    Check(!Resolve(nullptr, false, normal), "null matrix rejected");
    Matrix invalid = NormalFinite(near_z, far_z);
    invalid[0] = nan;
    Check(!Resolve(invalid.data(), false, normal), "NaN matrix rejected");
    invalid = NormalFinite(near_z, far_z);
    invalid[2] = 0.1f;
    Check(!Resolve(invalid.data(), false, normal), "oblique Z/X rejected");
    invalid = NormalFinite(near_z, far_z);
    invalid[6] = -0.1f;
    Check(!Resolve(invalid.data(), false, normal), "oblique Z/Y rejected");
    invalid = NormalFinite(near_z, far_z);
    invalid[3] = 0.1f;
    Check(!Resolve(invalid.data(), false, normal), "oblique W/X rejected");
    invalid = NormalFinite(near_z, far_z);
    invalid[7] = -0.1f;
    Check(!Resolve(invalid.data(), false, normal), "oblique W/Y rejected");
    invalid = NormalFinite(near_z, far_z);
    invalid[11] = 0.0f;
    invalid[15] = 1.0f;
    Check(!Resolve(invalid.data(), false, normal), "orthographic projection rejected");
    invalid = NormalFinite(near_z, far_z);
    invalid[15] = 1.0f;
    Check(!Resolve(invalid.data(), false, normal), "constant W rejected");
    invalid = NormalFinite(near_z, far_z);
    invalid[11] = -1.0f;
    Check(!Resolve(invalid.data(), false, normal), "negative W slope rejected");
    invalid = NormalFinite(near_z, far_z);
    invalid[0] = 0.0f;
    Check(!Resolve(invalid.data(), false, normal), "zero focal X rejected");
    invalid = NormalFinite(near_z, far_z);
    invalid[5] = -1.0f;
    Check(!Resolve(invalid.data(), false, normal), "negative focal Y rejected");
    invalid = NormalFinite(near_z, far_z);
    invalid[14] = 0.0f;
    Check(!Resolve(invalid.data(), false, normal), "zero projection offset rejected");
    invalid = NormalFinite(near_z, far_z);
    invalid[10] = inf;
    Check(!Resolve(invalid.data(), false, normal), "infinite matrix rejected");

    // The inversion bit must agree with projection direction, in both finite
    // and infinite forms.
    Check(!Resolve(NormalFinite(near_z, far_z).data(), true, normal),
          "normal matrix rejects reverse DepthInverted");
    Check(!Resolve(ReverseFinite(near_z, far_z).data(), false, normal),
          "reverse matrix rejects normal DepthInverted");
    Check(!Resolve(NormalInfinite(near_z).data(), true, normal),
          "normal infinite rejects reverse DepthInverted");
    Check(!Resolve(ReverseInfinite(near_z).data(), false, normal),
          "reverse infinite rejects normal DepthInverted");

    std::printf("RR depth projection: %s (%d failures)\n",
                failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
'''


def main() -> int:
    compiler = _compiler()
    with tempfile.TemporaryDirectory(prefix="rr-depth-projection-") as temp:
        temp_path = Path(temp)
        source = temp_path / "projection.cpp"
        executable = temp_path / "projection.exe"
        source.write_text(HARNESS, encoding="utf-8")
        command = [
            str(compiler),
            "-std=c++20",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(source),
            f"-I{ROOT}",
            "-o",
            str(executable),
        ]
        print("compile:", " ".join(command))
        subprocess.run(command, cwd=ROOT, check=True)
        environment = os.environ.copy()
        environment["PATH"] = str(compiler.parent) + os.pathsep + environment.get(
            "PATH", ""
        )
        completed = subprocess.run(
            [str(executable)],
            cwd=ROOT,
            env=environment,
            check=False,
            text=True,
            capture_output=True,
        )
        if completed.stdout:
            print(completed.stdout, end="")
        if completed.stderr:
            print(completed.stderr, end="")
        if completed.returncode != 0:
            raise SystemExit(completed.returncode)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
