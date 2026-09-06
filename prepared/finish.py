from pathlib import Path
import sys,subprocess
r=Path(sys.argv[1]).resolve()
def edit(path,old,new):
 p=r/path;s=p.read_text(encoding='utf-8-sig');assert s.count(old)==1,(path,old[:100],s.count(old));p.write_text(s.replace(old,new),encoding='utf-8',newline='\n')
(r/'src/nr_ui_activity.h').write_text(Path(__file__).with_name('nr_ui_activity.h').read_text(encoding='utf-8'),encoding='utf-8')
edit('src/nr_before_sr.cpp','#include "nr_shortcut_edge.h"','#include "nr_shortcut_edge.h"\n#include "nr_ui_activity.h"')
p=r/'src/nr_settings_ui.inl';s=p.read_text(encoding='utf-8')
helper='''// ImGui can be drawn by more than one swapchain; do not block another caller.
nr_ui_activity::Sample ReadUiActivity() {
    static SRWLOCK lock=SRWLOCK_INIT;
    static nr_ui_activity::Window window;
    if(!TryAcquireSRWLockExclusive(&lock)) return {};
    const auto result=window.Observe(GetTickCount64(),{g_carrier_evaluations.load(),
        g_nr_successful_evaluations.load(),g_nr_transfer_submitted_frames.load()});
    ReleaseSRWLockExclusive(&lock);
    return result;
}

'''
assert s.count('void DrawSettings(')==1;s=s.replace('void DrawSettings(',helper+'void DrawSettings(')
a='    ImGui::BeginChild("nr-live-status", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 7.0f), true);'
b=a+'''
    const auto activity=ReadUiActivity();
    if(activity.valid) {
        ImGui::TextWrapped(ui("Last %llu ms (process): SR %llu | NR %llu | submitted %llu",
            "最近 %llu 毫秒（进程）：SR %llu | NR %llu | 已提交 %llu"),
            static_cast<unsigned long long>(activity.elapsed_ms),
            static_cast<unsigned long long>(activity.delta.carriers),
            static_cast<unsigned long long>(activity.delta.nr),
            static_cast<unsigned long long>(activity.delta.submitted));
    } else ImGui::TextDisabled("%s",ui("Sampling actual activity...", "正在采样实际活动……"));
    SettingsTooltip(ui("Historical process-wide counter deltas, not FPS or proof of picture quality. Enable is your request; NR success and composition submission are separate stages. Raw per-carrier reasons remain in F8.",
        "这是历史进程计数增量，不是帧率或画质证明。开关是请求，NR 成功与合成提交是不同阶段；F8 仍保留真实的逐载体原因。"));'''
assert s.count(a)==1;s=s.replace(a,b)
p.write_text(s,encoding='utf-8',newline='\n')
(r/'tests/ui_activity_test.cpp').write_text('''#include "nr_ui_activity.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main() {
    using namespace nr_ui_activity;
    Window w;
    assert(!w.Observe(100,{10,5,4}).valid);
    assert(!w.Observe(1099,{12,6,4}).valid);
    auto s=w.Observe(1100,{20,10,8});
    assert(s.valid && s.elapsed_ms==1000 && s.delta.carriers==10 && s.delta.nr==5 && s.delta.submitted==4);
    auto held=w.Observe(1110,{21,11,9});
    assert(held.delta.carriers==s.delta.carriers && held.elapsed_ms==s.elapsed_ms);
    s=w.Observe(2100,{20,10,8});assert(s.valid && !s.delta.carriers && !s.delta.nr && !s.delta.submitted);
    s=w.Observe(12100,{30,20,12});assert(s.elapsed_ms==10000 && s.delta.carriers==10);
    assert(!w.Observe(12200,{0,0,0}).valid);
    assert(!w.Observe(100,{1,1,1}).valid);
    s=w.Observe(1100,{2,1,1});assert(s.valid && s.delta.carriers==1 && !s.delta.nr);
    assert(!w.Observe(1200,{2,0,1}).valid);
    assert(!w.Observe(1300,{2,0,0}).valid);
    auto hi=std::numeric_limits<Count>::max();Window huge;
    assert(!huge.Observe(0,{hi-1,hi-1,hi-1}).valid);
    s=huge.Observe(1000,{hi,hi,hi});assert(s.delta.nr==1 && s.delta.submitted==1);
    std::puts("UI activity: interval/idle/no-submit/reset/stale/overflow checks PASS");
}
''',encoding='utf-8')
# Unique build identity, no change to existing Release/tag or automatic publisher.
for path in ['src/nr_before_sr.cpp','src/nr_before_sr.rc','scripts/build.ps1','scripts/package-045.py','scripts/validate-beta04-structure.py','.github/workflows/beta042-validate.yml']:
 p=r/path;s=p.read_text(encoding='utf-8-sig')
 if path=='scripts/validate-beta04-structure.py':
  # Keep historical notes path; only the inspected identities change.
  s=s.replace('docs/releases/beta0.4.5.md','docs/fixes/BETA045_FOLLOWUP.md')
 s=s.replace('beta0.4.5','beta0.4.5-fix1')
 s=s.replace('0.4.5 beta','0.4.5-fix1 beta').replace('0,4,5,0','0,4,5,1')
 if path=='.github/workflows/beta042-validate.yml':
  s=s.replace('$v.FilePrivatePart -ne 0','$v.FilePrivatePart -ne 1')
  s=s.replace('docs/releases/beta0.4.5-fix1.md','docs/fixes/BETA045_FOLLOWUP.md')
  s=s.replace('name: beta045-ota','name: beta045-fix1-ota').replace('name: beta045-d3d12-candidate','name: beta045-fix1-candidate')
  a='          python3 scripts/validate-043r2.py --compiler g++'
  assert a in s;s=s.replace(a,a+'''\n          g++ -std=c++20 -Wall -Wextra -Werror -Isrc tests/shortcut_edge_test.cpp -o /tmp/shortcut-edge
          /tmp/shortcut-edge
          g++ -std=c++20 -Wall -Wextra -Werror -Isrc tests/ui_activity_test.cpp -o /tmp/ui-activity
          /tmp/ui-activity''')
  a='          ./scripts/validate-command-state-native.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT'
  assert a in s;s=s.replace(a,a+'\n          ./scripts/validate-command-state-native.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -PoolNegativeControl')
  a='          ./scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -BlendStateNegativeControl'
  assert a in s;s=s.replace(a,a+'\n          ./scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -TargetsNegativeControl')
 p.write_text(s,encoding='utf-8',newline='\n')
# Numeric/PE test fixture must represent the new candidate, not reject good binaries.
p=r/'tests/test_045_ota.py';s=p.read_text(encoding='utf-8');s=s.replace('0.4.5 beta','0.4.5-fix1 beta');p.write_text(s,encoding='utf-8',newline='\n')
(r/'.github/workflows/followup-validate.yml').write_text('''name: Validate 0.4.5 fix1 candidate
on:
  pull_request:
    branches: [release/0.4.5-test]
  push:
    branches: [fix/045-flicker-input-targets]
permissions:
  contents: read
jobs:
  validate:
    uses: ./.github/workflows/beta042-validate.yml
  feedback:
    uses: ./.github/workflows/feedback-integrity.yml
''',encoding='utf-8')
(r/'docs/fixes/BETA045_FOLLOWUP.md').write_text('''# beta0.4.5-fix1: non-DX11 compatibility and UI candidate

This branch builds on the published beta0.4.5, PR #117. 不含 DX11、0.5算法。
Not a Release; do not overwrite the existing beta0.4.5 tag or files.

## Runtime changes
- #121: start observation before a real Reset on cold members of an already
  verified device/vtable/interface family. No binding is guessed or copied
  between lists. Fresh successful Reset is still mandatory. A bounded cache
  evicts only records NEVER used as NR carriers, never queued plugin work.
- #123: admit only fully observed known-null viewport/scissor/blend/owned
  RTV/DSV state. Existing private CPU descriptor copies are deep-snapshotted
  and replayed; recycled application descriptor slots must not affect replay.
  Unknown/tainted/meta-command/queue/resource contracts remain fail-closed.
- #121 UI: fixed-height live status area stops checkbox/slider movement.
  F6/F7/F8 recognize a physical down edge once; low-bit taps are best effort.
  Mouse Enable commits through the normal generation/capture guard without
  waiting for unrelated active controls. No global font/focus/layout changes.
- Recent process SR/NR/submission counts explain actual activity separately
  from Enable. They are historical counts, not FPS or visual quality proof.
- Existing F8 independent TXT/ZIP and explicit HDR colour opt-in remain; fix
  the contradictory HDR caption. No proprietary runtime or INI is replaced.

## Verification
Production policy and shortcut tests, real native Reset hooks, rotating-pool
capacity/eviction/failed Reset/device/vtable/interface controls, and WARP
RTV/DSV recycled-handle pixel fixtures. Old lazy-only and wrong RTV replay
are explicit negative controls. Full Windows/shader/static analysis/dual-
language build checks must pass on the PR head before a test package is used.
No actual game, NVIDIA model, HDR quality, FG cadence or performance result
is claimed. Prospective observation adds bounded CPU/memory/descriptor work.

## Minimal player test
Exit game, back up the old addon/companion outside its directory; install ONE
language and matching nrchain_nvngx.dll, keep INI/NVIDIA DLL/ReShade/providers.
ACShadows RTX5060: old scene, open Home, click Enable once and briefly hold F6;
check visual flicker, not just label changes. Starfield RTX5070Ti Laptop: same
scene, F6 compare. Use F8 TXT/ZIP; F7 only for convenient visual evidence.
Stop and revert on persistent black screen or crash. No driver swap/debugger.

## Unresolved
#122 NBA2K26 has no observed Feature1 carrier; this is NOT NBA2K27 #68 and is
not fixed by changing null-state policy. #82 full independent NR precision
requires actual post-SR resampling/guide/history work; no fake post slider is
added. Other game flicker, #49/#18 crashes and #63 external MFG ownership are
not declared fixed. All game issues stay open pending exact-build evidence.
''',encoding='utf-8')
subprocess.run(['git','diff','--check'],cwd=r,check=True)
print('Candidate identity/UI activity/full CI wiring ready.')
