"""Exercise the production config normalization and CombineCS strength math."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def definition(source, marker, semicolon=False):
    start = source.index(marker)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end + int(semicolon)]


parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--source", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (args.source or root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
shader = source[source.index('const char kCodecShader[] = R"HLSL('):]
if "1.0 + relativeLumaDelta * TransferStrength" not in shader:
    raise SystemExit("CombineCS transfer expression is missing")
if "saturate(TransferStrength)" in shader:
    raise SystemExit("TransferStrength is incorrectly saturated to 0..1")

harness = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
ENUM
CONFIG
FINITE
MIGRATE
NORMALIZE
int failures=0;
void check(bool ok,const char* label) { if(!ok) { ++failures; std::printf("FAIL: %s\n",label); } }
float shader_ratio(float strength, float relative_delta=0.5f, float max_ratio=2.0f) {
    return std::clamp(1.0f + relative_delta * strength, 1.0f / max_ratio, max_ratio);
}
int main() {
    const float inputs[] = {0.0f, 1.0f, 1.2f, 2.0f, NAN, INFINITY, -INFINITY};
    const float ratios[] = {1.0f, 1.5f, 1.6f, 2.0f, 1.0f, 1.0f, 1.0f};
    for (unsigned i=0; i<sizeof(inputs)/sizeof(inputs[0]); ++i) {
        ChainConfig c{}; c.transfer_strength=inputs[i]; c.post_transfer_strength=inputs[i];
        c.color_strength=inputs[i]; c.max_luma_ratio=inputs[i]; c.proxy_scale=inputs[i];
        c.intensity=inputs[i]; c.local_tone=inputs[i]; c.local_structure=inputs[i]; c.skin_structure=inputs[i];
        NormalizeConfig(c);
        const float expected = std::isfinite(inputs[i]) ? std::clamp(inputs[i],0.0f,8.0f) : 0.0f;
        check(std::isfinite(c.transfer_strength) && c.transfer_strength==expected,"finite transfer normalization");
        check(std::isfinite(c.post_transfer_strength) && c.post_transfer_strength==expected,"finite post normalization");
        check(std::isfinite(c.color_strength) && c.color_strength==std::clamp(expected,0.0f,2.0f),"finite color normalization");
        check(std::isfinite(c.max_luma_ratio) && c.max_luma_ratio==std::clamp(std::isfinite(inputs[i])?inputs[i]:2.0f,1.0f,16.0f),"finite luma normalization");
        check(std::isfinite(c.proxy_scale) && c.proxy_scale==std::clamp(std::isfinite(inputs[i])?inputs[i]:1.0f,.25f,2.0f),"finite proxy normalization");
        check(std::isfinite(c.intensity) && c.intensity==std::clamp(std::isfinite(inputs[i])?inputs[i]:1.0f,0.0f,2.0f),"finite model normalization");
        check(std::fabs(shader_ratio(c.transfer_strength)-ratios[i])<1e-6f,"GPU scalar strength reaches CombineCS");
        std::printf("strength[%u]=%g normalized=%g ratio=%g\n",i,inputs[i],c.transfer_strength,shader_ratio(c.transfer_strength));
    }
    check(MigrateConfigMode(4,1)==1,"current config keeps selected mode");
    check(MigrateConfigMode(0,1)==2,"legacy config migrates mode 1 to mode 2");
    check(MigrateConfigMode(0,2)==2,"legacy config keeps mode 2");
    std::printf("production strength/config regression: %s (%d failures)\n", failures?"FAIL":"PASS", failures);
    return failures ? 1 : 0;
}
'''
harness = harness.replace("ENUM", "enum class NrJitterMode : int { Legacy=0, GameInputPixel=1 };\n")
harness = harness.replace("CONFIG", definition(source, "struct ChainConfig", True))
harness = harness.replace("FINITE", definition(source, "float FiniteOr("))
harness = harness.replace("MIGRATE", definition(source, "int MigrateConfigMode("))
harness = harness.replace("NORMALIZE", definition(source, "void NormalizeConfig("))
with tempfile.TemporaryDirectory(prefix="nr-strength-") as td:
    cpp, exe = Path(td) / "strength.cpp", Path(td) / "strength.exe"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([args.compiler, "-std=c++20", "-O0", "-static", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
