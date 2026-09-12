#!/usr/bin/env python3
"""Compile the production scalar and verify independent chroma budget contracts.

CPU mathematics only. The production SM5 shader and deferred WARP export cover
the actual shader integration; this does not claim NVIDIA or game validation.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    source = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    helper = re.search(r'// INDEPENDENT_CHROMA_SCALAR_BEGIN\n(.*?)// INDEPENDENT_CHROMA_SCALAR_END', source, re.S)
    assert helper, 'Production scalar missing'
    program = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
float min(float a,float b){return std::min(a,b);}
float clamp(float v,float a,float b){return std::clamp(v,a,b);}
float saturate(float v){return clamp(v,0,1);}
unsigned checks=0;
void require(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
''' + helper.group(1) + r'''
int main(){
    for(float r:{-12.f,-1.f,0.f,.1f,6.f})
    for(float g:{-3.f,0.f,.5f,10.f})
    for(float b:{-9.f,-.2f,0.f,2.f})
    for(float strength:{0.f,.25f,1.f,1.5f,2.f})
    for(float confidence:{0.f,.2f,.75f,1.f})
    for(float limit:{0.f,.5f,1.f,2.f,4.f}) {
        float peak=std::max({std::abs(r),std::abs(g),std::abs(b)});
        float scale=IndependentChromaScaleV4(peak,strength,confidence,limit);
        require(std::isfinite(scale)&&scale>=0,"finite nonnegative scale");
        require(scale<=strength*confidence+1e-6f,"protection only attenuates requested edit");
        require(peak*scale<=limit+1e-6f,"one common budget");
        for(float component:{r,g,b}) {
            require(component==0 ? component*scale==0 : component*(component*scale)>=0,"no sign reversal or invented zero component");
        }
        require(strength!=0 || scale==0,"zero colour stays zero");
        require(confidence!=0 || scale==0,"zero confidence stays zero");
        require(peak!=0 || scale==0,"identity has zero edit");
    }
    require(IndependentChromaScaleV4(.2f,1,1,2)==1,"ordinary valid edits retained at 1x");
    require(IndependentChromaScaleV4(4,1,1,2)==.5f,"oversize vector uniformly attenuated");
    require(IndependentChromaScaleV4(.25f,2,1,2)==2,"independent colour amplification remains useful");
    std::printf("production independent chroma scalar: %u checks passed\n",checks);
}
'''
    with tempfile.TemporaryDirectory(prefix='nr-independent-chroma-') as directory:
        cpp = Path(directory) / 'oracle.cpp'
        exe = Path(directory) / 'oracle.exe'
        cpp.write_text(program, encoding='utf-8')
        subprocess.run([args.compiler, '-std=c++20', '-O2', '-static', str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    # Public control must survive persistence/config-generation and real routing;
    # it must never expand the colour policy or the root-constant ABI.
    assert 'bool independent_chroma_composition = false;' in source
    assert 'a.independent_chroma_composition == b.independent_chroma_composition &&' in source
    assert 'L"IndependentChromaComposition", 0, g_ini_path' in source
    assert 'WriteIniInt(L"IndependentChromaComposition", config.independent_chroma_composition ? 1 : 0)' in source
    assert 'if (independent_chroma) pipeline = g_codec.combine_v2;' in source
    assert '(independent_chroma ? 4096u : 0u)' in source
    branch = source.split('if (independentChroma) {', 1)[1].split('} else if (protectBoost)', 1)[0]
    assert 'peak, ColorStrength, confidence, logLimit' in branch
    assert 'TransferStrength *' not in branch
    assert 'logRgbRatio - rawModelLumaLog.xxx' in branch
    assert 'IndependentChromaComposition=0' in (ROOT / 'config/nr_before_sr.ini').read_text()
    print('Production config, default-off, independent gain and candidate route wiring passed')


if __name__ == '__main__':
    main()
