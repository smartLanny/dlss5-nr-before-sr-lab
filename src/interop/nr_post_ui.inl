// This include precedes nr_settings_ui.inl in the generated translation unit.
void SettingsTooltip(const char *text);
void SettingsNote(const char *text);

// Explicit picture reset commits front preferences and post precision under
// the same lock. A capture starting meanwhile cannot snapshot half a reset:
// its SnapshotConfig waits for this lock. ApplyOverlayConfig checks both the
// captured generation and the hold before any mutation; no request occurs on
// Busy/Conflict. Ordinary edits keep their independent post preference.
OverlayApplyResult ApplyPictureReset(ChainConfig next, unsigned expected_generation) {
    EnterCriticalSection(&g_chain_cs);
    const auto result=ApplyOverlayConfig(next,expected_generation);
    if (result==OverlayApplyResult::Applied) {
        nr_post_settings::revision.fetch_add(1,std::memory_order_release);
        nr_post_settings::ApplyLocked({nr_post_settings::RrPostAutomatic(),100});
        nr_post_settings::ready.store(true,std::memory_order_release);
        nr_post_settings::request_rejected.store(false,std::memory_order_release);
        const bool rr_saved=WriteIniInt(L"RRPostEnabled",1);
        const bool scale_saved=WriteIniInt(L"PostWorkPercent",100);
        nr_post_settings::persistence_failed.store(!rr_saved||!scale_saved,std::memory_order_release);
    }
    LeaveCriticalSection(&g_chain_cs);
    return result;
}

void DrawGenericCompatibilityStatus() {
#if defined(NR_HAS_ROUTE_COMPATIBILITY)
    const auto snapshot=SnapshotNrRoute();
    const auto text=nr_route::Notice(snapshot,IsRayReconstructionRouteObserved(),UseChineseUi());
    if (*text) ImGui::TextWrapped("%s",text);
#else
    const auto snapshot=SnapshotColorPolicy();
    const bool recorded=snapshot.status==NrColorPolicyStatus::Current && snapshot.mode==1;
    ImGui::TextWrapped("%s",UseChineseUi() ? (recorded ?
        "已自动切换到兼容处理：SR → NR。原来的 NR → SR 设置已保留；请按 F8 反馈，帮助我们改善兼容性。" :
        "正在尝试兼容处理：SR → NR，暂未确认 NR 生效。原设置已保留；持续无效时请按 F8。") : (recorded ?
        "Compatibility fallback: SR -> NR recently recorded. Your NR -> SR preference is retained. Please save an F8 report." :
        "Trying compatibility fallback: SR -> NR; NR processing is not confirmed. Your preference is retained. Save F8 if this persists."));
#endif
}

// RR observation selects a route; only recent, matching NR activity describes
// execution. A saved front preference must not be mistaken for applied post work.
void DrawPostRouteStatus() {
    const bool zh = UseChineseUi();
    auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    const auto snapshot = SnapshotColorPolicy();
    if (snapshot.status == NrColorPolicyStatus::Current && snapshot.mode == 1) {
        ImGui::TextWrapped("%s", ui("After game RR: recent NR processing recorded.",
            "游戏 RR 后增强：最近有处理记录。"));
        return;
    }
    const auto tick = nr_post_settings::last_rr_tick.load(std::memory_order_acquire);
    const auto now = GetTickCount64();
    const bool recent = tick && now >= tick && now - tick <= 2000;
    const int depth = nr_post_settings::last_rr_depth_type.load(std::memory_order_acquire);
    if (recent && depth != 1 && snapshot.status == NrColorPolicyStatus::Bypassed) {
        ImGui::TextWrapped("%s", depth == 0 ? ui(
            "After game RR: NR not applied; depth unavailable.",
            "游戏 RR 后增强未生效：深度不适用。") : ui(
            "After game RR: NR not applied; depth unverified.",
            "游戏 RR 后增强未生效：深度未确认。"));
    } else if (snapshot.status == NrColorPolicyStatus::Bypassed) {
        ImGui::TextWrapped("%s", ui("After game RR: NR not applied.",
            "游戏 RR 后增强未生效。"));
    } else {
        ImGui::TextWrapped("%s", ui("After game RR: preparing; awaiting NR record.",
            "游戏 RR 后增强：准备中，等待处理记录。"));
    }
}

// Called directly below the mode selector for an effective post route.
void DrawPostResolutionControl() {
    const bool zh=UseChineseUi();
    auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    auto options=nr_post_settings::Read();
    static int edit=100;
    static unsigned last_observed=0;
    if(last_observed!=options.percent) {
        edit=static_cast<int>(options.percent); last_observed=options.percent;
    }
    const bool held=nr_f8::g_config_hold.load(std::memory_order_acquire);
    if(held) ImGui::BeginDisabled();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(ui("NR rendering scale","NR 渲染比例"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##post-nr-working-scale",&edit,25,100,"%d%%");
    if(ImGui::IsItemDeactivatedAfterEdit()) {
        options.percent=static_cast<unsigned>(edit);
        nr_post_settings::Request(options);
        edit=static_cast<int>(nr_post_settings::Read().percent);
    }
    if(held) ImGui::EndDisabled();
    SettingsTooltip(ui(
        "Controls only post-SR/RR NR work from 25% to 100%. It does not change the game's render scale or enable F6.",
        "控制后置 NR 的工作比例，支持 25–100%。数值越高，通常细节越完整，但会占用更多显卡性能，帧率可能降低。此处只调整 NR，游戏本身的分辨率不变。"));
    if(nr_post_settings::request_rejected.load(std::memory_order_acquire))
        ImGui::TextWrapped(ui("Unsupported request %u%%: previous precision retained, not clamped. F8 TXT for BUG feedback.",
            "不支持 %u%% 请求：保留原精度，不暗中改为其他比例。异常时请 F8 收集 TXT 上传 BUG。"),
            nr_post_settings::rejected_percent.load(std::memory_order_acquire));
}

void DrawPostCompatibility() {
    const bool zh=UseChineseUi();
    auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    ImGui::SeparatorText(ui("After game SR / RR", "游戏 SR / RR 后增强"));
    if(IsRayReconstructionRouteObserved()) {
        const auto at=nr_post_settings::last_rr_tick.load(std::memory_order_acquire);
        const auto now=GetTickCount64();
        const bool recent=at && now>=at && now-at<=2000;
        ImGui::TextWrapped("%s",recent ? ui(
            "After game RR: this enhancement position is selected automatically.",
            "游戏 RR 后增强：自动选择此位置。") : ui(
            "After game RR: waiting for a new picture.",
            "游戏 RR 后增强：等待新画面。"));
        if(ImGui::CollapsingHeader(ui("RR diagnostics###rr-post-details","RR 诊断详情###rr-post-details"))) {
            ImGui::Text(ui("Cumulative RR attempts / NR recordings: %llu / %llu | last stage %d",
                           "累计 RR 尝试 / NR 录制：%llu / %llu | 最近阶段 %d"),
                nr_post_settings::rr_attempts.load(),nr_post_settings::rr_recorded.load(),
                nr_post_settings::last_rr_stage.load());
            if(!recent) SettingsNote(ui("No recent RR attempt; counts are historical.","暂无近期 RR 尝试；计数为历史累计。"));
        }
    }
    if(nr_post_settings::persistence_failed.load())
        ImGui::TextWrapped("%s",ui("Save failed; these requests are session-only.","保存失败，请求仅在本进程生效。"));
    SettingsTooltip(ui(
        "Enhancement runs after game RR. Sending NR into game RR is not available. A recording does not prove a visible result; use F8 if bypass persists.",
        "这里是在游戏 RR 后增强；先增强再交给游戏 RR 尚未提供。处理记录不代表画面已生效；持续无效请按 F8。"));
}
