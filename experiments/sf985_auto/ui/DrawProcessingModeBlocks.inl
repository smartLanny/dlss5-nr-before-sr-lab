// Drop-in replacement for D14's existing selector, NOT a Present NR backend.
// Keep the existing four-argument call and the two legacy Mode values.
// The third tile is intentionally unavailable until a real Present consumer,
// a separate persisted request and current-frame capability checks are wired.
// Do not enable it merely because a ReShade Present callback is observed.
bool DrawProcessingModeBlocks(ChainConfig &next, bool rr_guarded,
    bool compatible_post_locked, int effective_mode) {
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    const char *full_labels[] = {
        ui("Enhance first###route-option", "先增强，后放大###route-option"),
        ui("Upscale first###route-option", "先放大，后增强###route-option"),
        ui("Before output (not connected)###route-option", "输出前增强（未接通）###route-option")};
    const char *compact_labels[] = {
        ui("Enhance first\nNR -> SR###route-option", "先增强\n后放大###route-option"),
        ui("Upscale first\nSR -> NR###route-option", "先放大\n后增强###route-option"),
        ui("Before output\nNot connected###route-option", "输出前增强\n未接通###route-option")};
    const float available = std::max(1.0f, SettingsContentWidth());
    const auto &style = ImGui::GetStyle();
    const float gap = std::max(0.0f, style.ItemSpacing.x);
    const float pad_x = std::max(0.0f, style.FramePadding.x);
    const float pad_y = std::max(0.0f, style.FramePadding.y);
    // CalcTextSize crosses the same MSVC/MinGW ImVec2-return boundary as
    // GetContentRegionAvail. Never call the foreign aggregate return directly.
    const auto measure = [](const char *label) {
        return nr_imgui_abi::CallHostVec2(
            imgui_function_table_instance()->CalcTextSize,
            label, static_cast<const char *>(nullptr), true, -1.0f);
    };
    float full_width = 0.0f, compact_width = 0.0f;
    for (int i = 0; i < 3; ++i) {
        full_width = std::max(full_width, measure(full_labels[i]).x + 2.0f * pad_x);
        compact_width = std::max(compact_width, measure(compact_labels[i]).x + 2.0f * pad_x);
    }
    const bool full = available >= 3.0f * full_width + 2.0f * gap;
    const char *const *labels = full ? full_labels : compact_labels;
    const bool horizontal = full || available >= 3.0f * compact_width + 2.0f * gap;
    const char *terse_labels[] = {"NR -> SR###route-option", "SR -> NR###route-option",
        ui("Present\nNot ready###route-option", "输出前\n未接通###route-option")};
    if (!horizontal && compact_width > available) labels = terse_labels;
    const float width = horizontal ? (available - 2.0f * gap) / 3.0f : available;
    float height = ImGui::GetFrameHeight() * 1.25f;
    for (int i = 0; i < 3; ++i)
        height = std::max(height, measure(labels[i]).y + 2.0f * pad_y);

    bool changed = false;
    const bool mode_guarded = rr_guarded || compatible_post_locked;
    const int shown_mode = mode_guarded ? 1 : effective_mode;
    ImGui::PushID("nr-processing-mode");
    for (int option = 0; option < 3; ++option) {
        if (horizontal && option != 0) ImGui::SameLine();
        ImGui::PushID(option); // Existing hidden IDs for 0/1 stay unchanged.
        const bool present = option == 2;
        const bool disabled = present || mode_guarded;
        const int requested_mode = option == 0 ? 2 : 1;
        const bool selected = !present && shown_mode == requested_mode;
        if (disabled) ImGui::BeginDisabled();
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button,
            ImGui::GetStyleColorVec4(ImGuiCol_Header));
        const bool clicked = ImGui::Button(labels[option], ImVec2(width, height));
        if (selected) ImGui::PopStyleColor();
        // Defense in depth: even a synthetic click on a disabled tile cannot
        // overwrite Mode, enable NR or masquerade as post-SR / Mode=3.
        if (clicked && !disabled && next.mode != requested_mode) {
            next.mode = requested_mode;
            changed = true;
        }
        if (present) SettingsTooltip(ui(
            "Not connected in this build. Present NR would enhance this game's final image before display; it is not desktop-wide processing and does not enable or disable game SR. It may include HUD. A real input/NR/submit/writeback path and FG ownership checks are still required. Compatibility and cost depend on the game and NR work size; neither is an absolute ranking.",
            "本构建尚未接通。Present 会在当前游戏输出前增强整张画面，不是处理整个系统桌面，也不负责开关游戏超分；可能包含字幕和界面。仍需接通输入、NR、提交、回填及帧生成归属。兼容性和开销取决于游戏与 NR 工作尺寸，不代表兼容最好或性能最差。"));
        else if (option == 0) SettingsTooltip(ui(
            "Render / before reconstruction: NR enhances an already rendered scene input, then the game performs its existing SR. This is not before the scene is rendered. RR or a verified compatibility guard can select post-SR while retaining your saved preference. No additional Render selector is needed.",
            "对应 Render／重建前：先增强已经渲染出的场景输入，再由游戏执行原有 SR；不是场景还没渲染就运行 AI。RR 或已确认的兼容保护可改走后置，并保留你的选择，不必再添加一个 Render 开关。"));
        else SettingsTooltip(ui(
            "Upscaled / after reconstruction: the game performs its existing SR or RR first, then NR enhances that result. A 1:1 reconstruction can also use this stage. This is not the later Present stage. RR uses this effective post route; changing the selector does not control game SR.",
            "对应 Upscaled／重建后：游戏先完成原有 SR 或 RR，再增强重建结果；1:1 重建也可以有这个阶段。它不同于更晚的 Present 输出点，选择此项不会开关游戏超分。"));
        if (disabled) ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}
