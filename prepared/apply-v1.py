from pathlib import Path
import subprocess, sys
r=Path(sys.argv[1]).resolve()
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip()=='d403388158b189103f76bb0a6a3d1a404782e02b'
def edit(path,old,new):
 p=r/path;s=p.read_text(encoding='utf-8-sig');assert s.count(old)==1,(path,old[:90],s.count(old));p.write_text(s.replace(old,new),encoding='utf-8',newline='\n')
def put(path,text):
 p=r/path;assert not p.exists(),path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8',newline='\n')
for name,folder in [('nr_command_pool_observation.inl','src'),('command_pool_native.inl','tests'),('targets_only_gpu.inl','tests')]:
 put(folder+'/'+name,Path(__file__).with_name(name).read_text(encoding='utf-8'))
cs='src/nr_command_state.inl'
edit(cs,'    bool known = false;\n    UINT initialization_dirty = 0;\n    UINT execution_dirty = 0;\n};','    bool known = false;\n    UINT initialization_dirty = 0;\n    UINT execution_dirty = 0;\n    UINT64 device_identity = 0; // private lifetime identity, not a device address\n};\nstd::atomic<UINT64> g_next_command_device_identity{1};')
edit(cs,'    NativeCommandState *state = nullptr; // Only NR carrier lists allocate this.','    NativeCommandState *state = nullptr; // Carriers and bounded prospective observation.\n    UINT64 prime_serial = 0; // Cleared before first actual carrier capture.')
edit(cs,'        ID3D12Device_SetPrivateData(native, kCommandStateMetaContract, sizeof(meta), &meta);','        meta.device_identity = g_next_command_device_identity.fetch_add(1, std::memory_order_relaxed);\n        ID3D12Device_SetPrivateData(native, kCommandStateMetaContract, sizeof(meta), &meta);')
edit(cs,'void CompleteCommandStateInstallation(unsigned long long token,','#include "nr_command_pool_observation.inl"\n\nvoid CompleteCommandStateInstallation(unsigned long long token,')
edit(cs,'        record->hooks_ready = ready;\n        record->next_hook_attempt','        record->hooks_ready = ready;\n        if (ready) RememberObservedCommandFamilyLocked(*record, native, observed_null);\n        record->next_hook_attempt')
edit(cs,'    if (record && record->state && !record->hooks_ready) {','    // Prospective only: actual successful Reset remains mandatory.\n    if (record && g_enabled_fast.load(std::memory_order_acquire))\n        PrimeCommandListBeforeResetLocked(*record, native);\n    if (record && record->state && !record->hooks_ready) {')
edit(cs,'        if (record->state != nullptr) { ReleaseNativeCommandState(*record->state); delete record->state; }','        if (record->prime_serial != 0) --g_primed_command_state_count;\n        if (record->state != nullptr) { ReleaseNativeCommandState(*record->state); delete record->state; }')
edit(cs,'    if (record != nullptr) {\n        if (record->state == nullptr) {','    if (record != nullptr) {\n        // A real carrier is permanently outside the speculative eviction pool.\n        if (record->prime_serial != 0) {\n            record->prime_serial = 0;\n            --g_primed_command_state_count;\n        }\n        if (record->state == nullptr) {')
edit(cs,'(d.recorded & ~(kDynamicViewport | kDynamicScissor | kDynamicBlend)) != 0 ||','(d.recorded & ~(kDynamicViewport | kDynamicScissor | kDynamicBlend | kDynamicTargets)) != 0 ||')
edit(cs,'        d.viewport_count > 16 || d.scissor_count > 16) return false;','''        d.viewport_count > 16 || d.scissor_count > 16) return false;
    // Raw application CPU handles may be recycled; only privately copied
    // contiguous RTV and independently owned DSV descriptors may be restored.
    if ((d.recorded & kDynamicTargets) &&
        (d.target_count > 8 ||
         (d.target_count && (!state.rtv_cache || !d.contiguous_targets || !d.targets[0].ptr)) ||
         (d.has_depth_target && (!state.dsv_cache || !d.depth_target.ptr)))) return false;''')
# Production-extracted policy tests keep all previous negative assertions.
t='scripts/validate-044-compat.py'
edit(t,'constexpr UINT64 kDynamicViewport=2,kDynamicScissor=4,kDynamicBlend=8;','constexpr UINT64 kDynamicViewport=2,kDynamicScissor=4,kDynamicBlend=8,kDynamicTargets=64;')
edit(t,' void *pipeline=nullptr,*raytracing_pipeline=nullptr;UINT unknown_domains=0;',' void *pipeline=nullptr,*raytracing_pipeline=nullptr,*rtv_cache=nullptr,*dsv_cache=nullptr;UINT unknown_domains=0;')
edit(t,' struct {UINT64 recorded=',' struct {UINT target_count=0;bool contiguous_targets=false,has_depth_target=false;struct Handle{unsigned long long ptr=0;}targets[8],depth_target;UINT64 recorded=')
edit(t,'(bit==1||bit==2||bit==3)','(bit==1||bit==2||bit==3||bit==6)')
edit(t,' return finish("actual bounded null-raster policy");',''' {auto t=s;t.dynamic.recorded=0x46;check(CanAutoRestoreRasterNull(t),"empty target set restores");
  t.dynamic.target_count=1;check(!CanAutoRestoreRasterNull(t),"raw RTV rejected");
  t.rtv_cache=&s;t.dynamic.contiguous_targets=true;t.dynamic.targets[0].ptr=1;
  check(CanAutoRestoreRasterNull(t),"owned RTV accepted");
  t.dynamic.has_depth_target=true;check(!CanAutoRestoreRasterNull(t),"missing DSV cache rejected");
  t.dsv_cache=&s;t.dynamic.depth_target.ptr=2;check(CanAutoRestoreRasterNull(t),"owned DSV accepted");
  t.dynamic.target_count=9;check(!CanAutoRestoreRasterNull(t),"RTV count bounded");
  t.dynamic.target_count=8;t.dynamic.contiguous_targets=false;check(!CanAutoRestoreRasterNull(t),"cache contiguous"); }
 return finish("actual bounded null-raster policy");''')
put('src/nr_shortcut_edge.h','''#pragma once
namespace nr_shortcut {
// Low-bit short taps are best effort; a held key must not repeat actions.
constexpr bool Pressed(short state, bool was_down) {
    return !was_down && (static_cast<unsigned short>(state) & 0x8001u) != 0;
}
}
''')
put('tests/shortcut_edge_test.cpp','''#include "nr_shortcut_edge.h"
#include <cassert>
#include <cstdio>
int main() {
    unsigned actions=0;bool down=false;
    const auto sample=[&](unsigned short raw) {
        if(nr_shortcut::Pressed(static_cast<short>(raw),down)) ++actions;
        down=(raw & 0x8000u)!=0;
    };
    sample(0);sample(0x8001);
    for(unsigned i=0;i<5000;++i)sample(i%2?0x8000:0x8001);
    assert(actions==1);sample(0);sample(1);assert(actions==2);
    sample(0);sample(0x8000);sample(0);sample(0x8001);assert(actions==4);
    sample(0);sample(0x7ffe);assert(actions==4);
    std::puts("shortcut physical-edge: held/repeat/consumed-low-bit/short-tap PASS");
}
''')
m='src/nr_before_sr.cpp'
edit(m,'#include "nr_feedback_ticket.h"','#include "nr_feedback_ticket.h"\n#include "nr_shortcut_edge.h"')
edit(m,'const bool f6_pressed = (f6_state & 0x0001) != 0 ||\n                            (f6_is_down && !f6_was_down);','const bool f6_pressed = nr_shortcut::Pressed(f6_state, f6_was_down);')
edit(m,'if ((f8_state & 0x0001) != 0 || (feedback_down && !feedback_key_was_down)) {','if (nr_shortcut::Pressed(f8_state, feedback_key_was_down)) {')
edit(m,'const bool stage_pressed = (f7_state & 0x0001) != 0 ||\n        (stage_key_is_down && !stage_key_was_down);','const bool stage_pressed = nr_shortcut::Pressed(f7_state, stage_key_was_down);')
p=r/'src/nr_settings_ui.inl';s=p.read_text(encoding='utf-8-sig');a=s.index('    const auto live_reason =',s.index('void DrawSettings('));b=s.index('    static ChainConfig pending',a);messages=s[a:b];s=s[:a]+s[b:]
marker='    const auto reason = static_cast<BypassReason>(g_last_bypass_reason.load());';assert s.count(marker)==1
s=s.replace(marker,'    // Keep controls stationary while live status text changes (#121).\n    ImGui::BeginChild("nr-live-status", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 7.0f), true);\n'+messages+marker)
s=s.replace('else if (!next.enabled || ActiveTransferStrength(next) <= 0.000001f)','else if (!current.config.enabled || ActiveTransferStrength(current.config) <= 0.000001f)')
s=s.replace('    if (ImGui::BeginTabBar("nr-main-tabs")) {','    ImGui::EndChild();\n    bool apply_discrete_edit = false;\n\n    if (ImGui::BeginTabBar("nr-main-tabs")) {')
s=s.replace('    changed |= ImGui::Checkbox(ui("Enable (F6)", "启用（F6）"), &next.enabled);','    apply_discrete_edit = ImGui::Checkbox(ui("Enable (F6)", "启用（F6）"), &next.enabled);\n    changed |= apply_discrete_edit;')
s=s.replace('if (has_pending && !ImGui::IsAnyItemActive()) {','if (has_pending && (apply_discrete_edit || !ImGui::IsAnyItemActive())) {')
s=s.replace('Unknown HDR inputs remain luminance-only. The post-processing preset follows colour only when the live resource/exposure contract permits it.','Unknown HDR uses luminance-only unless you explicitly try colour above. Storage, exposure and ownership safety still apply in both modes.')
s=s.replace('未知 HDR 输入仍只修改明暗。后置预设也只有在实际纹理和曝光条件允许时才跟随颜色。','未知 HDR 默认只修改明暗；上方手动试色是明确例外。两种模式仍保留存储、曝光与所有权安全检查。')
p.write_text(s,encoding='utf-8',newline='\n')
# Hook and pixel fixtures; no production tests are removed.
t='scripts/validate-command-state-native.ps1'
edit(t,'param([string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT)','param([string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT, [switch]$PoolNegativeControl)')
edit(t,'$main = Get-Content','''$production = $production.Replace('#include "nr_command_pool_observation.inl"', (Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_command_pool_observation.inl') -Raw))
if ($PoolNegativeControl) {
    $needle='PrimeCommandListBeforeResetLocked(*record, native);'
    if (!$production.Contains($needle)) { throw 'Pool negative anchor missing' }
    $production=$production.Replace($needle,'(void)0; // exact old lazy-only defect')
}
$main = Get-Content'''.replace("'src\nr_command", "'src\\nr_command"))
a="$pristine = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\\pristine_null_native.inl') -Raw"
edit(t,a,a+"\n$pristine = (Get-Content -LiteralPath (Join-Path $projectRoot 'tests\\command_pool_native.inl') -Raw) + \"`n\" + $pristine")
edit(t,"& $binary '--pristine-null'",'''if ($PoolNegativeControl) {
    $output=& $binary '--pristine-null' 2>&1
    $code=$LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if ($code -ne 41 -or ($output -join "`n") -notmatch 'POOL_FIRST_CAPTURE_ORACLE') { throw 'Wrong pool negative failure' }
    Write-Output 'Negative control passed: old lazy-only observation misses first carrier.'
    exit 0
}
& $binary '--pristine-null' ''')
edit('tests/pristine_null_native.inl','    const auto old_token=ReadCommandStateToken(list);','    if (!negative) CheckCommandPoolPriming(device,od);\n    const auto old_token=ReadCommandStateToken(list);')
t='tests/command_state_gpu_harness.cpp.in'
edit(t,'// @BLEND_ONLY_TEST@','// @BLEND_ONLY_TEST@\n// @TARGETS_ONLY_TEST@')
edit(t,'        unsigned int count = 0;','''        if (argc==2 && std::strcmp(argv[1],"--targets-only")==0) {
            for(bool depth:{false,true})for(bool inject:{false,true})RunTargetsOnly(gpu,depth,inject);
            std::puts("Targets-only WARP: four RTV/DSV controls/restores passed.");return 0;
        }
        unsigned int count = 0;''')
edit(t,'        std::printf("Command-state WARP validation: %u scenarios passed','''        if (!null_only) for(bool depth:{false,true})for(bool inject:{false,true}) {
            RunTargetsOnly(gpu,depth,inject);++count;
        }
        std::printf("Command-state WARP validation: %u scenarios passed''')
t='scripts/validate-command-state-gpu.ps1'
edit(t,'    [switch]$BlendStateNegativeControl','    [switch]$BlendStateNegativeControl,\n    [switch]$TargetsNegativeControl')
edit(t,'$DynamicStateNegativeControl,$BlendStateNegativeControl)','$DynamicStateNegativeControl,$BlendStateNegativeControl,$TargetsNegativeControl)')
a="$harness = $harness.Replace('// @BLEND_ONLY_TEST@', $blendFixture)"
edit(t,a,a+"\n$harness = $harness.Replace('// @TARGETS_ONLY_TEST@', (Get-Content -LiteralPath (Join-Path $projectRoot 'tests\\targets_only_gpu.inl') -Raw))\n"+'''if ($TargetsNegativeControl) {
    $marker='d.target_count, d.targets, d.contiguous_targets, d.has_depth_target ? &d.depth_target : nullptr'
    if (!$production.Contains($marker)) { throw 'Target replay anchor missing' }
    $production=$production.Replace($marker,'d.target_count, &g_wrong_target, TRUE, d.has_depth_target ? &d.depth_target : nullptr')
    $harness=$harness.Replace('// @NEGATIVE_CONTROL_CONSTANTS@','D3D12_CPU_DESCRIPTOR_HANDLE g_wrong_target{};')
    $harness=$harness.Replace('// @TARGET_NEGATIVE_BINDING@','g_wrong_target=rtv; // valid decoy')
}''')
edit(t,'$stem = if ($BlendStateNegativeControl)',"$stem = if ($TargetsNegativeControl) { 'command_state_gpu_targets_negative' } elseif ($BlendStateNegativeControl)")
edit(t,"if ($MetaProbeOnly) { & $binary",'''if ($TargetsNegativeControl) {
    $output=& $binary '--targets-only' 2>&1
    $code=$LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if ($code -ne 1 -or ($output -join "`n") -notmatch 'TARGETS_REPLAY_PIXEL_ORACLE') { throw 'Wrong target negative failure' }
    Write-Host 'Negative control passed: recycled target rejected by WARP pixels.'
    exit 0
}
if ($MetaProbeOnly) { & $binary''')
subprocess.run(['git','diff','--check'],cwd=r,check=True)
print('Reviewed prospective-pool, target replay and UI changes applied to exact published 0.4.5.')
