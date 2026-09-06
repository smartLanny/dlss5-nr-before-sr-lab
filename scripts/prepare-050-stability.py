"""Isolated preparation: immutable inputs, exact tree oracle, no branch writes."""
from pathlib import Path
import subprocess, re, os, tempfile, json, base64, shutil
A='962f15577ac86b2f9ca6d1d2d670714ee0ca0cb6'
B='12009d0448fb8bba8c699020382bf5bb6b84dec4'
EXPECTED='3a030f45c23ee41713ed9b7906d68b81440a1d2e'
origin=Path.cwd()
new_paths=['src/nr_feedback_picture.inl','scripts/validate-050-stability.py',
 'docs/BETA_0_5_DEV3_STABILITY.md','docs/testing/CP2077_DEV3.zh-CN.md','docs/testing/CP2077_DEV3_RESULTS.txt']
new_files={p:(origin/p).read_bytes() for p in new_paths}
def git(*args):return subprocess.check_output(['git',*args])
merge=subprocess.run(['git','merge-tree','--write-tree',A,B],capture_output=True,text=True)
assert merge.returncode==1,merge.stdout+merge.stderr
mt=merge.stdout.splitlines()[0]
with tempfile.TemporaryDirectory(prefix='nr-merge-') as td:
 env=dict(os.environ,GIT_INDEX_FILE=str(Path(td)/'index'))
 subprocess.run(['git','read-tree',mt],env=env,check=True)
 for path in ['.gitattributes','config/nr_before_sr.ini','scripts/validate-beta04-structure.py','src/nr_before_sr.rc','scripts/build.ps1','src/nr_before_sr.cpp']:
  if path in ['scripts/build.ps1','src/nr_before_sr.cpp']:
   text=git('show',mt+':'+path).decode()
   def resolve(m):
    assert m[1]==m[2].replace('beta0.4.5','beta0.5-dev2')
    return m[1]
   text,n=re.subn(r'^<<<<<<< [^\n]*\n(.*?)^=======\n(.*?)^>>>>>>> [^\n]*\n',resolve,text,flags=re.M|re.S)
   assert n==(2 if path.endswith('ps1') else 3)
   raw=text.encode()
  else:raw=git('show',(B if path=='.gitattributes' else A)+':'+path)
  sha=subprocess.check_output(['git','hash-object','-w','--stdin'],input=raw).decode().strip()
  subprocess.run(['git','update-index','--cacheinfo','100644',sha,path],env=env,check=True)
 merged=subprocess.check_output(['git','write-tree'],env=env).decode().strip()
assert merged=='2408dbe1cd52e186c05e1bef6bf1f99420d96fd8',merged
r=Path(tempfile.mkdtemp(prefix='nr-dev3-'))
subprocess.run(['git','worktree','add','--detach',str(r),A],check=True)
os.chdir(r)
subprocess.run(['git','read-tree','--reset','-u',merged],check=True)
for path,raw in new_files.items():
 (r/path).parent.mkdir(parents=True,exist_ok=True)
 (r/path).write_bytes(raw)
def edit(p,old,new,count=1):
 t=(r/p).read_text();assert t.count(old)==count,(p,old,t.count(old));(r/p).write_text(t.replace(old,new))
# Explicit 0.5 reset ownership, not constructor defaults or broad assignment.
edit('src/nr_recommended_picture.inl','    next.detail_stability = 0.0f;','''    next.detail_stability = 0.0f;
    next.high_strength_protection = true;
    next.layered_enhancement = false;
    next.layer_tone = 1.0f;
    next.layer_structure = 1.0f;
    next.layer_bright = 1.0f;
    next.layer_shadow = 1.0f;''')
edit('scripts/validate-recommended-picture.py'," 'detail_stability':'DetailStability', 'motion_stability':'MotionStability',", """ 'detail_stability':'DetailStability', 'motion_stability':'MotionStability',
 'high_strength_protection':'HighStrengthProtection', 'layered_enhancement':'LayeredEnhancement',
 'layer_tone':'LayerTone', 'layer_structure':'LayerStructure',
 'layer_bright':'LayerBright', 'layer_shadow':'LayerShadow',""")
edit('src/nr_feedback_core.inl','#include "nr_feedback_report.inl"','#include "nr_feedback_report.inl"\n#include "nr_feedback_picture.inl"')
edit('src/nr_feedback_core.inl','    FeedbackAppendActivity(s, "start");','    AppendPictureConfig(s, config, session->snapshot_ok);\n    FeedbackAppendActivity(s, "start");')
edit('src/nr_f8_stream.inl','    b.Append(",\\"layeredEnhancement\\":%d",c.layered_enhancement);','    b.Append(",\\"highStrengthProtection\\":%d",c.high_strength_protection);\n    b.Append(",\\"layeredEnhancement\\":%d",c.layered_enhancement);')
edit('src/nr_f8_stream.inl','    b.Append(",\\"layeredEffective\\":%d",(n.codec_flags & 2048u) != 0u);','    // Flag is captured per dispatch; it is not proof every pixel exceeded 1x.\n    b.Append(",\\"highStrengthProtectionFlag\\":%d",(n.codec_flags & 1024u) != 0u);\n    b.Append(",\\"layeredEffective\\":%d",(n.codec_flags & 2048u) != 0u);')
# Real Windows coordinator -> metadata -> disk TXT -> cache/ZIP assertions.
edit('scripts/validate-core-feedback.py','    begin(runtime);check(nr_core_feedback::Busy(),"worker owned by current session");','''    g_config.high_strength_protection=false;g_config.layered_enhancement=true;
    g_config.layer_tone=.5f;g_config.layer_structure=1.5f;g_config.layer_bright=.75f;g_config.layer_shadow=1.25f;
    begin(runtime);check(nr_core_feedback::Busy(),"worker owned by current session");
    g_config.layer_bright=2.f; // Worker must serialize the earlier coherent snapshot.''')
edit('scripts/validate-core-feedback.py','    check(report.find("text.complete=1")!=std::string::npos,"all included core summaries present in TXT");','''    check(report.find("text.complete=1")!=std::string::npos,"all included core summaries present in TXT");
    check(report.find("requested.highStrengthProtection=0\\n")!=std::string::npos &&
          report.find("requested.layeredEnhancement=1\\n")!=std::string::npos &&
          report.find("requested.layerBright=0.75\\n")!=std::string::npos &&
          report.find("requested.layerShadow=1.25\\n")!=std::string::npos,
          "TXT carries frozen 0.5 picture settings, not current globals");''')
edit('scripts/validate-core-feedback.py',"'end.ngxHookFailure.entry=existing-short-hotpatch'):","'end.ngxHookFailure.entry=existing-short-hotpatch',\n                    'requested.highStrengthProtection=0', 'requested.layeredEnhancement=1',\n                    'requested.layerBright=0.75', 'requested.layerShadow=1.25'):")
# Version identity changes only in current product files, never archived evidence.
paths=subprocess.check_output(['git','grep','-l','-e','beta0.5-dev2','-e','0,5,0,2','-e','0.5.0 dev2','-e','beta050-dev2','HEAD','--',':!bug-inbox'],cwd=r,text=True).splitlines()
for line in paths:
 p=line[len('HEAD:'):];t=(r/p).read_text()
 for a,b in [('beta0.5-dev2','beta0.5-dev3'),('beta050-dev2','beta050-dev3'),('0,5,0,2','0,5,0,3'),('0.5.0 dev2','0.5.0 dev3')]:t=t.replace(a,b)
 (r/p).write_text(t)
# .5 workflow must run new maintenance checks as well as all original guards.
edit('.github/workflows/beta050-validate.yml','branches: [next/0.5, codex/050-high-strength-guard]','branches: [next/0.5, codex/050-high-strength-guard, feature/050-layered-composite]')
edit('.github/workflows/beta050-validate.yml','          python3 scripts/validate-layered-composite.py --compiler g++','''          python3 scripts/validate-050-stability.py --compiler g++
          python3 scripts/validate-050-stability.py --compiler g++ --negative-control
          python3 scripts/validate-045-color.py --compiler g++
          python3 scripts/validate-045-color.py --compiler g++ --negative-control
          python3 scripts/validate-044-compat.py --compiler g++
          python3 scripts/validate-retirement-return.py --compiler g++
          python3 scripts/validate-recommended-picture.py --compiler g++
          python3 scripts/validate-recommended-picture.py --compiler g++ --negative-control
          python3 -m unittest discover -s tests -p test_evidence_checkout.py -v
          python3 scripts/validate-layered-composite.py --compiler g++''')
edit('.github/workflows/beta050-validate.yml',"          python scripts/validate-bug-sweep.py --compiler $compiler",'''          python scripts/validate-050-stability.py --compiler $compiler
          if ($LASTEXITCODE -ne 0) { throw '0.5 snapshot/reset regression failed.' }
          python scripts/validate-044-compat.py --compiler $compiler
          if ($LASTEXITCODE -ne 0) { throw '0.4.5 forwarding/guard regression failed.' }
          python scripts/validate-retirement-return.py --compiler $compiler
          if ($LASTEXITCODE -ne 0) { throw 'Retirement return regression failed.' }
          python scripts/validate-recommended-picture.py --compiler $compiler
          if ($LASTEXITCODE -ne 0) { throw '0.5 recommendation regression failed.' }
          python scripts/validate-bug-sweep.py --compiler $compiler''')
edit('.github/workflows/beta050-validate.yml','          Copy-Item docs/BETA_0_5_LAYERS.md dist/README-TESTING.md','''          Copy-Item docs/BETA_0_5_LAYERS.md dist/README-TESTING.md
          Copy-Item docs/testing/CP2077_DEV3.zh-CN.md dist/2077-TESTING.zh-CN.md
          Copy-Item docs/testing/CP2077_DEV3_RESULTS.txt dist/2077-RESULTS.txt''')
# Ensure common feedback workflow sees the stacked PR too.
p='.github/workflows/feedback-integrity.yml';t=(r/p).read_text();start,end=t.split('  push:',1)
assert '      - next/0.5\n' in start
start=start.replace('      - next/0.5\n','      - next/0.5\n      - feature/050-layered-composite\n',1);(r/p).write_text(start+'  push:'+end)
# Remove false 'all Reset(NULL) refused' description, not the safety setting.
edit('config/nr_before_sr.ini','''; FH6 stability default: do not inject NR into a Reset(NULL)/ClearState(NULL)
; carrier. Set to 1 only for a controlled compatibility experiment, then fully
; restart the game. This publishes a much wider D3D12 state-observer surface.''','''; General null-PSO replay stays off. Only fully observed, untainted pristine or
; bounded viewport/scissor/blend subsets are automatically admitted. Unknown
; state stays native; do not enable broad replay to make an A/B test look active.''')
# Existing 0.4.5 loader fallback has no true last error: report unknown, not guess a missing DLL.
edit('src/nr_runtime_load_evidence.inl','return error != ERROR_SUCCESS ? error : ERROR_MOD_NOT_FOUND;','return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;')
print('Applied stabilization, snapshot/reset fixes and version identity.')

edit('.github/workflows/beta050-validate.yml',
 '          ./scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -NegativeControl',
 '          ./scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -NegativeControl\n'
 '          ./scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -DynamicStateNegativeControl\n'
 '          ./scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -BlendStateNegativeControl')
edit('src/nr_settings_ui.inl','from this OTA, then restart.','from this package, then restart.')
edit('src/nr_settings_ui.inl','使用本 OTA 配套','使用本测试包配套')
p=r/'docs/BETA_0_5_DEVELOPMENT.md'
p.write_text(p.read_text()+"\n## dev3稳定化\n\n见[BETA_0_5_DEV3_STABILITY.md](BETA_0_5_DEV3_STABILITY.md)。0.4.5候选以保留父提交的方式前向同步；0.5分层推荐重置与反馈快照单独补齐，未合并canonical分支。\n")
p=r/'docs/BETA_0_5_LAYERS.md'
p.write_text("# dev3 稳定化补充\n\n本包保留 dev2 分层算法，并吸收0.4.5候选修复。先读同包 `2077-TESTING.zh-CN.md` 与 `2077-RESULTS.txt`；完整范围见仓库 `docs/BETA_0_5_DEV3_STABILITY.md`。本包未完成真实游戏验收。\n\n"+p.read_text())
subprocess.run(['git','add','--all'],check=True)
final=git('write-tree').decode().strip()
print('CANDIDATE_TREE',final,flush=True)
assert final==EXPECTED,(final,EXPECTED)
assert git('rev-parse',final+':bug-inbox')==git('rev-parse',B+':bug-inbox')
old=git('show',A+':src/nr_before_sr.cpp').decode()
current=(r/'src/nr_before_sr.cpp').read_text()
def shader(t):return t[t.index('const char kCodecShader[]'):t.index(')HLSL";',t.index('const char kCodecShader[]'))]
assert shader(old)==shader(current)
for name in ['validate-050-stability','validate-recommended-picture','validate-045-color']:
 for extra in [[],['--negative-control']]:
  subprocess.run(['python3','scripts/'+name+'.py','--compiler','g++',*extra],check=True)
for name in ['validate-044-compat','validate-retirement-return','validate-layered-composite','validate-beta05-boost','validate-bug-sweep','validate-043r2','validate-compat-sweep']:
 subprocess.run(['python3','scripts/'+name+'.py','--compiler','g++'],check=True)
for name in ['validate-strength-cap','validate-beta04-structure','test-beta04-math']:
 subprocess.run(['python3','scripts/'+name+'.py'],check=True)
subprocess.run(['git','diff','--cached','--check'],check=True)
if os.environ.get('NR_LOCAL_ONLY')=='1':
 print('LOCAL_VALIDATED',final)
 raise SystemExit(0)
repo=os.environ['GITHUB_REPOSITORY']
def post(endpoint,body):
 result=subprocess.check_output(['gh','api','--method','POST','repos/'+repo+'/git/'+endpoint,'--input','-'],input=json.dumps(body).encode())
 return json.loads(result)
entries=[]
for path in git('diff','--name-only',B,final).decode().splitlines():
 assert not path.startswith('bug-inbox/') and '050-stability-patch' not in path and 'prepare-050-stability' not in path
 raw=git('show',final+':'+path)
 sha=post('blobs',{'encoding':'base64','content':base64.b64encode(raw).decode()})['sha']
 expected=subprocess.check_output(['git','hash-object','--stdin'],input=raw).decode().strip()
 assert sha==expected
 entries.append({'path':path,'mode':'100644','type':'blob','sha':sha})
 print('VALIDATED_BLOB',path,sha,flush=True)
tree=post('trees',{'base_tree':git('rev-parse',B+'^{tree}').decode().strip(),'tree':entries})['sha']
assert tree==EXPECTED
print('FINAL_TREE',tree,'PARENTS',A,B,'PREPARATION_COMPLETE',flush=True)
