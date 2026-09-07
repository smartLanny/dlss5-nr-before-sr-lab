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
    ImGui::TextUnformatted(ui("NR working scale","NR 工作比例"));
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
        "仅调整 SR/RR 后置 NR 的工作量，可在 50%–100% 连续选择；不改变游戏真实渲染比例，也不会开启 F6。"));
    if(nr_post_settings::request_rejected.load(std::memory_order_acquire))
        ImGui::TextWrapped(ui("Unsupported request %u%%: previous precision retained, not clamped. F8 TXT for BUG feedback.",
            "不支持 %u%% 请求：保留原精度，不暗中改为其他比例。异常时请 F8 收集 TXT 上传 BUG。"),
            nr_post_settings::rejected_percent.load(std::memory_order_acquire));
}

void DrawPostCompatibility() {
    const bool zh=UseChineseUi();
    auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    ImGui::SeparatorText(ui("Post-SR / RR compatibility", "SR / RR 后置兼容"));
    if(ShouldGuardFeature1ForRayReconstruction()) {
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
