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

template<class Config>
void DrawMoreLayerControls(Config& next,bool& changed,bool& discrete,bool zh) {
    if constexpr(requires {next.nr_full_reference;next.nr_full_passes;}) {
        if(ImGui::Checkbox(zh?"启用更多层":"Enable more layers",&next.nr_full_reference)) {
            changed=true;discrete=true;
        }
        if(!next.nr_full_reference)return;

        SettingsField(zh?"总层数":"Total layers");
        const char* labels[]={
            zh?"3层###full-passes-3":"3 layers###full-passes-3",
            zh?"4层###full-passes-4":"4 layers###full-passes-4",
            zh?"5层###full-passes-5":"5 layers###full-passes-5"};
        const float width=std::max(1.f,(SettingsContentWidth()-
            2.f*ImGui::GetStyle().ItemSpacing.x)/3.f);
        const bool stacked=width<ImGui::GetFontSize()*6.f;
        ImGui::PushID("nr-full-passes");
        for(unsigned i=0;i<3;++i) {
            if(i&&!stacked)ImGui::SameLine();
            ImGui::PushID(static_cast<int>(i));
            const unsigned passes=i+3;
            const bool selected=next.nr_full_passes==passes;
            if(selected)ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::GetStyleColorVec4(ImGuiCol_Header));
            const bool clicked=ImGui::Button(labels[i],ImVec2(
                stacked?SettingsContentWidth():width,ImGui::GetFrameHeight()*1.25f));
            if(selected)ImGui::PopStyleColor();
            if(clicked&&!selected) {
                next.nr_full_passes=passes;changed=true;discrete=true;
            }
            SettingsTooltip(zh?"选择启用更多层时的总层数。":"Choose the total number of layers when more layers are enabled.");
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

// Keep the frequently used controls next to the first NR scale. Detailed
// observations belong on the feedback page so the home page stays readable.
void DrawQuickUi(ChainConfig& next,bool& changed,bool& discrete,
                 bool has_pending=false) {
    const bool zh=UseChineseUi();
    ImGui::SeparatorText(zh?"二次 AI 增强（实验）":"Second AI (experimental)");
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
    SettingsTooltip(zh?"第2层及后续层使用相同比例；100%时与主层同尺寸。":"Layer 2 and later layers use the same scale; at 100%, they match the primary layer size.");
    DrawMoreLayerControls(next,changed,discrete,zh);
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
