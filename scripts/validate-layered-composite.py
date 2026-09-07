#!/usr/bin/env python3
"""Compile the scalar helper extracted from the production shader.
Checks numeric direction/budget and real configuration/ABI/capture wiring.
This is not a NVIDIA model, performance or game-quality test.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile
import os

ROOT = Path(__file__).resolve().parents[1]

def definition(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    source = (ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    match = re.search(r'// LAYERED_SCALAR_BEGIN\n(.*?)// LAYERED_SCALAR_END', source, re.S)
    assert match, 'Production layer helper is missing'
    helper = match.group(1)
    prefix = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
static float min(float a,float b){return std::min(a,b);}
static float max(float a,float b){return std::max(a,b);}
static float abs(float a){return std::abs(a);}
static float clamp(float v,float lo,float hi){return std::clamp(v,lo,hi);}
static float saturate(float v){return std::clamp(v,0.f,1.f);}
unsigned checks=0;
void require(bool ok,const char* name){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
'''
    tests = r'''
int main(){
    for(float e:{-8.f,-.5f,-1e-6f,0.f,1e-6f,.5f,8.f})
    for(float mean:{-8.f,-.25f,0.f,.25f,8.f})
    for(float master:{0.f,.5f,1.f,1.2f,2.f,4.f}) {
        const float neutral=LayeredLumaStrengthV3(e,mean,master,1,1,1,1);
        require(neutral==master,"neutral exactly preserves the public master");
        for(float tone:{0.f,.65f,1.f,2.f})
        for(float structure:{0.f,1.f,1.5f,2.f})
        for(float bright:{0.f,.75f,1.f,2.f})
        for(float shadow:{0.f,1.f,2.f}) {
            const float s=LayeredLumaStrengthV3(e,mean,master,tone,structure,bright,shadow);
            require(std::isfinite(s)&&s>=0.f&&s<=4.f,"finite bounded total exponent");
            require(master!=0.f||s==0.f,"master zero cannot be resurrected");
            require(e==0.f||((e*s)*e>=0.f),"no invented opposite-sign edit");
            if(e>0.f) require(s==LayeredLumaStrengthV3(e,mean,master,tone,structure,bright,0.f),"shadow knob cannot affect a positive edit");
            if(e<0.f) require(s==LayeredLumaStrengthV3(e,mean,master,tone,structure,0.f,shadow),"bright knob cannot affect a negative edit");
        }
    }
    require(LayeredLumaStrengthV3(.5f,.25f,2.f,.5f,1.5f,1.f,1.f)==2.f,"independent half-broad fixture");
    require(LayeredLumaStrengthV3(.5f,-.25f,2.f,0.f,1.5f,1.f,1.f)==3.f,"opposite broad mean contributes nothing");
    require(LayeredLumaStrengthV3(.5f,1.f,2.f,.5f,2.f,1.f,1.f)==1.f,"oversized broad mean cannot reverse fine edit");
    require(LayeredLumaStrengthV3(.5f,.25f,4.f,2.f,2.f,2.f,2.f)==4.f,"layers cannot recreate hidden 8x or 16x");
    require(LayeredLumaStrengthV3(.5f,.25f,2.f,1.f,1.f,0.f,2.f)==0.f,"zero brighten removes luminance edit");
    require(LayeredLumaStrengthV3(-.5f,-.25f,2.f,1.f,1.f,0.f,.5f)==1.f,"dark edit retains its own gain");
    for(float e:{-.5f,.5f}) {
        float prev=0.f;
        for(float gain:{0.f,.25f,.5f,1.f,1.5f,2.f}) {
            const float s=LayeredLumaStrengthV3(e,.25f,1.2f,gain,gain,1.f,1.f);
            require(s>=prev,"uniform gain is monotone");prev=s;
        }
    }
    std::printf("production layer scalar: %u checks passed\n",checks);
}
'''
    finite = definition(source, 'float FiniteOr(')
    normalize = definition(source, 'void NormalizeConfig(')
    layer_statements = '\n'.join(line for line in normalize.splitlines() if 'config.layer_' in line)
    assert len(layer_statements.splitlines()) == 4
    norm_code = r'''
struct ChainConfig {float layer_tone,layer_structure,layer_bright,layer_shadow;};
''' + finite + '\nvoid Norm(ChainConfig& config){\n' + layer_statements + '\n}\n' + r'''
int main(){unsigned checks=0;
for(float v:{-1.f,0.f,.65f,1.f,2.f,8.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
ChainConfig c{v,v,v,v};Norm(c);float expected=std::isfinite(v)?std::clamp(v,0.f,2.f):1.f;
if(c.layer_tone!=expected||c.layer_structure!=expected||c.layer_bright!=expected||c.layer_shadow!=expected)return 1;
++checks;
}
std::printf("production layer normalization: %u cases passed\n",checks);}
'''
    with tempfile.TemporaryDirectory(prefix='nr-layer-oracle-') as td:
        for name,code in [('scalar',prefix+helper+tests),('normalization','#include <algorithm>\n#include <cmath>\n#include <cstdio>\n#include <limits>\n#include <initializer_list>\n'+norm_code)]:
            cpp=Path(td)/(name+'.cpp');exe=Path(td)/(name+('.exe' if os.name=='nt' else ''))
            cpp.write_text(code,encoding='utf-8')
            subprocess.run([args.compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    ui=(ROOT/'src/nr_settings_ui.inl').read_text(encoding='utf-8')
    ini=(ROOT/'config/nr_before_sr.ini').read_text(encoding='utf-8')
    stream=(ROOT/'src/nr_f8_stream.inl').read_text(encoding='utf-8')
    history=definition(source,'bool SameHistoryConfig(')
    tuning=definition(source,'NrTuningSignature CurrentNrTuningSignature(')
    config=definition(source,'bool SameConfig(')
    reload=definition(source,'void ReloadConfig(')
    save=definition(source,'bool SaveConfig(')
    shader=source[source.index('const char kCodecShader'):source.index('ID3DBlob *CompileCodecShader')]
    combine=definition(shader,'void CombineV2CS(')
    mode1=definition(shader,'void CombineCS(')
    for field,key in [('layer_tone','LayerTone'),('layer_structure','LayerStructure'),('layer_bright','LayerBright'),('layer_shadow','LayerShadow')]:
        assert field in config and field not in history and field not in tuning
        assert key in reload and key in save and key+'=1.00' in ini
        assert '&next.'+field in ui
        assert 'f8_info.'+field in source and 'info.'+field in source
        assert 'n.'+field in stream and 'c.'+field in stream
        assert field not in mode1
    assert 'layered_enhancement' in config and 'layered_enhancement' not in history
    assert 'LayeredEnhancement=0' in ini and 'bool layered_enhancement = false;' in source
    assert '((v2_combine && g_config.layered_enhancement) ? 2048u : 0u)' in source
    assert 'kCodecRootConstantCount = 35;' in source
    assert 'kCodecSrvCount = 5;' in source and 'kCodecUavCount = 5;' in source
    assert 'layerMean = localMean;' in combine
    assert 'layeredActive && LayerTone != LayerStructure' in combine
    assert 'abs(baseLog), lumaStrength - 1.0, budget)' in combine
    assert 'const bool boostProtectionEnabled = (Passthrough & 1024u) != 0u;' in combine
    assert 'if (boostProtectionEnabled && lumaStrength > 1.0)' in combine
    assert 'TransferStrength * ColorStrength * confidence' in combine
    assert 'FrameConfidence <= 0.000001' in combine
    assert 'Reset only these layers' in ui
    assert 'next.layered_enhancement = false;' in ui
    print('Layer config / Mode2 / independent colour / history / ABI / capture integration passed.')

if __name__=='__main__':main()
