"""Execute the actual bilingual temporal panel with recording-only UI services."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
CPP=r'''
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include "nr_temporal_status.h"
#include "nr_temporal_ui_status.h"
bool zh=false;unsigned long long clock_ms=5000;std::string shown;
bool UseChineseUi(){return zh;}
unsigned long long GetTickCount64(){return clock_ms;}
std::atomic<unsigned> g_config_generation{42};
void SettingsSection(const char* s){shown+=s;shown+='\n';}
void SettingsHelp(const char* s){SettingsSection(s);}
void SettingsNote(const char* s){SettingsSection(s);}
struct ChainConfig {unsigned temporal_backend=2,temporal_scope=0,temporal_mode=0,nr_passes=2;bool private_residual_sr=false;};
namespace ImGui {
void SameLine(){}void PushID(int){}void PopID(){}void BeginDisabled(bool){}void EndDisabled(){}
bool RadioButton(const char*,bool){return false;}
bool CollapsingHeader(const char*){return true;}
void Text(const char* format,...){char out[512];va_list args;va_start(args,format);std::vsnprintf(out,sizeof(out),format,args);va_end(args);SettingsNote(out);}
}
#include "nr_temporal_ui.inl"
int main(){
 using namespace nr_temporal_ui;
 assert(!Recent(1000,0));
 assert(!Recent(5000,0)&&!Recent(5000,5001)&&!Recent(5000,3000));
 assert(Recent(5000,3001)&&Recent(5000,5000));
 assert(!Recent(0,~0ull));
 assert(std::string(Describe(15,true).en).find("missing")!=std::string::npos);
 assert(std::string(Describe(16,true).en).find("direction")!=std::string::npos);
 assert(std::string(Describe(17,true).en).find("snapshot")!=std::string::npos);
 assert(std::string(Describe(18,true).en).find("Whole chain")!=std::string::npos);
 assert(std::string(Describe(20,true).en).find("allocation")!=std::string::npos);
 assert(std::string(Describe(21,true).en).find("private SR")!=std::string::npos);
 assert(std::string(Describe(22,true).en).find("Present")!=std::string::npos);
 for(bool chinese:{false,true}){
  zh=chinese;
  for(unsigned state=0;state<=23;++state){
   ChainConfig c;bool changed=false,discrete=false;shown.clear();
   nr_temporal_status::state=state;nr_temporal_status::last_tick=4500;
   DrawTemporalOptions(c,changed,discrete);
   const auto m=Describe(state,true);assert(shown.find(chinese?m.zh:m.en)!=std::string::npos);
   assert(!changed&&!discrete&&c.temporal_backend==2&&c.temporal_scope==0&&c.temporal_mode==0&&c.nr_passes==2&&!c.private_residual_sr);
   if(m.request_feedback)assert(shown.find(chinese?"若持续如此，请按 F8":"If this persists, press F8")!=std::string::npos);
  }
  ChainConfig c;bool changed=false,discrete=false;
  nr_temporal_status::state=8;nr_temporal_status::last_tick=3000;shown.clear();
  DrawTemporalOptions(c,changed,discrete);
  const auto stale=Describe(8,false);assert(shown.find(chinese?stale.zh:stale.en)!=std::string::npos);
  nr_temporal_status::gpu_config=42;nr_temporal_status::gpu_instance=7;nr_temporal_status::gpu_frames=1;
  nr_temporal_status::gpu_tested=10;nr_temporal_status::gpu_adopted=4;nr_temporal_status::gpu_rejected=6;
  for(bool dirty:{false,true}){
   shown.clear();changed=dirty;DrawTemporalOptions(c,changed,discrete,!dirty);
   assert(shown.find(chinese?"设置待应用":"Settings pending")!=std::string::npos);
   assert(shown.find(chinese?"GPU 历史抽样（实例":"GPU history samples (owner")==std::string::npos);
   assert(shown.find(chinese?"暂无当前设置":"No matching GPU")!=std::string::npos);
  }
  changed=false;nr_temporal_status::last_tick=4500;shown.clear();DrawTemporalOptions(c,changed,discrete);
  assert(shown.find(chinese?"GPU 历史抽样（实例":"GPU history samples (owner")!=std::string::npos);
  nr_temporal_status::gpu_config=41;shown.clear();DrawTemporalOptions(c,changed,discrete);
  assert(shown.find(chinese?"GPU 历史抽样（实例":"GPU history samples (owner")==std::string::npos);
  c.private_residual_sr=true;shown.clear();DrawTemporalOptions(c,changed,discrete);
  auto conflict=Describe(21,true);assert(shown.find(chinese?conflict.zh:conflict.en)!=std::string::npos);
  nr_temporal_status::state=9;shown.clear();DrawTemporalOptions(c,changed,discrete,true);
  auto fault=Describe(9,true);assert(shown.find(chinese?fault.zh:fault.en)!=std::string::npos);
  c.private_residual_sr=false;c.temporal_scope=1;c.nr_passes=1;nr_temporal_status::state=8;shown.clear();
  DrawTemporalOptions(c,changed,discrete);auto layers=Describe(11,true);assert(shown.find(chinese?layers.zh:layers.en)!=std::string::npos);
 }
 std::puts("PASS actual temporal panel: bilingual 24 states, stale/clock boundaries, pending statistics, conflicts and preserved controls");
}
'''

class TemporalUiStatus(unittest.TestCase):
    def compile_run(self, replacements=None):
        compiler=shutil.which(os.environ.get('CXX','g++'))
        if not compiler:self.skipTest('C++ compiler unavailable; not rendered UI validation')
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for name in ('nr_temporal_ui_status.h','nr_temporal_ui.inl'):
                text=(ROOT/'src'/name).read_text(encoding='utf-8')
                if replacements and name in replacements:
                    old,new=replacements[name]
                    self.assertEqual(text.count(old),1)
                    text=text.replace(old,new)
                (root/name).write_text(text,encoding='utf-8')
            source=root/'panel.cpp';exe=root/'panel.exe';source.write_text(CPP,encoding='utf-8')
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-I'+str(root),'-I'+str(ROOT/'src'),str(source),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            return subprocess.run([str(exe)],capture_output=True,text=True)
    def test_actual_panel_and_policy(self):
        ran=self.compile_run()
        self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
    def test_independent_regressions_are_rejected(self):
        mutations=(
            ('nr_temporal_ui.inl','has_pending||changed','has_pending&&changed'),
            ('nr_temporal_ui_status.h','tick != 0 && now >= tick && now-tick < 2000','now-tick < 2000'),
            ('nr_temporal_ui_status.h','the motion/depth snapshot is unavailable','waiting for a supported route'),
        )
        for name,old,new in mutations:
            with self.subTest(regression=old):
                ran=self.compile_run({name:(old,new)})
                self.assertNotEqual(ran.returncode,0)
                self.assertIn('Assertion',ran.stderr)
    def test_pending_transaction_reaches_panel(self):
        source=(ROOT/'src/nr_unified_pages.inl').read_text(encoding='utf-8')
        self.assertIn('DrawTemporalOptions(next,changed,discrete,pending||changed);',source)
        self.assertNotIn('SaveConfig(', (ROOT/'src/nr_temporal_ui.inl').read_text(encoding='utf-8'))

if __name__=='__main__':unittest.main(verbosity=2)
