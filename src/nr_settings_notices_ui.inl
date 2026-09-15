// Render snapshots supplied by the configuration and route owners. This file
// delegates preference changes to their owners and never reconstructs a route.
#if defined(NR_HAS_ENERGY_GATE)
void DrawEnergyGateOptions() {
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    auto status = nr_processing_runtime::ReadEnergyGateStatus();

    SettingsSection(ui("Power saving", "省电"));
    bool conservative = status.conservative;
    if (ImGui::Checkbox(ui("Pause NR in game menus (conservative, default)###nr-energy-safe",
                           "菜单暂停 NR 省电（保守，默认）###nr-energy-safe"), &conservative)) {
        (void)nr_processing_runtime::SetEnergyGateOption(true, conservative);
        status = nr_processing_runtime::ReadEnergyGateStatus();
    }
    SettingsTooltip(ui(
        "Uses only the game's explicit DLSSG NotRenderingGameFrames signal. Two confirmations separated by a short debounce pause private NR. One explicit false resumes immediately. Missing/stale evidence fails open after 2 seconds. DLSS/SR/FG continue and NR resources stay allocated.",
        "只使用游戏明确提供的 DLSSG NotRenderingGameFrames 信号。短暂防抖后连续两次确认才暂停插件自己的 NR；一次明确恢复就立即放行。信号缺失或超过 2 秒未刷新会 fail-open 自动恢复。DLSS / SR / FG 原链路继续，NR 资源不会反复销毁。"));

    bool heuristic = status.heuristic;
    if (ImGui::Checkbox(ui("Guess menu/fullscreen-UI pauses (experimental)###nr-energy-guess",
                           "猜测菜单 / 大界面暂停（实验）###nr-energy-guess"), &heuristic)) {
        (void)nr_processing_runtime::SetEnergyGateOption(false, heuristic);
        status = nr_processing_runtime::ReadEnergyGateStatus();
    }
    SettingsTooltip(ui(
        "Fallback only when the explicit DLSSG pause bit is unavailable. It guesses a menu/loading pause when the public FG BackbufferFrameID stays unchanged for at least 300 ms and four observations. A changing frame ID or an explicit game-frame signal resumes immediately. This does not inspect UI pixels and may mis-detect unusual stalls; report false pauses with F8.",
        "只在拿不到明确 DLSSG 暂停位时启用猜测：如果公开 FG BackbufferFrameID 至少 300 ms、连续 4 次观察都不变化，就把它当作菜单 / 加载类暂停。帧号一变化，或游戏重新给出明确渲染信号，就立即恢复。它不会读取 UI 像素，极端卡顿仍可能误判；如果正常游戏被暂停，请按 F8 反馈。"));

    if (status.active) {
        const bool guessed = status.cause == nr_energy_gate::Cause::Heuristic;
        ImGui::TextWrapped("%s", guessed ? ui(
            "Power saver active (heuristic): NR for the matching picture is paused; resources are retained.",
            "省电已生效（猜测）：对应画面的 NR 已暂停，资源保持。") : ui(
            "Power saver active (explicit game signal): NR for the matching picture is paused; resources are retained.",
            "省电已生效（游戏明确信号）：对应画面的 NR 已暂停，资源保持。"));
        if (guessed) SettingsNote(ui(
            "This is a speculative pause. If gameplay is still moving or the guess is wrong, press F8 and submit the report.",
            "这是猜测性暂停。如果游戏仍在正常运行或判断错误，请按 F8 生成报告并反馈。"));
    } else if (status.heuristic && status.guess_samples) {
        ImGui::TextDisabled(zh ? "猜测候选：FG 源帧停滞 %u/4，等待 300 ms 确认。" :
            "Heuristic candidate: FG source stalled %u/4 observations; waiting for 300 ms confirmation.",
            status.guess_samples);
    } else if (status.conservative || status.heuristic) {
        ImGui::TextDisabled("%s", ui(
            "Power saver armed; normal gameplay keeps NR running.",
            "省电功能已待命；正常游戏画面仍会继续运行 NR。"));
    }
    if (status.persistence_failed)
        SettingsNote(ui(
            "The last power-saver change is active for this session but could not be saved. Check that the plugin folder is writable.",
            "上一次省电选项修改已用于本次运行，但保存失败。请检查插件目录是否可写。"));
}


#endif

void DrawConfigPolicyMigrationNotice(const ConfigPolicyMigrationStatus &status,
                                    bool &dismissed) {
    if (!status.visible || dismissed) return;
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    ImGui::TextWrapped("%s", status.failed ? ui(
        "Previous settings were updated for this session, but could not be saved.",
        "旧版设置已更新用于本次运行，但保存失败。") : status.persisted ? ui(
        "Previous settings have been updated and saved.",
        "旧版设置已更新并保存。") : ui(
        "Saving the update to previous settings...",
        "正在保存旧版设置更新……"));
    if (status.extra_disabled)
        SettingsNote(ui(
            "Extra enhancement is now 1. The plugin stays off to preserve your previous zero-effect setting; F6 enables it.",
            "额外增强已改为 1。为保留旧版零效果的停用意图，插件保持关闭；可按 F6 启用。"));
    else if (status.extra_changed)
        SettingsNote(ui(
            "Extra enhancement below 1 was changed to 1. Lower Model strength if you want a weaker effect.",
            "旧版低于 1 的额外增强已改为 1。想减弱效果，请调低模型强度。"));
    if (status.jitter_changed)
        SettingsNote(ui("NR jitter forwarding is now off. You can turn it on again in the compatibility settings.",
            "已关闭向 NR 传递 Jitter（采样抖动）；需要时可在兼容设置中重新开启。"));
    if (status.failed)
        SettingsNote(ui(
            "Check that the plugin folder is writable and save an F8 report. These settings currently apply only to this session.",
            "请检查插件目录是否可写，并按 F8 生成报告。当前设置仅用于本次运行。"));
    if (ImGui::Button(ui("Got it###nr-migration-dismiss", "知道了###nr-migration-dismiss")))
        dismissed = true;
}

void DrawCompatibilityFallbackNotice(const CompatibilityFallbackSnapshot &status) {
    if (!status.recent) return;
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    ImGui::TextWrapped("%s", status.recorded ? ui(
        "Automatic compatibility route: SR -> NR. Recent NR processing was recorded.",
        "已自动使用兼容后置：SR → NR；最近已记录到 NR 处理。") : ui(
        "Trying the automatic compatibility route: SR -> NR. NR processing is not yet confirmed.",
        "正在尝试兼容后置：SR → NR；暂未确认 NR 开始处理。"));
    const char *cause = nullptr;
    switch (status.cause) {
    case 1:
        cause = ui("The game's pre-SR image format is not supported for NR.",
            "游戏前置画面的格式暂不支持 NR。");
        break;
    case 2:
        cause = ui("The game's pre-SR colour encoding is not yet verified.",
            "游戏前置画面的颜色编码尚未确认。");
        break;
    case 3:
        cause = ui("The pre-SR 8-bit image has an exposure setup that is not yet verified.",
            "游戏前置画面的曝光组合尚未确认。");
        break;
    case 4:
        cause = ui("The pre-SR HDR image has an exposure setup that is not yet verified.",
            "游戏前置 HDR 画面的曝光组合尚未确认。");
        break;
    case 5:
        cause = ui("The game's pre-SR image cannot be read by the current NR processing path.",
            "当前 NR 处理方式无法读取游戏的前置画面。");
        break;
    default:
        cause = ui("The current pre-SR image did not meet NR compatibility requirements.",
            "当前游戏前置画面尚不满足 NR 兼容条件。");
        break;
    }
    ImGui::TextWrapped(ui("Reason: %s", "原因：%s"), cause);
    SettingsNote(ui(
        "Your pre-SR preference is retained. Please save an F8 report to help improve compatibility.",
        "已保留你选择的前置设置；请按 F8 生成报告，帮助改善兼容性。"));
}
