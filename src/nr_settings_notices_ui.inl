// Render snapshots supplied by the configuration and route owners. This file
// never changes saved preferences or reconstructs a route from loose atomics.
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
