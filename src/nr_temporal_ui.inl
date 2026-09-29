void DrawTemporalOptions(ChainConfig& next,bool& changed,bool& discrete){
    const bool zh=UseChineseUi();const auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    SettingsSection(ui("Residual stabilization (experimental)","残差防闪（实验）"));
    const char* modes[]={ui("Off","关闭"),ui("Native motion","原生运动"),"AMDOF"};
    for(unsigned i=0;i<3;++i){if(i)ImGui::SameLine();ImGui::PushID(int(i));
        if(ImGui::RadioButton(modes[i],next.temporal_backend==i)){next.temporal_backend=i;changed=discrete=true;}ImGui::PopID();}
    SettingsHelp(ui("Motion aligns earlier NR edits; it does not denoise by itself. AMDOF also supports enhancement after successful game RR. Private SR and stabilization cannot run together.",
        "运动数据用于对齐旧画面，本身不降噪。AMDOF 也支持在成功的游戏 RR 后增强。私有 SR 与防闪不能同时使用。"));
    ImGui::BeginDisabled(next.temporal_backend==0);
    if(ImGui::RadioButton(ui("Whole chain","整链"),next.temporal_scope==0)){next.temporal_scope=0;changed=discrete=true;}
    ImGui::SameLine();if(ImGui::RadioButton(ui("Later layers","后续层"),next.temporal_scope==1)){next.temporal_scope=1;changed=discrete=true;}
    ImGui::EndDisabled();
    ImGui::BeginDisabled(next.temporal_backend==0);
    if(ImGui::RadioButton(ui("Normal accumulation","普通累积"),next.temporal_mode==0)){next.temporal_mode=0;changed=discrete=true;}
    ImGui::SameLine();
    if(ImGui::RadioButton(ui("Low-frequency reconstruction###Low-frequency reconstruction","低频时域###低频时域重建"),next.temporal_mode==1)){next.temporal_mode=1;changed=discrete=true;}
    ImGui::EndDisabled();
    SettingsHelp(ui("Low-frequency mode stabilizes broad edits while retaining current fine detail. Native motion needs a verified post-SR picture; game RR and Present use AMDOF. These options do not guarantee ghost removal.",
        "低频模式稳定大范围改动，保留当前帧细节。原生运动需要已确认的游戏 SR 后画面；游戏 RR 后和 Present 使用 AMDOF。不保证消除拖影。"));
    if(next.temporal_scope&&next.nr_passes<2)SettingsNote(ui("Not applied: later layers need at least two layers.","未生效：后续层需要至少两层。"));
    if(next.temporal_backend){
        const auto state=nr_temporal_status::state.load();const auto tick=nr_temporal_status::last_tick.load();
        if(next.private_residual_sr)SettingsNote(ui("Not applied: turn off private SR first.","未生效：请先关闭私有 SR。"));
        else if(state==8&&GetTickCount64()-tick<2000)SettingsNote(ui("Processing submitted; image adoption unverified.","已提交处理；画面采用待确认。"));
        else if(state==9)SettingsNote(ui("Not applied: recording fault. Save F8 and restart.","未生效：处理异常。保存 F8 后重启。"));
        else if(state==6||state==3)SettingsNote(ui("Preparing: waiting for resources.","准备中：等待资源。"));
        else if(state==5||state==16||state==19)SettingsNote(ui("Not applied: motion data unverified.","未生效：运动数据未确认。"));
        else if(state==15)SettingsNote(ui("Not applied: no motion data.","未生效：缺少运动数据。"));
        else if(state==17||state==20)SettingsNote(ui("Not applied: picture copy unavailable.","未生效：画面快照不可用。"));
        else if(state==18)SettingsNote(ui("Not applied: select Whole chain for this route.","未生效：此路线请选择整链。"));
        else if(state==10)SettingsNote(ui("Not applied: picture source or exposure unverified.","未生效：画面来源或曝光未确认。"));
        else if(state==12)SettingsNote(ui("Not applied: picture format unavailable.","未生效：画面格式不适用。"));
        else if(state==13)SettingsNote(ui("Preparing: waiting for a new game frame.","准备中：等待新的游戏画面。"));
        else SettingsNote(ui("Preparing: waiting for a supported route.","准备中：等待可用路线。"));
        SettingsHelp(ui("A submitted frame is not proof of history adoption. F8 has the exact source, queue and reason.",
            "已提交不代表采用历史；F8 可查看来源、队列和具体原因。"));
        if(ImGui::CollapsingHeader(ui("History adoption details###temporal-adoption","历史采用详情###temporal-adoption"))) {
            const auto gpu_config=nr_temporal_status::gpu_config.load();
            const auto gpu_instance=nr_temporal_status::gpu_instance.load();
            if(gpu_config==g_config_generation.load()&&gpu_instance&&nr_temporal_status::gpu_frames.load())
                ImGui::Text(ui("GPU history samples (owner %llu): tested %llu, adopted %llu, rejected %llu",
                    "GPU 历史抽样（实例 %llu）：检测 %llu、采用 %llu、拒绝 %llu"),gpu_instance,
                    nr_temporal_status::gpu_tested.load(),nr_temporal_status::gpu_adopted.load(),nr_temporal_status::gpu_rejected.load());
            else SettingsNote(ui("No matching GPU adoption samples yet.","暂无当前设置的 GPU 采用抽样。"));
            SettingsHelp(ui("One decision per 8x8 pixels after GPU completion; this is not a final image quality verdict.",
                "GPU 完成后每 8×8 像素抽样一次；不代表最终画质。"));
        }
    }
}
