// Output-only tests. These controls never change models, colour amount, input
// curves, working resolution, layer counts or another saved experiment.
void DrawReconstructionControls(ChainConfig& next, unsigned generation,
    bool pending, bool& changed, bool& discrete) {
    SettingsSection(NrUi("Low-resolution clarity / colour stability", "低分清晰度／保色稳定对照"));
    int mode=nr_reconstruction::Normalize(next.reconstruction_mode);
    if(NrCombo(NrUi("Reconstruct NR edits", "NR 修改量重建"),"##reconstruction-mode",mode,
        NrUi("Existing sampling\0Source-guided 4 taps\0Source-guided 16 taps\0",
             "原有取样\0原图引导 4 点\0原图引导 16 点\0"))) {
        next.reconstruction_mode=mode;changed=true;discrete=true;
    }
    if(ImGui::Checkbox(NrUi("Stabilize amplified near-black colour", "稳定近黑结果被放大的色彩"),&next.near_black_chroma_guard)) {
        changed=true;discrete=true;
    }
    SettingsTooltip(NrUi("Only dark AND strongly amplified candidates reduce unreliable chroma; keeps the selected new luminance. Not a global saturation reduction or black-spot cure. No extra NR call or history filter.",
        "仅在候选很暗且需大幅恢复时降低不可信色度，保留已选定的新亮度。不是全局降饱和，也不保证消除纯亮度黑点；不增加 NR 调用或历史滤波。"));
    const auto choose=[&](int r,bool g){if(next.reconstruction_mode!=r||next.near_black_chroma_guard!=g){next.reconstruction_mode=r;next.near_black_chroma_guard=g;changed=true;discrete=true;}};
    if(ImGui::Button(NrUi("Baseline##reconstruction-off", "原恢复##reconstruction-off")))choose(0,false);
    ImGui::SameLine();if(ImGui::Button("A##reconstruction-a"))choose(2,false);
    ImGui::SameLine();if(ImGui::Button("B##reconstruction-b"))choose(0,true);
    ImGui::SameLine();if(ImGui::Button("A+B##reconstruction-ab"))choose(2,true);
    SettingsNote(NrUi("A: guided 16-tap edits. B: chroma reliability. Buttons only change these two controls; keep AI colour, lighting, model and scale identical. GPU cost and game quality are not yet certified.",
        "A：16 点引导修改；B：色度可信度。按钮只改这两项，AI 色彩、光影、模型与比例保持不变。实际画质及 GPU 开销仍需对照。"));
    const auto sample=SnapshotColorPolicy();
    if(!pending&&!changed&&sample.config_generation==generation&&sample.status==NrColorPolicyStatus::Current) {
        ImGui::TextWrapped(NrUi("Last recorded: %s / chroma %s", "最近录制：%s／保色 %s"),
            nr_reconstruction::ModeName(sample.policy.recorded_reconstruction_mode),
            sample.policy.recorded_near_black_guard?"on":"off");
        if(next.reconstruction_mode&&!sample.policy.recorded_reconstruction_mode)
            ImGui::TextWrapped(NrUi("Reconstruction inactive: %s", "重建未采用：%s"),
                nr_reconstruction::ReasonName(sample.policy.reconstruction_reason));
        if(next.near_black_chroma_guard&&!sample.policy.recorded_near_black_guard)
            ImGui::TextWrapped(NrUi("Chroma protection inactive: %s", "保色未采用：%s"),
                nr_reconstruction::ReasonName(sample.policy.near_black_reason));
    } else SettingsNote(NrUi("Waiting for a matching recorded frame; a checked option is not proof of use.", "等待当前配置的实际录制；勾选不等于已经生效。"));
    SettingsNote(NrUi("Guided recovery: native pre-SR and scaled SR/RR-post with verified paired encoding; equal sizes keep original sampling. Legacy edge/low-grid fields and raw diagnostics are independent. Persistent bypass, missing effect or corruption: save F8 and report the bug.",
        "引导恢复用于已确认配对编码的原生前置及缩放 SR/RR 后置；同尺寸保留原取样。旧边缘／低分场与原始诊断单独对照。持续旁路、无效果或异常请保存 F8 反馈 BUG。"));
}
