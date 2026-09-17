"""Production colour finalizer, precision adapter and UI grouping regressions.
Host-only evidence, not shader/GPU/NVIDIA/game verification.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler', default=os.environ.get('CXX', 'g++'))
    ap.add_argument('--negative-control', action='store_true')
    args = ap.parse_args()
    policy = (ROOT/'src/nr_color_policy.inl').read_text(encoding='utf-8-sig')
    policy = policy.split('enum class NrColorPolicyStatus')[0]
    # Inject the historical unbudgeted assignment, not an arbitrary failure.
    if args.negative_control:
        line = 'policy.effective_color_strength = policy.color_allowed ? budget.coefficient : 0.0f;'
        assert line in policy
        policy = policy.replace(line, 'policy.effective_color_strength = policy.color_allowed ? policy.effective_color_strength : 0.0f;')
    cpp = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>
#include "nr_precision_ui.h"
struct Config {float transfer_strength=1.0f;} g_config;
float ActiveTransferStrength(const Config &c){return c.transfer_strength;}
enum class CodecColorMode {Automatic,LinearSdr,SrgbEncodedSdr,ModelEncodedValueDomain,PqEncodedHdr};
POLICY
unsigned checks=0,failures=0;
void check(bool good,const char *label){++checks;if(!good){if(failures<8)std::printf("FAIL %s\n",label);++failures;}}
int main(){
 for(int mode:{1,2}) for(bool hdr:{false,true}) for(bool allow:{false,true})
 for(float a:{0.f,.01f,.49f,1.f,1.0001f,1.2f,2.f,4.f})
 for(float c:{0.f,.01f,.25f,.75f,1.f,2.f}) {
  g_config.transfer_strength=a;
  auto p=ResolveNrColorPolicy(mode,hdr,hdr?CodecColorMode::PqEncodedHdr:CodecColorMode::LinearSdr,false,false,c,false);
  ApplyRequestedUnverifiedHdrColor(p,allow,c); // SAME default argument as DispatchCodec
  const float before=(!hdr||allow)?c:0.f;
  const float expected=a>1?std::min(before,1.f)/a:before;
  check(std::abs(p.effective_color_strength-expected)<1e-6f,"AMPLIFICATION_COLOR_BUDGET_ORACLE");
  check(p.requested_color_strength==c,"saved colour request is not rewritten");
  check(p.amplification==a,"same strength recorded with coefficient");
  check(p.color_allowed==(!hdr||allow),"colour budgeting cannot open a denied HDR contract");
  if(a<=1)check(p.effective_color_strength==before,"0..1x behaviour unchanged exactly");
  if(a>1)check(a*p.effective_color_strength<=std::min(before,1.f)+1e-6f,"amplified colour gain bounded");
 }
 // Check the actual V2 exponent interface over positive and negative edits.
 for(int ai=101;ai<=400;ai+=3)for(int ci=0;ci<=100;ci+=2)for(int e=-16;e<=16;++e){
  const float a=ai*.01f,c=ci*.01f,log_chroma=e*.25f;
  const auto b=nr_color_amplification::Resolve(c,a);
  const float confidence=.63f;
  const float power=std::clamp(log_chroma*a*b.coefficient*confidence,-2.f,2.f);
  const float unit=std::clamp(log_chroma*c*confidence,-2.f,2.f);
  check(std::abs(power-unit)<2e-6f,"pre-SR 1x chroma budget independent of brightness amplification");
 }
 for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-1.f}){
  check(nr_color_amplification::Resolve(.75f,bad).coefficient==0,"invalid amplification fails closed");
  check(nr_color_amplification::Resolve(bad,4.f).coefficient==0,"invalid colour fails closed");
 }
 for(float p:{-1.f,1.f,25.f,49.9f,100.1f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
  check(!nr_precision_ui::Resolve(p,true).accepted,"invalid/unsafe scale is NOT a hidden clamp");
 for(float p:{0.f,50.f,75.f,99.9f,100.f}){
  auto r=nr_precision_ui::Resolve(p,true);
  check(r.accepted,"valid front precision accepted");
  check(r.disable==(p==0),"zero is master-off action not zero-size texture");
  check(!nr_precision_ui::Resolve(p,false).accepted,"UI cannot invent reduced post execution");
  if(p==100)check(r.mode==0&&r.scale==1.f,"100 selects EXACT reference bypassing alignment rounding");
  if(p>=50&&p<100)check(r.mode==5&&r.scale>=.5f&&r.scale<1.f,"legal custom scale maps unchanged");
 }
 check(nr_precision_ui::LegacyEstimate(1,.75f,2228,1253,3840,2160)>99.f,"stored .75 is NOT actual Quality scale in HTGame");
 check(nr_precision_ui::LegacyEstimate(5,.75f,2228,1253,3840,2160)==75.f,"custom maps only after explicit edit");
 std::printf("picture budgets: %u checks / %u failures\n",checks,failures);
 return failures?1:0;
}
'''.replace('POLICY', policy)
    with tempfile.TemporaryDirectory(prefix='nr-picture-budget-') as td:
        p = Path(td); src=p/'test.cpp'; exe=p/('test.exe' if os.name=='nt' else 'test')
        src.write_text(cpp,encoding='utf-8')
        subprocess.run([args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),str(src),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        print(result.stdout,end='')
        if args.negative_control:
            assert result.returncode==1 and 'FAIL AMPLIFICATION_COLOR_BUDGET_ORACLE' in result.stdout
            print('Historical unbudgeted colour assignment rejected by exact oracle.')
        elif result.returncode:
            raise SystemExit(result.returncode)
    ui=(ROOT/'src/nr_settings_ui.inl').read_text(encoding='utf-8-sig')
    start=ui.index('ImGui::BeginTabItem(ui("Picture###nr-picture"')
    capture=ui.index('if (collecting) ImGui::BeginDisabled();',start)
    mode=ui.index('"Processing mode"',start)
    precision=ui.index('"##nr-processing-precision"',mode)
    model=ui.index('DrawUniformLayerControls(next,changed,apply_discrete_edit);',precision)
    extra=ui.index('"##nr-strength-amplification"',model)
    end=ui.index('// END PRIMARY MODEL CONTROLS',extra)
    advanced=ui.index('"Advanced picture controls"',end)
    amp=ui.index('"Maximum luminance ratio"',advanced)
    unlock=ui.index('if (collecting) ImGui::EndDisabled();',amp)
    assert start<capture<mode<precision<model<extra<end<advanced<amp<unlock
    layers=(ROOT/'src/nr_layers_ui.inl').read_text(encoding='utf-8-sig')
    assert 'for(unsigned i=0;i<nr_layers::Count;++i)' in layers
    assert '"##intensity"' in layers and '"##skin"' in layers
    assert 'ApplySettingsOverlay(pending,pending_generation,' in ui
    print('Real controls grouped under the same capture lock and configuration transaction.')

if __name__=='__main__': main()
