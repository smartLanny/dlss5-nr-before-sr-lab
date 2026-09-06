"""Prepare verified follow-up blobs on the exact 0.4.5 source; no ref/commit writes."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import urllib.request

BASE='c8a3791b10811bea9a764a74d41c4e162e84dc3b'
REPO='smartLanny/dlss5-nr-before-sr-lab'
HERE=Path(__file__).resolve().parents[1]

def main():
 with tempfile.TemporaryDirectory(prefix='nr045-final-') as tmp:
  root=Path(tmp)/'tree'
  def git(*args):return subprocess.check_output(['git',*args],cwd=root,text=True).strip()
  subprocess.run(['git','worktree','add','--detach',str(root),BASE],cwd=HERE,check=True)
  def read(p):return (root/p).read_text(encoding='utf-8-sig')
  def write(p,s):
   f=root/p;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(s,encoding='utf-8',newline='\n')
  def swap(s,a,b,count=1):
   if s.count(a)!=count:raise RuntimeError(f'Anchor mismatch {s.count(a)} != {count}: {a[:120]}')
   return s.replace(a,b)
  p='scripts/validate-beta04-structure.py'
  write(p,swap(read(p),'package-044-test1.py','package-045.py'))
  # Blend-only WARP controls exercise the actual production model and restore.
  write('tests/blend_only_gpu.inl',(HERE/'tests/blend_only_gpu.inl').read_text())
  p='tests/command_state_gpu_harness.cpp.in';s=read(p)
  s=swap(s,'void ProbeHardwareMetaCommands() {','// @BLEND_ONLY_TEST@\n\nvoid ProbeHardwareMetaCommands() {')
  s=swap(s,'        Gpu gpu;','''        Gpu gpu;
        if (argc == 2 && std::strcmp(argv[1], "--blend-only") == 0) {
            for (bool defaults : {false,true}) for (bool inject : {false,true}) RunBlendOnly(gpu,defaults,inject);
            std::puts("Blend-only WARP: four pixel controls/restores passed.");
            return 0;
        }''')
  s=swap(s,'        std::printf("Command-state WARP validation:', '''        if (!null_only) for (bool defaults : {false,true}) for (bool inject : {false,true}) {
            RunBlendOnly(gpu,defaults,inject); ++count;
        }
        std::printf("Command-state WARP validation:''')
  write(p,s)
  p='scripts/validate-command-state-gpu.ps1';s=read(p)
  s=swap(s,'    [switch]$DynamicStateNegativeControl\n','    [switch]$DynamicStateNegativeControl,\n    [switch]$BlendStateNegativeControl\n')
  s=swap(s,'$CpuDescriptorNegativeControl,$DynamicStateNegativeControl)', '$CpuDescriptorNegativeControl,$DynamicStateNegativeControl,$BlendStateNegativeControl)')
  s=swap(s,"$production = $model.Groups[1].Value",'''$production = $model.Groups[1].Value
$blendFixture = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\\blend_only_gpu.inl') -Raw
if (!$harness.Contains('// @BLEND_ONLY_TEST@')) { throw 'Blend pixel fixture anchor missing' }
$harness = $harness.Replace('// @BLEND_ONLY_TEST@', $blendFixture)
if ($BlendStateNegativeControl) {
    $marker = 'ID3D12GraphicsCommandList_OMSetBlendFactor(list, d.blend)'
    if (!$production.Contains($marker)) { throw 'Blend restore negative-control anchor missing' }
    $production = $production.Replace($marker,'ID3D12GraphicsCommandList_OMSetBlendFactor(list, g_wrong_restore_blend)')
    $harness = $harness.Replace('// @NEGATIVE_CONTROL_CONSTANTS@','const float g_wrong_restore_blend[4] = {};')
}''')
  s=swap(s,"$stem = if ($NegativeControl)","$stem = if ($BlendStateNegativeControl) { 'command_state_gpu_blend_negative' } elseif ($NegativeControl)")
  s=swap(s,"if ($MetaProbeOnly) { & $binary '--meta-probe-only' }",'''if ($BlendStateNegativeControl) {
    $output = & $binary '--blend-only' 2>&1
    $code = $LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if ($code -ne 1 -or ($output -join "`n") -notmatch 'Restored blend-only state produced wrong pixels') {
        throw 'Blend pixel oracle did not reject exact deliberately corrupted restore'
    }
    Write-Host 'Negative control passed: wrong blend factors rejected by real WARP pixels.'
    exit 0
}
if ($MetaProbeOnly) { & $binary '--meta-probe-only' }''')
  write(p,s)
  p='.github/workflows/beta042-validate.yml';s=read(p)
  marker='          ./scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -DynamicStateNegativeControl'
  s=swap(s,marker,marker+'\n          ./scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -BlendStateNegativeControl')
  s=s.replace('Expected two test1 language addons','Expected two 0.4.5 language addons')
  write(p,s)
  p='scripts/validate-core-feedback.py';s=read(p)
  s=swap(s,'std::ifstream txt(nr_core_feedback::g_report_path,std::ios::binary);','std::ifstream txt(std::filesystem::path(nr_core_feedback::g_report_path),std::ios::binary);')
  # Dedicated independent atomic TXT outcomes: denial does not destroy ZIP; retry cannot overwrite existing text.
  marker='    std::ifstream summary(std::filesystem::path(output_root)/"core-1/summary-0.txt",std::ios::binary);'
  s=swap(s,marker,'''    {
        nr_core_feedback::nr_feedback_report::Output blocked;
        const auto dir=std::filesystem::path(output_root)/"core-1";
        check(!nr_core_feedback::nr_feedback_report::WriteAtomic(dir.c_str(),"replacement",blocked),"TXT cannot overwrite this session's published report");
        check(blocked.error!=0 && blocked.path.empty(),"TXT error is explicit and has no false published path");
        check(g_feedback_last_zip[0] && g_feedback_status==3,"independent TXT failure preserves valid ZIP");
        check(!std::filesystem::exists(dir/"feedback-report.txt.tmp"),"failed TXT publication removes only owned temporary file");
    }
'''+marker)
  write(p,s)
  p='src/nr_feedback_report.inl';s=read(p)
  s=swap(s,'    Output output;\n    std::string text', '    Output output;\n    output.complete = capture_complete && zip.required_complete && zip.status != nr_bundle::Status::Failed;\n    std::string text')
  s=s.replace('text.reason=summary unavailable, changed, or bounded text budget reached','text.reason=capture partial, summary unavailable/changed, or bounded text budget reached')
  write(p,s)
  p='src/nr_f8_sequence.inl';s=read(p)
  s=swap(s,'void DrawCoreTextStatus(bool chinese) {','void DrawCoreTextStatus(bool chinese) {\n    if (g_feedback_status.load(std::memory_order_acquire) < 3) return;')
  s=s.replace('只保存有界日志与状态，不采图片、不改设置，ZIP 上限 8 MiB。','同时保存 ZIP 和独立 TXT，可复制全文填表；只收有界日志与状态，不采图片、不改设置。ZIP 上限 8 MiB，TXT 上限 2 MiB。')
  s=s.replace('Bounded logs and state only; no images or setting changes. ZIP ceiling: 8 MiB.','Saves ZIP plus standalone TXT with a copy-full-text button. Bounded logs/state only; no images or setting changes. ZIP ceiling: 8 MiB; TXT: 2 MiB.')
  write(p,s)
  p='src/nr_settings_ui.inl';s=read(p)
  marker='    static ChainConfig pending = {};'
  s=swap(s,marker,'''    const auto live_reason = static_cast<BypassReason>(g_last_bypass_reason.load(std::memory_order_acquire));
    if (live_reason == BypassReason::RuntimeUnavailable) {
        ImGui::TextWrapped("%s", ui("NR runtime could not load or initialize. Keep your NVIDIA DLLs; install the matching nrchain_nvngx.dll from this OTA, then restart. F8 TXT identifies the failed component and error.",
            "NR 运行库加载或初始化失败。保留显卡对应 NVIDIA DLL，使用本 OTA 配套 nrchain_nvngx.dll 后重启游戏；F8 TXT 会列出失败组件及错误。"));
    } else if (live_reason == BypassReason::CodecUnavailable) {
        ImGui::TextWrapped("%s", ui("The NR conversion/composition pipeline could not initialize. Native output is preserved; use F8 for the actual failure record.",
            "NR 转换/合成管线初始化失败，保留原生输出；请用 F8 记录具体失败。"));
    }
'''+marker)
  write(p,s)
  # Resolve colour override through one production helper, tested independently below.
  p='src/nr_color_policy.inl';s=read(p)
  marker='const char *NrColorReasonName(NrColorReason reason) {'
  helper='''void ApplyRequestedUnverifiedHdrColor(NrColorPolicy &policy, bool allow, float strength) {
    if (allow && policy.color_reason == NrColorReason::UnverifiedHdr) {
        policy.color_allowed = true;
        policy.effective_color_strength = std::clamp(FiniteOr(strength, 0.0f), 0.0f, 2.0f);
        policy.color_reason = NrColorReason::ExperimentalUnverifiedHdr;
    }
}

'''
  s=swap(s,marker,helper+marker);write(p,s)
  p='src/nr_before_sr.cpp';s=read(p)
  s=swap(s,'''    if (g_config.allow_unverified_hdr_color && color_policy.color_reason == NrColorReason::UnverifiedHdr) {
        color_policy.color_allowed = true;
        color_policy.effective_color_strength = g_config.color_strength;
        color_policy.color_reason = NrColorReason::ExperimentalUnverifiedHdr;
    }''','''    ApplyRequestedUnverifiedHdrColor(color_policy, g_config.allow_unverified_hdr_color,
        g_config.color_strength);''')
  write(p,s)
  for p in ['scripts/validate-045-color.py','scripts/validate-045-companion.py','tests/test_045_ota.py']:
   if (HERE/p).exists():write(p,(HERE/p).read_text())
  git('add','.')
  for script in ['validate-beta04-structure.py','validate-044-compat.py','validate-retirement-return.py','validate-strength-cap.py','validate-recommended-picture.py','validate-043r2.py']:
   subprocess.run(['python3','scripts/'+script],cwd=root,check=True)
  paths=git('diff','--name-only',BASE).splitlines()
  entries=[]
  for path in paths:
   data=(root/path).read_bytes()
   req=urllib.request.Request('https://api.github.com/repos/'+REPO+'/git/blobs',data=json.dumps({'encoding':'utf-8','content':data.decode('utf-8')}).encode(),method='POST',headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'],'Content-Type':'application/json'})
   with urllib.request.urlopen(req,timeout=60) as r:sha=json.load(r)['sha']
   assert sha==hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
   entries.append({'path':path,'mode':git('ls-files','-s','--',path).split()[0],'type':'blob','sha':sha})
  print('REVIEWED_TREE_ENTRIES '+json.dumps({'base':BASE,'entries':entries}))
  print(git('diff','--stat',BASE))
  subprocess.run(['git','worktree','remove','--force',str(root)],cwd=HERE,check=True)

if __name__=='__main__':main()
