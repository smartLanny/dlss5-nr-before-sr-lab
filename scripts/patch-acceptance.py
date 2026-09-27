"""Session-only guided comparison, applied after temporal and native O capture."""
from pathlib import Path
INPUTS=('scripts/patch-acceptance.py','src/nr_acceptance_policy.h','src/nr_acceptance_runtime.inl','src/nr_acceptance_screenshot_export.inl',
        'tests/acceptance_policy_test.cpp','tests/acceptance_core_harness.inl','scripts/validate-acceptance.py')

def apply(stage:Path,inputs,helpers):
    root=stage/'core'
    for path in ('src/nr_acceptance_policy.h','src/nr_acceptance_runtime.inl','src/nr_acceptance_screenshot_export.inl'):
        (root/path).write_bytes(inputs[path])
    p=root/'src/nr_before_sr.cpp';s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'#include "nr_f8_sequence.inl"',
        '#define NR_HAS_ACCEPTANCE 1\nnamespace nr_acceptance { bool Active(); }\n#include "nr_f8_sequence.inl"\n#include "nr_acceptance_runtime.inl"','guided controller ordering')
    s=helpers.once(s,'#include "nr_feedback_capture.inl"',
        'namespace nr_acceptance { void Screenshot(reshade::api::effect_runtime*,const char*); }\n#include "nr_feedback_capture.inl"',
        'screenshot callback forward declaration before feedback include')
    s=helpers.once(s,'#include "nr_compat_present_runtime.inl"',
        '''#include "nr_compat_present_runtime.inl"
namespace nr_acceptance {
PresentCounters ReadPresentLocked() {
    // The acceptance controller already holds g_chain_cs. Do not call Snapshot(),
    // which acquires the same lock again.
    const auto& o=nr_compat_present_runtime::observation;
    return {nr_compat_present_runtime::stable_config,o.generation,o.completed_source,
        o.submitted,o.completed,o.evaluated[0],o.attempted[0],o.timing_source,
        o.reset_each_frame,o.timing_valid,o.total_ms};
}
}''','Present baseline reads exact owner counters under existing chain lock')
    s=helpers.once(s,'    nr_f8::Present(runtime);','    nr_f8::Present(runtime);\n    nr_acceptance::Present(runtime);','guided Present tick')
    s=helpers.once(s,'any_combine && nr_f8::g_config_hold.load() && ReadLifecycleToken(command_list,f8_token)',
        'any_combine && nr_f8::IsActive() && ReadLifecycleToken(command_list,f8_token)',
        'configuration hold alone must not enter per-frame image capture preparation')
    p.write_text(s,encoding='utf-8',newline='\n')
    p=root/'src/nr_f8_sequence.inl';s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'bool RequestCore() {','bool RequestCore() {\n    if(nr_acceptance::Active())return false;','single config owner report')
    s=helpers.once(s,'    const bool accepted = !IsActive() && g_status.load()!=Status::Packaging &&',
        '    const bool accepted = !nr_acceptance::Active() && !IsActive() && g_status.load()!=Status::Packaging &&',
        'recheck single config owner under F8 control lock')
    s=helpers.once(s,'bool RequestWithOptions(const Options &options) {',
        'bool RequestWithOptions(const Options &options) {\n    if(nr_acceptance::Active())return false;','single config owner images')
    p.write_text(s,encoding='utf-8',newline='\n')
    p=root/'src/nr_feedback_capture.inl';s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'    nr_f8::Screenshot(runtime, path);',
        '    nr_f8::Screenshot(runtime, path);\n    nr_acceptance::Screenshot(runtime, path);',
        'guided screenshot callback')
    p.write_text(s,encoding='utf-8',newline='\n')
    p=root/'src/nr_settings_ui.inl';s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'    nr_f8::RuntimeDestroyed(runtime);','    nr_acceptance::RuntimeDestroyed(runtime);\n    nr_f8::RuntimeDestroyed(runtime);','guided runtime shutdown')
    s=helpers.once(s,'        DrawEnergyGateOptions();\n        ImGui::EndDisabled();',
        '        DrawEnergyGateOptions();\n        ImGui::EndDisabled();\n        nr_acceptance::DrawScreenshotUi(runtime, has_pending);',
        'one-click picture comparison outside settings disabled block')
    p.write_text(s,encoding='utf-8',newline='\n')
