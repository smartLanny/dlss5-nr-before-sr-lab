#!/usr/bin/env python3
"""Compile the real legacy-post luma statements with deterministic model taps.

The oracle independently derives LightingLock in log space and the final
linear relative transfer. This is CPU/source validation; no GPU or game work.
"""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def definition(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


PREFIX = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
using std::abs; using std::isfinite;
static float min(float a,float b){return std::min(a,b);}
static float max(float a,float b){return std::max(a,b);}
static float clamp(float v,float a,float b){return std::clamp(v,a,b);}
static float saturate(float v){return std::clamp(v,0.f,1.f);}
static float lerp(float a,float b,float t){return a+(b-a)*t;}
static float smoothstep(float a,float b,float v){float t=saturate((v-a)/(b-a));return t*t*(3-2*t);}
static unsigned checks=0;
static void require(bool okay,const char *name){
    ++checks;if(!okay){std::fprintf(stderr,"POST_LIGHT_ORACLE: %s\n",name);std::exit(23);}
}
static bool close(float a,double b){return std::abs(double(a)-b)<=1e-5*std::max(1.,std::abs(b));}
static bool sameBits(float a,float b){return std::memcmp(&a,&b,sizeof(a))==0;}
struct float2 {
    float x,y;
    float2(float xx,float yy):x(xx),y(yy){}
    float2 &operator+=(float2 b){x+=b.x;y+=b.y;return *this;}
};
static float2 operator+(float2 a,float2 b){return {a.x+b.x,a.y+b.y};}
static float2 operator-(float2 a,float2 b){return {a.x-b.x,a.y-b.y};}
static float2 operator*(float a,float2 b){return {a*b.x,a*b.y};}
static float tapEdit=0;
static float2 ModelLightingTapV2(float2,float){return {1.f+tapEdit,1.f};}
static float2 PostLightEditTapV1(float2,float){return {tapEdit,1.f};}
static float TransferStrength=1, MaxLumaRatio=4, LightingLock=0;
static float LightBroad=1, LightDark=1, LightReflection=1, LightStructure=1, LightGlow=0;
static unsigned Passthrough=0;
'''

TESTS = r'''
static double oracle(double edit,double partition,double lock,double master,
                     double broad,double dark,double reflection,double structure,double glow,
                     double source=0.25){
    double remaining=edit;
    if(lock>1e-6){
        const double logEdit=std::log2(1.+edit);
        const double logMean=std::log2(1.+edit*partition);
        const double protectedEdit=std::clamp(logEdit-logMean,std::min(logEdit,0.),std::max(logEdit,0.));
        remaining=std::exp2(logEdit+(protectedEdit-logEdit)*lock)-1.;
    }
    const double gain=partition*(edit>0?broad*(1.-glow):dark)+
        (1.-partition)*(edit>0?reflection:structure);
    const double ratio=std::clamp(1.+remaining*std::clamp(master*gain,0.,4.),.25,4.);
    double t=std::clamp((source-1./65536.)/(1./1024.-1./65536.),0.,1.);
    const double confidence=t*t*(3.-2.*t);
    return 1.+(ratio-1.)*confidence;
}
int main(){
    // Distinguish the old linear post transfer from V2 log amplification.
    require(PostLightTransferRatioV1(.25f,.25f,0,1,4,1,1,1.2f,1,0)==1.3f,
        "post retains one plus relative edit; never exp2(relative)");
    for(float edit:{-.5f,-.125f,.125f,.5f})
    for(float partition:{0.f,.25f,.5f,.75f,1.f})
    for(float lock:{0.f,.2f,1.f})
    for(float strength:{0.f,1.f,2.f,4.f})
    for(float source:{0.f,1.f/65536,1.f/4096,.25f,4.f}){
        TransferStrength=strength; LightingLock=lock; Passthrough=0;
        tapEdit=(partition-.4f)*edit/.6f;
        LightBroad=LightDark=LightReflection=LightStructure=1; LightGlow=0;
        const float native=ShadePostLuma(.25f,.25f*(1+edit),source,false);
        const float marked=ShadePostLuma(.25f,.25f*(1+edit),source,true);
        require(sameBits(native,marked),"neutral flag follows exact legacy luma arithmetic");
        for(unsigned preset:{0u,1u,2u,3u}){
            LightBroad=preset==3?1.4f:1.f;
            LightDark=preset==1?.85f:1.f;
            LightReflection=preset==1?1.10f:preset==3?1.4f:1.f;
            LightStructure=preset==1?1.05f:preset==3?1.3f:1.f;
            LightGlow=preset==0?1.f:preset==1?.3f:preset==2?.7f:0.f;
            const float actual=ShadePostLuma(.25f,.25f*(1+edit),source,true);
            const double expected=oracle(edit,partition,lock,strength,LightBroad,
                LightDark,LightReflection,LightStructure,LightGlow,source);
            require(close(actual,expected),"actual post protection then independent relative gain");
            require(isfinite(actual)&&actual>=.25f&&actual<=4.f,"post final ratio bounded");
            if(strength==0) require(actual==1.f,"post master zero stays identity");
        }
    }
    // Four pure partitions, each repeated through real lighting statements.
    for(float lock:{0.f,.2f,1.f})
    for(float edit:{-.25f,.25f})
    for(float partition:{0.f,1.f}){
        LightingLock=lock;TransferStrength=1;Passthrough=0;
        tapEdit=(partition-.4f)*edit/.6f;
        LightBroad=LightDark=LightReflection=LightStructure=1;LightGlow=0;
        const float reference=ShadePostLuma(.25f,.25f*(1+edit),.25f,true);
        LightGlow=1;
        const float glow=ShadePostLuma(.25f,.25f*(1+edit),.25f,true);
        if(edit<0 || partition==0)require(sameBits(reference,glow),"glow cannot alter negative or pure fine-positive post edits");
        else require(close(glow,1.),"full glow removes only broad-positive post edit");
    }
    // The legacy binary preservation bit must retain its existing precedence.
    TransferStrength=1;LightingLock=.2f;Passthrough=2;
    LightBroad=LightDark=LightReflection=LightStructure=1;LightGlow=.3f;tapEdit=.25f;
    float withBit=ShadePostLuma(.25f,.3125f,.25f,true);
    Passthrough=0;
    require(sameBits(withBit,ShadePostLuma(.25f,.3125f,.25f,true)),"scalar LightingLock overrides legacy bit");
    LightingLock=0;Passthrough=2;
    float binaryFull=ShadePostLuma(.25f,.3125f,.25f,true);
    LightingLock=1;Passthrough=0;
    require(sameBits(binaryFull,ShadePostLuma(.25f,.3125f,.25f,true)),"legacy binary full lock retained");
    std::printf("PASS production post luma: %u assertions; LightingLock 0/.2/1, four partitions, natural/glow controls and near-black confidence\n",checks);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX','g++'))
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--baseline-ref', default='c237ac9')
    args = parser.parse_args()
    source = (ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    common = re.search(r'// FOUR_REGION_SCALAR_BEGIN\n(.*?)// FOUR_REGION_SCALAR_END',source,re.S).group(1)
    post_helper = re.search(r'// POST_LIGHT_SCALAR_BEGIN\n(.*?)// POST_LIGHT_SCALAR_END',source,re.S).group(1)
    mean = definition(source,'float PostLightEditMeanV1(')
    combine = definition(source,'void CombineCS(')
    body = combine[combine.index('    const float lightOriginalEdit'):combine.index('    float3 lumaOnly')]
    # Only the coordinate value is a deterministic stub. The original
    # production lighting and final luma statements are compiled unchanged.
    body = body.replace('float2(tid.xy) + 0.5','float2(0.0, 0.0)')
    shade = 'float ShadePostLuma(float proxyLuma,float neuralLuma,float sourceLuma,bool lightStyleActive){\n'+body+'\nreturn lumaRatio;\n}\n'
    production = PREFIX+common+post_helper+mean+shade+TESTS
    variants = [('production',production)]
    if args.negative_control:
        variants += [
            ('log-in-post',production.replace('1.0 + remaining * strength','exp2(remaining * strength)')),
            ('all-positive-glow',production.replace('? clamp(reflection, 0.0, 2.0) : clamp(structure, 0.0, 2.0);','? clamp(reflection, 0.0, 2.0) * (1.0 - saturate(glow)) : clamp(structure, 0.0, 2.0);')),
            ('ignore-post-controls',production.replace('if (lightStyleActive && !LightStyleNeutralV1(', 'if (false && !LightStyleNeutralV1(')),
            ('ignore-lighting-protection',production.replace('PostLightTransferRatioV1(relativeLumaDelta,','PostLightTransferRatioV1(lightOriginalEdit,')),
        ]
    with tempfile.TemporaryDirectory(prefix='nr-post-light-') as directory:
        for name,code in variants:
            assert name=='production' or code!=production, 'negative control did not change implementation'
            cpp=Path(directory)/(name+'.cpp');exe=Path(directory)/(name+('.exe' if os.name=='nt' else ''))
            cpp.write_text(code,encoding='utf-8')
            subprocess.run([args.compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            if name=='production':
                assert run.returncode==0, run.stdout+run.stderr
                print(run.stdout.strip())
            else:
                assert run.returncode==23 and 'POST_LIGHT_ORACLE:' in run.stderr,(name,run.returncode,run.stdout,run.stderr)
                print(f'PASS negative control {name}: {run.stderr.strip()}')
    baseline=subprocess.check_output(['git','show',args.baseline_ref+':src/nr_before_sr.cpp'],cwd=ROOT,text=True,encoding='utf-8')
    old=definition(baseline,'void CombineCS(')
    for start,end in [
        ('    const bool modelEncodedInput','    float3 modelDelta'),
        ('    float normalizer','    float proxyLuma'),
        ('    float lightingAmount','    // The proxy may be compressed'),
        ('    // Full colour following','    WriteCombined(tid.xy'),
    ]:
        before=old[old.index(start):old.index(end,old.index(start))]
        after=combine[combine.index(start):combine.index(end,combine.index(start))]
        if start=='    // Full colour following':
            after=after[:after.index('    if (lightStylePreview)')]
        assert before==after,(start,'legacy colour/exposure/protection statements changed')
    v2 = definition(source,'void CombineV2CS(')
    selected = 'SelectLightStyleStrengthV1((Passthrough & 524288u) != 0u, '
    assert v2.count(selected)==1, 'expected only the explicit directional selector in V2'
    v2 = v2.replace(selected, 'LightStyleStrengthV1(')
    assert v2==definition(baseline,'void CombineV2CS('),'shared V2 post keeps its original log compositor apart from the exact selector'
    original_dispatch=definition(baseline,'void DispatchCodec(')
    dispatch=definition(source,'void DispatchCodec(')
    route_start='    const bool independent_chroma ='
    route_end='    const bool v2_combine ='
    assert dispatch[dispatch.index(route_start):dispatch.index(route_end)]==original_dispatch[original_dispatch.index(route_start):original_dispatch.index(route_end)],'D must not select a different colour/composition route'
    assert 'const bool light_style_post_supported = pipeline == g_codec.combine;' in dispatch
    print('PASS frozen legacy decode/normalizer/lighting/colour/storage, unchanged V2 and opt-in routing')


if __name__=='__main__': main()
