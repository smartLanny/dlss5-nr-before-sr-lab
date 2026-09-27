void DrawTemporalOptions(ChainConfig& next,bool& changed,bool& discrete){
    const bool zh=UseChineseUi();const auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    SettingsSection(ui("Residual stabilization (experimental)","残差防闪（实验）"));
    const char* modes[]={ui("Off","关闭"),ui("Native motion","原生运动"),"AMDOF"};
    for(unsigned i=0;i<3;++i){if(i)ImGui::SameLine();ImGui::PushID(int(i));
        if(ImGui::RadioButton(modes[i],next.temporal_backend==i)){next.temporal_backend=i;changed=discrete=true;}ImGui::PopID();}
    ImGui::BeginDisabled(next.temporal_backend==0);
    if(ImGui::RadioButton(ui("Whole chain","整链"),next.temporal_scope==0)){next.temporal_scope=0;changed=discrete=true;}
    ImGui::SameLine();if(ImGui::RadioButton(ui("Later layers","后续层"),next.temporal_scope==1)){next.temporal_scope=1;changed=discrete=true;}
    ImGui::EndDisabled();
    ImGui::BeginDisabled(next.temporal_backend==0);
    if(ImGui::RadioButton(ui("Normal accumulation","普通累积"),next.temporal_mode==0)){next.temporal_mode=0;changed=discrete=true;}
    ImGui::SameLine();
    if(ImGui::RadioButton(ui("Low-frequency reconstruction","低频时域重建"),next.temporal_mode==1)){next.temporal_mode=1;changed=discrete=true;}
    ImGui::EndDisabled();
    SettingsNote(ui("Motion source (native vectors or AMD optical flow) aligns history; it does not denoise on its own. Low-frequency mode stabilizes the NR edit while retaining current-frame fine detail. Submission does not prove visible history adoption.",
        "运动来源（原生向量或 AMD 光流）用于对齐历史，本身不负责降噪。低频模式稳定 NR 修改量并保留本帧细节；提交不证明画面采用了历史。"));
    SettingsNote(ui("AMDOF supports native SR before/after NR and successful RR followed by NR. SDR and declared linear HDR with valid exposure are supported. Pre-SR uses whole-chain scope. Other inputs retain the current NR result.",
        "AMDOF 支持原生 SR 前后增强及成功 RR 后增强；可处理 SDR 和曝光有效的线性 HDR。前置限整链范围，其余输入保留当前 NR 结果。"));
    SettingsNote(ui("Native motion currently requires a proven post-SR SDR snapshot. HDR uses AMDOF here. Present requires its own proven colour lineage and adoption; an NR submission alone is insufficient. Motion does not replace model guides or remove existing ghosts.",
        "原生运动目前需后置 SR 的 SDR 安全快照；HDR 请选 AMDOF。Present 还需独立证明颜色来源与实际采用，只有 NR 提交并不足够。运动不替换模型导引，也不保证消除已有拖影。"));
    if(next.temporal_scope&&next.nr_passes<2)SettingsNote(ui("Later-layer scope is unavailable with one layer; the selection is retained.","仅一层时后续层范围不可用，保留所选范围。"));
    if(next.temporal_backend){
        const auto gpu_config=nr_temporal_status::gpu_config.load();
        const auto gpu_instance=nr_temporal_status::gpu_instance.load();
        if(gpu_config==g_config_generation.load()&&gpu_instance&&nr_temporal_status::gpu_frames.load())
            ImGui::Text(ui("GPU history samples (owner %llu): tested %llu, adopted %llu, rejected %llu",
                "GPU 历史抽样（实例 %llu）：检测 %llu、采用 %llu、拒绝 %llu"),gpu_instance,
                nr_temporal_status::gpu_tested.load(),nr_temporal_status::gpu_adopted.load(),nr_temporal_status::gpu_rejected.load());
        else SettingsNote(ui("GPU history adoption for the current configuration is not yet available.","当前配置的 GPU 历史采用数据尚不可用。"));
        SettingsNote(ui("GPU counters sample one decision per 8x8 pixels after fence completion; they are not a full-pixel or final-display quality verdict.",
            "GPU 计数在 fence 完成后每 8×8 像素抽样一个决策，不代表全部像素或最终显示画质。"));
        const auto state=nr_temporal_status::state.load();const auto tick=nr_temporal_status::last_tick.load();
        if(state==8&&GetTickCount64()-tick<2000)SettingsNote(ui("Stabilization was submitted for a recent source frame. Check F8 for adoption counts.","最近源帧已提交防闪处理；F8 可查看实际采用次数。"));
        else if(state==9)SettingsNote(ui("Recording fault: resources quarantined. Save F8 and restart after disabling the experiment.","录制异常，资源已隔离。保存 F8，关闭实验后重启。"));
        else if(state==6)SettingsNote(ui("Waiting for a recording/resource slot; current NR result retained.","等待录制／资源槽，保留当前 NR 结果。"));
        else if(state==3)SettingsNote(ui("Creation budget or resource retirement unavailable; current NR result retained.","创建预算或资源退休条件不足，保留当前 NR 结果。"));
        else if(state==5)SettingsNote(ui("Native motion source, units or sampling phase could not be proven. AMDOF is a separate manual choice.","尚不能确认原生运动来源、单位或采样相位。可手动另选 AMDOF。"));
        else if(state==15)SettingsNote(ui("The current source did not provide motion vectors.","当前来源未提供运动向量。"));
        else if(state==16)SettingsNote(ui("Native motion was provided, but its units are invalid or unverified.","已提供原生运动，但单位无效或未确认。"));
        else if(state==17)SettingsNote(ui("Native motion exists, but a safe snapshot was not prepared; NR continues without stabilization.","原生运动存在，但尚未取得安全快照；继续 NR，暂不执行防闪。"));
        else if(state==18)SettingsNote(ui("Pre-SR later-layer anchor is not proven; choose whole chain to use the supported AMDOF route.","前置后续层锚点尚未确认；整链范围才支持此 AMDOF 路线。"));
        else if(state==19)SettingsNote(ui("Native motion phase is not proven on this route. AMDOF is a separate manual choice.","此路线尚不能确认原生运动相位；AMDOF 是独立的手动选项。"));
        else if(state==20)SettingsNote(ui("Snapshot allocation was refused; the current NR result is retained without stabilization. Save and upload F8 feedback.","防闪快照分配被拒绝；保留当前 NR 结果，暂不防闪。请保存并上传 F8 反馈。"));
        else if(state==10)SettingsNote(ui("The source encoding, same-frame exposure or native route is unverified. Keep the current NR result; F8 records the reason.","来源编码、同帧曝光或原生路线尚未确认，保留当前 NR 结果；F8 可查看原因。"));
        else if(state==12)SettingsNote(ui("Current candidate format or active snapshot does not match the temporal consumer; NR result retained.","当前候选格式或有效区域快照不满足防闪输入约定，保留 NR 结果。"));
        else if(state==13)SettingsNote(ui("Repeated source frame; no additional flow or history work.","重复源帧，不追加光流或历史工作。"));
        else SettingsNote(ui("Waiting for the supported route and source/queue evidence; selection alone does not mean stabilization is running.","等待支持的路线及来源／队列证据；选择功能不代表已经执行防闪。"));
    }
}
