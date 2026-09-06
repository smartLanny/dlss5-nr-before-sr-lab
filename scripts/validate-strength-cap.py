"""Compile the production normalization block; check public UI and INI limits.
The model's Intensity and the final TransferStrength are distinct controls.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

HARNESS=r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>
struct ChainConfig {float transfer_strength,post_transfer_strength;};
@FINITE@
void Normalize(ChainConfig &config) {
@NORMALIZE@
}
int checks=0,failures=0;
void check(bool ok,const char* n){++checks;if(!ok){++failures;std::printf("FAIL %s\n",n);}}
int main(){
 for(float in:{-100.f,-1.f,-0.f,0.f,0.5f,1.f,1.2f,2.f,4.f,4.01f,8.f,1000.f}) {
  ChainConfig c{in,8.f};Normalize(c);float expected=std::clamp(in,0.f,4.f);
  check(c.transfer_strength==expected,"final strength public cap");
  check(c.post_transfer_strength==expected,"mode switch cannot restore legacy excessive strength");
 }
 for(float in:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
  ChainConfig c{in,8.f};Normalize(c);check(c.transfer_strength==0.f&&c.post_transfer_strength==0.f,"nonfinite input stays safe zero");
 }
 std::printf("production final-strength normalization: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
'''

def definition(text, marker):
 start=text.index(marker);end=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]

def main():
 p=argparse.ArgumentParser(description=__doc__);root=Path(__file__).resolve().parents[1]
 p.add_argument('--source',type=Path,default=root/'src/nr_before_sr.cpp')
 p.add_argument('--compiler',default=os.environ.get('CXX','g++'));p.add_argument('--expect-rejection',action='store_true');p.add_argument('--math-only',action='store_true')
 a=p.parse_args();text=a.source.read_text(encoding='utf-8-sig')
 finite=definition(text,'float FiniteOr(')
 body=definition(text,'void NormalizeConfig(')
 start=body.index('    config.transfer_strength =');end=body.index('    config.work_mode =')
 code=HARNESS.replace('@FINITE@',finite).replace('@NORMALIZE@',body[start:end]);compiler=shutil.which(a.compiler)
 if not compiler:raise SystemExit('C++ compiler not found')
 with tempfile.TemporaryDirectory(prefix='nr-strength-') as td:
  src=Path(td)/'test.cpp';exe=Path(td)/('test.exe' if os.name=='nt' else 'test');src.write_text(code,encoding='utf-8')
  flags=['-std=c++20','-Wall','-Wextra','-Werror']+(['-static'] if os.name=='nt' else [])
  subprocess.run([compiler,*flags,str(src),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],text=True,capture_output=True);print(result.stdout,end='');print(result.stderr,end='')
  if a.expect_rejection:
   if result.returncode!=1 or 'FAIL final strength public cap' not in result.stdout or 'FAIL mode switch cannot restore legacy excessive strength' not in result.stdout:raise SystemExit('Historical normalization was not rejected by exact cap oracle')
   print('PASS historical 8x control rejected');return
  if result.returncode:raise SystemExit(result.returncode)
 if not a.math_only:
  ui=(root/'src/nr_settings_ui.inl').read_text(encoding='utf-8-sig');ini=(root/'config/nr_before_sr.ini').read_text(encoding='utf-8-sig')
  assert 'constexpr float maximum_strength = 4.0f;' in ui
  assert 'constexpr float maximum_strength = 8.0f;' not in ui
  assert 'std::isfinite(blend) ? blend : 0.0f, 0.0f, 4.0f' in text
  assert '&next.intensity, 0.f, 2.f' in ui
  assert 'TransferStrength=1.20' in ini and 'PostTransferStrength=1.20' in ini
  assert '8 extreme/diagnostic' not in ini
  # Follow the real production variable and functions, not guessed aliases.
  reload=definition(text,'void ReloadConfig(')
  apply=definition(text,'bool ApplyConfig(')
  overlay=definition(text,'OverlayApplyResult ApplyOverlayConfig(')
  assert reload.index('ReadConfiguredTransferStrength(configured_mode)') < reload.index('NormalizeConfig(next);') < reload.index('g_config = next;')
  assert apply.index('NormalizeConfig(next);') < apply.index('g_config = next;') < apply.index('SaveConfig(g_config)')
  assert 'ApplyConfig(next, "ReShade simplified settings")' in overlay
  assert 'ApplyOverlayConfig(pending, pending_generation)' in ui
  print('PASS public UI/filename/INI/default/model-intensity and reload/commit integration')

if __name__=='__main__':main()
