"""Host behaviour and final export contracts. No game acceptance is inferred."""
import argparse
import importlib.util
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
GENERATED=None
CPP=r'''
#include <cassert>
#include <cmath>
#include "nr_colour_preferences.h"
#include "nr_hotkeys.h"
struct Config {
 int colour_lab_mode=nr_colour_lab::kDefaultMode; bool allow_unverified_hdr_color=false;
 bool colour_range_control=false;
 float color_strength=1.f,colour_priority_strength=.7f,colour_conservative_strength=1.f;
 unsigned master_virtual_key=117,image_virtual_key=118,feedback_virtual_key=119,screenshot_virtual_key=189;
 unsigned spatial_experiment_mode=0;bool private_residual_sr=false;
 unsigned temporal_backend=0,temporal_scope=0,temporal_mode=0;bool temporal_legacy=false;
};
int main(){
 using namespace nr_colour_preferences;
 const float missing=std::nanf("");
 Config c;Load(c,missing,missing,missing);assert(c.color_strength==1.f && Policy(c)==1);
 Select(c,1);assert(c.color_strength==1.f && !nr_colour_lab::AllowUnverified(c.colour_lab_mode,false));
 c.color_strength=.3f;Select(c,0);assert(c.color_strength==.7f);
 c.color_strength=.7f;Select(c,1);assert(c.color_strength==.3f);
 Select(c,0);assert(c.color_strength==.7f);
 for(int mode=0;mode<3;++mode)for(bool permission:{false,true}){
   Config old;old.colour_lab_mode=mode;old.allow_unverified_hdr_color=permission;
   for(float saved:{0.f,.4f,.5f,.7f,1.f,1.7f}){
     Load(old,saved,missing,missing);assert(old.color_strength==saved);
     const auto active=Policy(old);Select(old,1-active);Select(old,active);assert(old.color_strength==saved);
     Config reloaded=old;
     Load(reloaded,0.f,old.colour_priority_strength,old.colour_conservative_strength);
     assert(reloaded.color_strength==saved);
   }
 }
 // Permission and amount remain independent: conservative allows verified
 // colour, while unverified HDR is blocked even with a request of one.
 assert(nr_colour_lab::AllowHdrColour(2,false,true));
 assert(!nr_colour_lab::AllowHdrColour(2,false,false));
 assert(nr_colour_lab::AllowHdrColour(1,false,false));
 c.colour_lab_mode=2;c.color_strength=0;Remember(c);
 assert(c.colour_lab_mode==2 && c.colour_conservative_strength==0);
 Reset(c);assert(Policy(c)==0 && c.color_strength==.5f && c.colour_range_control);
 for(float v:{0.f,.5f,.7f,1.f}){SetRange(c,v);assert(c.color_strength==v&&c.colour_lab_mode==1);}
 Load(c,1.f,missing,missing);LoadRange(c,missing,true);assert(!c.colour_range_control&&c.color_strength==1.f);
 LoadRange(c,missing,false);assert(c.colour_range_control&&c.color_strength==.5f);
 LoadRange(c,2.f,true);assert(c.color_strength==1.f);
 using namespace nr_hotkeys;
 assert(!Set(c,Master,118));assert(c.master_virtual_key==117);
 assert(Set(c,Image,0));assert(Set(c,Master,118));assert(Set(c,Image,117));
 Publish(c);assert(Key(Master)==118 && Key(Image)==117 && Key(Report)==119);
 assert(!Set(c,Report,65));
 c.feedback_virtual_key=118;Normalize(c);assert(c.feedback_virtual_key==0);
 Config keys;
 for(const auto& choice:Choices){
   assert(Valid(choice.key));assert(Choices[Index(choice.key)].key==choice.key);
   assert(Name(choice.key)[0]&&Name(choice.key,true)[0]);
 }
 assert(!Set(keys,Master,189)&&!Set(keys,Report,109));
 assert(Overlaps(Master,36,36)&&Overlaps(Screenshot,189,109));
 assert(!Overlaps(Screenshot,0,109)&&!Overlaps(Master,36,0));
 assert(!Set(keys,static_cast<Action>(Count),36));
 assert(Set(keys,Master,36)&&Set(keys,Image,35)&&Set(keys,Report,145)&&Set(keys,Screenshot,19));
 Normalize(keys);Publish(keys);
 assert(Key(Master)==36&&Key(Image)==35&&Key(Report)==145&&Key(Screenshot)==19);
 assert(!Set(keys,Screenshot,36));
 keys.image_virtual_key=36;keys.feedback_virtual_key=999;Normalize(keys);
 assert(keys.master_virtual_key==36&&keys.image_virtual_key==0&&keys.feedback_virtual_key==0&&keys.screenshot_virtual_key==19);
 for(unsigned preset=0;preset<4;++preset){
   assert(ApplyPreset(keys,preset));assert(PresetIndex(keys)==preset+1);
   const auto before=Pack(keys.master_virtual_key,keys.image_virtual_key,keys.feedback_virtual_key,keys.screenshot_virtual_key);
   Normalize(keys);Publish(keys);assert(published.load()==before);
 }
 assert(Key(Master)==0&&Key(Image)==0&&Key(Report)==0&&Key(Screenshot)==0);
 assert(!ApplyPreset(keys,99));
 unsigned reads=0;assert(State(0,Screenshot,[&](unsigned){++reads;return short(0x8001);})==0&&reads==0);
 Edge e;assert(!e.Press(117,0));assert(e.Press(117,short(0x8001)));
 for(int i=0;i<100;++i)assert(!e.Press(117,short(0x8001)));
 assert(!e.Press(117,0));assert(e.Press(117,1));
 assert(!e.Press(118,short(0x8001))); // rebinding while held
 assert(!e.Press(118,short(0x8000)));assert(!e.Press(118,0));
 assert(e.Press(118,short(0x8001)));assert(!e.Press(0,1));assert(!e.Press(0,1));
}
'''

class UserControls(unittest.TestCase):
    def run_cpp(self,code):
        compiler=os.environ.get('DLSS5_TEST_CXX') or str(Path(os.environ.get('NR_LLVM_BIN',str(Path(os.environ.get('DLSS5_LLVM_MINGW_ROOT',''))/'bin')))/'x86_64-w64-mingw32-clang++.exe')
        if not Path(compiler).is_file():compiler=shutil.which('clang++') or compiler
        if not Path(compiler).is_file():self.skipTest('Windows host compiler unavailable')
        root=GENERATED or ROOT
        with tempfile.TemporaryDirectory(prefix='nr-user-controls-') as td:
            cpp=Path(td)/'test.cpp';exe=Path(td)/'test.exe';cpp.write_text(code,encoding='utf-8')
            subprocess.run([compiler,'-std=c++17','-O2','-static','-Wall','-Wextra','-Werror','-I',str(root/'src'),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=td)

    def test_behaviour(self):self.run_cpp(CPP)

    def test_actual_reshade_conflict_notice(self):
        if GENERATED is None:self.skipTest('complete export checked separately')
        spec=importlib.util.spec_from_file_location('hotkey_fixture',ROOT/'scripts/validate-feedback-hotkeys.py')
        helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
        source=(GENERATED/'src/nr_unified_pages.inl').read_text(encoding='utf-8')
        notice=helper.function(source,'void DrawKnownReShadeHotkeyConflicts(')
        code=CPP[:CPP.index('int main()')]+r'''
#include <cstring>
using ChainConfig=Config;
unsigned reads=0,notices=0;
namespace reshade::api {struct effect_runtime{};}
namespace reshade {
bool get_config_value(api::effect_runtime*,const char*,const char* name,unsigned& key){
 ++reads;if(std::strcmp(name,"KeyOverlay")==0){key=36;return true;}
 if(std::strcmp(name,"KeyScreenshot")==0){key=109;return true;}return false;
}}
namespace ImGui {void TextWrapped(const char*,...){++notices;}}
const char* NrUi(const char* en,const char*){return en;}
bool UseChineseUi(){return false;}
'''+notice+r'''
int main(){Config c;c.master_virtual_key=36;
 DrawKnownReShadeHotkeyConflicts(nullptr,c);assert(reads==0&&notices==0);
 reshade::api::effect_runtime runtime;DrawKnownReShadeHotkeyConflicts(&runtime,c);
 assert(reads==8&&notices==2);assert(c.master_virtual_key==36&&c.screenshot_virtual_key==189);
 nr_hotkeys::ApplyPreset(c,3);DrawKnownReShadeHotkeyConflicts(&runtime,c);assert(notices==2);
}
'''
        self.run_cpp(code)

    def test_actual_screenshot_dispatch(self):
        if GENERATED is None:self.skipTest('complete export checked separately')
        core=(GENERATED/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        start=core.index('    const auto screenshot_key=nr_hotkeys::Key(')
        poll=core[start:core.index('    if (screenshot_pressed &&',start)]
        code=r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include "nr_hotkeys.h"
SHORT key_state[256]{};
SHORT TestKey(int key){return key_state[key];}
#define GetAsyncKeyState TestKey
bool Poll(){static nr_hotkeys::Edge screenshot_edge;
 const auto shortcut_bindings=nr_hotkeys::published.load();
'''+poll+r'''
 return screenshot_pressed;}
int main(){
 assert(!Poll());key_state[VK_OEM_MINUS]=SHORT(0x8001);assert(Poll());
 for(unsigned i=0;i<100;++i)assert(!Poll());
 key_state[VK_OEM_MINUS]=0;assert(!Poll());
 key_state[VK_SUBTRACT]=SHORT(0x8001);assert(Poll());
 for(unsigned i=0;i<100;++i)assert(!Poll());
 nr_hotkeys::published=nr_hotkeys::Pack(0,0,0,0);
 key_state[VK_OEM_MINUS]=SHORT(0x8001);for(unsigned i=0;i<100;++i)assert(!Poll());
 nr_hotkeys::published=nr_hotkeys::Pack(117,118,119,VK_END);
 key_state[VK_END]=SHORT(0x8001);assert(!Poll()); // Rebinding while held must not fire.
 key_state[VK_END]=0;assert(!Poll());key_state[VK_END]=SHORT(0x8001);assert(Poll());
 for(unsigned i=0;i<100;++i)assert(!Poll());
 key_state[VK_END]=0;assert(!Poll());key_state[VK_END]=1;assert(Poll());
}
'''
        self.run_cpp(code)

    def test_actual_remapped_dispatch(self):
        if GENERATED is None:self.skipTest('complete export checked separately')
        spec=importlib.util.spec_from_file_location('hotkey_fixture',ROOT/'scripts/validate-feedback-hotkeys.py')
        helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
        source=(GENERATED/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        present=helper.function(source,'void OnReShadePresent(')
        a=present.index('    const auto shortcut_bindings=')
        b=present.index('    // Core feedback has no GPU-stage request',a)
        f8=present[a:b]
        a=present.index('    const auto image_key=')
        b=present.index('    nr_core_feedback::Present(runtime);',a)
        f7=present[a:b]
        sequence=(GENERATED/'src/nr_f8_sequence.inl').read_text(encoding='utf-8')
        page='\n'.join(helper.function(sequence,s) for s in (
            'void RequestSettingsFeedbackPage(','bool TakeSettingsFeedbackPageRequest(','void OpenWindow('))
        code=helper.HARNESS[:helper.HARNESS.index('int main()')]
        code=code.replace('#include "nr_shortcut_edge.h"','#include "nr_hotkeys.h"')
        code=code.replace('    static bool feedback_key_was_down = false;','')
        code=code.replace('    static bool stage_key_was_down = false;','    static nr_hotkeys::Edge image_edge;')
        code=code.replace('F8_BLOCK',f8).replace('F7_BLOCK',f7).replace('PAGE_FUNCTIONS',page)
        # Compile the actual generated F6 edge dispatcher as well.
        a=source.index('    static nr_hotkeys::Edge master_edge;')
        b=source.index('    UpdateEvaluateHookActivation();',a)
        code+='''\nunsigned toggles=0;ULONGLONG clock_tick=1000;
void ToggleMasterFromHotkey(){++toggles;}
ULONGLONG TestTick(){return clock_tick;}
#define GetTickCount64 TestTick
void Master(){\n'''+source[a:b]+'''\n}
int main(){
 reshade::api::effect_runtime runtime;
 nr_hotkeys::published=nr_hotkeys::Pack(VK_F9,VK_F10,VK_F11);
 Dispatch(&runtime,1000);Master(); // Arm released bindings.
 key_state[VK_F6]=key_state[VK_F7]=key_state[VK_F8]=SHORT(0x8001);
 Dispatch(&runtime,1100);Master();
 Require(!toggles&&!nr_f8::current_requests&&!nr_f8::core_requests,"old keys remained active");
 key_state[VK_F9]=key_state[VK_F10]=key_state[VK_F11]=SHORT(0x8001);
 Dispatch(&runtime,2000);Master();
 Require(toggles==1&&nr_f8::current_requests==1&&nr_f8::core_requests==1,"remapped action missing");
 for(unsigned i=0;i<100;++i){Dispatch(&runtime,2001+i);Master();}
 Require(toggles==1&&nr_f8::current_requests==1&&nr_f8::core_requests==1,"held keys repeated");
 nr_hotkeys::published=nr_hotkeys::Pack(VK_F6,0,VK_F8); // Both new keys currently held.
 clock_tick=3000;Dispatch(&runtime,3000);Master();
 Require(toggles==1&&nr_f8::core_requests==1,"rebinding fired a held key");
 key_state[VK_F6]=key_state[VK_F8]=0;Dispatch(&runtime,3100);Master();
 key_state[VK_F6]=key_state[VK_F8]=SHORT(0x8001);clock_tick=4000;Dispatch(&runtime,4000);Master();
 Require(toggles==2&&nr_f8::core_requests==2&&nr_f8::current_requests==1,"released remapped keys failed");
}
'''
        self.run_cpp(code)

    def test_actual_ini_load_save(self):
        if GENERATED is None:self.skipTest('complete export checked separately')
        core=(GENERATED/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        start=core.index('    wchar_t saved_mode[32]{}')
        end=core.index('    next.light_preset_color',start)
        colour_load=core[start:end]
        start=core.index('    next.temporal_backend=') if '    next.temporal_backend=' in core else core.index('    next.master_virtual_key=')
        key_load=core[start:core.index('    next.enabled =',start)]
        start=core.index('    record_light(WriteIniFloat(L"ColorStrength"')
        last='    record_write(WriteIniInt(L"ScreenshotVirtualKey",config.screenshot_virtual_key));'
        saved=core[start:core.index(last,start)+len(last)]
        # Run the generated loader and writes against real Win32 INI storage,
        # including a fresh load after mode changes and partial old settings.
        code=CPP[:CPP.index('int main()')]+r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
const wchar_t* g_ini_path=L".\\user.ini";
void put(const wchar_t* key,const wchar_t* value){assert(WritePrivateProfileStringW(L"NRBeforeSR",key,value,g_ini_path));}
bool WriteIniFloat(const wchar_t* key,float value){wchar_t s[64];swprintf(s,64,L"%.9g",double(value));put(key,s);return true;}
bool WriteIniInt(const wchar_t* key,unsigned value){wchar_t s[64];swprintf(s,64,L"%u",value);put(key,s);return true;}
float ReadIniFloat(const wchar_t* key,float fallback){wchar_t s[64]{};GetPrivateProfileStringW(L"NRBeforeSR",key,L"",s,64,g_ini_path);return s[0]?float(wcstod(s,nullptr)):fallback;}
Config load(){Config next;next.colour_lab_mode=GetPrivateProfileIntW(L"NRBeforeSR",L"ColourLabMode",nr_colour_lab::kDefaultMode,g_ini_path);
next.allow_unverified_hdr_color=GetPrivateProfileIntW(L"NRBeforeSR",L"AllowUnverifiedHdrColor",0,g_ini_path)!=0;
'''+key_load+colour_load+r'''
nr_hotkeys::Normalize(next);return next;}
void save(const Config& config){auto record_light=[](bool ok){assert(ok);};auto record_write=record_light;
'''+saved+r'''
WriteIniInt(L"ColourLabMode",config.colour_lab_mode);}
int main(){
 auto c=load();assert(c.color_strength==.5f && c.colour_lab_mode==1 && c.colour_range_control);
 assert(c.temporal_backend==0&&c.temporal_scope==0&&c.temporal_mode==0&&!c.temporal_legacy);
 put(L"AllowUnverifiedHdrColor",L"0");put(L"ColorStrength",L"1.0");c=load();
 assert(c.colour_lab_mode==0 && c.color_strength==1.f && nr_colour_preferences::Policy(c)==1);
 nr_colour_preferences::Select(c,0);assert(c.color_strength==.7f);
 c.color_strength=.6f;nr_colour_preferences::Remember(c);
 assert(nr_hotkeys::Set(c,nr_hotkeys::Master,120));save(c);c=load();
 assert(c.color_strength==.6f && c.master_virtual_key==120);
 assert(nr_hotkeys::Set(c,nr_hotkeys::Master,36));assert(nr_hotkeys::Set(c,nr_hotkeys::Image,145));
 assert(nr_hotkeys::Set(c,nr_hotkeys::Screenshot,0));save(c);c=load();
 assert(c.master_virtual_key==36&&c.image_virtual_key==145&&c.screenshot_virtual_key==0);
 nr_colour_preferences::Select(c,1);assert(c.color_strength==1.f);
 c.color_strength=.2f;nr_colour_preferences::Remember(c);save(c);c=load();
 assert(c.color_strength==.2f);nr_colour_preferences::Select(c,0);assert(c.color_strength==.6f);
 c.temporal_backend=2;c.temporal_scope=1;c.temporal_mode=1;c.temporal_legacy=true;save(c);c=load();
 assert(c.temporal_backend==2&&c.temporal_scope==1&&c.temporal_mode==1&&c.temporal_legacy);
 nr_colour_preferences::SetRange(c,.5f);save(c);put(L"AllowUnverifiedHdrColor",L"0");put(L"ColourLabMode",L"2");c=load();
 assert(c.colour_range_control&&c.colour_lab_mode==1&&c.color_strength==.5f);
 nr_colour_preferences::SetRange(c,0.f);save(c);c=load();assert(c.color_strength==0.f&&c.colour_lab_mode==1);
}
'''
        self.run_cpp(code)

    def test_final_wiring(self):
        if GENERATED is None:self.skipTest('complete export checked separately')
        core=(GENERATED/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        for key in ('MasterVirtualKey','ImageVirtualKey','FeedbackVirtualKey','ScreenshotVirtualKey','ColourPriorityStrength','ColourConservativeStrength'):
            self.assertIn('L"'+key+'"',core)
        self.assertEqual(core.count('nr_hotkeys::Publish(g_config);'),2)
        self.assertIn('nr_colour_preferences::Load(next,',core)
        self.assertIn('ReadIniFloat(L"ColorStrength",std::nanf(""))',core)
        self.assertNotIn('GetAsyncKeyState(VK_F6)',core)
        self.assertNotIn('GetAsyncKeyState(VK_F7)',core)
        self.assertNotIn('feedback_key_loaded',core)
        for action in ('master_edge.Press','image_edge.Press','report_edge.Press'):self.assertIn(action,core)
        s=(GENERATED/'src/nr_f8_sequence.inl').read_text(encoding='utf-8')
        self.assertNotIn('Save shortcut',s)
        ini=(GENERATED/'config/nr_before_sr.ini').read_text(encoding='utf-8')
        self.assertIn('AIColorRange=0.5',ini)
        self.assertIn('ColorStrength=0.5',ini)
        self.assertIn('ColourLabMode=1',ini)
        self.assertIn('nr_colour_preferences::Reset(next)',(GENERATED/'src/nr_recommended_picture.inl').read_text())

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--repo');p.add_argument('--generated',type=Path)
    args,rest=p.parse_known_args();GENERATED=args.generated
    unittest.main(argv=['test_user_controls.py']+rest)
