// This include precedes nr_settings_ui.inl in the generated translation unit.
void SettingsTooltip(const char *text);

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

// RR observation selects a route; only recent, matching NR activity describes
// execution. A saved front preference must not be mistaken for applied post work.
void DrawPostRouteStatus() {
    const bool zh = UseChineseUi();
    auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    const auto snapshot = SnapshotColorPolicy();
    if (snapshot.status == NrColorPolicyStatus::Current && snapshot.mode == 1) {
        ImGui::TextWrapped("%s", ui("RR is active; NR has recently processed its output.",
            "光线重建（RR）已开启；NR 最近已在它之后处理画面。"));
        return;
    }
    const auto tick = nr_post_settings::last_rr_tick.load(std::memory_order_acquire);
    const auto now = GetTickCount64();
    const bool recent = tick && now >= tick && now - tick <= 2000;
    const int depth = nr_post_settings::last_rr_depth_type.load(std::memory_order_acquire);
    if (recent && depth != 1 && snapshot.status == NrColorPolicyStatus::Bypassed) {
        ImGui::TextWrapped("%s", depth == 0 ? ui(
            "RR is active, but NR is not processing: this game's linear depth needs an adapter. Native RR is retained; NR picture controls cannot take effect yet.",
            "光线重建（RR）已开启，但 NR 未生效：当前游戏的深度格式还需要适配。已保留游戏原生 RR；此时调整 NR 画质不会改变画面。") : ui(
            "RR is active, but NR is not processing: the game's depth format is unknown. Native RR is retained.",
            "光线重建（RR）已开启，但暂未确认游戏的深度格式，NR 未生效。已保留游戏原生 RR。"));
    } else if (snapshot.status == NrColorPolicyStatus::Bypassed) {
        ImGui::TextWrapped("%s", ui("RR is selected, but NR is currently bypassed. Save an F8 report if this persists.",
            "已选择兼容光线重建的处理方式，但 NR 当前未参与画面处理。持续无效时请按 F8。"));
    } else {
        ImGui::TextWrapped("%s", ui("RR selects NR after reconstruction. Waiting for recent NR processing; this is not an enabled-result claim.",
            "检测到光线重建，NR 将在它之后处理；正在等待新的处理记录，当前尚不能确认 NR 生效。"));
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
    ImGui::SliderInt("##post-nr-working-scale",&edit,50,100,"%d%%");
    if(ImGui::IsItemDeactivatedAfterEdit()) {
        options.percent=static_cast<unsigned>(edit);
        nr_post_settings::Request(options);
        edit=static_cast<int>(nr_post_settings::Read().percent);
    }
    if(held) ImGui::EndDisabled();
    SettingsTooltip(ui(
        "Controls only post-SR/RR NR work from 50% to 100%. It does not change the game's render scale or enable F6.",
        "控制 DLSS5 NR AI 渲染的分辨率。数值越高，通常细节越完整，但会占用更多显卡性能，帧率可能降低。此处只调整 NR，游戏本身的分辨率不变。"));
    if(nr_post_settings::request_rejected.load(std::memory_order_acquire))
        ImGui::TextWrapped(ui("Unsupported request %u%%: previous precision retained, not clamped. F8 TXT for BUG feedback.",
            "不支持 %u%% 请求：保留原精度，不暗中改为其他比例。异常时请 F8 收集 TXT 上传 BUG。"),
            nr_post_settings::rejected_percent.load(std::memory_order_acquire));
}

void DrawPostCompatibility() {
    const bool zh=UseChineseUi();
    auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    ImGui::SeparatorText(ui("Post-SR / RR compatibility", "SR / RR 后置兼容"));
    if(IsRayReconstructionRouteObserved()) {
        const auto at=nr_post_settings::last_rr_tick.load(std::memory_order_acquire);
        const auto now=GetTickCount64();
        const bool recent=at && now>=at && now-at<=2000;
        ImGui::TextWrapped("%s",recent ? ui(
            "Ray Reconstruction protection is active: SR/RR -> NR is automatic; NR -> SR is unavailable.",
            "光线重建保护中，强制后置 NR：当前为 SR/RR → NR，NR → SR 暂不可选。") : ui(
            "Ray Reconstruction protection is active; NR -> SR stays unavailable while a new RR evaluation is awaited.",
            "光线重建保护中，NR → SR 保持禁用；正在等待新的 RR 评估。"));
        ImGui::Text(ui("Cumulative RR attempts / NR recordings: %llu / %llu | last stage %d",
                       "累计 RR 尝试 / NR 录制：%llu / %llu | 最近阶段 %d"),
            nr_post_settings::rr_attempts.load(),nr_post_settings::rr_recorded.load(),
            nr_post_settings::last_rr_stage.load());
        if(!recent)
            ImGui::TextDisabled("%s",ui("No recent RR-post attempt; above counts are historical.",
                "暂无近期 RR 后置尝试；上方计数是历史累计值。"));
    }
    if(nr_post_settings::persistence_failed.load())
        ImGui::TextWrapped("%s",ui("Save failed; these requests are session-only.","保存失败，请求仅在本进程生效。"));
    SettingsTooltip(ui(
        "RR post-NR is automatic. Recording is not proof of GPU completion or a visible change; use F8 if the route remains bypassed.",
        "RR 后置 NR 自动选择。录制不代表 GPU 完成或画面生效；持续旁路时请生成 F8 TXT。"));
}
