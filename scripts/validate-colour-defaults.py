"""Production defaults and independent-layer reset; host tests, not a game test."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import os

def definition(text, marker):
    a=text.index(marker); i=text.index('{',a)+1; depth=1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[a:i]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',default=os.environ.get('CXX','g++'))
    ap.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1],
                    help='Actual source tree, including a generated candidate core')
    a=ap.parse_args();root=a.root.resolve()
    source=(root/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    ui=(root/'src/nr_settings_ui.inl').read_text(encoding='utf-8-sig')
    restore=(root/'src/nr_recommended_picture.inl').read_text(encoding='utf-8-sig')
    ini=(root/'config/nr_before_sr.ini').read_text(encoding='utf-8-sig')
    # Verify real loader/default/persistence wiring; a standalone default-only
    # mock cannot detect a different fallback in the actual INI loader.
    assert 'next.color_strength = ReadIniFloat(L"ColorStrength", 1.0f);' in source
    assert 'next.intensity = ReadIniFloat(L"Intensity", 1.2f);' in source
    assert 'next.skin_structure = ReadIniFloat(L"SkinStructureStrength", 0.4f);' in source
    assert 'L"NRBeforeSR", L"AutoMask", 1, g_ini_path) != 0' in source
    assert 'WriteIniFloat(L"ColorStrength", config.color_strength)' in source
    assert 'WriteIniFloat(L"Intensity", config.intensity)' in source
    assert re.search(r'^ColorStrength=1\.0$',ini,re.M)
    assert re.search(r'^Intensity=1\.2$',ini,re.M)
    assert 'AllowUnverifiedHdrColor=0' in ini
    layers=(root/'src/nr_layers_ui.inl').read_text(encoding='utf-8-sig')
    model=definition(layers,'            if(ImGui::Button(zh?"重置本层":"Reset this layer"))')
    commit=definition(layers,'            if(edited) {')
    assert ui.count('DrawColorControls(next, has_pending, changed);')==1
    assert '"HDR and colour"' not in ui
    assert ui.index('DrawUniformLayerControls(next,changed,apply_discrete_edit);')<ui.index('DrawColorControls(next, has_pending, changed);')
    assert 'next.allow_unverified_hdr_color' not in model+commit
    assert 'next.enabled' not in model+commit
    body=definition(source,'struct ChainConfig {')+';'
    code=r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "nr_processing_route.h"
#include "nr_dual_pass.hpp"
#include "nr_layer_config.h"
#include "nr_colour_lab_policy.h"
#include "nr_colour_restore_policy.h"
#include "nr_colour_edge_policy.h"
#include "nr_generic_residual.h"
#include "nr_light_style.h"
enum class NrJitterMode { Legacy=0,GameInputPixel=1 };
int checks=0,failures=0;bool click=false;int buttons=0;
void check(bool ok,const char*name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
namespace ImGui {
void TextDisabled(const char*,const char*){}
bool Button(const char*){++buttons;return click;}
}
'''+body+'\n'+definition(source,'float FiniteOr(')+'\n'+definition(source,'void NormalizeConfig(')+'\n'+restore+r'''
void Controls(ChainConfig &next,unsigned i,bool zh,bool &changed,bool &discrete){
 auto model=nr_layers::Read(next,i);bool edited=false;
MODEL
COMMIT
}
int main(){
 ChainConfig d;
 check(d.color_strength==1.f&&d.intensity==1.2f,"aggregate defaults match production");
 for(unsigned i=0;i<nr_layers::Count;++i){const auto m=nr_layers::Read(d,i);
  check(m.auto_mask&&m.skin_structure==.4f&&m.ui_correction,"all native layer protection defaults agree");}
 check(!d.face_protection,"independent face backend remains opt-in");
 for(bool zh:{false,true})for(bool enabled:{false,true})for(unsigned layer=0;layer<nr_layers::Count;++layer)for(float saved:{0.f,.25f,1.f,1.2f,2.f})for(bool pressed:{false,true}){
  ChainConfig c;c.intensity=saved;c.enabled=enabled;c.color_strength=.37f;c.mode=1;
  c.depth_mode=1;c.r8_output_encoding=2;c.allow_unverified_hdr_color=true;
  c.transfer_strength=2.3f;c.post_transfer_strength=2.4f;c.custom_work_scale=.75f;
  for(auto& m:c.nr_layers){m.intensity=saved;m.local_tone=.4f;m.enabled=true;m.initialized=true;}
  c.local_tone=.4f;const auto before=c;
  bool changed=false,discrete=false;click=pressed;buttons=0;
  Controls(c,layer,zh,changed,discrete);
  check(nr_layers::Read(c,layer)==(pressed?nr_layers::Model{}:nr_layers::Read(before,layer)),"reset affects all controls of selected layer only on click");
  check(changed==pressed&&discrete==pressed,"ordinary atomic settings transaction");
  check(c.enabled==enabled&&c.mode==1&&c.color_strength==.37f&&c.allow_unverified_hdr_color&&c.depth_mode==1&&c.r8_output_encoding==2,"reset preserves unrelated user state");
  check(c.transfer_strength==2.3f&&c.post_transfer_strength==2.4f&&c.custom_work_scale==.75f,"layer reset preserves final composition and work scale");
  for(unsigned other=0;other<nr_layers::Count;++other)if(other!=layer)
   check(nr_layers::Read(c,other)==nr_layers::Read(before,other),"other independent layers are untouched");
  for(unsigned i=0;i<c.nr_layers.size();++i)check(c.nr_layers[i].enabled==before.nr_layers[i].enabled,"reset preserves enabled layers");
  check(buttons==1,"single reset entry per layer");
  NormalizeConfig(c);
  check(nr_layers::Read(c,layer).intensity==(pressed?1.2f:saved)&&c.color_strength==.37f,"normalization retains valid saved choices");
 }
 for(bool enabled:{false,true}){
  ChainConfig c;c.enabled=enabled;c.intensity=2.f;c.color_strength=.37f;
  RestoreRecommendedPictureSettings(c);NormalizeConfig(c);
  check(c.intensity==1.2f&&c.color_strength==1.f,"one click recommendation restores defaults");
  check(c.enabled==enabled&&!c.allow_unverified_hdr_color,"recommendation never unlocks safety or master");
 }
 std::printf("production colour defaults/reset: %d checks, %d failures\n",checks,failures);
 return failures?1:0;
}
'''.replace('MODEL',model).replace('COMMIT',commit)
    with tempfile.TemporaryDirectory(prefix='nr-defaults-') as td:
        s=Path(td)/'test.cpp';e=Path(td)/('test.exe' if os.name=='nt' else 'test')
        s.write_text(code,encoding='utf-8')
        subprocess.run([a.compiler,'-std=c++20','-Wall','-Wextra','-Werror',
                        '-I',str(root/'src'),*(['-static'] if os.name=='nt' else []),
                        str(s),'-o',str(e)],check=True)
        subprocess.run([str(e)],check=True)
    print('PASS current loader, save and picture-control wiring')
if __name__=='__main__':main()
