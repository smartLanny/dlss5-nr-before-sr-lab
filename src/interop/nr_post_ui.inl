void DrawPostCompatibility() {
    const bool zh=UseChineseUi();
    auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    auto options=nr_post_settings::Read();
    ImGui::SeparatorText(ui("Post-SR / RR compatibility", "SR / RR 后置兼容"));
    if(ImGui::Checkbox(ui("Use RR -> NR when Ray Reconstruction is active",
                          "检测到光线重建时使用 RR → NR"), &options.rr))
        nr_post_settings::Request(options);
    static int edit=100;
    static unsigned last_observed=0;
    if(last_observed!=options.percent) {edit=static_cast<int>(options.percent);last_observed=options.percent;}
    ImGui::SliderInt(ui("Post NR resolution (% of output)","后置 NR 分辨率（输出尺寸百分比）"),&edit,50,100,"%d%%");
    if(ImGui::IsItemDeactivatedAfterEdit()) {
        options.percent=static_cast<unsigned>(edit); nr_post_settings::Request(options);
    }
    for(int percent : {100,75,67,50}) {
        char label[24]{}; snprintf(label,sizeof(label),"%d%%##post-nr",percent);
        if(ImGui::Button(label)) { options.percent=percent; nr_post_settings::Request(options); }
        if(percent!=50) ImGui::SameLine();
    }
    ImGui::TextWrapped("%s",ui(
        "This changes NR cost after SR/RR, not the game's render resolution. Your requested front/post mode is retained. The 100% path is the reference; smaller work rasters reconstruct only the matched model change over the original output.",
        "这里调节 SR/RR 之后的 NR 成本，不是游戏原生渲染分辨率。你原来请求的前置/后置模式保留；100% 为基准，小工作图仅把匹配的模型改动重建到原始输出。"));
    if(g_live_rr_handles.load(std::memory_order_acquire)!=0) {
        ImGui::TextWrapped("%s", options.rr ? ui(
            "RR detected: RR -> NR requested. Recorded work is not proof of GPU completion or a visible change; compare current submissions and pictures.",
            "检测到 RR，当前请求 RR → NR。录制成功不等于 GPU 完成或画面生效，请结合当前提交计数与画面对比。") : ui(
            "RR detected; post-NR is disabled. Native RR is retained and your Mode request is unchanged.",
            "检测到 RR，但后置 NR 已关闭。保留原生 RR，原 Mode 请求不变。"));
        ImGui::Text(ui("Cumulative RR attempts / NR recordings: %llu / %llu | last stage %d",
                       "累计 RR 尝试 / NR 录制：%llu / %llu | 最近阶段 %d"),
            nr_post_settings::rr_attempts.load(),nr_post_settings::rr_recorded.load(),
            nr_post_settings::last_rr_stage.load());
        const auto at=nr_post_settings::last_rr_tick.load();
        if(!at || GetTickCount64()-at>2000)
            ImGui::TextDisabled("%s",ui("No recent RR-post attempt; above counts are historical.",
                "暂无近期 RR 后置尝试；上方计数是历史累计值。"));
    }
    if(nr_post_settings::persistence_failed.load())
        ImGui::TextWrapped("%s",ui("Save failed; these requests are session-only.","保存失败，请求仅在本进程生效。"));
    ImGui::TextWrapped("%s",ui(
        "If it remains bypassed or the picture is wrong, press F8 and send the TXT as a BUG report. Input depth/motion are native; post-color phase alignment still needs game validation.",
        "仍旁路或画面异常时，请按 F8 生成 TXT 并上传 BUG。深度/运动来自原生输入；后置画面与导引相位仍需游戏验证。"));
}
