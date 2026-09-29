void DrawPrivateResidualSr(ChainConfig& next,bool& changed,bool& discrete) {
    const bool zh=UseChineseUi();const auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    SettingsSection(ui("Private SR (experimental)","私有 SR（实验）"));
    if(ImGui::Checkbox(ui("Enable private residual SR","启用私有残差 SR"),&next.private_residual_sr))changed=discrete=true;
    SettingsTooltip(ui("Rebuilds the final NR edit once after game SR or successful game RR. It needs proven D3D12, colour and motion inputs. Present, FG and multiple GPUs are unsupported. Game image quality remains unverified.",
        "在游戏 SR 或成功的游戏 RR 之后，对最终 NR 改动重建一次。需要可信的图形、颜色和运动数据；Present、FG、多显卡暂不支持。游戏画质仍待验证。"));
    if(next.private_residual_sr&&next.temporal_backend)
        SettingsNote(ui("Not applied: turn off residual stabilization first; its saved choices remain.",
            "未生效：请先关闭残差防闪；原设置会保留。"));
    if(next.private_residual_sr) {
        const auto state=nr_private_sr_status::state.load();
        const bool current_state=!changed&&nr_private_sr_status::state_configuration.load()==g_config_generation.load();
        if(!current_state)SettingsNote(ui("Preparing: checking this configuration.","准备中：等待当前设置状态。"));
        else if(state==nr_private_sr_status::State::Quarantined)
            SettingsNote(ui("Not applied: processing fault. Save F8 and restart with this off.","未生效：处理异常。保存 F8，关闭后重启。"));
        else if(state==nr_private_sr_status::State::OriginalInterface)
            SettingsNote(ui("Not applied: graphics interface unverified.","未生效：图形接口未确认。"));
        else if(state==nr_private_sr_status::State::Phase)
            SettingsNote(ui("Not applied: motion sampling unverified.","未生效：运动采样未确认。"));
        else if(state==nr_private_sr_status::State::Submitted)
            SettingsNote(ui("Processing submitted; image adoption unverified.","已提交处理；画面采用待确认。"));
        else if(state==nr_private_sr_status::State::Route)
            SettingsNote(ui("Not applied: this route or FG is unsupported.","未生效：路线或 FG 暂不支持。"));
        else if(state==nr_private_sr_status::State::Domain)
            SettingsNote(ui("Not applied: picture colour unverified.","未生效：画面颜色未确认。"));
        else if(state==nr_private_sr_status::State::Guides)
            SettingsNote(ui("Not applied: motion guide unavailable.","未生效：运动导引不可用。"));
        else if(state==nr_private_sr_status::State::Queue||state==nr_private_sr_status::State::Source)
            SettingsNote(ui("Not applied: processing source unverified.","未生效：处理来源未确认。"));
        else if(state==nr_private_sr_status::State::BudgetCap)
            SettingsNote(ui("Not applied: this scale exceeds private SR memory limit.","未生效：当前比例超出私有 SR 显存上限。"));
        else if(state==nr_private_sr_status::State::BudgetHeadroom)
            SettingsNote(ui("Not applied yet: available graphics memory is too low.","暂未生效：显存余量不足。"));
        else if(state==nr_private_sr_status::State::BudgetUnknown)
            SettingsNote(ui("Not applied: memory allocation size unverified.","未生效：显存需求未确认。"));
        else if(state==nr_private_sr_status::State::MemoryInfoUnknown)
            SettingsNote(ui("Not applied: graphics memory information unverified.","未生效：显存信息未确认。"));
        else if(state==nr_private_sr_status::State::Budget)
            SettingsNote(ui("Not applied: processing resources unavailable.","未生效：处理资源暂不可用。"));
        else if(state==nr_private_sr_status::State::Busy)
            SettingsNote(ui("Preparing: waiting for resources.","准备中：等待资源。"));
        else SettingsNote(ui("Preparing or awaiting adoption confirmation.","准备中：等待采用确认。"));
        SettingsHelp(ui("A selected or submitted option does not prove final image adoption. F8 has the detailed reason and counts.",
            "勾选或提交不代表最终画面采用；F8 可查看详细原因和计数。"));
        if(ImGui::CollapsingHeader(ui("GPU timing details###private-sr-timing","GPU 计时详情###private-sr-timing"))) {
            const auto timing_config=nr_private_sr_status::timing_config.load();
            const auto timing_instance=nr_private_sr_status::timing_instance.load();
            const auto timing_samples=nr_private_sr_status::timing_samples.load();
            if(nr_private_sr_status::gpu_timing_available.load()&&timing_samples&&timing_instance&&timing_config==g_config_generation.load())
                ImGui::Text(ui("Private SR GPU (owner %llu): median %.3f ms, P95 %.3f ms (%llu stable samples)","私有 SR GPU（实例 %llu）：中位 %.3f 毫秒、P95 %.3f 毫秒（%llu 个稳定样本）"),
                    timing_instance,nr_private_sr_status::gpu_median_ms.load(),nr_private_sr_status::gpu_p95_ms.load(),timing_samples);
            else SettingsNote(ui("No matching GPU timing yet.","暂无当前设置的 GPU 计时。"));
            SettingsHelp(ui("Only private Encode, Feature1 and Compose are timed; this is not the game's full frame cost.",
                "仅计私有编码、重建和合成，不代表游戏整帧开销。"));
        }
    }
}
