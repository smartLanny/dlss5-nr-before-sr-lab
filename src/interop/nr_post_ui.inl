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
    ImGui::SliderInt(ui("Post NR precision (% of output)","后置 NR 处理精度（输出尺寸 %）"),
        &edit,0,100,"%d%%");
    if(ImGui::IsItemDeactivatedAfterEdit()) {
        options.percent=static_cast<unsigned>(edit);
        nr_post_settings::Request(options);
        edit=static_cast<int>(nr_post_settings::Read().percent);
    }
    for(int percent : {100,75,67,50}) {
        char label[24]{}; snprintf(label,sizeof(label),"%d%%##post-nr",percent);
        if(ImGui::Button(label)) { options.percent=percent; nr_post_settings::Request(options); }
        if(percent!=50) ImGui::SameLine();
    }
    if(held) ImGui::EndDisabled();
    ImGui::TextWrapped("%s",ui(
        "Npost/O controls NR cost after SR/RR, not game rendering R. 100% is full output; 50-99% rebuilds only matched neural edits over the original output. 0 switches NR off and retains precision. 1-49% is rejected. Nonzero edits never enable F6.",
        "Npost/O 只调节 SR/RR 后的 NR 成本，不改变游戏渲染 R。100% 为完整输出，50–99% 仅把匹配的模型改动回填原图。0 关闭 NR 并保留精度；1–49% 明确拒绝，非零编辑不会开启 F6。"));
    if(nr_post_settings::request_rejected.load(std::memory_order_acquire))
        ImGui::TextWrapped(ui("Unsupported request %u%%: previous precision retained, not clamped. F8 TXT for BUG feedback.",
            "不支持 %u%% 请求：保留原精度，不暗中改为其他比例。异常时请 F8 收集 TXT 上传 BUG。"),
            nr_post_settings::rejected_percent.load(std::memory_order_acquire));
}

void DrawPostCompatibility() {
    const bool zh=UseChineseUi();
    auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    auto options=nr_post_settings::Read();
    ImGui::SeparatorText(ui("Post-SR / RR compatibility", "SR / RR 后置兼容"));
    if(ImGui::Checkbox(ui("Use RR -> NR when Ray Reconstruction is active",
                          "检测到光线重建时使用 RR → NR"), &options.rr))
        nr_post_settings::Request(options);
    ImGui::TextWrapped("%s",ui(
        "Your requested front/post mode is retained. Precision for the effective route is below Processing mode, not a second independent NR instance.",
        "保留你请求的前置/后置模式；有效路线的精度位于处理模式下方，不会额外运行第二套 NR。"));
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
