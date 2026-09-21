"""Session-only guided comparison, applied after temporal and native O capture."""
from pathlib import Path
INPUTS=('scripts/patch-acceptance.py','src/nr_acceptance_policy.h','src/nr_acceptance_runtime.inl',
        'tests/acceptance_policy_test.cpp','tests/acceptance_core_harness.inl','scripts/validate-acceptance.py')

def apply(stage:Path,inputs,helpers):
    root=stage/'core'
    for path in ('src/nr_acceptance_policy.h','src/nr_acceptance_runtime.inl'):
        (root/path).write_bytes(inputs[path])
    p=root/'src/nr_before_sr.cpp';s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'#include "nr_f8_sequence.inl"',
        'namespace nr_acceptance { bool Active(); }\n#include "nr_f8_sequence.inl"\n#include "nr_acceptance_runtime.inl"','guided controller ordering')
    s=helpers.once(s,'    nr_f8::Present(runtime);','    nr_f8::Present(runtime);\n    nr_acceptance::Present(runtime);','guided Present tick')
    s=helpers.once(s,'if(any_combine && nr_f8::g_config_hold.load() && ReadLifecycleToken(command_list,f8_token)',
        'if(any_combine && nr_f8::IsActive() && ReadLifecycleToken(command_list,f8_token)',
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
    p=root/'src/nr_settings_ui.inl';s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'    nr_f8::RuntimeDestroyed(runtime);','    nr_acceptance::RuntimeDestroyed(runtime);\n    nr_f8::RuntimeDestroyed(runtime);','guided runtime shutdown')
    p.write_text(s,encoding='utf-8',newline='\n')
    p=root/'src/nr_unified_settings.inl';s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'    const bool collecting=nr_f8::IsActive();','    const bool collecting=nr_f8::g_config_hold.load();','guided UI config ownership')
    p.write_text(s,encoding='utf-8',newline='\n')
    p=root/'src/nr_unified_pages.inl';s=p.read_text(encoding='utf-8')
    marker='void DrawUnifiedFeedback('
    begin,end=helpers.definition(s,marker)
    body=s[begin:end];at=body.index('{')+1
    body=body[:at]+'\n    nr_acceptance::Draw(runtime,pending);\n    ImGui::BeginDisabled(nr_acceptance::Active());'+body[at:]
    body=body[:-1]+'    ImGui::EndDisabled();\n}'
    s=s[:begin]+body+s[end:];p.write_text(s,encoding='utf-8',newline='\n')
