"""Prepare audited blobs only. This script never creates/updates branches or releases.
The connector constructs the final reviewed commits and PRs from the returned SHAs.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import urllib.request

REPO='smartLanny/dlss5-nr-before-sr-lab'
BASE='647ac292433bf00506eead16ccda1a30c2b3a153'
ROOT=Path(__file__).resolve().parents[1]

def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT)
def original(path):return git('show',BASE+':'+path).decode('utf-8')
def replace_once(text,old,new):
    if text.count(old)!=1:raise ValueError('Patch anchor must occur exactly once: '+old[:100])
    return text.replace(old,new,1)
def definition(text,marker):
    start=text.index(marker);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

DESTROY=r'''void DestroyContext(NrContext &context) {
    if (context.create_dependencies_quarantined ||
        context.lifecycle_dependencies_quarantined) {
        // Keep the complete unproven dependency closure until process exit.
        return;
    }
    {
        // Serialize returned-error handling with every other NR API call.
        // This is not an exception/AV recovery boundary: no opaque fault is caught.
        ScopedSrwExclusive api_guard(&g_nr_api_lock);
        if (g_lifecycle_tracking_uncertain.load(std::memory_order_acquire)) {
            context.lifecycle_dependencies_quarantined = true;
            return;
        }
        if (context.nr_handle != nullptr) {
            if (!g_nr_api.initialized || g_nr_api.bridge_release == nullptr ||
                g_nr_api.signed_release == nullptr) {
                context.lifecycle_dependencies_quarantined = true;
                MarkLifecycleUncertain("NR retirement Release entry unavailable");
                return;
            }
            const NgxResult result = g_nr_api.bridge_release(
                g_nr_api.signed_release, context.nr_handle);
            Log("NR ReleaseFeature handle=%p result=0x%08X",
                context.nr_handle, result);
            if (result != kNgxSuccess) {
                // A returned error does not transfer ownership back to us.
                // In particular, do not destroy parameters/textures or retry Release.
                context.lifecycle_dependencies_quarantined = true;
                MarkLifecycleUncertain("NR retirement Release returned an error");
                return;
            }
            context.nr_handle = nullptr;
        }
        if (context.params != nullptr) {
            if (g_nr_api.destroy == nullptr) {
                context.lifecycle_dependencies_quarantined = true;
                MarkLifecycleUncertain("NR retirement DestroyParameters entry unavailable");
                return;
            }
            const NgxResult result = g_nr_api.destroy(context.params);
            if (result != kNgxSuccess) {
                Log("NR retirement DestroyParameters returned result=0x%08X; "
                    "remaining dependencies quarantined, no automatic retry.", result);
                context.lifecycle_dependencies_quarantined = true;
                MarkLifecycleUncertain("NR retirement DestroyParameters returned an error");
                return;
            }
            context.params = nullptr;
        }
    }
    for (auto &slot : context.slots) DestroySlot(slot);
    context = {};
}'''

def workflow(name,script):
    return f'''name: {name}
on:
  pull_request:
    branches: [main, release/0.4]
permissions:
  contents: read
jobs:
  production-return-oracles:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          persist-credentials: false
      - uses: actions/checkout@v4
        with:
          ref: {BASE}
          path: historical-control
          persist-credentials: false
      - name: Historical production must fail exact oracle
        run: python3 scripts/{script} --source historical-control/src/nr_before_sr.cpp --expect-rejection
      - name: Current production must pass
        run: python3 scripts/{script}
'''

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs):raise RuntimeError('Unexpected redirect refused')

def main():
    if os.environ.get('GITHUB_REPOSITORY')!=REPO or os.environ.get('GITHUB_REF')!='refs/heads/prep/sep06-review-fixes':
        raise RuntimeError('Preparation is restricted to the isolated owner branch')
    cpp=original('src/nr_before_sr.cpp')
    if git('rev-parse',BASE+':src/nr_before_sr.cpp').decode().strip()!='abaf4eec0c5da804712466674ef2604196a347f4':
        raise RuntimeError('Reviewed source blob changed')
    old_destroy=definition(cpp,'void DestroyContext(NrContext &context)')
    retirement=replace_once(cpp,old_destroy,DESTROY)
    strength=replace_once(cpp,'FiniteOr(config.transfer_strength, 0.0f), 0.0f, 8.0f','FiniteOr(config.transfer_strength, 0.0f), 0.0f, 4.0f')
    strength=replace_once(strength,'std::isfinite(blend) ? blend : 0.0f, 0.0f, 8.0f','std::isfinite(blend) ? blend : 0.0f, 0.0f, 4.0f')
    ui=replace_once(original('src/nr_settings_ui.inl'),'constexpr float maximum_strength = 8.0f;','constexpr float maximum_strength = 4.0f;')
    ui=replace_once(ui,'Higher values can amplify low-resolution noise; this is not the model\'s internal strength.',
        'The public range is 0-4; older requests above 4 are clamped. Higher values can amplify low-resolution noise and colour distortion; this is not the model\'s internal strength.')
    ui=replace_once(ui,'提高数值可能放大低分辨率噪点；这项不是模型内部强度。',
        '范围为 0–4，旧配置超过 4 会限制到 4。较高数值可能放大低分辨率噪点和颜色失真；这项不是模型内部强度。')
    ini=replace_once(original('config/nr_before_sr.ini'),'; Multiplicative strength: 1 normal, 2 doubled, 4 strong, 8 extreme/diagnostic.',
        '; Final blend range 0..4: 0 native bypass, 1 normal, 2 doubled, 4 maximum (not recommended).\n; Existing values above 4 are clamped on load. Model Intensity remains a separate 0..2 control.')
    retirement_test='scripts/validate-retirement-return.py';strength_test='scripts/validate-strength-cap.py'
    with tempfile.TemporaryDirectory(prefix='reviewed-candidates-') as td:
        td=Path(td);old=td/'old.cpp';nr=td/'retirement.cpp';blend=td/'strength.cpp'
        for path,text in [(old,cpp),(nr,retirement),(blend,strength)]:path.write_text(text,encoding='utf-8')
        for script,candidate in [(retirement_test,nr),(strength_test,blend)]:
            subprocess.run(['python3',str(ROOT/script),'--source',str(old),'--expect-rejection'],check=True)
            command=['python3',str(ROOT/script),'--source',str(candidate)]
            if script==strength_test:command.append('--math-only')
            subprocess.run(command,check=True)
    notes_retirement='''# NR retirement returned-error ownership fix (unreleased)

Refs #106. This is a concrete ordinary-error defect found during the September feedback audit, not proof that #49/#97/#98/#99/#101 share a crash root cause.

The old production DestroyContext cleared the NR handle and freed dependent parameters/textures even when ReleaseFeature returned an error. DestroyParameters errors and missing teardown exports were also ignored. The new path clears each owner only after its corresponding success, retains all remaining dependencies on failure, and uses the existing process-lifetime quarantine. Subsequent retirement calls do not reenter the provider after quarantine. F6 cannot clear the existing lifecycle latch.

No SEH/AV is caught, no exception handler or driver is patched, no blind retry/rebuild is added, and fences/queues/resource admission are not weakened. This does not implement the entire hard-fault/poison state machine in #106. Previously admitted work and real driver fault behavior need separate analysis/runtime testing.

Regression: actual production function extraction with ordinary Release/DestroyParameters failure, missing exports, repeated cleanup, pre-existing quarantine and successful cleanup controls. A pinned historical implementation must fail exact ownership oracles; the current one must pass. Full existing CI remains required.

Risks: conservative quarantine retains the bounded existing pool until process exit and disables further NR admission instead of attempting recovery. FeatureNotFound is not treated as success for an owned NR retirement. This may skip NR after an otherwise recoverable provider error; it avoids guessing ownership. No game/NVIDIA/runtime performance validation has been performed here. No Release or tag is changed by this PR.
'''
    notes_strength='''# Final effect strength limited to 4 (unreleased)

Refs #102. The public final blend slider changes from 0–8 to 0–4 in both processing modes. Existing TransferStrength values above 4 normalize to 4, and PostTransferStrength stays synchronized; values 0–4, the new-install default 1.2 and master-off are unchanged. Nonfinite requests retain the existing safe-zero behavior.

This is **final effect strength**, not the model Intensity control (still 0–2). No hidden >4 override is added. UI, configuration comments and diagnostic screenshot naming share the same cap. This does not claim that 4 is a recommended setting or guarantee colour fidelity; the 0.5 high-strength protection and #103 colour-follow research remain separate.

Tests compile the actual production normalization statements, reject the historical 8x implementation, and check UI/INI/filename/default wiring. Runtime INI write permissions and visual quality still need normal use validation. Existing complete CI must remain green; no shader/NR resolution/MV/jitter/HDR/FG algorithm change is included. This PR does not create or replace a Release, and should later be forward-synced into next/0.5 through the canonical maintenance flow.
'''
    lanes={
      'retirement':{'src/nr_before_sr.cpp':retirement,retirement_test:(ROOT/retirement_test).read_text(),
        '.github/workflows/retirement-return-validate.yml':workflow('NR retirement returned-error ownership',Path(retirement_test).name),
        'docs/fixes/NR_RETIREMENT_RETURN_ERRORS.md':notes_retirement},
      'strength':{'src/nr_before_sr.cpp':strength,'src/nr_settings_ui.inl':ui,'config/nr_before_sr.ini':ini,
        strength_test:(ROOT/strength_test).read_text(),'.github/workflows/strength-cap-validate.yml':workflow('Public final-strength cap',Path(strength_test).name),
        'docs/fixes/FINAL_STRENGTH_CAP.md':notes_strength}}
    opener=urllib.request.build_opener(NoRedirect());url='https://api.github.com/repos/'+REPO+'/git/blobs'
    for lane,files in lanes.items():
        entries=[]
        for path,text in files.items():
            data=json.dumps({'content':text,'encoding':'utf-8'}).encode()
            req=urllib.request.Request(url,data=data,method='POST',headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'],
                'Accept':'application/vnd.github+json','Content-Type':'application/json','X-GitHub-Api-Version':'2022-11-28','User-Agent':'reviewed-source-preparation'})
            with opener.open(req,timeout=60) as r:result=json.load(r)
            blob=text.encode();expected=hashlib.sha1(b'blob '+str(len(blob)).encode()+b'\0'+blob).hexdigest()
            if result.get('sha')!=expected:raise RuntimeError('Server blob differs from verified bytes')
            entries.append({'path':path,'mode':'100644','type':'blob','sha':expected})
        print('REVIEWED_TREE_ENTRIES '+json.dumps({'lane':lane,'base':BASE,'entries':entries}),flush=True)

if __name__=='__main__':main()
