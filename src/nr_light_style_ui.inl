// Every edit joins the existing pending overlay transaction. Presets never
// call ApplyConfig independently or write another group's preferences.
bool LightStylePercent(const char *label,const char *id,float &value,float maximum) {
    float percent=value*100.f;
    if (!SettingsSliderFloat(label,id,&percent,0.f,maximum*100.f,"%.0f%%")) return false;
    value=percent*.01f;
    return true;
}

void DrawLightStyleControls(ChainConfig &next,bool before_sr,unsigned generation,
    nr_light_style::UndoState &undo,bool has_pending,bool &pending_undo,
    bool &changed,bool &apply_discrete_edit) {
    const bool zh=UseChineseUi();
    const auto ui=[zh](const char *en,const char *cn) { return zh?cn:en; };
    SettingsSection(ui("Light and detail (candidate)","光影与细节（候选）"));
    if (!before_sr)
        SettingsNote(ui("This candidate supports post-SR light controls, including the existing RR automatic post route. If NR is bypassed or cannot run, requests remain saved without a picture change. No new RR game adapter is included.",
            "本候选已接入后置光影控制，包括既有 RR 自动后置。NR 被旁路或无法运行时，请求仍会保留，但画面不会改变；本版未增加新的 RR 游戏适配。"));
    else
        SettingsNote(ui("This candidate supports pre-SR light controls. If NR is bypassed, requests are retained without applying the effect. 100% retains the original contribution; these are not material or face masks.",
            "本候选已接入前置光影控制。NR 被旁路时，请求保留但效果不应用。100% 保留原有对应成分；这不是材质或人脸识别。"));

    bool manual=false;
    manual|=LightStylePercent(ui("Reflection enhancement","反射增强"),
        "##nr-light-reflection",next.light_reflection,2.f);
    SettingsTooltip(ui("Weights small bright changes, including possible reflections, highlights and other fine bright regions. It does not identify reflective materials.",
        "控制细小亮部变化，可能包含反光、高光或其他细亮区域，不代表识别了反射材质。"));
    manual|=LightStylePercent(ui("Dark structure","暗部结构"),
        "##nr-light-structure",next.light_structure,2.f);
    manual|=LightStylePercent(ui("Glow suppression","辉光抑制"),
        "##nr-light-glow",next.light_glow,1.f);
    SettingsTooltip(ui("Reduces the candidate's broad brightening. This is not a game-engine bloom switch; 0% preserves the neutral candidate.",
        "减少候选新增的大范围提亮，不是游戏引擎的泛光开关；0% 保留中性候选。"));
    if (manual) { nr_light_style::MarkCustom(next); changed=true; }

    const char *labels[]={ui("Original effect###light-preset","原样效果###light-preset"),
        ui("Natural###light-preset","自然耐看###light-preset"),
        ui("Less glow###light-preset","减少辉光###light-preset")};
    const float width=std::max(1.f,
        (SettingsContentWidth()-ImGui::GetStyle().ItemSpacing.x*2.f)/3.f);
    ImGui::PushID("nr-light-presets");
    for (unsigned preset=0;preset<3;++preset) {
        if (preset) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(preset));
        const bool selected=next.light_preset==preset;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_Header));
        const bool clicked=ImGui::Button(labels[preset],ImVec2(width,0));
        if (selected) ImGui::PopStyleColor();
        if (clicked) {
            nr_light_style::Write(next,nr_light_style::Select(preset,next.color_strength));
            changed=true; apply_discrete_edit=true;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    SettingsNote(next.light_preset==nr_light_style::Custom
        ? ui("Custom: values or colour were edited.","自定义：本组数值或颜色已手动调整。")
        : ui("Candidate preset; game quality is still being validated.","候选预设，游戏画质仍在验证。"));
    SettingsTooltip(ui("Presets change only this group's five coefficients and AI colour amount. Original effect and Less glow keep the current colour; Natural uses 80%. Model, mode and other experiments stay as requested.",
        "预设只改本组五项系数与 AI 色彩强度。“原样效果”和“减少辉光”保留当前颜色，“自然耐看”设为 80%；模型、处理方式和其他实验开关仍使用原设置。"));

    if (ImGui::Button(ui("Reset this group###light-reset","本组恢复中性###light-reset"))) {
        nr_light_style::Reset(next); changed=true; apply_discrete_edit=true;
    }
    SettingsTooltip(ui("Restores neutral light coefficients, keeps AI colour amount and exits diagnostic preview.",
        "恢复本组中性系数，保留 AI 色彩强度，并退出诊断预览。"));
    ImGui::SameLine();
    ImGui::BeginDisabled(has_pending||changed||!undo.Available(generation));
    if (ImGui::Button(ui("Undo group edit###light-undo","撤销本组修改###light-undo")) &&
        undo.Restore(next,generation)) {
        pending_undo=true; changed=true; apply_discrete_edit=true;
    }
    ImGui::EndDisabled();
    SettingsTooltip(ui("Undoes only the last committed group snapshot. Any later settings generation invalidates it to preserve subsequent changes.",
        "仅撤销最近提交的本组快照；后续配置发生变化后失效，避免覆盖新设置。"));
    if (next.light_preview!=0) {
        ImGui::TextWrapped("%s",ui("Diagnostic preview is active for this process only. It is not the final picture.",
            "当前为诊断预览，仅本次运行有效，显示的不是最终画面。"));
        if (ImGui::Button(ui("Exit preview###light-preview-off","退出预览###light-preview-off"))) {
            next.light_preview=0; changed=true; apply_discrete_edit=true;
        }
    }
    if (ImGui::CollapsingHeader(ui("Advanced light controls###light-advanced","光影高级选项###light-advanced"))) {
        bool broad_changed=false;
        broad_changed|=LightStylePercent(ui("Broad brightening","大范围提亮"),
            "##nr-light-broad",next.light_broad,2.f);
        broad_changed|=LightStylePercent(ui("Broad darkening","大范围暗化"),
            "##nr-light-dark",next.light_dark,2.f);
        if (broad_changed) { nr_light_style::MarkCustom(next); changed=true; }
        int preview=static_cast<int>(next.light_preview);
        if (ImGui::Combo(ui("Diagnostic preview###light-preview","诊断预览###light-preview"),&preview,
            ui("0: Final picture\0" "1: Four contribution regions\0" "2: NR before/after difference\0",
               "0：最终画面\0" "1：四分区范围\0" "2：NR 修改前后差\0"))) {
            next.light_preview=static_cast<unsigned>(preview); changed=true; apply_discrete_edit=true;
        }
        SettingsTooltip(ui("Regions: orange broad-bright, blue broad-dark, yellow fine-bright, purple fine-dark; intensity shows the remaining protected luminance edit in the active route. Difference is absolute final NR output minus original input in linear colour, magnified 4x. It includes NR, colour and protections, not only this group. Preview is not saved or an NR-history reset.",
            "四分区：橙色宽亮、蓝色宽暗、黄色细亮、紫色细暗；强度表示当前处理路线在保护后仍保留的亮度改动。差分为 NR 最终输出与原图的绝对线性差乘 4，包含原 NR、颜色和保护，并非本组独立影响。预览不保存，也不重建 NR 历史。"));
    }
}

// Issue the one normal transaction and record undo under the same lock. A
// later writer cannot be mistaken for this edit's committed generation.
OverlayApplyResult ApplyLightStyleOverlay(ChainConfig next,unsigned expected_generation,
    bool picture_reset,bool undo_requested,nr_light_style::UndoState &undo) {
    EnterCriticalSection(&g_chain_cs);
    if (undo_requested && !undo.Available(expected_generation)) {
        LeaveCriticalSection(&g_chain_cs); return OverlayApplyResult::Conflict;
    }
    const auto before=nr_light_style::Read(g_config);
    const auto result=picture_reset ? ApplyPictureReset(next,expected_generation)
        : ApplyOverlayConfig(next,expected_generation);
    if (result==OverlayApplyResult::Applied) {
        const auto committed=g_config_generation.load(std::memory_order_relaxed);
        if (undo_requested) undo.Clear();
        else if (!nr_light_style::SameSnapshot(before,nr_light_style::Read(g_config)))
            undo.Record(before,committed);
        else undo.Observe(committed);
    } else if (result==OverlayApplyResult::Conflict) undo.Clear();
    LeaveCriticalSection(&g_chain_cs);
    return result;
}
