"""Check our explicit UICorrection writer with mocks, not the vendor getter.

This verifies writer shape and 0/1 values only. It does not prove that a real
consumer requires unsigned storage or that UI/face-mask correction is visual.
"""

from pathlib import Path
import os
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
source = (root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
start = source.index("bool BindNrParameters(")
opening = source.index("{", start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
binding = source[start:end]
line = next((item.strip() for item in binding.splitlines()
             if '"DLSSNR.UICorrection"' in item), None)
if line is None:
    raise SystemExit("UICorrection binding is missing")
if not line.startswith("SetUInt("):
    raise SystemExit(f"UICorrection writer convention changed; review boolean encoding: {line}")

harness = r'''
#include <cstdio>
#include <cstring>
struct NgxParams { unsigned ui = 99; unsigned unsigned_writes = 0; unsigned signed_writes = 0; };
struct Tuning { bool ui_correction = false; };
void SetInt(NgxParams *params, const char *key, int value) {
    if (std::strcmp(key, "DLSSNR.UICorrection") == 0) {
        ++params->signed_writes; params->ui = static_cast<unsigned>(value);
    }
}
void SetUInt(NgxParams *params, const char *key, unsigned value) {
    if (std::strcmp(key, "DLSSNR.UICorrection") == 0) {
        ++params->unsigned_writes; params->ui = value;
    }
}
void Bind(NgxParams *params, const Tuning &tuning) { BINDING }
int main() {
    NgxParams params{}; Tuning tuning{};
    Bind(&params, tuning);
    if (params.ui != 0 || params.unsigned_writes != 1 || params.signed_writes != 0) return 1;
    tuning.ui_correction = true; Bind(&params, tuning);
    if (params.ui != 1 || params.unsigned_writes != 2 || params.signed_writes != 0) return 2;
    std::puts("DLSSNR.UICorrection: unsigned parameter contract PASS");
    return 0;
}
'''.replace("BINDING", line)

toolchain = os.environ.get("DLSS5_LLVM_MINGW_ROOT")
if not toolchain:
    raise SystemExit("Set DLSS5_LLVM_MINGW_ROOT before running this validator")
compiler = Path(toolchain) / "bin" / "x86_64-w64-mingw32-clang++.exe"
with tempfile.TemporaryDirectory(prefix="dlss5-ngx-types-") as temporary:
    cpp = Path(temporary) / "types.cpp"
    exe = Path(temporary) / "types.exe"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([str(compiler), "-std=c++20", "-O2", "-static", str(cpp), "-o", str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
