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
 float color_strength=1.f,colour_priority_strength=.7f,colour_conservative_strength=1.f;
 unsigned master_virtual_key=117,image_virtual_key=118,feedback_virtual_key=119;
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
 Reset(c);assert(Policy(c)==1 && c.color_strength==1.f && c.colour_priority_strength==.7f);
 using namespace nr_hotkeys;
 assert(!Set(c,Master,118));assert(c.master_virtual_key==117);
 assert(Set(c,Image,0));assert(Set(c,Master,118));assert(Set(c,Image,117));
 Publish(c);assert(Key(Master)==118 && Key(Image)==117 && Key(Report)==119);
 assert(!Set(c,Report,65));
 c.feedback_virtual_key=118;Normalize(c);assert(c.feedback_virtual_key==0);
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
        start=core.index('    next.master_virtual_key=')
        key_load=core[start:core.index('    next.enabled =',start)]
        start=core.index('    record_light(WriteIniFloat(L"ColorStrength"')
        last='    record_write(WriteIniInt(L"FeedbackVirtualKey",config.feedback_virtual_key));'
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
 auto c=load();assert(c.color_strength==1.f && c.colour_lab_mode==2);
 put(L"AllowUnverifiedHdrColor",L"0");put(L"ColorStrength",L"1.0");c=load();
 assert(c.colour_lab_mode==0 && c.color_strength==1.f && nr_colour_preferences::Policy(c)==1);
 nr_colour_preferences::Select(c,0);assert(c.color_strength==.7f);
 c.color_strength=.6f;nr_colour_preferences::Remember(c);
 assert(nr_hotkeys::Set(c,nr_hotkeys::Master,120));save(c);c=load();
 assert(c.color_strength==.6f && c.master_virtual_key==120);
 nr_colour_preferences::Select(c,1);assert(c.color_strength==1.f);
 c.color_strength=.2f;nr_colour_preferences::Remember(c);save(c);c=load();
 assert(c.color_strength==.2f);nr_colour_preferences::Select(c,0);assert(c.color_strength==.6f);
}
'''
        self.run_cpp(code)

    def test_final_wiring(self):
        if GENERATED is None:self.skipTest('complete export checked separately')
        core=(GENERATED/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        for key in ('MasterVirtualKey','ImageVirtualKey','FeedbackVirtualKey','ColourPriorityStrength','ColourConservativeStrength'):
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
        self.assertIn('ColorStrength=1.0',ini)
        self.assertIn('ColourPriorityStrength=0.7',ini)
        self.assertIn('ColourLabMode=2',ini)
        self.assertIn('nr_colour_preferences::Reset(next)',(GENERATED/'src/nr_recommended_picture.inl').read_text())

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--repo');p.add_argument('--generated',type=Path)
    args,rest=p.parse_known_args();GENERATED=args.generated
    unittest.main(argv=['test_user_controls.py']+rest)
