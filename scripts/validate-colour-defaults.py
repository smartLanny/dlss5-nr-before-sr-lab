"""Production defaults and primary-model reset; host tests, not a game test."""
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
    assert 'next.intensity = ReadIniFloat(L"Intensity", 1.0f);' in source
    assert 'WriteIniFloat(L"ColorStrength", config.color_strength)' in source
    assert 'WriteIniFloat(L"Intensity", config.intensity)' in source
    assert re.search(r'^ColorStrength=1\.0$',ini,re.M)
    assert re.search(r'^Intensity=1\.0$',ini,re.M)
    assert 'AllowUnverifiedHdrColor=0' in ini
    model=definition(ui,'        if (next.intensity != 1.0f) {')
    advanced_start=ui.index('    SettingsSection(ui("Advanced picture controls"')
    advanced_end=ui.index('        if (ImGui::CollapsingHeader(ui("Developer options"',advanced_start)
    advanced=ui[advanced_start:advanced_end]
    assert advanced.count('DrawColorControls(next, has_pending, changed);')==1
    assert ui.count('DrawColorControls(next, has_pending, changed);')==1
    assert '"HDR and colour"' not in ui
    assert ui.index(model)<ui.index(advanced)
    assert 'next.allow_unverified_hdr_color' not in model
    assert 'next.enabled' not in model
    body=definition(source,'struct ChainConfig {')+';'
    code=r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
enum class NrJitterMode { Legacy=0,GameInputPixel=1 };
int checks=0,failures=0;bool click=false;int buttons=0;
void check(bool ok,const char*name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
namespace ImGui {
void TextDisabled(const char*,const char*){}
bool Button(const char*){++buttons;return click;}
}
'''+body+'\n'+definition(source,'float FiniteOr(')+'\n'+definition(source,'void NormalizeConfig(')+'\n'+restore+r'''
void Controls(ChainConfig &next,bool zh,bool &changed,bool &apply_discrete_edit){
 auto ui=[zh](const char*en,const char*cn){return zh?cn:en;};
MODEL
}
int main(){
 ChainConfig d;
 check(d.color_strength==1.f&&d.intensity==1.f,"aggregate defaults one");
 for(bool zh:{false,true})for(bool enabled:{false,true})for(float saved:{0.f,.25f,1.f,2.f})for(bool pressed:{false,true}){
  ChainConfig c;c.intensity=saved;c.enabled=enabled;c.color_strength=.37f;c.mode=1;
  c.depth_mode=1;c.r8_output_encoding=2;c.allow_unverified_hdr_color=true;
  bool changed=false,discrete=false;click=pressed;buttons=0;
  Controls(c,zh,changed,discrete);
  const bool did=pressed&&saved!=1.f;
  check(c.intensity==(did?1.f:saved),"reset changes only on explicit click");
  check(changed==did&&discrete==did,"ordinary atomic settings transaction");
  check(c.enabled==enabled&&c.mode==1&&c.color_strength==.37f&&c.allow_unverified_hdr_color&&c.depth_mode==1&&c.r8_output_encoding==2,"reset preserves unrelated user state");
  check(buttons==(saved!=1.f?1:0),"no redundant reset control for one");
  NormalizeConfig(c);
  check(c.intensity==(did?1.f:saved)&&c.color_strength==.37f,"normalization retains valid saved choices");
 }
 for(bool enabled:{false,true}){
  ChainConfig c;c.enabled=enabled;c.intensity=2.f;c.color_strength=.37f;
  RestoreRecommendedPictureSettings(c);NormalizeConfig(c);
  check(c.intensity==1.f&&c.color_strength==1.f,"one click recommendation one defaults");
  check(c.enabled==enabled&&!c.allow_unverified_hdr_color,"recommendation never unlocks safety or master");
 }
 std::printf("production colour defaults/reset: %d checks, %d failures\n",checks,failures);
 return failures?1:0;
}
'''.replace('MODEL',model)
    with tempfile.TemporaryDirectory(prefix='nr-defaults-') as td:
        s=Path(td)/'test.cpp';e=Path(td)/('test.exe' if os.name=='nt' else 'test')
        s.write_text(code,encoding='utf-8')
        subprocess.run([a.compiler,'-std=c++20','-Wall','-Wextra','-Werror',*(['-static'] if os.name=='nt' else []),str(s),'-o',str(e)],check=True)
        subprocess.run([str(e)],check=True)
    print('PASS current loader, save and advanced-colour control wiring')
if __name__=='__main__':main()
