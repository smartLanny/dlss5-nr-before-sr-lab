"""One-shot source edits, restricted to the exact reviewed R3 baseline blobs.
Removed from the final candidate tree after the branch-only action commits edits.
"""
from pathlib import Path
import hashlib
import subprocess

ROOT=Path(__file__).resolve().parents[2]
def read(path): return (ROOT/path).read_text(encoding='utf-8-sig')
def write(path,text): (ROOT/path).write_text(text,encoding='utf-8')
def edit(text,old,new,count=1):
    if text.count(old)!=count: raise RuntimeError('Unexpected patch anchor/count: '+old[:120])
    return text.replace(old,new)
def verify(path,sha):
    b=(ROOT/path).read_bytes()
    actual=hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()
    if actual!=sha: raise RuntimeError('Unreviewed source blob: '+path)
for path,sha in {
 'src/nr_before_sr.cpp':'abaf4eec0c5da804712466674ef2604196a347f4',
 'src/nr_command_state.inl':'922f0cff2b64eb56fbf229d8b3a579b44f91bc43',
 'src/nr_settings_ui.inl':'b28a034def4af05f02966151930a10ac227a2296',
}.items(): verify(path,sha)

# Red control is specifically the old source's forwarding regression, not any
# arbitrary compiler failure. The caller checks the named failing assertions.
cpp=read('src/nr_before_sr.cpp')
start=cpp.index('NgxResult HookEvaluate(')
end=cpp.index('NgxResult HookRelease(',start)
block=cpp[start:end]
for name in ['evaluate','evaluate_c','core_evaluate','dlssd_evaluate']:
    import re
    pattern=r'return g_original_'+name+r'\(\s*command_list, handle, params, callback\);'
    block,n=re.subn(pattern,'return CallNativeEvaluatePassThrough(\n            g_original_'+name+', command_list, handle, params, callback);',block)
    if n!=1: raise RuntimeError('Expected one raw fast Evaluate forwarding call: '+name)
cpp=cpp[:start]+block+cpp[end:]
cpp=edit(cpp,'next.mode = 1; next.enabled = true;', 'next.mode = 1; next.enabled = true;') if False else cpp
# Explicit post-state failures formerly only logged when output was redirected.
cpp=edit(cpp,
'''            } else if (after_sr.redirected_output) {
                Log("[command-state] post-SR state unavailable; native SR is "''',
'''            } else if (after_sr.redirected_output) {
                Log("[command-state] post-SR state unavailable; native SR is "''')
marker='''                result = original(command_list, handle, params, callback);
            }
        } else if (after_sr.redirected_output &&'''
cpp=edit(cpp,marker,'''                result = original(command_list, handle, params, callback);
            } else {
                static unsigned int post_state_failures = 0;
                const unsigned int count = ++post_state_failures;
                if (count <= 3 || (count % 600) == 0) {
                    const auto state = SnapshotCommandStateEvidence();
                    Log("[post-SR] stopped after successful native SR: stage=command-state "
                        "reason=%s prepared-only=1 count=%u; original Output retained, "
                        "no Feature18 call was made.", state.reason, count);
                }
            }
        } else if (after_sr.redirected_output &&''')
cpp=cpp.replace('beta0.4.3r3','beta0.4.4-test1')
write('src/nr_before_sr.cpp',cpp)

state=read('src/nr_command_state.inl')
policy=r'''
// A separate, bounded subset: only viewport/scissor arrays changed after a
// fully observed successful Reset. No handles, descriptors, predication,
// vertex/stream-output buffers, queries, opaque commands or unknown domains
// are admitted by this path. Full dynamic replay is still experimental.
bool CanAutoRestoreRasterNull(const NativeCommandState &state) {
    const auto &d = state.dynamic;
    if (!state.known || !state.null_pipeline_baseline || state.pipeline ||
        state.raytracing_pipeline || !state.null_guard_covered ||
        !state.null_guard_reset_observed || state.null_guard_tainted ||
        state.unknown_domains != 0 || d.vertex_mask != 0 || d.stream_mask != 0 ||
        d.recorded == 0 || (d.recorded & ~(kDynamicViewport | kDynamicScissor)) != 0 ||
        d.viewport_count > 16 || d.scissor_count > 16) return false;
    for (UINT i = 0; i < d.viewport_count; ++i) {
        const auto &v = d.viewports[i];
        // Ordered bounded comparisons also reject NaN and infinities.
        if (!(v.TopLeftX >= D3D12_VIEWPORT_BOUNDS_MIN &&
              v.TopLeftX <= D3D12_VIEWPORT_BOUNDS_MAX &&
              v.TopLeftY >= D3D12_VIEWPORT_BOUNDS_MIN &&
              v.TopLeftY <= D3D12_VIEWPORT_BOUNDS_MAX &&
              v.Width >= 0 && v.Height >= 0 &&
              v.Width <= D3D12_VIEWPORT_BOUNDS_MAX - v.TopLeftX &&
              v.Height <= D3D12_VIEWPORT_BOUNDS_MAX - v.TopLeftY &&
              v.MinDepth >= 0 && v.MaxDepth <= 1 && v.MinDepth <= v.MaxDepth))
            return false;
    }
    for (UINT i = 0; i < d.scissor_count; ++i) {
        const auto &r = d.scissors[i];
        if (r.left > r.right || r.top > r.bottom) return false;
    }
    return true;
}
'''
state=edit(state,'bool CanRestoreNativeCommandState(',policy+'\nbool CanRestoreNativeCommandState(')
state=edit(state,'        const bool pristine = record->state && CanAutoRestorePristineNull(*record->state);',
'''        const bool pristine = record->state && CanAutoRestorePristineNull(*record->state);
        const bool raster = record->state && CanAutoRestoreRasterNull(*record->state);''')
state=edit(state,'                experimental || pristine);','                experimental || pristine || raster);')
state=edit(state,'record->state->null_pipeline_baseline && !experimental && !pristine ?',
                 'record->state->null_pipeline_baseline && !experimental && !pristine && !raster ?')
state=edit(state,'        if (captured) evidence.route = pristine && !experimental ? "pristine-null" :',
'''        if (captured) evidence.route = pristine && !experimental ? "pristine-null" :
            raster && !experimental ? "guarded-null-raster" :''')
state=edit(state,'    g_command_state_registration_repair_needed.store(record == nullptr, std::memory_order_relaxed);',
'''    // A different, registered view can evaluate before this missing list is
    // submitted. It must not erase the pending recovery request (SF6/multi-view).
    // Keep it sticky; the submission caller already gates work on master/traffic.
    if (record == nullptr)
        g_command_state_registration_repair_needed.store(true, std::memory_order_relaxed);''')
write('src/nr_command_state.inl',state)

ui=read('src/nr_settings_ui.inl')
ui=edit(ui,'next.mode = 1; next.enabled = true;','next.mode = 1; next.enabled = true; next.identity_use_original = false;')
# Explicit user action, never a silent configuration migration or reset.
anchor='''    if (ImGui::CollapsingHeader(ui("Advanced picture controls", "高级画质"))) {'''
ui=edit(ui,anchor,'''    if (next.identity_use_original && ImGui::Button(ui("Show NR picture (leave diagnostic identity)", "显示 NR 效果（退出诊断原图）"))) {
        next.identity_use_original = false; changed = true;
    }
    if (ImGui::CollapsingHeader(ui("Advanced picture controls", "高级画质"))) {''')
old='''    if (next.work_mode == 5)
        changed |= ImGui::SliderFloat(ui("NR size ratio", "NR 尺寸比例"), &next.custom_work_scale, .5f, 1.f, "%.2f");'''
new='''    if (next.work_mode == 5) {
        float percent = next.custom_work_scale * 100.0f;
        if (ImGui::SliderFloat(ui("NR work resolution (%)", "NR 工作分辨率（%）"), &percent, 50.0f, 100.0f, "%.0f%%")) {
            next.custom_work_scale = percent * 0.01f; changed = true;
        }
        SettingsTooltip(ui("Linear NR size relative to game render input, not output or pixel count. 50% uses about 25% of the input pixels. The current safe range is 50-100%; use Enable/F6 to turn NR off. Native game rendering is unchanged.",
            "NR 宽高相对游戏渲染输入的比例，不是相对显示分辨率，也不是像素百分比。50% 约为 25% 的输入像素；当前安全范围为 50–100%，关闭请用启用/F6。不会降低游戏原始渲染分辨率。"));
    }'''
ui=edit(ui,old,new)
write('src/nr_settings_ui.inl',ui)

# Native regression: a known list evaluating between the miss and submission
# must not cancel the pending missing-list mapping repair.
p='tests/pristine_null_native.inl';text=read(p)
old='''    OnCommandStateObserveSubmission(&owner);
    const auto token=ReadCommandStateToken(list);'''
new='''    ID3D12GraphicsCommandList *other=nullptr;
    Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,
        IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&other)),"unrelated known list");
    reshade::api::command_list other_owner{&od,other}; OnCommandStateInitList(&other_owner);
    Check(!CaptureNativeCommandState(other,*snapshot),"unrelated known list still awaits its own baseline");
    Check(g_command_state_registration_repair_needed.load(),"known carrier does not cancel missing-list recovery");
    OnCommandStateObserveSubmission(&owner);
    OnCommandStateDestroyList(&other_owner);Drop(other);
    const auto token=ReadCommandStateToken(list);'''
text=edit(text,old,new)
text=edit(text,'Check(!CaptureNativeCommandState(list,*snapshot),"valid graphics state is not automatically replayed");',
'''Check(CaptureNativeCommandState(list,*snapshot),"fully observed bounded viewport is admitted without experimental replay");
    Check(std::strcmp(SnapshotCommandStateEvidence().route,"guarded-null-raster")==0,"bounded raster route is explicit");
    D3D12_RECT scissor{0,0,2,2};ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);
    Check(CaptureNativeCommandState(list,*snapshot),"viewport and scissor compose within the safe subset");
    {
        NativeCommandStateScope restore_raster(list);
        Check(restore_raster.active,"actual raster scope can be acquired");
        D3D12_VIEWPORT changed{0,0,1,1,0,1};
        ID3D12GraphicsCommandList_RSSetViewports(list,1,&changed);
        restore_raster.Restore();
        Check(record->state->dynamic.viewports[0].Width==2,"native scope restores observed viewport bytes");
    }
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Check(!CaptureNativeCommandState(list,*snapshot),"other graphics domains remain outside automatic subset");''')
text=edit(text,'SnapshotCommandStateEvidence().dynamic==kDynamicViewport',
                 'SnapshotCommandStateEvidence().dynamic==(kDynamicViewport|kDynamicScissor|kDynamicTopology)')
write(p,text)

# Freeze runtime, resource and package identity. Historical documents stay intact.
for p in ['src/nr_before_sr.rc','scripts/build.ps1','scripts/validate-beta04-structure.py']:
    text=read(p).replace('beta0.4.3r3','beta0.4.4-test1')
    if p.endswith('.rc'):
        text=text.replace('0,4,3,3','0,4,4,1').replace('0.4.3.3','0.4.4.1')
    write(p,text)

p='.github/workflows/beta042-validate.yml';text=read(p)
text=text.replace('beta043r3-mainline-maintenance-candidate','beta044-test1-d3d12-candidate')
text=text.replace('build=beta0.4.3r3','build=beta0.4.4-test1')
text=text.replace('channel=mainline-maintenance-candidate','channel=d3d12-branch-test-only')
text=edit(text,'          python3 scripts/validate-compat-sweep.py --compiler g++',
'''          python3 scripts/validate-compat-sweep.py --compiler g++
          python3 scripts/validate-044-compat.py --compiler g++''')
text=edit(text,"          if ($LASTEXITCODE -ne 0) { throw 'Compatibility policy/UI regression failed.' }",
'''          if ($LASTEXITCODE -ne 0) { throw 'Compatibility policy/UI regression failed.' }
          python scripts/validate-044-compat.py --compiler $compiler
          if ($LASTEXITCODE -ne 0) { throw '044 production forwarding/raster regression failed.' }''')
text=edit(text,'          ./scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -NegativeControl',
'''          ./scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -NegativeControl
          ./scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -DynamicStateNegativeControl''')
write(p,text)
subprocess.run(['python3','scripts/validate-044-compat.py','--compiler','g++'],cwd=ROOT,check=True)
subprocess.run(['git','diff','--check'],cwd=ROOT,check=True)
print('Anchored source correction complete; DX11 and 0.5 code not modified.')
