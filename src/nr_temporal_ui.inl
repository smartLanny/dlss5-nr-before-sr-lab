#include "nr_temporal_ui_status.h"
void DrawTemporalOptions(ChainConfig& next,bool& changed,bool& discrete,bool has_pending=false){
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
        const bool pending=has_pending||changed;
        if(pending&&state!=9)SettingsNote(ui("Settings pending; previous observations are hidden.",
            "设置待应用；暂不显示此前状态。"));
        else {
            const unsigned reason=state==9?9u:next.private_residual_sr?21u:next.temporal_scope&&next.nr_passes<2?11u:state;
            const auto message=nr_temporal_ui::Describe(reason,nr_temporal_ui::Recent(GetTickCount64(),tick));
            SettingsNote(ui(message.en,message.zh));
            if(message.request_feedback)SettingsHelp(ui(
                "If this persists, press F8 and submit the report with reproduction steps. Keep safeguards enabled; bypass FPS is not an improvement.",
                "若持续如此，请按 F8 提交反馈和复现步骤。请保留安全保护；旁路后的高帧数不是优化收益。"));
        }
        SettingsHelp(ui("This is the latest observation, not proof for every frame or configuration. A submitted frame is not proof of history adoption; F8 records the source, queue and reason.",
            "这里是最近观察，不证明每帧或当前配置均已生效。提交不等于 GPU 历史采用；F8 记录来源、队列和原因。"));
        if(ImGui::CollapsingHeader(ui("History adoption details###temporal-adoption","历史采用详情###temporal-adoption"))) {
            const auto gpu_config=nr_temporal_status::gpu_config.load();
            const auto gpu_instance=nr_temporal_status::gpu_instance.load();
            if(!pending&&!next.private_residual_sr&&gpu_config==g_config_generation.load()&&gpu_instance&&nr_temporal_status::gpu_frames.load())
                ImGui::Text(ui("GPU history samples (owner %llu): tested %llu, adopted %llu, rejected %llu",
                    "GPU 历史抽样（实例 %llu）：检测 %llu、采用 %llu、拒绝 %llu"),gpu_instance,
                    nr_temporal_status::gpu_tested.load(),nr_temporal_status::gpu_adopted.load(),nr_temporal_status::gpu_rejected.load());
            else SettingsNote(ui("No matching GPU adoption samples yet.","暂无当前设置的 GPU 采用抽样。"));
            SettingsHelp(ui("One decision per 8x8 pixels after GPU completion; this is not a final image quality verdict.",
                "GPU 完成后每 8×8 像素抽样一次；不代表最终画质。"));
        }
    }
}
