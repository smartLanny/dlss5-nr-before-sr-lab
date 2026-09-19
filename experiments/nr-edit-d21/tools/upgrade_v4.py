#!/usr/bin/env python3
"""Harden a copied 04eda2e4 NR edit package for D21. Never patch a live checkout."""
from pathlib import Path

# Shared import contract with prepare_d21.py. These are immutable source refs,
# not a claim that the resulting D21 candidate has been compiled or merged.
BASE = "04eda2e4601ac6c57a2be85149d9c817d8dbf8a0"
D21 = "cbfb33919696a2840ed5154c91abaea56228b661"

def replace(text, old, new):
    if text.count(old) != 1:
        raise ValueError("v4 source changed/ambiguous: " + old[:110])
    return text.replace(old, new, 1)

def runtime(text):
    text = replace(text, "using Mode=nr_edit_policy::Mode;",
        'using Mode=nr_edit_policy::Mode;\n#include "nr_edit_d21_chain.inl"')
    text = replace(text, "    Id feature=0,configuration=0,bytes=0,view=0;",
        "    Id feature=0,configuration=0,bytes=0,view=0;\n    EditHistory edit_history{};")
    text = replace(text, "    Substitution image_{};Entry* entry_=nullptr;Recording recording_{};Source source_{};",
        "    Substitution image_{};Entry* entry_=nullptr;Recording recording_{};Source source_{};\n    AppliedChain applied_chain_{};")
    text = replace(text,
        "        using P=nr_edit_flow::Phase;const auto& img=image_.private_image;const auto& g=img.guides.signature;",
        """        using P=nr_edit_flow::Phase;const auto& img=image_.private_image;const auto& g=img.guides.signature;
        if(!ReadAppliedChain(img,applied_chain_)){
            Say(State::NativeFallback,"unproved-actual-D21-NR-chain");return {};
        }""")
    old = """        const Source reference{source_.stream,source_.epoch,source_.sequence>1?source_.sequence-1:0};
        const float exposure=img.color.pre_exposure;
        const bool primary_changed=!img.context||!img.context->instance_id||
            img.context->instance_id!=entry_->primary_instance||img.context->history_generation!=entry_->primary_history;
        const bool reset=primary_changed||!img.guides.capture_params.reset_known||img.guides.game_reset!=0||img.color.exposure_texture!=nullptr;"""
    text = replace(text, old, "        const float exposure=img.color.pre_exposure;")
    anchor = "        if(!entry_||!ReserveReceipt(*entry_,recording_)){Say(State::Busy,\"bounded-context-or-recording-pool\");return {};}"
    text = replace(text, anchor, anchor + """
        const Source reference{source_.stream,source_.epoch,source_.sequence>1?source_.sequence-1:0};
        const bool native_reset=!img.guides.capture_params.reset_known||img.guides.game_reset!=0||
            img.color.exposure_texture!=nullptr;
        const auto history=entry_->edit_history.Check(applied_chain_,
            {source_.stream,source_.epoch,source_.sequence},
            {reference.stream,reference.epoch,reference.sequence},native_reset);
        actual_chain_used.store(applied_chain_.used);actual_chain_requested.store(applied_chain_.requested);
        chain_history_reason.store(nr_edit_chain::Name(history.reason));
        if(!history.admitted)return {};
        const bool reset=history.reset;
        if(reset)chain_reset_requests.fetch_add(1);
""")
    text = replace(text, "            Visible(*entry_,recording_);composed.fetch_add(1);",
        """            Visible(*entry_,recording_);composed.fetch_add(1);
            entry_->edit_history.Commit(applied_chain_,{source_.stream,source_.epoch,source_.sequence});""")
    text = replace(text,
        "void CancelPendingEdit(){if(entry_){nr_deferred_dx12::Cancel(entry_->deferred);nr_rr_dx12::Cancel(entry_->rr);entry_->previous_camera={};}}",
        "void CancelPendingEdit(){if(entry_){nr_deferred_dx12::Cancel(entry_->deferred);nr_rr_dx12::Cancel(entry_->rr);entry_->previous_camera={};entry_->edit_history.Invalidate();}}")
    # A scalar Reset cannot establish a missing per-pixel exposure domain.
    text = replace(text,
        "    const auto color=ReadColorContract(params);NrColorSemanticContract semantic{};",
        "    const auto color=ReadColorContract(params);if(color.exposure_texture)return false;\n    NrColorSemanticContract semantic{};")
    return text

def settings_ui(text):
    anchor = '    if(mode==2){'
    block = r"""    if(mode==1){
        const float ranges[]={1.0f/64.0f,0.125f,1.0f,4.0f,16.0f};
        int selected=5;
        for(int i=0;i<5;++i)if(next.edit_experiment.carrier_range==ranges[i])selected=i;
        if(ImGui::Combo(zh?"残差载体范围（A/B）##editrange":"Residual carrier range (A/B)##editrange",&selected,
            "1/64 (fine, more spatial tail)\0" "1/8\0" "1\0" "4 (v4 baseline)\0" "16\0" "Custom\0")){
            if(selected<5){next.edit_experiment.carrier_range=ranges[selected];changed=true;}
        }
        SettingsNote(zh?"范围缩小提高小残差精度，但更多大幅修改走空间尾差；不是降噪强度。":
            "Smaller range preserves smaller edits but shifts more large edits to the spatial tail; not a denoising strength.");
    }
    ImGui::Text("NR edit input %u/%u; edit history reset requests %llu",
        nr_edit_core::actual_chain_used.load(),nr_edit_core::actual_chain_requested.load(),
        static_cast<unsigned long long>(nr_edit_core::chain_reset_requests.load()));
    ImGui::TextWrapped("%s",nr_edit_core::chain_history_reason.load());
"""
    return replace(text, anchor, block + anchor)

def upgrade(package:Path, additions:Path):
    package=package.resolve(strict=True);additions=additions.resolve(strict=True)
    marker=package/"D21-UPGRADE.json"
    if marker.exists():raise ValueError("D21 upgrade already applied")
    updates={}
    p="src/nr_edit_core_runtime.inl"
    updates[p]=runtime((package/p).read_text(encoding="utf-8-sig"))
    p="src/nr_edit_settings_ui.inl"
    updates[p]=settings_ui((package/p).read_text(encoding="utf-8-sig"))
    p="src/nr_edit_policy.hpp"
    t=(package/p).read_text(encoding="utf-8-sig")
    t=replace(t,"#include <cstdint>","#include <cstdint>\n#include \"nr_edit_chain_history.hpp\"")
    updates[p]=replace(t,"std::clamp(s.carrier_range,.125f,64.f)",
                          "std::clamp(s.carrier_range,1.0f/64.0f,64.f)")
    p="tools/stage_edit_integration.py"
    t=(package/p).read_text(encoding="utf-8-sig")
    t=replace(t," 'src/nr_edit_policy.hpp',",
        " 'src/nr_edit_chain_history.hpp','src/nr_edit_d21_chain.inl','src/nr_edit_policy.hpp',")
    t=t.replace("edit-v4","edit-d21-v5").replace("NR_EDIT_INTEGRATION_V4","NR_EDIT_INTEGRATION_V5")
    t=replace(t,'#include "../build/nr_edit_binaries.hpp"','#include "nr_edit_binaries.hpp"')
    updates[p]=t
    p="tools/build_against.py"
    t=(package/p).read_text(encoding="utf-8-sig").replace("edit-v4","edit-d21-v5")
    t=replace(t,"  run('runtime-test',[out/'runtime-contract-test.exe'])",
        """  run('runtime-test',[out/'runtime-contract-test.exe'])
  run('d21-chain-compile',[compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-static',
   '-I'+str(PACKAGE/'src'),PACKAGE/'tests/chain_history_test.cpp','-o',out/'d21-chain.exe'])
  run('d21-chain',[out/'d21-chain.exe'])
  run('carrier-precision',[sys.executable,PACKAGE/'tests/test_carrier_precision.py','-v'])""")
    updates[p]=t
    p="tests/test_integration.py"
    if (package/p).is_file():
        updates[p]=(package/p).read_text(encoding="utf-8-sig").replace("edit-v4","edit-d21-v5")
    for name in ("nr_edit_chain_history.hpp","nr_edit_d21_chain.inl"):
        p="src/"+name
        if (package/p).exists():raise ValueError("Unexpected existing new module "+p)
        updates[p]=(additions/"src"/name).read_text(encoding="utf-8-sig")
    for name in ("chain_history_test.cpp","test_carrier_precision.py"):
        p="tests/"+name
        if (package/p).exists():raise ValueError("Unexpected existing new test "+p)
        updates[p]=(additions/"tests"/name).read_text(encoding="utf-8-sig")
    # Validate generated Python before modifying the exported candidate.
    # Import/grammar validation is not Windows/Core/HLSL validation.
    for name,text in updates.items():
        if name.endswith(".py"):
            compile(text, name, "exec")
    for name,text in updates.items():
        (package/name).parent.mkdir(parents=True,exist_ok=True)
        (package/name).write_text(text,encoding="utf-8",newline="\n")
    import hashlib,json
    marker.write_text(json.dumps({
        "schema":"nr-edit-d21-upgrade-v1",
        "source_base":BASE,
        "intended_target":D21,
        "changes":{name:hashlib.sha256((package/name).read_bytes()).hexdigest() for name in updates},
        "windows_build":"not-run","nvidia":"not-run","game":"not-run",
        "post_rr_dlss_enlargement":"not-implemented",
        "amd_optical_flow":"not-installed",
        "original_docs_are_historical":True,
    },indent=2),encoding="utf-8")
    return updates
