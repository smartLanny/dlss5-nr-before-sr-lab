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
    SettingsNote(ui("Current route: native DX12 SR, then NR, SDR. Native motion needs a proven prepared guide; AMD uses the unenhanced picture. Does not replace model guides. Other routes retain the current NR candidate.",
        "当前支持：DX12 原生 SR 后增强、SDR。原生运动需已确认的导引；AMDOF 读取未增强画面，不替换模型导引。其他路线保留当前 NR 结果。"));
    SettingsNote(ui("Additional bounded SDR routes: AMDOF before native SR on a full FP16 input (whole chain), or after successful native RR. HDR, pre-SR later-layer scope and pre-SR/RR native motion remain unavailable; the current NR result is retained.",
        "新增有界 SDR 路线：完整 FP16 输入的原生 SR 前整链 AMDOF，或成功原生 RR 后 AMDOF。HDR、前置后续层范围、前置及 RR 原生运动暂不支持，保留当前 NR 结果。"));
    if(next.temporal_scope&&next.nr_passes<2)SettingsNote(ui("Later-layer scope is unavailable with one layer; the selection is retained.","仅一层时后续层范围不可用，保留所选范围。"));
    if(next.temporal_backend){
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
        else if(state==10)SettingsNote(ui("The source color domain or native route is unsupported or unverified. HDR stabilization is unavailable; the current NR result and your selection are retained.","来源颜色域或原生路线不支持／尚未确认。HDR 防闪暂不可用，保留当前 NR 结果和所选设置。"));
        else if(state==12)SettingsNote(ui("Current candidate format or active snapshot does not match the temporal consumer; NR result retained.","当前候选格式或有效区域快照不满足防闪输入约定，保留 NR 结果。"));
        else if(state==13)SettingsNote(ui("Repeated source frame; no additional flow or history work.","重复源帧，不追加光流或历史工作。"));
        else SettingsNote(ui("Waiting for the supported route and source/queue evidence; selection alone does not mean stabilization is running.","等待支持的路线及来源／队列证据；选择功能不代表已经执行防闪。"));
    }
}
