"""Production colour policy tests; no GPU/visual-quality claim."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def definition(text,marker):
 start=text.index(marker);end=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--compiler',default=os.environ.get('CXX','g++'))
 parser.add_argument('--negative-control',action='store_true')
 args=parser.parse_args()
 cpp=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
 policy=(ROOT/'src/nr_color_policy.inl').read_text(encoding='utf-8-sig').split('enum class NrColorPolicyStatus')[0]
 ui=(ROOT/'src/nr_settings_ui.inl').read_text(encoding='utf-8-sig')
 assert 'ApplyRequestedUnverifiedHdrColor(color_policy, g_config.allow_unverified_hdr_color,' in cpp
 assert 'if (color_follow_changed) next.allow_unverified_hdr_color = next.color_strength > 0.0f;' in ui
 assert 'next.allow_unverified_hdr_color = false;' in (ROOT/'src/nr_recommended_picture.inl').read_text()
 if args.negative_control:
  old=definition(policy,'void ApplyRequestedUnverifiedHdrColor(')
  policy=policy.replace(old,'void ApplyRequestedUnverifiedHdrColor(NrColorPolicy &, bool, float) {}')
 code=r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>
enum class CodecColorMode { Automatic, LinearSdr, SrgbEncodedSdr, ModelEncodedValueDomain, PqEncodedHdr };
'''+definition(cpp,'float FiniteOr(')+'\n'+policy+r'''
int checks=0, failures=0;
void check(bool okay,const char*message){++checks;if(!okay){++failures;std::printf("FAIL %s\n",message);}}
int main(){
 for(int mode:{1,2})for(bool allow:{false,true})for(float strength:{0.f,.25f,.75f,1.f,2.f}) {
  auto p=ResolveNrColorPolicy(mode,true,CodecColorMode::PqEncodedHdr,false,false,strength,false);
  ApplyRequestedUnverifiedHdrColor(p,allow,strength);
  check(p.effective_color_strength==(allow?strength:0.f),"manual unverified colour request reaches both modes");
  check(p.color_reason==(allow?NrColorReason::ExperimentalUnverifiedHdr:NrColorReason::UnverifiedHdr),"unverified stays correctly labelled");
  check(p.requested_color_strength==strength,"saved request preserved");
 }
 for(int mode:{1,2})for(auto encoding:{CodecColorMode::LinearSdr,CodecColorMode::SrgbEncodedSdr,CodecColorMode::ModelEncodedValueDomain}) {
  auto p=ResolveNrColorPolicy(mode,false,encoding,true,true,.75f,false);auto old=p;
  ApplyRequestedUnverifiedHdrColor(p,true,1.f);
  check(p.color_reason==old.color_reason && p.effective_color_strength==old.effective_color_strength,"verified contract unchanged by override");
 }
 for(int mode:{-1,0,3,18}) {
  auto p=ResolveNrColorPolicy(mode,true,CodecColorMode::Automatic,false,false,.75f,false);
  ApplyRequestedUnverifiedHdrColor(p,true,.75f);
  check(!p.color_allowed && p.effective_color_strength==0.f,"invalid mode not made admissible");
 }
 for(float value:{-1.f,4.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
  auto p=ResolveNrColorPolicy(2,true,CodecColorMode::Automatic,false,false,value,false);
  ApplyRequestedUnverifiedHdrColor(p,true,value);
  check(std::isfinite(p.effective_color_strength) && p.effective_color_strength>=0.f && p.effective_color_strength<=2.f,"nonfinite/out-of-range colour stays bounded");
 }
 std::printf("0.4.5 production colour policy: %d checks, %d failures\n",checks,failures);
 return failures?1:0;
}
'''
 compiler=shutil.which(args.compiler)
 if not compiler:raise SystemExit('C++ compiler not found')
 with tempfile.TemporaryDirectory(prefix='nr045-colour-') as td:
  src=Path(td)/'test.cpp';exe=Path(td)/('test.exe' if os.name=='nt' else 'test')
  src.write_text(code,encoding='utf-8')
  subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',*(['-static'] if os.name=='nt' else []),str(src),'-o',str(exe)],check=True)
  r=subprocess.run([str(exe)],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='')
  if args.negative_control:
   if r.returncode!=1 or 'FAIL manual unverified colour request reaches both modes' not in r.stdout:raise SystemExit('Exact disabled override was not rejected')
   print('PASS deliberate no-op override rejected')
  elif r.returncode:raise SystemExit(r.returncode)
if __name__=='__main__':main()
