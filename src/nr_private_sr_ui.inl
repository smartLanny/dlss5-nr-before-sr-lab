void DrawPrivateResidualSr(ChainConfig& next,bool& changed,bool& discrete) {
    const bool zh=UseChineseUi();const auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    SettingsSection(ui("Private residual SR (experimental)","私有残差 SR（实验）"));
    if(ImGui::Checkbox(ui("Enable private residual SR","启用私有残差 SR"),&next.private_residual_sr))changed=discrete=true;
    SettingsNote(ui("Reconstructs the final edit from all NR layers once. Initial support: post-SR FP16 SDR, proven guides with zero native jitter, no FG. Unsupported inputs retain the current NR result.",
        "对全部 NR 层的最终修改量重建一次。首批支持后置 SR、FP16 SDR、原始抖动为零且来源可确认的导引，不支持 FG。其余输入保留当前 NR 结果。"));
    if(next.private_residual_sr&&next.temporal_backend)SettingsNote(ui("Disable residual stabilization to use private SR. Your saved stabilization settings are retained.",
        "使用私有 SR 前请关闭残差防闪；已保存的防闪设置会保留。"));
    if(next.private_residual_sr) {
        const auto state=nr_private_sr_status::state.load();
        if(state==nr_private_sr_status::State::Quarantined)SettingsNote(ui("Resources quarantined after an uncertain failure. Save F8, disable this experiment and restart.","异常后资源已隔离。请保存 F8，关闭此实验后重启。"));
        else if(state==nr_private_sr_status::State::Phase)SettingsNote(ui("The native guide sampling phase is unproven. Private SR was not adopted; save F8 for diagnosis.","尚不能确认原生导引采样相位，未采用私有 SR；请保存 F8 供排查。"));
        else SettingsNote(ui("Selection, recording and submission are separate. F8 reports the current reason and adoption counts; these do not certify image quality.","勾选、录制与提交分别统计。F8 提供当前原因和采用次数，这些计数不代表画质验收通过。"));
    }
}
