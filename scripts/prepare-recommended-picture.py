"""Prepare validated reset-button blobs; never move branches, tags or releases."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import subprocess
import tempfile
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('prep',ROOT/'scripts/prepare-sep06-fixes.py')
prep=importlib.util.module_from_spec(spec);spec.loader.exec_module(prep)


def main():
    if os.environ.get('GITHUB_REPOSITORY')!=prep.REPO or os.environ.get('GITHUB_REF')!='refs/heads/prep/sep06-review-fixes':
        raise RuntimeError('Wrong preparation branch')
    ui=prep.original('src/nr_settings_ui.inl')
    if prep.git('rev-parse',prep.BASE+':src/nr_settings_ui.inl').decode().strip()!='b28a034def4af05f02966151930a10ac227a2296':
        raise RuntimeError('Reviewed UI blob changed')
    ui=prep.replace_once(ui,'#include "nr_present_api_status.h"','#include "nr_present_api_status.h"\n#include "nr_recommended_picture.inl"')
    marker='    changed |= ImGui::Checkbox(ui("Enable (F6)", "启用（F6）"), &next.enabled);'
    addition='''
    if (ImGui::Button(ui("Restore recommended picture settings", "一键恢复推荐画质"))) {
        RestoreRecommendedPictureSettings(next);
        changed = true;
    }
    SettingsTooltip(ui(
        "Restores Before SR, strength 1.2, colour request 0.75 and the shipped picture defaults. Exits diagnostic identity and disables unverified HDR colour/peak experiments. Keeps Enable (F6), depth/jitter/R8 game contracts and feedback preferences. Uses the normal save/history rules; runtime safety faults are not cleared.",
        "恢复前置 NR、强度 1.2、颜色请求 0.75 等随包推荐画质；退出诊断原图，关闭未验证 HDR 调色与高光实验。保留 F6 总开关、深度/Jitter/R8 游戏兼容设置和反馈偏好；按正常规则保存及更新历史，不清除运行时安全故障。"));'''
    ui=prep.replace_once(ui,marker,marker+addition)
    files={'src/nr_settings_ui.inl':ui,
      'src/nr_recommended_picture.inl':(ROOT/'src/nr_recommended_picture.inl').read_text(encoding='utf-8'),
      'scripts/validate-recommended-picture.py':(ROOT/'scripts/validate-recommended-picture.py').read_text(encoding='utf-8')}
    with tempfile.TemporaryDirectory(prefix='nr-recommended-tree-') as td:
        td=Path(td)
        for path,text in {**files,'src/nr_before_sr.cpp':prep.original('src/nr_before_sr.cpp'),
                           'config/nr_before_sr.ini':prep.original('config/nr_before_sr.ini')}.items():
            dst=td/path;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_text(text,encoding='utf-8')
        command=['python3',str(td/'scripts/validate-recommended-picture.py'),'--root',str(td)]
        subprocess.run(command,check=True);subprocess.run(command+['--negative-control'],check=True)
    files['.github/workflows/recommended-picture-validate.yml']='''name: Recommended picture reset
on:
  pull_request:
    branches: [main, release/0.4]
permissions:
  contents: read
jobs:
  production-defaults:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          persist-credentials: false
      - name: Production reset and independent INI defaults
        run: python3 scripts/validate-recommended-picture.py
      - name: Wrong legacy default must fail exact oracle
        run: python3 scripts/validate-recommended-picture.py --negative-control
'''
    files['docs/fixes/RESTORE_RECOMMENDED_PICTURE.md']='''# Restore recommended picture settings (unreleased)

Refs #110. The current 0.4 simplified overlay lost the older one-click reset. A button in the Picture tab restores the shipped picture defaults through the same pending configuration transaction used for ordinary edits.

Targets: Before SR; final strength 1.2 in both modes; Quality work mode and saved custom ratio 0.75; colour request 0.75; maximum luma ratio 4; lighting/edge protection 0.20/0.25; hybrid proxy curve; model intensity/local tone/local structure 1; default style; automatic face mask off with skin preference -1; UI correction on; warmup 2; original-SR retry on. Diagnostic identity, detail/motion experiments, unverified HDR colour and peak-shoulder experiments are reset to their shipped defaults. Reset-frame suppression stays enabled.

This is not a destructive factory reset: Enable/F6, provider-specific depth/jitter/R8 encoding and feedback preferences are preserved. Runtime hook/ownership/resource/queue/quarantine faults are never cleared. The tooltip states these boundaries. Colour 0.75 is a requested value; existing colour admission still applies. This PR does not implement #109's unverified-colour policy change.

The button remains inside the existing F8/F7 configuration hold. It writes only the local pending candidate, then uses ApplyOverlayConfig's generation check and ApplyConfig/SaveConfig/history semantics. No out-of-band INI overwrite, capture cancellation, forced resource rebuild or DLL change occurs. Read-only-directory behavior remains the existing live-settings fallback.

Do not use ChainConfig{} as the reset recipe: legacy aggregate colour/luma/UI-correction values differ from the shipped INI. The production helper and actual ChainConfig are compiled in the regression; all 27 changed fields are compared with the independent shipped INI. All remaining configuration fields must be explicitly classified as preserved. Repeated reset is idempotent and both master states, all R8 profiles, depth/jitter and perf identity survive. The wrong legacy colour default is rejected by an exact negative oracle. Source assertions cover the capture hold, pending generation, persistence and normal history path.

Source/host tests and complete CI are not actual ReShade or NVIDIA/game runtime verification. Verify a normal reset from altered picture settings, capture-busy behavior, a read-only folder and continued native fallback in a future uniquely identified candidate. This PR is for release/0.4; next/0.5 must receive it through canonical forward synchronization and explicitly add its own new preferences to the reset table before #110 is fully closed. No existing Release/tag/package is changed here.
'''
    opener=urllib.request.build_opener(prep.NoRedirect());url='https://api.github.com/repos/'+prep.REPO+'/git/blobs'
    entries=[]
    for path,text in files.items():
        req=urllib.request.Request(url,data=json.dumps({'content':text,'encoding':'utf-8'}).encode(),method='POST',
            headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'],'Accept':'application/vnd.github+json',
                     'Content-Type':'application/json','X-GitHub-Api-Version':'2022-11-28','User-Agent':'reviewed-source-preparation'})
        with opener.open(req,timeout=60) as response:result=json.load(response)
        blob=text.encode();sha=hashlib.sha1(b'blob '+str(len(blob)).encode()+b'\0'+blob).hexdigest()
        if result.get('sha')!=sha:raise RuntimeError('Blob content mismatch')
        entries.append({'path':path,'mode':'100644','type':'blob','sha':sha})
    print('REVIEWED_TREE_ENTRIES '+json.dumps({'base':prep.BASE,'entries':entries}),flush=True)

if __name__=='__main__':main()
