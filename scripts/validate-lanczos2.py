#!/usr/bin/env python3
"""Compile production Lanczos weight mathematics and check its input-only wiring."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',required=True)
    a=ap.parse_args()
    s=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    helper=re.search(r'// LANCZOS2_SCALAR_BEGIN\n(.*?)// LANCZOS2_SCALAR_END',s,re.S)
    assert helper
    program=r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using std::sin;using std::abs;
unsigned checks=0;
void require(bool ok,const char*why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
''' + helper.group(1) + r'''
int main(){
    bool saw_negative=false;
    for(float scale:{.125f,.25f,.5f,.67f,.75f,.85f,1.f}) {
        require(Lanczos2AAWeightV6(0,scale)==1,"centre weight");
        require(abs(Lanczos2AAWeightV6(2.f/scale,scale))<1e-10f,"scaled support endpoint roundoff");
        require(Lanczos2AAWeightV6(2.001f/scale,scale)==0,"outside scaled support cutoff");
        for(int i=-500;i<=500;++i){
            float d=i/50.f,w=Lanczos2AAWeightV6(d,scale);
            require(std::isfinite(w),"finite weight");
            require(w==Lanczos2AAWeightV6(-d,scale),"symmetric kernel");
            saw_negative|=w<-1e-6f;
        }
    }
    require(saw_negative,"signed lobes retained");
    require(Lanczos2AAWeightV6(2.5f,.5f)!=0,"AA support expands beyond fixed radius two");
    for(int rw:{13,31,64,129})for(float percent:{.5f,.67f,.75f,.85f,1.f}){
        int nw=std::max(1,int(std::round(rw*percent)));float scale=float(nw)/rw;
        for(int x=0;x<nw;++x){
            float c=(x+.5f)/scale-.5f,weights=0,total=0,l1=0;
            for(int p=int(std::ceil(c-2/scale));p<=int(std::floor(c+2/scale));++p){
                float w=Lanczos2AAWeightV6(p-c,scale);int mapped=std::clamp(p,0,rw-1);
                require(mapped>=0&&mapped<rw,"active rectangle clamp");
                weights+=w;total+=.375f*w;l1+=abs(w);
            }
            require(weights>1e-6f,"non-singular finite support normalization");
            require(abs(total/weights-.375f)<2e-6f,"constant preservation across phases and clamped edges");
            require(l1/weights<4.f,"signed FP16 four-times storage margin on sweep");
        }
    }
    std::printf("production Lanczos2 scalar: %u checks passed\n",checks);
}
'''
    with tempfile.TemporaryDirectory(prefix='nr-lanczos2-') as tmp:
        src=Path(tmp)/'oracle.cpp';exe=Path(tmp)/'oracle.exe';src.write_text(program)
        subprocess.run([a.compiler,'-std=c++20','-O2','-static',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    assert 'unsigned int nr_input_filter = 0;' in s
    assert 'a.nr_input_filter == b.nr_input_filter &&' in s
    assert 'a.input_filter == b.input_filter' in s
    assert 'if (mode != 2) requested_tuning.input_filter = 0;' in s
    assert 'context->reset_retry_pending = true;' in s
    assert 'rw>8ull*nw || rh>8ull*nh || (nw==rw && nh==rh)' in s
    assert 'FitsExtraResourcePeak(slot, allocation.SizeInBytes)' in s
    assert 'new_bytes <= kExtraTotalBudget - g_extra_allocated_bytes' in s
    horizontal=s.split('void LanczosHorizontalCS(',1)[1].split('void LanczosVerticalCS(',1)[0]
    assert 'saturate(' not in horizontal and 'NeuralColor' not in horizontal
    assert '2.0/scale' in horizontal and 'SourceHeight' in horizontal
    assert 'WriteLanczosDescriptors(device,slot)' in s
    assert 'NRInputFilter=0' in (ROOT/'config/nr_before_sr.ini').read_text()
    print('PASS default Area, scale support, signed intermediate, bounded allocation and history/fallback wiring')


if __name__=='__main__':main()
