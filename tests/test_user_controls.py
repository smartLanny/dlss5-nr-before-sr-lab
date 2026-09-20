"""Host behaviour and final export contracts. No game acceptance is inferred."""
import argparse
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
 int colour_lab_mode=1; bool allow_unverified_hdr_color=false;
 float color_strength=.5f,colour_priority_strength=.5f,colour_conservative_strength=1.f;
 unsigned master_virtual_key=117,image_virtual_key=118,feedback_virtual_key=119;
};
int main(){
 using namespace nr_colour_preferences;
 const float missing=std::nanf("");
 Config c;Load(c,missing,missing,missing);assert(c.color_strength==.5f);
 Select(c,1);assert(c.color_strength==1.f && !nr_colour_lab::AllowUnverified(c.colour_lab_mode,false));
 c.color_strength=.3f;Select(c,0);assert(c.color_strength==.5f);
 c.color_strength=.7f;Select(c,1);assert(c.color_strength==.3f);
 Select(c,0);assert(c.color_strength==.7f);
 for(int mode=0;mode<3;++mode)for(bool permission:{false,true}){
   Config old;old.colour_lab_mode=mode;old.allow_unverified_hdr_color=permission;
   for(float saved:{0.f,.4f,1.f,1.7f}){
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
 Reset(c);assert(c.color_strength==.5f && c.colour_conservative_strength==1.f);
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
    def test_behaviour(self):
        compiler=os.environ.get('DLSS5_TEST_CXX') or str(Path(os.environ.get('NR_LLVM_BIN',str(Path(os.environ.get('DLSS5_LLVM_MINGW_ROOT',''))/'bin')))/'x86_64-w64-mingw32-clang++.exe')
        if not Path(compiler).is_file():compiler=shutil.which('clang++') or compiler
        if not Path(compiler).is_file():self.skipTest('Windows host compiler unavailable')
        root=GENERATED or ROOT
        with tempfile.TemporaryDirectory(prefix='nr-user-controls-') as td:
            cpp=Path(td)/'test.cpp';exe=Path(td)/'test.exe';cpp.write_text(CPP)
            subprocess.run([compiler,'-std=c++17','-O2','-static','-Wall','-Wextra','-Werror','-I',str(root/'src'),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

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
        self.assertIn('ColorStrength=0.5',ini)
        self.assertIn('nr_colour_preferences::Reset(next)',(GENERATED/'src/nr_recommended_picture.inl').read_text())

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--repo');p.add_argument('--generated',type=Path)
    args,rest=p.parse_known_args();GENERATED=args.generated
    unittest.main(argv=['test_user_controls.py']+rest)
