// Output-only controls. Labels are user-facing; the stable INI and F8 keys
// remain unchanged. Each action edits only its own setting and never a model.
const char* ReconstructionUiModeName(int mode) {
    return mode==1?NrUi("Balanced","均衡"):
        mode==2?NrUi("Fine","精细"):NrUi("Off","关闭");
}
const char* ReconstructionUiReason(nr_reconstruction::Reason reason) {
    using R=nr_reconstruction::Reason;
    switch(reason) {
    case R::Off:return NrUi("Turned off.","已关闭。");
    case R::Recorded:return NrUi("Included in the latest recorded processing commands.","已加入最近一帧的处理记录。");
    case R::NotCombine:return NrUi("This stage does not combine the final picture.","当前阶段不进行最终画面合成。");
    case R::UnknownEncoding:return NrUi("The picture's colour information is not confirmed; keeping the original processing.","当前画面的颜色信息尚未确认，暂用原有处理。");
    case R::RawDiagnostic:return NrUi("An original-image diagnostic is active; keeping its output unchanged.","正在查看原始画面诊断，暂不改变诊断输出。");
    case R::EqualSize:return NrUi("NR already uses the target resolution; detail reconstruction is unnecessary.","NR 已使用目标分辨率，无需重建细节。");
    case R::LegacySampling:return NrUi("The older edge-recovery experiment is active; keeping it instead of applying both.","旧版边缘恢复实验已开启，暂保留原实验，避免重复处理。");
    case R::OtherEditField:return NrUi("Another low-resolution recovery experiment is active; keeping its processing.","其他低分辨率恢复实验已开启，暂保留其处理。");
    case R::NotMapped:return NrUi("The current composition path does not support detail enhancement; keeping the original processing.","当前合成方式暂不支持细节增强，保留原有处理。");
    case R::ColourInactive:return NrUi("AI colour is not being applied, so dark-noise reduction is inactive.","当前未采用 AI 色彩，去除暗噪暂不起作用。");
    case R::Preview:return NrUi("A diagnostic preview is active; keeping the preview unchanged.","正在查看诊断预览，暂不改变预览输出。");
    }
    return NrUi("Status is not confirmed. Save an F8 report if this persists.","状态尚未确认；持续异常请按 F8 提交反馈。");
}

void DrawReconstructionControls(ChainConfig& next, unsigned generation,
    bool pending, bool& changed, bool& discrete) {
    SettingsSection(NrUi("Clarity and dark noise", "清晰度与暗噪"));
    int mode=nr_reconstruction::Normalize(next.reconstruction_mode);
    bool detail_enabled=mode!=0;
    if(ImGui::Checkbox(NrUi("Low-resolution detail enhancement##reconstruction-enable",
        "低分辨率细节增强##reconstruction-enable"),&detail_enabled)) {
        // Enabling from Off starts with the lower-cost option. Merely drawing
        // this panel does not normalize or rewrite an existing saved selection.
        next.reconstruction_mode=detail_enabled?1:0;
        mode=next.reconstruction_mode;changed=true;discrete=true;
    }
    SettingsTooltip(NrUi(
        "Helps keep hair, clothing and face edges clearer when NR uses a lower resolution. Uses the original picture to place NR changes more accurately, rather than sharpening the whole image. Does not change the NR resolution or layer count. At equal resolutions it keeps the original processing; results and cost depend on the scene.",
        "用于降低 NR 分辨率后保住头发、衣物和人脸轮廓。参考原图把增强细节放回合适的位置，不是给整张画面强行锐化；不会自动改变 NR 分辨率或层数。同分辨率时沿用原处理，效果和开销需要结合场景对照。"));
    if(detail_enabled) {
        const float width=std::max(1.f,(SettingsContentWidth()-ImGui::GetStyle().ItemSpacing.x)*0.5f);
        ImGui::PushID("nr-reconstruction-quality");
        for(int choice=1;choice<=2;++choice) {
            if(choice==2)ImGui::SameLine();
            ImGui::PushID(choice);
            const bool selected=mode==choice;
            if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_Header));
            if(ImGui::Button(ReconstructionUiModeName(choice),ImVec2(width,ImGui::GetFrameHeight()*1.15f))&&!selected) {
                next.reconstruction_mode=choice;mode=choice;changed=true;discrete=true;
            }
            if(selected)ImGui::PopStyleColor();
            SettingsTooltip(choice==1?NrUi(
                "Balanced: samples a smaller neighbourhood to keep added cost lower. Start here when frame rate matters; it does not guarantee higher FPS.",
                "均衡：参考较少的周边像素，额外开销较低。优先帧率时可先试这一档；不代表开启后一定提高帧数。"):
                NrUi("Fine: uses a wider neighbourhood to reconstruct low-resolution changes. Try it for hair, fine lines and face edges; costs more than Balanced and is not guaranteed to look better in every scene.",
                "精细：参考更多周边像素恢复低分辨率细节，可对照头发、细线和人脸边缘。开销高于均衡，并不保证所有场景都更清楚。"));
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    if(ImGui::Checkbox(NrUi("Reduce dark noise##reconstruction-dark-noise",
        "去除暗噪##reconstruction-dark-noise"),&next.near_black_chroma_guard)) {
        changed=true;discrete=true;
    }
    SettingsTooltip(NrUi(
        "Tries to reduce black-grey or coloured speckles on faces, bodies and dark areas at low NR resolutions. It mainly limits unreliable colour amplified when very dark model results regain brightness. Keeps the selected lighting and aims to preserve reliable colours; it is not full-image skin smoothing. Pure black spots already generated by the model and other noise may remain. Independent of detail enhancement; compare on and off in your scene.",
        "尝试减少低分辨率 NR 下人脸、身体和暗部的黑灰／彩色杂点，主要抑制很暗的模型结果恢复亮度时被放大的色彩噪声。保留当前亮度和光影，尽量不影响可靠色彩，不做全图磨皮。模型已产生的纯黑亮度斑点或其他噪声不一定能消除，请开关对照。此项独立于细节增强，可单独使用。"));

    const auto sample=SnapshotColorPolicy();
    if(!pending&&!changed&&sample.config_generation==generation&&sample.status==NrColorPolicyStatus::Current) {
        ImGui::TextWrapped(NrUi("Latest processing record: detail %s / dark noise %s",
            "最近处理记录：细节增强 %s／去除暗噪 %s"),
            ReconstructionUiModeName(sample.policy.recorded_reconstruction_mode),
            sample.policy.recorded_near_black_guard?NrUi("On","开启"):NrUi("Off","关闭"));
        SettingsTooltip(NrUi("This reports recorded commands for matching settings, not GPU completion or a guarantee about the final picture.",
            "这里显示与当前设置匹配的处理命令记录，不代表 GPU 已完成，也不能代替最终画面的效果对照。"));
        if(next.reconstruction_mode&&!sample.policy.recorded_reconstruction_mode)
            ImGui::TextWrapped(NrUi("Detail enhancement: %s","细节增强：%s"),ReconstructionUiReason(sample.policy.reconstruction_reason));
        if(next.near_black_chroma_guard&&!sample.policy.recorded_near_black_guard)
            ImGui::TextWrapped(NrUi("Dark-noise reduction: %s","去除暗噪：%s"),ReconstructionUiReason(sample.policy.near_black_reason));
    } else SettingsNote(NrUi("Waiting for a processing record with these settings; checking an option does not prove it is being used.",
        "等待当前设置的处理记录；勾选不代表画面已采用。"));
    SettingsNote(NrUi("Changes here do not alter AI colour, lighting, models, resolution or layers. If enhancement stays unavailable, flickers or looks wrong, save F8 and report the bug.",
        "这里的开关不会改动 AI 色彩、光影、模型、分辨率或层数。持续无效、闪烁或画面异常，请按 F8 反馈 BUG。"));
}
