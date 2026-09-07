"""Compile actual ChainConfig/reset helper against independent shipped INI values.
This does not simulate a game, ImGui, Win32 persistence or NVIDIA runtime.
"""
from pathlib import Path
import argparse
import configparser
import os
import re
import shutil
import subprocess
import tempfile

FIELDS = {
 'mode':'Mode', 'identity_use_original':'IdentityUseOriginalColor',
 'transfer_strength':'TransferStrength', 'post_transfer_strength':'PostTransferStrength',
 'work_mode':'WorkMode', 'custom_work_scale':'CustomWorkScale',
 'lighting_lock':'LightingLock', 'edge_guard':'EdgeGuard',
 'detail_stability':'DetailStability', 'motion_stability':'MotionStability',
 'proxy_curve':'ProxyCurve', 'allow_unverified_hdr_color':'AllowUnverifiedHdrColor',
 'suppress_reset_frame':'SuppressResetFrame', 'color_strength':'ColorStrength',
 'max_luma_ratio':'MaxLumaRatio', 'preserve_lighting':'PreserveLighting',
 'proxy_scale':'ProxyScale', 'experimental_peak_shoulder':'ExperimentalPeakShoulder',
 'warmup_after_create':'WarmupAfterCreate', 'retry_original_on_sr_failure':'RetryOriginalOnSRFailure',
 'intensity':'Intensity', 'local_tone':'LocalToneStrength',
 'local_structure':'LocalStructureStrength', 'skin_structure':'SkinStructureStrength',
 'auto_mask':'AutoMask', 'style':'Style', 'ui_correction':'UICorrection',
}
PRESERVED={'enabled','depth_mode','r8_output_encoding','jitter_mode','perf_epoch'}
IDENTIFIER=r'[a-z_][a-z0-9_]*'

def definition(text,marker):
 start=text.index(marker);end=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]

def check_wiring(cpp,ui,helper):
 assert '#include "nr_recommended_picture.inl"' in ui
 draw=definition(ui,'void DrawSettings(');call=draw.index('RestoreRecommendedPictureSettings(next);')
 picture=draw.index('ImGui::BeginTabItem(ui("Picture###nr-picture"')
 assert picture < draw.index('if (collecting) ImGui::BeginDisabled();',picture) < call
 assert call < draw.index('if (collecting) ImGui::EndDisabled();',picture)
 assert 'changed = true;' in draw[call:call+100]
 assert 'ApplyOverlayConfig(pending, pending_generation)' in draw
 apply=definition(cpp,'bool ApplyConfig(')
 assert apply.index('NormalizeConfig(next);') < apply.index('g_config = next;') < apply.index('SaveConfig(g_config)')
 assert 'SameHistoryConfig(next, g_config)' in apply
 overlay=definition(cpp,'OverlayApplyResult ApplyOverlayConfig(')
 assert 'expected_generation' in overlay and 'nr_f8::g_config_hold' in overlay
 assert 'ApplyConfig(next, "ReShade simplified settings")' in overlay
 reset=definition(helper,'void RestoreRecommendedPictureSettings(')
 assert set(re.findall(r'next\.('+IDENTIFIER+r')\s*=',reset))==set(FIELDS)
 for forbidden in ['g_config','SaveConfig(','ApplyConfig(','Release','.store(','CreateFeature']:
  assert forbidden not in reset,forbidden

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
 p.add_argument('--compiler',default=os.environ.get('CXX','g++'))
 p.add_argument('--negative-control',action='store_true');a=p.parse_args();root=a.root
 cpp=(root/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
 ui=(root/'src/nr_settings_ui.inl').read_text(encoding='utf-8-sig')
 helper=(root/'src/nr_recommended_picture.inl').read_text(encoding='utf-8-sig')
 check_wiring(cpp,ui,helper)
 config=configparser.ConfigParser();config.read(root/'config/nr_before_sr.ini',encoding='utf-8-sig')
 values=config['NRBeforeSR'];struct=definition(cpp,'struct ChainConfig {')+';'
 declared=set(re.findall(r'^\s*(?:bool|int|float|unsigned int|NrJitterMode)\s+('+IDENTIFIER+r')\s*=',struct,re.M))
 expected_fields=set(FIELDS)|PRESERVED
 assert declared==expected_fields,f'Unclassified fields: {declared^expected_fields}'
 if a.negative_control:
  marker='next.color_strength = 1.0f;';assert helper.count(marker)==1
  helper=helper.replace(marker,'next.color_strength = 0.5f;',1)
 dirty=[];checks=[]
 for field,key in FIELDS.items():
  number=values[key];expected=f'static_cast<decltype(c.{field})>({number})'
  dirty.append(f'c.{field}=static_cast<decltype(c.{field})>(({number})==0?1:0);')
  checks.append(f'check(c.{field}=={expected},"INI default {key}");')
 code=r'''#include <cstdio>
#include <initializer_list>
enum class NrJitterMode {Legacy=0,GameInputPixel=1};
int count=0,failures=0;
void check(bool ok,const char* name){++count;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
'''+struct+'\n'+helper+'\nint main(){\n'
 code+='for(bool enabled:{false,true})for(int encoding:{0,1,2}){ChainConfig c;\n'+'\n'.join(dirty)+'\n'
 code+='c.enabled=enabled;c.depth_mode=1;c.r8_output_encoding=encoding;c.jitter_mode=NrJitterMode::Legacy;c.perf_epoch=777;\n'
 code+='for(int repeat=0;repeat<2;++repeat){RestoreRecommendedPictureSettings(c);\n'+'\n'.join(checks)+'\n'
 code+='check(c.enabled==enabled&&c.depth_mode==1&&c.r8_output_encoding==encoding&&c.jitter_mode==NrJitterMode::Legacy&&c.perf_epoch==777,"master and provider contracts preserved");\n'
 code+='}\n}\nstd::printf("recommended picture reset: %d checks, %d failures\\n",count,failures);return failures?1:0;}\n'
 compiler=shutil.which(a.compiler)
 if not compiler:raise SystemExit('C++ compiler not found')
 with tempfile.TemporaryDirectory(prefix='nr-recommended-') as td:
  src=Path(td)/'test.cpp';exe=Path(td)/('test.exe' if os.name=='nt' else 'test');src.write_text(code,encoding='utf-8')
  flags=['-std=c++20','-O0','-Wall','-Wextra','-Werror']+(['-static'] if os.name=='nt' else [])
  subprocess.run([compiler,*flags,str(src),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True);print(result.stdout,end='');print(result.stderr,end='')
  if a.negative_control:
   if result.returncode!=1 or 'FAIL INI default ColorStrength' not in result.stdout:raise SystemExit('Wrong-default negative oracle was not rejected')
   print('PASS wrong legacy colour default rejected by independent INI oracle')
  elif result.returncode:raise SystemExit(result.returncode)
 print('PASS reset wiring: capture hold, stale-edit generation and normal persistence/history path')

if __name__=='__main__':main()
