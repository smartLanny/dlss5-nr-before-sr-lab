"""CPU numerical/selection regression using source-extracted production code.

HLSL vectors are adapted to small CPU float3 operators; this complements, and
does not replace, compilation/execution of the actual shader on D3D12.
"""
import argparse
from pathlib import Path
import re
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
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
shader_functions = "\n".join(definition(source, name) for name in (
    "float RationalPeakShoulderScale(", "float3 RationalPeakShoulder(",
    "float3 HdrSourceToModelLinear("))
# Keep arithmetic in float, matching HLSL literal semantics.
shader_functions = re.sub(r"(\d+\.\d+)\.xxx", r"float3(\1)", shader_functions)
shader_functions = re.sub(r"(?<![\w.])(\d+\.\d+)(?![\w.])", r"\1f", shader_functions)
harness = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
using std::max;
using std::exp;
struct bool3 { bool x,y,z; };
bool all(bool3 b) { return b.x&&b.y&&b.z; }
struct float3 {
    float r,g,b;
    explicit float3(float v):r(v),g(v),b(v){}
    float3(float x,float y,float z):r(x),g(y),b(z){}
    float3& operator*=(float s) { r*=s; g*=s; b*=s; return *this; }
};
float3 operator*(float3 a,float s) { return a*=s; }
float3 operator/(float3 a,float s) { return a*(1/s); }
bool3 operator==(float3 a,float3 b) { return {a.r==b.r,a.g==b.g,a.b==b.b}; }
bool3 operator<=(float3 a,float3 b) { return {a.r<=b.r,a.g<=b.g,a.b<=b.b}; }
float3 max(float3 a,float3 b) { return {max(a.r,b.r),max(a.g,b.g),max(a.b,b.b)}; }
float3 abs(float3 a) { return {std::fabs(a.r),std::fabs(a.g),std::fabs(a.b)}; }
float3 saturate(float3 a) { return {std::clamp(a.r,0.f,1.f),std::clamp(a.g,0.f,1.f),std::clamp(a.b,0.f,1.f)}; }
float dot(float3 a,float3 b) { return a.r*b.r+a.g*b.g+a.b*b.b; }
float rcp(float p) { return 1.f/p; }
const float3 kLuma(.2126f,.7152f,.0722f);
float ProxyScale=1, ExposureScale=1;
unsigned Passthrough=0;
float ResolveSourceToCanonical() { return ExposureScale; }
SHADER_FUNCTIONS
COLOR_MODE
USE_PEAK
int checks=0, failures=0;
void check(bool ok,const char* name) { ++checks; if(!ok) { ++failures; std::printf("FAIL: %s\n",name); } }
bool near(float a,float b,float eps=2e-6f) { return std::fabs(a-b)<=eps; }
bool near(float3 a,float3 b,float eps=2e-6f) { return near(a.r,b.r,eps)&&near(a.g,b.g,eps)&&near(a.b,b.b,eps); }
int main() {
    check(!UsePeakShoulder(CodecColorMode::Automatic,true,false),"default off");
    check(UsePeakShoulder(CodecColorMode::Automatic,true,true),"automatic HDR opt-in");
    check(UsePeakShoulder(CodecColorMode::PqEncodedHdr,true,true),"verified PQ opt-in");
    for(auto mode:{CodecColorMode::LinearSdr,CodecColorMode::SrgbEncodedSdr,CodecColorMode::ModelEncodedValueDomain})
        for(bool hdr:{false,true}) check(!UsePeakShoulder(mode,hdr,true),"bounded SDR unaffected by option");
    check(!UsePeakShoulder(CodecColorMode::Automatic,false,true),"non-HDR automatic unchanged");
    Passthrough=0;
    check(near(HdrSourceToModelLinear(float3(2,1.2f,.8f)),float3(1,.6f,.4f)),"legacy warm peak 2");
    check(near(HdrSourceToModelLinear(float3(4,2.4f,1.6f)),float3(1,.6f,.4f)),"legacy warm peak 4");
    check(near(HdrSourceToModelLinear(float3(.5f)),float3(.5f),0),"legacy low identity");
    Passthrough=256;
    check(near(HdrSourceToModelLinear(float3(2,1.2f,.8f)),float3(23.f/24, .575f,23.f/60)),"distinct bright warm level 2");
    check(near(HdrSourceToModelLinear(float3(4,2.4f,1.6f)),float3(55.f/56,33.f/56,11.f/28)),"distinct bright warm level 4");
    for(int i=0;i<=1000;++i) {
        float p=.75f*i/1000;
        float3 s(p,p*.6f,p*.4f);
        check(near(HdrSourceToModelLinear(s),s,0),"candidate low cube exact identity");
    }
    float last=0;
    for(int i=0;i<=4096;++i) {
        float p=.7501f+(32.f-.7501f)*i/4096;
        float3 out=HdrSourceToModelLinear(float3(p,.6f*p,.4f*p));
        check(out.r>last,"candidate source range .75..32 monotonic in float");
        check(out.r<1 && out.r>0 && near(out.g/out.r,.6f) && near(out.b/out.r,.4f),"bounded RGB ray");
        const double decoded=.75+.25*(out.r-.75)/(1-out.r);
        check(std::fabs(decoded-p)/p<7e-5,"float inverse before proxy quantization");
        last=out.r;
    }
    check(near(HdrSourceToModelLinear(float3(-1,.5f,.25f)),float3(0,.5f,.25f)),"negative source proxy policy");
    check(near(RationalPeakShoulder(float3(std::numeric_limits<float>::infinity(),1,1)),float3(0),0),"infinite proxy sanitized");
    check(near(RationalPeakShoulder(float3(std::numeric_limits<float>::quiet_NaN(),1,1)),float3(0),0),"NaN proxy sanitized");
    ProxyScale=2; ExposureScale=1024;
    float3 extreme=HdrSourceToModelLinear(float3(65504,32752,0));
    check(std::isfinite(extreme.r)&&std::isfinite(extreme.g)&&extreme.r<=1,"largest allowed normalized source finite");
    ProxyScale=1; ExposureScale=.5f;
    check(near(HdrSourceToModelLinear(float3(4,2.4f,1.6f)),float3(23.f/24,.575f,23.f/60)),"exposure before peak mapping");
    std::printf("Peak shoulder CPU regression: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
'''
harness = harness.replace("SHADER_FUNCTIONS", shader_functions)
harness = harness.replace("COLOR_MODE", definition(source, "enum class CodecColorMode", True))
harness = harness.replace("USE_PEAK", definition(source, "bool UsePeakShoulder("))
with tempfile.TemporaryDirectory(prefix="nr-peak-shoulder-") as temporary:
    cpp, binary = Path(temporary) / "test.cpp", Path(temporary) / "test.exe"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([args.compiler, "-std=c++20", "-O2", "-static", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
