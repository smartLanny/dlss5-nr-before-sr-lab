// Final UI replaces the historical overlay layout after all source adapters.
#include "nr_unified_pages.inl"
void DrawSettings(reshade::api::effect_runtime *runtime) {
    const bool use_chinese_ui = UseChineseUi();
    const auto ui = [use_chinese_ui](const char *en, const char *zh) { return use_chinese_ui ? zh : en; };
    static ChainConfig pending = {};
    static bool has_pending = false;
    static bool pending_picture_reset = false;
    static bool pending_light_undo = false;
    static nr_light_style::UndoState light_undo;
    static nr_model_controls::UndoState model_undo;
    static auto pending_model_action = nr_model_controls::Action::None;
    static unsigned pending_generation = 0;
    static bool edit_conflict = false;
    static bool migration_notice_dismissed = false;
    const auto current = SnapshotOverlayConfig();
    const auto compatibility_snapshot = SnapshotCompatibilityFallback();
    light_undo.Observe(current.generation);
    model_undo.Observe(current.generation);
    if (has_pending && pending_generation != current.generation) {
        has_pending = false; pending_picture_reset = false; pending_light_undo = false; edit_conflict = true;
        pending_model_action = nr_model_controls::Action::None;
    }
    ChainConfig next = has_pending ? pending : current.config;
    bool changed = false;

    bool apply_discrete_edit=false;
    const bool collecting=nr_f8::g_config_hold.load();
    const bool rr_guarded=IsRayReconstructionRouteObserved();
    const bool compatibility_post=!rr_guarded&&next.mode==2&&compatibility_snapshot.recent&&compatibility_snapshot.effective_mode==1;
    const bool before_sr=!rr_guarded&&nr_processing_runtime::Effective()==nr_processing::Start::Before;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,4.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(ImGui::GetStyle().ItemSpacing.x,6.f));
    // A read-only, common header stays outside each page's scrolling child.
    ImGui::TextWrapped("DLSS 5  %s",kAddonVersion);
    SettingsTooltip(ui("Full build identity, author and links are on Feedback.","完整构建信息、作者和链接见反馈页。"));
    DrawNrActivityStatus(runtime,current.config,current.generation,nr_ui_activity::Site::Top);
    const char* routes[]={ui("Enhance → upscale","增强→放大"),ui("Upscale → enhance","放大→增强"),ui("Compatibility","自动兼容")};
    const auto actual=nr_processing_runtime::Effective();
    unsigned actual_w=current.work_width,actual_h=current.work_height;
    if(actual==nr_processing::Start::Present){nr_compat_present::Observation cp{};if(nr_compat_present_runtime::TrySnapshot(cp)){actual_w=cp.work_width;actual_h=cp.work_height;}else actual_w=actual_h=0;}
    if(actual_w&&actual_h){
        ImGui::TextDisabled(ui("Current position: %s","当前位置：%s"),routes[std::min(static_cast<unsigned>(actual),2u)]);
        char size_hint[128]{};std::snprintf(size_hint,sizeof(size_hint),ui("Model work size: %u x %u per layer","模型工作尺寸：每层 %u × %u"),actual_w,actual_h);
        SettingsHelp(size_hint);
    } else SettingsNote(ui("Preparing: waiting for a game picture.","准备中：等待游戏画面。"));
    if(has_pending||changed)SettingsNote(ui("Settings pending; apply on release or input commit.","设置准备应用。"));
    if(next.identity_use_original||next.colour_neutral_probe)SettingsNote(ui("Diagnostic output is active; this is not the normal NR result.","诊断中：非正常 NR 画面。"));
    if(next.colour_restore_mode)SettingsNote(ui("RGB recovery active; reported halo regression. Change it in Experiments.","RGB 实验开启：有亮边反馈。"));
    if(next.light_preview||next.region_preview||next.face_preview){
        SettingsNote(ui("A preview is active; this is not the final picture.","预览中：非最终画面。"));
    }
    if(g_config_persistence_failed.load())SettingsNote(ui("Settings could not be saved; current session still uses them.","设置未保存：本次运行仍采用。"));
    if (edit_conflict)
        ImGui::TextWrapped("%s", ui(
            "Settings changed elsewhere. The stale edit was discarded; adjust again using the current values.",
            "设置已更新：请按当前值重新调整。"));
    if(ImGui::BeginTabBar("nr-main-tabs",ImGuiTabBarFlags_FittingPolicyScroll)){
        const ImGuiTabItemFlags feedback_tab_flags=nr_f8::TakeSettingsFeedbackPageRequest()?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;
        if(ImGui::BeginTabItem(ui("Picture###nr-picture","画面###nr-picture"))){
            ImGui::BeginChild("nr-page-picture",ImVec2(0,0));
            ImGui::BeginDisabled(collecting);
            char nr_label[96]{};
            std::snprintf(nr_label,sizeof(nr_label),ui("NR (%s)###master-nr","启用 NR（%s）###master-nr"),nr_hotkeys::Name(next.master_virtual_key));
            if(ImGui::Checkbox(nr_label,&next.enabled)){changed=true;apply_discrete_edit=true;}
            if(SettingsSliderFloat(ui("AI colour range","AI 色彩范围"),"##final-colour",&next.color_strength,0.f,1.f,"%.2f")){
                nr_colour_preferences::SetRange(next,next.color_strength);changed=true;
            }
            SettingsTooltip(ui("0 keeps the game colour; 1 requests the full NR colour change, subject to safety limits. This is not whole-image saturation. Editing adopts the newer colour range, including internal HDR inputs.",
                "0 保留游戏颜色，1 请求完整 NR 色彩，仍受安全限制。不是全图饱和度；调整后采用新版色彩范围，也包括游戏内部 HDR。"));
            if(!next.colour_range_control)SettingsNote(ui("Saved legacy colour behaviour retained; this is its requested amount. Editing this slider adopts the new colour range.","已保留旧颜色规则；当前数值为旧请求量。调整此滑条后改用新的色彩范围。"));
            DrawUnifiedColourState(next,current.generation,has_pending||changed);
            SettingsSection(ui("Processing order","增强位置"));
            if(DrawProcessingModeBlocks(next,rr_guarded,compatibility_post,compatibility_post?1:next.mode)){changed=true;apply_discrete_edit=true;}
            SettingsSection(ui("Workload","工作量"));
            DrawNrNetworkControls(next,changed,apply_discrete_edit);
#if NR_HAS_DETAIL_STABILITY
    const auto scale_target=nr_precision_ui::SelectedTarget(
        static_cast<unsigned>(next.processing_start),rr_guarded,
        nr_processing_runtime::FgRedirectRequested());
    const auto post_options=nr_post_settings::Read();
    const auto post_revision=nr_post_settings::revision.load(std::memory_order_acquire);
    const float saved_precision=scale_target==nr_precision_ui::Target::Present
        ? static_cast<float>(next.compat_percent)
        : scale_target==nr_precision_ui::Target::Post ? static_cast<float>(post_options.percent)
        : nr_precision_ui::LegacyEstimate(next.work_mode,next.custom_work_scale,
            g_last_dlss_input_width.load(),g_last_dlss_input_height.load(),
            g_last_dlss_output_width.load(),g_last_dlss_output_height.load());
    static nr_precision_ui::Edit scale_edit;
    float precision=scale_edit.Observe(scale_target,current.generation,post_revision,
        next.processing_selection,std::clamp(saved_precision,nr_precision_ui::MinimumPercent(scale_target),100.f));
    const auto edit_target=scale_edit.target;
    const char* request_name=edit_target==nr_precision_ui::Target::Before
        ? ui("Pre-SR request","前置请求") : edit_target==nr_precision_ui::Target::Post
        ? ui("Post-SR request","后置请求") : ui("Compatibility request","兼容请求");
    ImGui::TextDisabled(ui("Editing: %s","正在设置：%s"),request_name);
    const bool scale_changed=SettingsSliderFloat(ui("Chain working scale", "NR 工作比例"),
        "##nr-processing-precision", &precision, nr_precision_ui::MinimumPercent(edit_target), 100.0f,"%.0f%%");
    if (scale_edit.Finish(precision,ImGui::IsItemActivated(),scale_changed,
            ImGui::IsItemActive(),ImGui::IsItemDeactivatedAfterEdit())) {
        if (!nr_precision_ui::SupportedPercent(scale_edit.preview,edit_target)) {
            scale_edit.conflict=true;
        } else if (edit_target==nr_precision_ui::Target::Present) {
            next.compat_percent=static_cast<unsigned>(std::lround(scale_edit.preview));
            changed=true;
        } else if (edit_target==nr_precision_ui::Target::Post) {
            // The ordinary Post request publishes separately from the overlay.
            // Keep its existing chain lock while checking the captured revision,
            // so a concurrent INI/config update cannot be overwritten after check.
            EnterCriticalSection(&g_chain_cs);
            if (g_config_generation.load(std::memory_order_relaxed)==scale_edit.generation &&
                nr_post_settings::revision.load(std::memory_order_acquire)==scale_edit.post_revision) {
                auto request=post_options;
                request.percent=static_cast<unsigned>(std::lround(scale_edit.preview));
                if (!nr_post_settings::Request(request)) scale_edit.conflict=true;
            } else scale_edit.conflict=true;
            LeaveCriticalSection(&g_chain_cs);
        } else {
            const auto request=nr_precision_ui::Resolve(scale_edit.preview,true);
            if (request.accepted) {
                next.work_mode=request.mode;next.custom_work_scale=request.scale;changed=true;
            }
        }
    }
    SettingsTooltip(ui(
        "Sets 50-100% before upscaling, or 25-100% after upscaling and in Compatibility, once for all layers. Automatic fallback does not change this saved request. RR and FG-redirected compatibility use the Post-SR request. Applied on release; game resolution stays unchanged.",
        "前置 50–100%，后置及兼容 25–100%。所有层共用；松手应用。游戏分辨率不变，游戏 RR 后增强使用后置比例。"));
    if (scale_edit.conflict)
        SettingsNote(ui("The scale edit was not applied: its target or settings changed, or the request was unavailable. Release and adjust the current value again.",
            "比例编辑未应用：编辑目标或设置已变化，或请求暂不可用。请松手后按当前值重新调整。"));
    unsigned observed_base_w=current.base_width,observed_base_h=current.base_height;
    unsigned observed_work_w=current.work_width,observed_work_h=current.work_height;
    if (nr_processing_runtime::Effective()==nr_processing::Start::Present) {
        nr_compat_present::Observation dimensions{};
        if (nr_compat_present_runtime::TrySnapshot(dimensions)) {
            observed_base_w=dimensions.source_width;observed_base_h=dimensions.source_height;
            observed_work_w=dimensions.work_width;observed_work_h=dimensions.work_height;
        } else observed_base_w=observed_base_h=observed_work_w=observed_work_h=0;
    }
    if (observed_base_w&&observed_base_h&&observed_work_w&&observed_work_h)
        ImGui::TextDisabled(ui("Last NR: base %u x %u -> model %u x %u (each layer)",
            "最近尺寸：%u × %u → %u × %u"),
            observed_base_w,observed_base_h,observed_work_w,observed_work_h);
    else SettingsNote(ui("Model dimensions appear after NR runs.","NR 运行后显示实际模型尺寸。"));
    if (scale_target==nr_precision_ui::Target::Before &&
        g_work_request_rejected.load(std::memory_order_acquire)) {
        SettingsNote(ui(
            "The INI work request was rejected. Previous valid precision is retained; 1-49% is unavailable, not silently raised to 50%.",
            "INI 工作比例请求无效，已保留上一有效精度；1–49% 尚不可用，不会暗中提高到 50%。"));
    }
#endif

            DrawUniformLayerCount(next,changed,apply_discrete_edit);
            SettingsSection(ui("Final effect","最终效果"));
            float strength=ActiveTransferStrength(next);
            if(SettingsSliderFloat(ui("Final extra enhancement","最终效果强度"),"##final-gain",&strength,1.f,4.f,"%.2f")){next.transfer_strength=next.post_transfer_strength=std::clamp(FiniteOr(strength,1.f),1.f,4.f);changed=true;}
            SettingsTooltip(ui("Scales final brightness/detail edits, not model strength. 1 keeps the model result's base amount.","调整最终明暗和细节修改量，不是模型强度；1 保留模型原量。"));
            if(nr_unified_ui::LightMethod(next)==0&&nr_unified_ui::LegacyLightActive(next))SettingsNote(ui("Legacy lighting scope or component gains are active; inspect Lighting.","旧光影组合开启：见光影页。"));
            if(next.low_grid_edit_field||next.low_grid_color_edit_field||next.independent_chroma_composition||next.colour_scaled_proxy_mode||next.colour_edge_recovery||!next.color_protection||!next.high_strength_protection)
                SettingsNote(ui("Custom composition or safeguard settings are active; inspect Developer.","画质实验开启：见开发者页。"));
            DrawUniformLayerControls(next,changed,apply_discrete_edit);
            ImGui::Separator();
            if(ImGui::Button(ui("Restore picture###Restore default picture","恢复推荐画质###恢复默认画质"))){
                RestoreRecommendedPictureSettings(next);
                next.processing_start=nr_processing::Start::Before;
                next.processing_selection=nr_processing_runtime::next_selection.fetch_add(1);
                // Keep compatibility controls, including their separately saved scale.
                next.nr_full_reference=false;next.nr_full_passes=3;
                changed=true;pending_picture_reset=true;pending_model_action=nr_model_controls::Action::None;apply_discrete_edit=true;
            }
            SettingsTooltip(ui("Restores pre-SR, one layer, 100% work, runtime-default global network, all layers following global, model 1.5, skin 0.4, final enhancement 1, AI colour range 0.5 and picture defaults. Keeps NR enabled state, shortcuts and compatibility preferences.",
                "恢复推荐画质参数与实验默认值：全局网络交给运行库默认，各层跟随全局。保留总开关、快捷键和兼容设置。"));
            ImGui::EndDisabled();ImGui::EndChild();ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(ui("Lighting###nr-lighting","光影###nr-lighting"))){
            ImGui::BeginChild("nr-page-lighting",ImVec2(0,0));
            ImGui::BeginDisabled(collecting);DrawUnifiedLight(next,before_sr,current.generation,light_undo,has_pending,pending_light_undo,changed,apply_discrete_edit);ImGui::EndDisabled();ImGui::EndChild();ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(ui("Experiments###nr-experiments","实验###nr-experiments"))){
            ImGui::BeginChild("nr-page-experiments",ImVec2(0,0));
            ImGui::BeginDisabled(collecting);DrawUnifiedExperiments(next,current.generation,has_pending,changed,apply_discrete_edit);ImGui::EndDisabled();
#if defined(NR_HAS_ACCEPTANCE)
            nr_acceptance::DrawScreenshotUi(runtime,has_pending);
#endif
            ImGui::EndChild();ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(ui("Developer###nr-developer","开发者###nr-developer"))){
            ImGui::BeginChild("nr-page-developer",ImVec2(0,0));
            ImGui::BeginDisabled(collecting);DrawUnifiedDeveloper(next,before_sr,changed,apply_discrete_edit);ImGui::EndDisabled();ImGui::EndChild();ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(ui("Feedback###nr-feedback-diagnostics","反馈###nr-feedback-diagnostics"),nullptr,feedback_tab_flags)){
            ImGui::BeginChild("nr-page-feedback-diagnostics",ImVec2(0,0));
            DrawConfigPolicyMigrationNotice(SnapshotConfigPolicyMigrationStatus(),migration_notice_dismissed);
            DrawUnifiedFeedback(runtime,current.config,has_pending);
            if(kChineseUi&&!use_chinese_ui)SettingsNote("Chinese glyphs are unavailable. Select a Chinese font in ReShade to use this language.");
            ImGui::EndChild();ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (changed) {
        pending = next; pending_generation = current.generation;
        has_pending = true; edit_conflict = false;
    }
    if (has_pending && (apply_discrete_edit || !ImGui::IsAnyItemActive())) {
        const auto result = ApplySettingsOverlay(pending,pending_generation,
            pending_picture_reset,pending_light_undo,light_undo,pending_model_action,model_undo);
        if (result != OverlayApplyResult::Busy) {
            has_pending = false; pending_picture_reset = false; pending_light_undo = false;
            pending_model_action = nr_model_controls::Action::None;
        }
        if (result == OverlayApplyResult::Conflict) edit_conflict = true;
    }
    ImGui::PopStyleVar(2);
}
