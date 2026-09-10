namespace nr_dual_runtime {
const char *StatusText(Status value,bool zh) {
    static const char *cn[]={"关闭","第一层暖机中","第二层暖机中","等待第二层创建完成","已录制双次 NR","导引暂不支持，当前使用单次 NR","资源或预算不足，当前使用单次 NR","等待有序队列","等待可安全复用的资源","第二层调用失败；需重启游戏","设备归属未确认，当前使用单次 NR","第二层管线暂不可用，当前使用单次 NR","第二层资源格式或尺寸不匹配，当前使用单次 NR"};
    static const char *en[]={"Off","Primary warming","Secondary warming","Waiting for Create completion","Two NR passes recorded","Guides unavailable; single NR retained","Resources unavailable; single NR retained","Waiting for ordered queue","Waiting for reusable resources","Secondary failure; restart required","Device identity unproven; single NR retained","Second-pass pipeline unavailable; single NR retained","Second-pass format or extent unsupported; single NR retained"};
    unsigned index=static_cast<unsigned>(value);
    if(index>12) index=12;
    return zh?cn[index]:en[index];
}

const char *UiStateText(nr_dual_status_runtime::UiState value, bool zh) {
    using UiState = nr_dual_status_runtime::UiState;
    switch (value) {
    case UiState::Off:
        return zh ? "二次 AI：未启用" : "Second AI: off";
    case UiState::WaitingForFrame:
        return zh ? "二次 AI：等待游戏画面" : "Second AI: waiting for game frames";
    case UiState::Preparing:
        return zh ? "二次 AI：准备中" : "Second AI: preparing";
    case UiState::Active:
        return zh ? "二次 AI：生效" : "Second AI: active";
    case UiState::Intermittent:
        return zh ? "二次 AI：间歇生效" : "Second AI: intermittent adoption";
    case UiState::NotYetApplied:
        return zh ? "二次 AI：暂未生效" : "Second AI: not yet applied";
    case UiState::Failed:
        return zh ? "二次 AI：失败，需要重启游戏" : "Second AI: failed; restart the game";
    }
    return zh ? "二次 AI：状态未知" : "Second AI: unknown state";
}

// Keep the frequently used controls next to the first NR scale. Detailed
// observations belong on the feedback page so the home page stays readable.
void DrawQuickUi(ChainConfig& next,bool& changed,bool& discrete,
                 bool has_pending=false) {
    const bool zh=UseChineseUi();
    ImGui::SeparatorText(zh?"二次 AI 增强（实验）":"Second AI enhancement (experimental)");
    bool enabled=next.nr_passes==2;
    if(ImGui::Checkbox(zh?"启用二次 AI 增强":"Enable second AI enhancement",&enabled)){
        next.nr_passes=enabled?2:1;changed=true;discrete=true;
    }
    if(!enabled)return;
    float percent=100.f*next.nr_second_numerator/next.nr_second_denominator;
    if(SettingsSliderFloat(zh?"第二次 AI 处理比例":"Second AI processing scale","##dual-scale",
        &percent,25.f,100.f,"%.0f%%")){
        next.nr_second_numerator=static_cast<unsigned>(std::lround(percent));
        next.nr_second_denominator=100;changed=true;
    }
    const unsigned n[]={1,3,1},d[]={2,4,1};
    const char *labels[]={"50%###dual-scale-50","75%###dual-scale-75","100%###dual-scale-100"};
    for(unsigned i=0;i<3;++i){
        if(i)ImGui::SameLine();
        if(ImGui::Button(labels[i])){
            next.nr_second_numerator=n[i];next.nr_second_denominator=d[i];
            changed=true;discrete=true;
        }
    }
    SettingsTooltip(zh
        ?"第二次 AI 会重新处理第一层产生的整幅变化；比例越低，第二次按更小画面处理，再恢复对应改动。100% 是同尺寸参考。最终仍只执行原有的一次游戏 SR，不会按负载自动调档。"
        :"The second AI pass reprocesses the full edit from the first pass. Lower scales process a smaller view, then restore the matching edit; 100% is the same-size reference. The game still performs its existing single SR, with no load-based auto scaling.");
    if(ImGui::CollapsingHeader(zh?"更多第二次比例":"More second-pass scales")){
        if(ImGui::Button("2/3###dual-scale-two-thirds")){
            // Preserve the historical exact fraction in the INI. Do not
            // replace it with the rounded 67/100 slider representation.
            next.nr_second_numerator=2;next.nr_second_denominator=3;
            changed=true;discrete=true;
        }
        SettingsTooltip(zh?"保留原有精确 2/3 请求与保存值；滑块或常用按钮仍可改成百分比。":"Keeps the historical exact 2/3 request and saved fraction; the slider and common buttons remain percentage based.");
    }
    const auto snapshot = nr_dual_status_runtime::SnapshotUi();
    if (snapshot.state != nr_dual_status_runtime::UiState::Failed && (has_pending || changed)) {
        ImGui::TextDisabled("%s", zh ? "二次 AI：等待请求的设置生效" :
            "Second AI: waiting for the requested settings to apply");
    } else {
        ImGui::TextDisabled("%s", UiStateText(snapshot.state, zh));
        if (snapshot.state == nr_dual_status_runtime::UiState::Failed)
            SettingsTooltip(zh ? "请先在反馈与诊断中保存 F8，再重启游戏。失败的处理链不会继续尝试。" :
                "Save F8 in Feedback and diagnostics, then restart the game. The failed chain will not retry.");
    }
}

void DrawDetailsUi(bool requested=true, bool has_pending=false) {
    const bool zh=UseChineseUi();
    ImGui::SeparatorText(zh?"二次 NR 详细诊断":"Dual NR details");
    if(!requested)return;
    const auto snapshot = nr_dual_status_runtime::SnapshotUi();
    if (has_pending && snapshot.state != nr_dual_status_runtime::UiState::Failed) {
        ImGui::TextDisabled("%s", zh ? "设置待应用：暂不显示上一配置的二次 NR 诊断。" :
            "Settings pending: the previous dual NR observation is hidden until the request applies.");
        return;
    }
    const Status value=status.load(std::memory_order_acquire);
    if(snapshot.counts.eligible_primary || snapshot.fatal_code)
        ImGui::TextWrapped(zh?"最近处理记录：%s":"Last processing record: %s",StatusText(value,zh));
    ImGui::TextWrapped("%s", UiStateText(snapshot.state, zh));
    ImGui::TextWrapped(zh?"当前统计窗口（最多 500 ms）：符合条件 %llu；第二层评估成功 %llu；暖机丢弃 %llu；最终采用 %llu。":"Current window (up to 500 ms): eligible %llu; secondary Evaluate success %llu; warm-up discarded %llu; final adopted %llu.",
        static_cast<unsigned long long>(snapshot.counts.eligible_primary),
        static_cast<unsigned long long>(snapshot.counts.secondary_evaluate_success),
        static_cast<unsigned long long>(snapshot.counts.warmup_discarded),
        static_cast<unsigned long long>(snapshot.counts.final_adopted));
    ImGui::TextWrapped(zh?"最近观察尺寸：%u × %u → %u × %u。":"Last observed sizes: %u x %u -> %u x %u.",
        primary_width.load(),primary_height.load(),second_width.load(),second_height.load());
    ImGui::TextWrapped(zh?"辅助纹理：%.1f MiB（全池上限 1024 MiB；不含模型内部显存）。":"Auxiliary textures: %.1f MiB (pool limit 1024 MiB; excludes model internals).",
        double(g_dual_allocated_bytes.load())/(1024*1024));
    const unsigned count=time_samples.load(std::memory_order_acquire);
    if(count&&time_config.load(std::memory_order_acquire)==g_config_generation.load()) {
        ImGui::TextWrapped(zh?"GPU 稳定样本 %u：链路中位 %.2f ms / P95 %.2f ms；第二层 %.2f ms。":"Stable GPU samples %u: chain median %.2f ms / P95 %.2f ms; NR2 %.2f ms.",
            count,time_median[5].load(),time_p95[5].load(),time_median[2].load());
        SettingsNote(zh?"这里只计 NR 与 SR 链路，不代表游戏整帧耗时；切换后的暖机不纳入稳定窗口。":"This measures the NR/SR chain, not the whole game frame; switch warmup is excluded.");
    }
    if(ImGui::Button(zh?"采集双次 NR 对照（F7）":"Capture dual NR comparison (F7)"))RequestStageCapture();
    SettingsTooltip(zh
        ?"F7 在双次 NR 生效时保存同一帧的 P1、L1、L2、第二层导引和最终合成；它是观察帧，不是完整历史重放。"
        :"When both passes are active, F7 saves P1, L1, L2, bound NR2 guides and final composition from one observed frame; it is not a complete history replay.");
    ImGui::TextWrapped(zh?"双次采集状态：%u（0未开始 / 1等待 / 2已导出 / 3失败）":"Dual capture status: %u (0 idle / 1 pending / 2 exported / 3 failed)",nr_dual_capture::status.load());
    if(value==Status::UnsupportedGuides||value==Status::ResourceBudget||value==Status::Failed||
       value==Status::DeviceIdentity||value==Status::PipelineUnavailable||value==Status::ResourceContract)
        SettingsNote(zh?"请用 F8 保存反馈；画质和性能需在游戏中对比。":"Save F8 feedback; compare quality and performance in the game.");
}

// Compatibility wrapper for small hosts and older test fixtures.
void DrawUi(ChainConfig& next,bool& changed,bool& discrete) {
    DrawQuickUi(next,changed,discrete);
    DrawDetailsUi(next.nr_passes==2);
}
}
