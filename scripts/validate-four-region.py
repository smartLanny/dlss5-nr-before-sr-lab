#!/usr/bin/env python3
"""Run independent analytic oracles against the production HLSL scalar helpers.

No GPU dispatch, NR inference, game-quality or timing claim. Negative controls
must fail the numeric oracle after a successful compile, not merely fail to run.
"""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

PREFIX = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
using std::abs;
using std::isfinite;
static float min(float a,float b){return std::min(a,b);}
static float max(float a,float b){return std::max(a,b);}
static float clamp(float v,float lo,float hi){return std::clamp(v,lo,hi);}
static float saturate(float v){return std::clamp(v,0.f,1.f);}
static unsigned checks=0;
static void require(bool condition,const char *name){
    ++checks;
    if(!condition){std::fprintf(stderr,"FOUR_REGION_ORACLE: %s\n",name);std::exit(23);}
}
static bool close(float a,double b){return std::abs(double(a)-b)<=2e-6*std::max(1.,std::abs(b));}
static bool sameBits(float a,float b){return std::memcmp(&a,&b,sizeof(float))==0;}
'''

TESTS = r'''
int main(){
    // Four independent pure partitions: h affects only a positive broad edit.
    for(float h:{0.f,.3f,.7f,1.f}){
        require(close(LightStyleStrengthV1(.5f,.5f,1.f,1.f,1,1,1,1,h),1.-h),"pure broad positive suppression");
        require(LightStyleStrengthV1(.5f,.5f,-1.f,1.f,1,1,1,1,h)==1.f,"pure fine positive survives glow");
        require(LightStyleStrengthV1(-.5f,-.5f,-1.f,1.f,1,1,1,1,h)==1.f,"pure broad negative survives glow");
        require(LightStyleStrengthV1(-.5f,-.5f,1.f,1.f,1,1,1,1,h)==1.f,"pure fine negative survives glow");
    }
    // A removed 75% of an edit must leave the ORIGINAL 25/75 partition.
    require(close(LightRemainingBroadV1(1.f,.25f,.25f),.0625),"protection cannot relabel fine as broad");
    require(close(LightRemainingBroadV1(-1.f,-.25f,-.25f),-.0625),"negative partition survives protection");
    require(LightRemainingBroadV1(1.f,.25f,0.f)==0.f,"protection zero cannot resurrect broad");
    require(LightRemainingBroadV1(1.f,.25f,-.1f)==0.f,"opposite remainder rejected");
    require(LightStyleStrengthV1(0,1,.5f,4,2,2,2,2,.5f)==0.f,"zero remaining edit cannot be amplified");
    require(LightStyleStrengthV1(1,1,.5f,4,2,2,2,2,0)==4.f,"combined effective strength capped at four");

    for(float sign:{-1.f,1.f})
    for(float magnitude:{1e-20f,1e-8f,.0625f,.5f,1.f,8.f})
    for(float partition:{0.f,.125f,.5f,.875f,1.f})
    for(float protection:{0.f,.25f,.75f,1.f})
    for(float master:{0.f,.5f,1.f,1.2f,2.f,4.f}){
        const float raw=sign*magnitude, mean=raw*partition, remaining=raw*protection;
        require(sameBits(LightStyleStrengthV1(remaining,raw,mean,master,1,1,1,1,0),master),"neutral exact public strength");
        for(float broad:{0.f,.85f,1.f,2.f})
        for(float fine:{0.f,1.f,1.3f,2.f})
        for(float glow:{0.f,.3f,.7f,1.f}){
            // Input construction provides known broad/fine proportions. This
            // oracle does not call the production partition or clamp helper.
            const float actual=LightStyleStrengthV1(remaining,raw,mean,master,
                broad,broad,fine,fine,glow);
            const double directedBroad=sign>0?double(broad)*(1.-glow):broad;
            double expected=double(master)*(double(partition)*directedBroad+(1.-partition)*fine);
            expected=std::min(4.,std::max(0.,expected));
            const bool neutral=broad==1.f&&fine==1.f&&glow==0.f;
            if(protection==0.f&&!neutral) expected=0.;
            require(close(actual,expected),"four partition analytic weighted contribution");
            require(isfinite(actual)&&actual>=0.f&&actual<=4.f,"finite bounded gain");
            require(remaining==0.f||remaining*(remaining*actual)>=0.f,"edit direction retained");
            if(sign<0 && remaining!=0.f)
                require(actual==LightStyleStrengthV1(remaining,raw,mean,master,
                    2,broad,2,fine,glow),"positive controls cannot change negative edit");
        }
    }
    for(float raw:{-.5f,.5f}){
        require(LightBroadFractionV1(raw,-raw)==0.f,"opposite mean contributes no broad edit");
        require(LightBroadFractionV1(raw,raw*3)==1.f,"oversized same-sign mean stays bounded");
        for(float nan:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
            require(LightBroadFractionV1(raw,nan)==0.f,"invalid mean cannot poison partition");
            require(LightStyleStrengthV1(raw,raw,nan,1,1,1,1,1,.5f)==0.f,"invalid mean fail closed");
            require(LightStyleStrengthV1(raw,raw,raw,nan,1,1,1,1,.5f)==0.f,"invalid master fail closed");
            require(LightStyleStrengthV1(raw,raw,raw,1,nan,1,1,1,.5f)==0.f,"invalid coefficient fail closed");
        }
    }
    // Glow sweep must preserve local positive content while broad gain falls.
    float previous=2.f;
    for(unsigned i=0;i<=100;++i){
        float actual=LightStyleStrengthV1(.1f,.4f,.2f,1,1,1,1.3f,1,i*.01f);
        require(actual<=previous+1e-6f,"glow monotone on mixed positive edit");
        require(actual>=.65f-1e-6f,"fine positive survives full glow suppression");
        previous=actual;
    }
    std::printf("PASS production four-region scalar: %u numeric assertions\n",checks);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--negative-control', action='store_true')
    args = parser.parse_args()
    source = (ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    helper = re.search(r'// FOUR_REGION_SCALAR_BEGIN\n(.*?)// FOUR_REGION_SCALAR_END', source, re.S)
    assert helper, 'production scalar helper missing'
    helper = helper.group(1)
    variants = [('production', helper)]
    if args.negative_control:
        variants += [
            ('all-positive-glow', helper.replace(
                '? clamp(reflection, 0.0, 2.0) : clamp(structure, 0.0, 2.0);',
                '? clamp(reflection, 0.0, 2.0) * (1.0 - saturate(glow)) : clamp(structure, 0.0, 2.0);')),
            ('remaining-reclassified', helper.replace(
                'return remaining * LightBroadFractionV1(edit, mean);',
                'return clamp(mean, min(remaining, 0.0), max(remaining, 0.0));')),
            ('missing-cap', helper.replace(
                '0.0, 4.0);\n}', '0.0, 16.0);\n}')),
        ]
    with tempfile.TemporaryDirectory(prefix='nr-four-region-') as directory:
        for name, variant in variants:
            assert name == 'production' or variant != helper, 'negative control did not mutate production'
            cpp = Path(directory)/(name+'.cpp')
            exe = Path(directory)/(name+('.exe' if os.name=='nt' else ''))
            cpp.write_text(PREFIX+variant+TESTS, encoding='utf-8')
            subprocess.run([args.compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
            run = subprocess.run([str(exe)],capture_output=True,text=True)
            if name == 'production':
                assert run.returncode == 0, run.stdout+run.stderr
                print(run.stdout.strip())
            else:
                assert run.returncode == 23 and 'FOUR_REGION_ORACLE:' in run.stderr, (name,run.returncode,run.stdout,run.stderr)
                print(f'PASS negative control {name}: {run.stderr.strip()}')
    assert 'const bool light_style_pre_supported = pipeline == g_codec.combine_v2 && g_config.mode == 2;' in source
    assert 'const bool light_style_post_supported = pipeline == g_codec.combine;' in source
    assert source.index('const bool light_style_supported') < source.index('if (independent_chroma || low_grid_color) pipeline = g_codec.combine_v2;')
    assert '(light_style_active ? 262144u : 0u)' in source
    assert 'const float lightOriginalEdit = logRatio;' in source
    assert source.index('const float lightOriginalEdit') < source.index('logRatio = StabilizeDetailLogV2(center, logRatio, proxyLuma);')
    assert 'const float radius = 8.0;' in source
    assert 'edit.y == 1.0 && isfinite(edit.x)' in source
    assert 'SampleRestoredEdit(center)' in source
    assert 'offsetof(CodecConstants, light_broad) == 35 * sizeof(UINT)' in source
    assert 'offsetof(CodecConstants, light_preview) == 40 * sizeof(UINT)' in source
    assert 'uint LightPreview;' in source
    print('PASS explicit pre/post route gates, same-grid edit sampling, strict validity and appended constant layout')


if __name__ == '__main__':
    main()
