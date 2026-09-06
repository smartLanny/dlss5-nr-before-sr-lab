"""Immutable integration/patch preparation. Creates blobs ONLY; connector owns commits/refs/PRs."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import tempfile
import urllib.request

REPO='smartLanny/dlss5-nr-before-sr-lab'
BASE='381223472e941d85cb08faa493d469661f0782b7'
PARENTS=['1a8490a2c76d93b0c5e4cc50b8d4f178df55eb83','453aa5fae3ecacc9f48c74020392d049a3aa16e5','2f070400b19595774bf3fa6b4d46ea2e6c64adf3','7cf62b5c3b9c8ab15b37820641933cb304402bd9']
HERE=Path(__file__).resolve().parents[1]

def definition(text, marker):
    start=text.index(marker); end=text.index('{',start)+1; depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

def main():
    with tempfile.TemporaryDirectory(prefix='nr045-') as tmp:
        root=Path(tmp)/'tree'
        def git(*args):return subprocess.check_output(['git',*args],cwd=root,text=True).strip()
        subprocess.run(['git','worktree','add','--detach',str(root),BASE],cwd=HERE,check=True)
        git('config','user.name','Local integration audit');git('config','user.email','audit@localhost')
        for ref in PARENTS:git('merge','--no-edit',ref)
        def read(path):return (root/path).read_text(encoding='utf-8-sig')
        def write(path,text):
            p=root/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8',newline='\n')
        def swap(text,old,new,count=1):
            if text.count(old)!=count:raise RuntimeError(f'Anchor count {text.count(old)} != {count}: {old[:100]}')
            return text.replace(old,new)
        for path in ['src/nr_feedback_report.inl','src/nr_feedback_modules.inl','src/nr_runtime_load_evidence.inl']:
            write(path,(HERE/path).read_text(encoding='utf-8'))
        cpp=read('src/nr_before_sr.cpp')
        cpp=swap(cpp,'#include <new>','#include <new>\n#include <memory>')
        cpp=swap(cpp,'    CommandStateUnverified,\n};','    CommandStateUnverified,\n    RuntimeUnavailable,\n    CodecUnavailable,\n};')
        cpp=swap(cpp,'    default: return "unknown";\n    }\n}\n\nvoid SetBypassReason',
            '    case BypassReason::RuntimeUnavailable: return "nr-runtime-unavailable";\n    case BypassReason::CodecUnavailable: return "codec-unavailable";\n    default: return "unknown";\n    }\n}\n\nvoid SetBypassReason')
        fn=definition(cpp,'bool EnsureNrApi(')
        new=fn.replace('if (g_nr_api.failed || device == nullptr) return false;',
            'if (g_nr_api.failed || device == nullptr) { SetBypassReason(BypassReason::RuntimeUnavailable); return false; }')
        new=swap(new,'    g_nr_api.bridge_module = LoadLibraryExW(', '    RuntimeLoadEvidence evidence;\n    evidence.stage = "load-modules";\n    g_nr_api.bridge_module = LoadLibraryExW(')
        new=swap(new,'    g_nr_api.signed_module = LoadLibraryExW(',
            '    evidence.loaded[0] = g_nr_api.bridge_module != nullptr;\n    evidence.errors[0] = RuntimeLoadError(evidence.loaded[0]);\n    g_nr_api.signed_module = LoadLibraryExW(')
        new=swap(new,'    HMODULE core = GetModuleHandleW(L"_nvngx.dll");',
            '    evidence.loaded[1] = g_nr_api.signed_module != nullptr;\n    evidence.errors[1] = RuntimeLoadError(evidence.loaded[1]);\n    HMODULE core = GetModuleHandleW(L"_nvngx.dll");\n    evidence.loaded[2] = core != nullptr;\n    evidence.errors[2] = RuntimeLoadError(evidence.loaded[2]);')
        start=new.index('        Log("NR runtime load failed:');end=new.index('        g_nr_api.failed = true;',start)
        new=new[:start]+'''        Log("NR runtime load failed: bridgeLoaded=%d bridgeWin32=%lu signedLoaded=%d signedWin32=%lu coreLoaded=%d coreWin32=%lu; restart after repairing the installation.",
            evidence.loaded[0] ? 1 : 0, evidence.errors[0], evidence.loaded[1] ? 1 : 0,
            evidence.errors[1], evidence.loaded[2] ? 1 : 0, evidence.errors[2]);
'''+new[end:]
        marker='    if (g_nr_api.signed_init == nullptr ||'
        pos=new.index(marker)
        export_names=['NVSDK_NGX_D3D12_Init_Ext','NVSDK_NGX_D3D12_CreateFeature','NVSDK_NGX_D3D12_EvaluateFeature','NVSDK_NGX_D3D12_ReleaseFeature','NVSDK_NGX_D3D12_AllocateParameters','NVSDK_NGX_D3D12_DestroyParameters','NRChain_Init','NRChain_Create','NRChain_Evaluate','NRChain_Release']
        fields=['signed_init','signed_create','signed_evaluate','signed_release','allocate','destroy','bridge_init','bridge_create','bridge_evaluate','bridge_release']
        code='    evidence.stage = "resolve-exports";\n'
        for i,(name,field) in enumerate(zip(export_names,fields)):
            code+=f'    if (!g_nr_api.{field}) {{ evidence.missing_exports |= 1u << {i}; Log("NR runtime missing export: {name}"); }}\n'
        new=new[:pos]+code+new[pos:]
        new=swap(new,'    const NgxResult result = g_nr_api.bridge_init(',
            '    evidence.stage = "initialize";\n    RecordRuntimeLoadEvidence(evidence);\n    const NgxResult result = g_nr_api.bridge_init(')
        new=swap(new,'    if (result != kNgxSuccess && result != 0xBAD00003u)',
            '    evidence.init_result = result;\n    if (result != kNgxSuccess && result != 0xBAD00003u)')
        new=new.replace('        g_nr_api.failed = true;\n        return false;',
            '        g_nr_api.failed = true;\n        RecordRuntimeLoadEvidence(evidence);\n        SetBypassReason(BypassReason::RuntimeUnavailable);\n        return false;')
        new=swap(new,'    g_nr_api.initialized = true;',
            '    g_nr_api.initialized = true;\n    evidence.stage = "initialized";\n    RecordRuntimeLoadEvidence(evidence);')
        cpp=swap(cpp,fn,'#include "nr_runtime_load_evidence.inl"\n\n'+new)
        cpp=swap(cpp,'if (v2_combine && g_config.allow_unverified_hdr_color && !color_policy.color_allowed)',
            'if (g_config.allow_unverified_hdr_color && color_policy.color_reason == NrColorReason::UnverifiedHdr)')
        # Accurate codec rejection without replacing an earlier runtime-loading reason.
        cpp=cpp.replace('const bool ready = EnsureNrApi(device) && EnsureCodec(device);',
            'const bool nr_ready = EnsureNrApi(device);\n    const bool ready = nr_ready && EnsureCodec(device);\n    if (nr_ready && !ready) SetBypassReason(BypassReason::CodecUnavailable);')
        write('src/nr_before_sr.cpp',cpp)
        cs=read('src/nr_command_state.inl')
        fn=definition(cs,'bool CanAutoRestoreRasterNull(')
        new=swap(fn,'kDynamicViewport | kDynamicScissor','kDynamicViewport | kDynamicScissor | kDynamicBlend')
        pos=new.rfind('    return true;')
        assert pos>0
        new=new[:pos]+'''    if ((state.dynamic.recorded & kDynamicBlend) != 0)
        for (float value : state.dynamic.blend)
            if (!(value >= 0.0f && value <= 1.0f)) return false;
'''+new[pos:]
        cs=swap(cs,fn,new)
        cs=swap(cs,'if (!values) CommandStateInvalidDynamic(state); else {memcpy(state.dynamic.blend, values, sizeof(state.dynamic.blend)); state.dynamic.recorded |= kDynamicBlend;}',
            'if (values) memcpy(state.dynamic.blend, values, sizeof(state.dynamic.blend)); else for (float &value : state.dynamic.blend) value=1.0f; state.dynamic.recorded |= kDynamicBlend;')
        write('src/nr_command_state.inl',cs)
        feedback=read('src/nr_feedback_capture.inl')
        old=definition(feedback,'bool RequestFeedbackBundle(')
        feedback=swap(feedback,old,'namespace nr_f8 { bool RequestCore(); void DrawControls(); }\n\nbool RequestFeedbackBundle() {\n    // Technical/legacy button and keyboard F8 share the same session initializer.\n    return nr_f8::RequestCore();\n}')
        old=definition(feedback,'void DrawFeedbackControls(')
        feedback=swap(feedback,old,'void DrawFeedbackControls() { nr_f8::DrawControls(); }')
        feedback=swap(feedback,'void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {', '''void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {
#if defined(NR_HAS_RUNTIME_LOAD_EVIDENCE)
    const auto load = SnapshotRuntimeLoadEvidence();
    FeedbackAppend(s, "%s.runtime.available=%d\\n%s.runtime.stage=%s\\n%s.runtime.ageMs=%llu\\n"
        "%s.runtime.bridgeLoaded=%d\\n%s.runtime.bridgeWin32=%lu\\n%s.runtime.signedLoaded=%d\\n%s.runtime.signedWin32=%lu\\n"
        "%s.runtime.coreLoaded=%d\\n%s.runtime.coreWin32=%lu\\n%s.runtime.missingExportMask=0x%X\\n%s.runtime.initResult=0x%X\\n",
        prefix, load.available ? 1 : 0, prefix, load.stage,
        prefix, load.available ? GetTickCount64() - load.sampled_at : 0,
        prefix, load.loaded[0] ? 1 : 0, prefix, load.errors[0], prefix, load.loaded[1] ? 1 : 0, prefix, load.errors[1],
        prefix, load.loaded[2] ? 1 : 0, prefix, load.errors[2], prefix, load.missing_exports, prefix, load.init_result);
    FeedbackAppend(s, "%s.scope.config=%u\\n%s.scope.epoch=%llu\\n%s.scope.featureGeneration=%llu\\n%s.scope.mode=%d\\n"
        "%s.scope.nrSuccess=%llu\\n%s.scope.transferSubmitted=%llu\\n%s.queue.admitted=%llu\\n%s.queue.unknownBypasses=%llu\\n"
        "%s.queue.predictionBypasses=%llu\\n%s.queue.actualMismatches=%llu\\n%s.queue.markerFailures=%llu\\n"
        "%s.scope.note=individual atomic observations; only compare matching config/epoch, never infer FPS\\n",
        prefix, g_config_generation.load(), prefix, g_pipeline_stats_epoch.load(), prefix, g_pipeline_stats_generation.load(),
        prefix, g_pipeline_stats_mode.load(), prefix, g_pipeline_feature18_success.load(), prefix, g_pipeline_transfer_submitted.load(),
        prefix, g_queue_affinity_admitted_frames.load(), prefix, g_queue_affinity_unknown_bypass_frames.load(),
        prefix, g_queue_affinity_prediction_bypass_frames.load(), prefix, g_queue_affinity_actual_mismatches.load(),
        prefix, g_queue_affinity_marker_failures.load(), prefix);
#endif''')
        mark='        if (s->observations == 1 || s->observations == 60) {'
        feedback=swap(feedback,mark,mark+'''
            FeedbackAppend(*s, "ngx.contract.sample=%u featureId=%d generation=%llu\\n", s->observations, feature.feature_id, feature.generation);
            for (const char *key : {"DLSS.Render.Subrect.Dimensions.Width", "DLSS.Render.Subrect.Dimensions.Height",
                    "DLSS.Input.Color.Subrect.Base.X", "DLSS.Input.Color.Subrect.Base.Y",
                    "DLSS.Input.Depth.Subrect.Base.X", "DLSS.Input.Depth.Subrect.Base.Y",
                    "DLSS.Input.MV.Subrect.Base.X", "DLSS.Input.MV.Subrect.Base.Y",
                    "DLSS.Output.Subrect.Base.X", "DLSS.Output.Subrect.Base.Y"}) {
                unsigned value = 0;
                if (GetUInt(params, key, &value)) FeedbackAppend(*s, "ngx.%s=%u\\n", key, value);
                else FeedbackAppend(*s, "ngx.%s=not-supplied\\n", key);
            }''')
        write('src/nr_feedback_capture.inl',feedback)
        core=read('src/nr_feedback_core.inl')
        core=swap(core,'namespace nr_core_feedback {','namespace nr_core_feedback {\n#define NR_HAS_CORE_TEXT_REPORT 1\n#include "nr_feedback_report.inl"')
        core=swap(core,'PairSnapshot g_pair{};', 'std::shared_ptr<const std::string> g_full_report; // g_feedback_lock\nwchar_t g_report_path[1024] = {};\nDWORD g_report_error = ERROR_SUCCESS;\nbool g_report_complete = false;\nPairSnapshot g_pair{};')
        core=swap(core,'    g_pair = {};','    g_pair = {};\n    g_full_report.reset(); g_report_path[0] = 0; g_report_error = ERROR_SUCCESS; g_report_complete = false;')
        core=swap(core,'             const char *report = nullptr) {','             const char *report = nullptr, const nr_feedback_report::Output *text = nullptr) {')
        core=swap(core,'    g_job = nullptr;\n    nr_bundle::CopyPath',
            '    g_job = nullptr;\n    g_full_report = text ? text->text : nullptr;\n    g_report_path[0] = 0;\n    g_report_error = text ? text->error : ERROR_SUCCESS;\n    g_report_complete = text && text->text && text->complete;\n    if (text && text->text) nr_bundle::CopyPath(g_report_path, text->path.c_str());\n    nr_bundle::CopyPath')
        core=swap(core,'void Modules(FeedbackSession &s) {','#include "nr_feedback_modules.inl"\n\nvoid Modules(FeedbackSession &s) {')
        core=core.replace('modules.contract=names only; no module bytes or process addresses collected',
            'modules.contract=name inventory plus bounded loaded-file fingerprints; no module bytes or process addresses exported')
        core=swap(core,'    bool complete = false;\n    try {','    bool complete = false;\n    nr_feedback_report::Output text;\n    try {')
        core=swap(core,'        Modules(s);','        Modules(s);\n        FingerprintRuntimeFiles(s, *session);')
        core=swap(core,'            complete = !g_cancel.load() && session->snapshot_ok && !s.report_truncated;',
            '            complete = !g_cancel.load() && session->snapshot_ok && !s.report_truncated;\n            text = nr_feedback_report::Build(*job, result, complete);')
        core=swap(core,'        // Never let a failed diagnostic allocation unwind into the game.',
            '        text.error = ERROR_NOT_ENOUGH_MEMORY;\n        // Never let a failed diagnostic allocation unwind into the game.')
        core=swap(core,'"core logs collected; no image readback requested", s.report);',
            '"core logs collected; no image readback requested", s.report, &text);')
        write('src/nr_feedback_core.inl',core)
        sequence=read('src/nr_f8_sequence.inl')
        helper='''
void DrawCoreTextStatus(bool chinese) {
    struct View { wchar_t path[1024]{}; char utf8[4096]{}; std::shared_ptr<const std::string> text; DWORD error=0; bool complete=false; };
    auto view = std::unique_ptr<View>(new (std::nothrow) View);
    if (!view) return;
    AcquireSRWLockShared(&g_feedback_lock);
    view->text=nr_core_feedback::g_full_report; view->error=nr_core_feedback::g_report_error;
    view->complete=nr_core_feedback::g_report_complete;
    nr_bundle::CopyPath(view->path,nr_core_feedback::g_report_path);
    ReleaseSRWLockShared(&g_feedback_lock);
    if (view->text) {
        ImGui::TextWrapped("%s", chinese ? "独立 TXT 已保存，可复制全文粘贴到群反馈文档；ZIP 请保留。" : "Standalone TXT saved. Copy its full text into your feedback document; retain the ZIP.");
        if (!view->complete) ImGui::TextWrapped("%s", chinese ? "TXT 部分内容不可用，文内已注明；与 ZIP 状态分别判断。" : "TXT is partial; missing sections are explicit and independent of ZIP status.");
        if (ImGui::Button(chinese ? "复制完整报告（群文档）" : "Copy full TXT report")) ImGui::SetClipboardText(view->text->c_str());
        WideToUtf8(view->path,view->utf8,sizeof(view->utf8));
        ImGui::TextWrapped("%s",view->utf8);
        if (ImGui::Button(chinese ? "复制 TXT 路径" : "Copy TXT path")) ImGui::SetClipboardText(view->utf8);
    } else if (view->error) {
        ImGui::TextWrapped(chinese ? "TXT 保存失败（Win32 %lu）；ZIP 是否成功请看上方。" : "TXT failed (Win32 %lu); ZIP status is shown separately above.",view->error);
    }
}
'''
        sequence=swap(sequence,'void DrawCoreStatus(',helper+'\nvoid DrawCoreStatus(')
        fn=definition(sequence,'void DrawCoreStatus(')
        sequence=swap(sequence,fn,fn[:-1]+'    DrawCoreTextStatus(UseChineseUi());\n}')
        write('src/nr_f8_sequence.inl',sequence)
        ui=read('src/nr_settings_ui.inl')
        ui=swap(ui,'    ImGui::BeginDisabled(current_color_policy &&\n                         !color_policy_snapshot.policy.color_allowed);\n','')
        ui=swap(ui,'    ImGui::EndDisabled();\n    changed |= color_follow_changed;',
            '    if (color_follow_changed) next.allow_unverified_hdr_color = next.color_strength > 0.0f;\n    changed |= color_follow_changed;')
        ui=ui.replace('Unknown HDR pauses only colour transfer; luminance NR can remain active.',
            'Editing a nonzero value explicitly permits unverified HDR colour with a warning; zero restores luminance only. This does not override resource or exposure safety.')
        ui=ui.replace('未知 HDR 只暂停颜色传递，明暗 NR 仍可继续运行；',
            '手动调到非零即允许尝试未验证 HDR 色彩，可能偏色；拉回 0 恢复仅明暗，不绕过资源或曝光安全检查。')
        ui=swap(ui,'    if (color_policy_snapshot.status == NrColorPolicyStatus::Current) {',
            '''    if (next.allow_unverified_hdr_color || (current_color_policy && !color_policy_snapshot.policy.color_allowed))
        ImGui::TextWrapped("%s", ui("WARNING: unverified HDR colour can shift hue or flicker. Set colour to 0 or restore recommended picture settings to undo. No colour-fidelity guarantee.",
            "警告：未验证 HDR 色彩可能偏色或闪烁。可拉回 0，或一键恢复推荐画质；不保证颜色准确。"));
    if (color_policy_snapshot.status == NrColorPolicyStatus::Current) {''',1) if ui.count('    if (color_policy_snapshot.status == NrColorPolicyStatus::Current) {')==1 else ui.replace('    if (color_policy_snapshot.status == NrColorPolicyStatus::Current) {','''    if (next.allow_unverified_hdr_color || (current_color_policy && !color_policy_snapshot.policy.color_allowed))
        ImGui::TextWrapped("%s", ui("WARNING: unverified HDR colour may shift hue or flicker. Set colour to 0 or restore recommended settings to undo.", "警告：未验证 HDR 色彩可能偏色或闪烁；可拉回 0 或一键恢复推荐画质。"));
    if (color_policy_snapshot.status == NrColorPolicyStatus::Current) {''',1)
        ui=ui.replace('            : policy.color_reason == NrColorReason::PostSrFp16UnitExposure',
            '            : policy.color_reason == NrColorReason::ExperimentalUnverifiedHdr\n                ? ui("User-requested colour on an unverified HDR contract; not a verified colour profile.", "用户已选择尝试未验证 HDR 颜色；这不代表色彩合同已验证。")\n            : policy.color_reason == NrColorReason::PostSrFp16UnitExposure')
        write('src/nr_settings_ui.inl',ui)
        # Existing production oracle follows the expanded bounded mask; all other negative domains stay rejected.
        test=read('scripts/validate-044-compat.py')
        test=swap(test,'kDynamicViewport=2,kDynamicScissor=4;', 'kDynamicViewport=2,kDynamicScissor=4,kDynamicBlend=8;')
        test=swap(test,'Viewport viewports[16];Rect scissors[16];','Viewport viewports[16];Rect scissors[16];float blend[4]={1,1,1,1};')
        test=swap(test,'(bit==1||bit==2)','(bit==1||bit==2||bit==3)')
        marker=' return finish("actual bounded null-raster policy");'
        test=swap(test,marker,''' {auto t=s;t.dynamic.recorded=kDynamicBlend;check(CanAutoRestoreRasterNull(t),"blend-only reset subset admitted");}
 for(float bad:{-1.f,1.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
   auto t=s;t.dynamic.recorded=kDynamicBlend;t.dynamic.blend[2]=bad;check(!CanAutoRestoreRasterNull(t),"unbounded or nonfinite blend rejected");
 }
'''+marker)
        write('scripts/validate-044-compat.py',test)
        native=read('tests/pristine_null_native.inl')
        marker='    auto saved=g_state_BeginRenderPass.originals[3];'
        native=swap(native,marker,'''    const float blend[4]={0.25f,0.5f,0.75f,1.0f};
    ID3D12GraphicsCommandList_OMSetBlendFactor(list,blend);
    Check(CaptureNativeCommandState(list,*snapshot),"blend-only game state now admitted");
    {
        NativeCommandStateScope scope(list);
        Check(scope.active,"blend-only native scope acquired");
        const float wrong[4]={0,0,0,0};
        ID3D12GraphicsCommandList_OMSetBlendFactor(list,wrong);
        scope.Restore();
        Check(memcmp(record->state->dynamic.blend,blend,sizeof(blend))==0,"exact native blend values restored");
    }
    ID3D12GraphicsCommandList_OMSetBlendFactor(list,nullptr);
    Check(CaptureNativeCommandState(list,*snapshot),"legal NULL blend resets all channels to one");
    for(float v:record->state->dynamic.blend) Check(v==1.0f,"NULL blend follows D3D12 default");
    ID3D12GraphicsCommandList_ClearState(list,nullptr);
'''+marker)
        write('tests/pristine_null_native.inl',native)
        # Windows test uses actual report/ZIP IO, and the legacy button now exercises the public session initializer.
        harness=read('scripts/validate-core-feedback.py')
        harness=swap(harness,'#include <fstream>','#include <fstream>\n#include <memory>')
        harness=swap(harness,'unsigned checks=0;','LEGACY_REQUEST\nunsigned checks=0;')
        harness=swap(harness,'check(nr_core_feedback::Request(),"new core request")','check(RequestFeedbackBundle(),"new core request through legacy UI")')
        harness=swap(harness,'check(!nr_core_feedback::g_submission[0],"new request clears old ticket");',
            'check(!nr_core_feedback::g_submission[0],"new request clears old ticket");check(!nr_core_feedback::g_full_report && !nr_core_feedback::g_report_path[0],"new request clears old TXT");check(nr_core_feedback::SnapshotPair().session[0] && g_feedback_status==1,"legacy request initializes session and parameter observer");')
        harness=swap(harness,'    save("valid-core.zip");','''    save("valid-core.zip");
    check(nr_core_feedback::g_full_report && nr_core_feedback::g_report_path[0],"standalone TXT and cached content published");
    const auto &report=*nr_core_feedback::g_full_report;
    check(report.find(first.session)!=std::string::npos && report.find("null-pso-policy-disabled")!=std::string::npos,"TXT carries this session and first-error log");
    check(report.find("Private Tester")==std::string::npos && report.find("ABCDEF00")==std::string::npos,"TXT privacy matches sanitized ZIP");
    check(report.find("text.complete=1")!=std::string::npos,"all included core summaries present in TXT");
    std::ifstream txt(nr_core_feedback::g_report_path,std::ios::binary);
    check(std::string((std::istreambuf_iterator<char>(txt)),{})==report,"disk TXT equals clipboard cache");
    txt.close();
    std::filesystem::copy_file(nr_core_feedback::g_report_path,std::filesystem::path(output_root)/"valid-core.txt");
''')
        harness=swap(harness,"        'COMMAND_EVIDENCE':", "        'LEGACY_REQUEST': definition(legacy, 'bool RequestFeedbackBundle('),\n        'COMMAND_EVIDENCE':")
        write('scripts/validate-core-feedback.py',harness)
        # Unreleased product identity only. Historical evidence and prior release scripts remain immutable.
        for path in ['src/nr_before_sr.cpp','src/nr_before_sr.rc','scripts/build.ps1','scripts/validate-beta04-structure.py','.github/workflows/beta042-validate.yml']:
            value=read(path).replace('beta0.4.4-test1','beta0.4.5').replace('0.4.4-test1 beta','0.4.5 beta').replace('0,4,4,1','0,4,5,0')
            if path.endswith('beta042-validate.yml'):
                value=value.replace('FileBuildPart -ne 4','FileBuildPart -ne 5').replace('FilePrivatePart -ne 1','FilePrivatePart -ne 0')
                value=value.replace('package-044-test1.py','package-045.py').replace('beta044-test1','beta045')
                value=value.replace('          python3 scripts/validate-044-compat.py --compiler g++',
                    '          python3 scripts/validate-044-compat.py --compiler g++\n          python3 scripts/validate-retirement-return.py\n          python3 scripts/validate-strength-cap.py\n          python3 scripts/validate-recommended-picture.py')
            write(path,value)
        # Full test package and OTA carry our companion DLL; proprietary/GPU-specific runtimes are preserved.
        package=read('scripts/package-044-test1.py').replace('beta0.4.4-test1','beta0.4.5').replace('0.4.4-test1 beta','0.4.5 beta')
        package=package.replace("FEEDBACK+[ADDONS[language]", "FEEDBACK+['nrchain_nvngx.dll',ADDONS[language]")
        package=swap(package,"data={addon:(ROOT/'build'/addon).read_bytes()}", "data={addon:(ROOT/'build'/addon).read_bytes(),'nrchain_nvngx.dll':(ROOT/'build/nrchain_nvngx.dll').read_bytes()}")
        package=package.replace("'ReShadePreset.ini','nrchain_nvngx.dll','nvngx_dlssnr.dll'", "'ReShadePreset.ini','nvngx_dlssnr.dll'")
        start=package.index("README_ZH='''");end=package.index('\ndef digest',start)
        package=package[:start]+'''README_ZH='''+repr('beta0.4.5 — 0.4 集中修复测试版（无 DX11）\n退出游戏，备份旧同类 addon 和 nrchain_nvngx.dll，再放入本包一个语言 addon 与配套 nrchain_nvngx.dll。\n保留原 INI、ReShade 和显卡对应 NVIDIA 运行库；不改驱动。自有配套 DLL 不是 NVIDIA DLL 或 DX11 桥。\n进原问题场景，F6 看效果；后置问题切到后置测试。画质页可一键恢复推荐，恢复不会强开 NR。\nF8 同时保存核心 ZIP 与独立 feedback-report.txt；用“复制完整报告（群文档）”粘贴，ZIP 留存。F7 仅供画面异常补充。\n回复有效/无效、是否闪烁/崩溃。异常就停止并还原备份，不必反复试崩。\n这是未完成游戏实测的候选，不是通用 MFG 解锁或首次安装包。\n')+'\nREADME_EN='+repr('beta0.4.5 — integrated 0.4 D3D12 test, NO DX11\nExit the game; back up the previous addon AND nrchain_nvngx.dll. Install ONE language addon and the companion DLL from this OTA. Preserve INI, ReShade and GPU-specific NVIDIA runtimes.\nUse F6 in the problem scene; test post mode for post-mode bugs. Restore recommended picture settings does not enable NR.\nF8 saves both core ZIP and standalone feedback-report.txt. Copy full TXT into your feedback document; retain ZIP. F7 is optional for image problems.\nStop and roll back on persistent faults. This candidate has not passed target-game validation; no MFG unlock or proprietary runtime included.\n')+'\n'+package[end:]
        write('scripts/package-045.py',package)
        doc='''# beta0.4.5 集中修复测试版 / Integrated D3D12 test

集成 #92、#111、#112、#114，保留其历史与最新补充日志。不是 DX11 或 0.5 分支；未以 CI 冒充游戏实测。

## 新修改
- 完整观察/成功 Reset 后，只允许现有 viewport/scissor 加有限 0..1 blend factor 的空 PSO 恢复；合法 OMSetBlendFactor(NULL) 恢复全 1。其余动态/未知/资源状态继续旁路。对应 #38/#64/#100/#101 的实际 0x8 记录。
- NR 运行库每个组件立即保存 Win32 错误、缺少导出和初始化阶段；错误不再显示成过去的命令状态旁路。OTA 包含自有 nrchain_nvngx.dll，保留 NVIDIA 运行库，针对 #97 的加载失败候选。
- 旧技术页反馈按钮统一使用 F8 会话初始化，修复空 session/参数窗口未激活。键盘 F8 原本正常，不混为所有入口都坏。
- F8 ZIP 同目录另存 UTF-8 feedback-report.txt：本次完整有界元数据、实际模块文件版本/哈希/路径类别、原始 Create 与 Evaluate subrect、状态和脱敏日志摘要。UI 可一键复制全文；ZIP/TXT 各自显示成功/失败。无自动上传、无默认图像/原始 INI 泄漏。
- 手动调节非零颜色跟随可明确尝试未验证 HDR 颜色并警告；恢复推荐关闭该尝试，0 回到明暗。不会解除存储/曝光/资源/RR 安全准入。

## 测试范围
#98 燕云和 #99 异环新包在采集窗口内均有 NR 与提交增长，不再描述为永久 Hook 安装失败。仅安排间歇性回归。#39 色彩控制可复测，黑边根因尚未用图像确认。#49/#18 崩溃、#63 RTX40MFG 热补丁、DX11、完整低分后置仍未宣称修复。

## 玩家操作
退出游戏，备份旧 addon 与 nrchain_nvngx.dll，再更新一个语言与配套 DLL；保留配置和 NVIDIA 运行库。进旧问题场景 F6 对比，后置问题切后置；F8 复制完整 TXT，画面异常方便时 F7。持续异常即停止并回退。

## 验证状态
生产测试、Windows 编译、WARP、反馈完整性与打包校验结果见本版本 PR 和绑定的 Actions run；未运行的层不能算通过。真实 NVIDIA/游戏/FG 节奏/帧时间需这轮测试。发布仅包含中英文 OTA，不覆盖旧 tag。
'''
        write('docs/releases/beta0.4.5.md',doc)
        # No publishing workflow yet: reviewers inspect complete compiled source before explicit final trigger.
        git('add','.')
        for command in [['python3','scripts/validate-044-compat.py','--compiler','g++'],['python3','scripts/validate-retirement-return.py'],['python3','scripts/validate-strength-cap.py'],['python3','scripts/validate-recommended-picture.py'],['python3','scripts/validate-043r2.py','--compiler','g++']]:
            subprocess.run(command,cwd=root,check=True)
        paths=git('diff','--name-only',BASE).splitlines()
        if any(p.startswith('bug-inbox/') for p in paths):raise RuntimeError('Historical evidence must remain byte-exact')
        entries=[]
        for path in paths:
            data=(root/path).read_bytes()
            payload=json.dumps({'content':data.decode('utf-8'),'encoding':'utf-8'}).encode()
            request=urllib.request.Request('https://api.github.com/repos/'+REPO+'/git/blobs',data=payload,
                headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'],'Accept':'application/vnd.github+json','Content-Type':'application/json'},method='POST')
            with urllib.request.urlopen(request,timeout=60) as response:sha=json.load(response)['sha']
            expected=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
            if sha!=expected:raise RuntimeError('Blob mismatch')
            entries.append({'path':path,'mode':'100644','type':'blob','sha':sha})
        print('REVIEWED_TREE_ENTRIES '+json.dumps({'base':BASE,'parents':PARENTS,'entries':entries}))
        print(git('diff','--stat',BASE))
        subprocess.run(['git','worktree','remove','--force',str(root)],cwd=HERE,check=True)

if __name__=='__main__':main()
