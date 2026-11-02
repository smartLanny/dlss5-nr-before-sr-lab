"""Run the production lighting estimator on analytic same-dispatch luma taps.

Only the scalar/vector arithmetic is hosted on the CPU; this is not a shader
compiler, texture-boundary, HDR display, temporal or game acceptance test.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def definition(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class LightingEdgeWeights(unittest.TestCase):
    def test_production_lighting_estimator(self):
        source = (ROOT / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
        model = ""
        if "float GenericNeuralLumaV2(" in source:
            model += definition(source, "float GenericControlLumaV2(")
            model += definition(source, "bool GenericContinuousRequestedV2(")
            model += definition(source, "float GenericNeuralLumaV2(")
        model += definition(source, "float ModelLumaRatioV2(")
        if "float2 ModelLightingTapV2(" in source:
            model += definition(source, "float2 ModelLightingTapV2(")
        if "float ModelLightingMeanV2(" in source:
            model += definition(source, "float ModelLightingMeanV2(")
        model = model.replace("[branch]", "")
        combine = definition(source, "void CombineV2CS(")
        lighting = definition(
            combine,
            "if (LightingLock > 0.000001 || "
            "(layeredActive && LayerTone != LayerStructure))",
        )
        harness = r'''
#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>
struct float2 {
    float x,y;
    float2(float a,float b):x(a),y(b){}
    float2& operator+=(float2 b) { x+=b.x; y+=b.y; return *this; }
};
float2 operator+(float2 a,float2 b){return {a.x+b.x,a.y+b.y};}
float2 operator-(float2 a,float2 b){return {a.x-b.x,a.y-b.y};}
float2 operator-(float2 a,float b){return {a.x-b,a.y-b};}
float2 operator*(float2 a,float2 b){return {a.x*b.x,a.y*b.y};}
float2 operator*(float a,float2 b){return {a*b.x,a*b.y};}
float2 operator/(float2 a,float2 b){return {a.x/b.x,a.y/b.y};}
float2 round(float2 a){return {std::round(a.x),std::round(a.y)};}
struct int2 { int x,y; explicit int2(float2 a):x(int(a.x)),y(int(a.y)){} };
float max(float a,float b){return a>b?a:b;}
unsigned max(unsigned a,unsigned b){return a>b?a:b;}
float min(float a,float b){return a<b?a:b;}
float clamp(float a,float lo,float hi){return max(lo,min(a,hi));}
float saturate(float a){return clamp(a,0,1);}
float lerp(float a,float b,float w){return a+(b-a)*w;}
float smoothstep(float lo,float hi,float a){float w=saturate((a-lo)/(hi-lo));return w*w*(3-2*w);}
using std::abs; using std::isfinite; using std::log2;
unsigned Width=128,Height=128,ModelWidth=128,ModelHeight=128;
using float3=float; // same analytic grey texture contract as before
unsigned RegionFlags=0; // this regression exercises the unchanged legacy path
float RegionWideBrightLuma=1,RegionWideBrightColor=1,RegionFineBrightLuma=1,RegionWideDarkLuma=1;
constexpr float kLuma=1; // analytic gray taps; dot returns their exact luminance
float dot(float a,float b){return a*b;}
float tapP[4],tapN[4]; unsigned proxyLoads=0,neuralLoads=0;
int TapIndex(int2 p){
    const int cx=int(64.5f*ModelWidth/Width-0.5f+0.5f);
    const int cy=int(64.5f*ModelHeight/Height-0.5f+0.5f);
    return p.x>cx?0:p.x<cx?1:p.y>cy?2:3;
}
float LoadProxyLinearV2(int2 p){++proxyLoads;return tapP[TapIndex(p)];}
float LoadNeuralLinearV2(int2 p){++neuralLoads;return tapN[TapIndex(p)];}
@MODEL@
float Evaluate(float p,float ratio,float stabilized,float lock){
    float proxyLuma=p,neuralLuma=p*ratio,logRatio=stabilized,LightingLock=lock;
    bool layeredActive=false;
    float LayerTone=1.0f,LayerStructure=1.0f;
    [[maybe_unused]] float layerMean=0.0f;
    float2 center(64.5f,64.5f);
    proxyLoads=neuralLoads=0;
    @LIGHTING@
    return logRatio;
}
void Flat(float p,float ratio){for(unsigned i=0;i<4;++i){tapP[i]=p;tapN[i]=p*ratio;}}
unsigned checks=0,failures=0;
void Check(bool ok,const char *label){
    ++checks;if(!ok){++failures;if(failures<12)std::printf("FAIL %s\n",label);}
}
int main(){
    for(unsigned model:{64u,86u,96u,128u}){
        ModelWidth=ModelHeight=model;
        for(float p:{0.02f,0.25f,0.5f,1.f,2.f})
        for(float ratio:{0.5f,0.8f,1.f,1.25f,2.f})
        for(float lock:{0.f,0.15f,0.2f,0.6f,1.f}){
            const float raw=std::log2(ratio);
            Flat(p,ratio);
            const float flat=Evaluate(p,ratio,raw,lock);
            Check(std::abs(flat-raw*(1-lock))<2e-6f,"uniform lighting drift behavior unchanged");
            Check(proxyLoads==(lock>0?4u:0u) && neuralLoads==proxyLoads,"same four paired neighbor loads; zero when disabled");
            for(unsigned edge=0;edge<4;++edge){
                Flat(p,ratio);
                tapP[edge]=p*0.05f;tapN[edge]=tapP[edge];
                const float darkEdge=Evaluate(p,ratio,raw,lock);
                Check(std::abs(darkEdge-flat)<2e-6f,"dark background cannot leave a protected face rim");
                tapP[edge]=p*4;tapN[edge]=tapP[edge];
                const float brightEdge=Evaluate(p,ratio,raw,lock);
                Check(std::abs(brightEdge-flat)<2e-6f,"bright background cannot change foreground protection");
            }
        }
    }
    ModelWidth=ModelHeight=128;
    // Keep the previous attenuation envelope after stabilization, even where
    // luma guidance cannot distinguish two objects of similar luminance.
    for(float raw:{-8.f,-1.f,-0.1f,0.f,0.1f,1.f,8.f})
    for(float scale:{0.f,0.25f,1.f}) for(float lock:{0.f,0.2f,1.f})
    for(float neighborRatio:{0.01f,0.5f,1.f,2.f,64.f}){
        Flat(0.5f,neighborRatio);
        float stabilized=raw*scale;
        float actual=Evaluate(0.5f,std::exp2(raw),stabilized,lock);
        Check(std::isfinite(actual) && actual>=min(stabilized,0)-2e-6f && actual<=max(stabilized,0)+2e-6f,
              "protection cannot reverse, amplify, or resurrect a zero stabilized residual");
    }
    // Rejected cross-edge weights need renormalization, not a fabricated unity
    // contribution or a darker total. No valid tap still has the center anchor.
    for(float ratio:{0.5f,0.8f,1.25f,2.f}){
        Flat(0.025f,1.f);
        Check(std::abs(Evaluate(0.5f,ratio,std::log2(ratio),1.f))<2e-6f,"center anchor handles all cross-edge taps");
        for(unsigned i=0;i<4;++i){tapP[i]=std::numeric_limits<float>::quiet_NaN();tapN[i]=1;}
        Check(std::abs(Evaluate(0.5f,ratio,std::log2(ratio),1.f))<2e-6f,"nonfinite neighbors do not bias the lighting estimate");
    }
    // Smooth guidance, not a hard region mask: protection changes continuously.
    float previous=0;
    for(unsigned i=0;i<=1000;++i){
        Flat(0.5f,0.8f);tapP[0]=0.5f*(1-float(i)/1100);tapN[0]=tapP[0];
        float actual=Evaluate(0.5f,0.8f,std::log2(0.8f),1);
        Check(std::isfinite(actual),"finite guidance sweep");
        if(i)Check(std::abs(actual-previous)<0.002f,"continuous luma weighting");
        previous=actual;
    }
    std::printf("lighting edge weights: %u checks, %u failures (analytic host taps only)\n",checks,failures);
    return failures?1:0;
}
'''.replace("@MODEL@", model).replace("@LIGHTING@", lighting)
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        self.assertTrue(compiler, "a host C++ compiler is required for the estimator regression")
        with tempfile.TemporaryDirectory(prefix="nr-light-weights-") as temporary:
            cpp = Path(temporary) / "test.cpp"
            exe = Path(temporary) / ("test.exe" if os.name == "nt" else "test")
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", str(cpp), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            print(result.stdout, end="")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
