void DrawPrivateResidualSr(ChainConfig& next,bool& changed,bool& discrete) {
    const bool zh=UseChineseUi();const auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    SettingsSection(ui("Private residual SR (experimental)","私有残差 SR（实验）"));
    if(ImGui::Checkbox(ui("Enable private residual SR","启用私有残差 SR"),&next.private_residual_sr))changed=discrete=true;
    SettingsNote(ui("Reconstructs the final edit from all NR layers once. Candidate boundary: D3D12 post-SR or successful post-RR, proven native FP16 colour domain including declared linear HDR, and original non-jittered low-resolution motion with proven native jitter. Unknown guides, jittered motion and nonzero jitter with high-resolution motion are refused. Present, FG and multi-GPU are unsupported; game acceptance remains pending.",
        "对全部 NR 层的最终修改量重建一次。候选边界为 D3D12 原生 SR 或成功 RR 后置、已证明的原生 FP16 颜色域（含声明的线性 HDR），以及原始未抖动低分辨率运动向量与已证明的原生抖动。未知导引、已抖动运动向量及非零抖动搭配高分辨率运动向量会被拒绝。Present、FG、多 GPU 不支持，仍待游戏验收。"));
    if(next.private_residual_sr&&next.temporal_backend)SettingsNote(ui("Disable residual stabilization to use private SR. Your saved stabilization settings are retained.",
        "使用私有 SR 前请关闭残差防闪；已保存的防闪设置会保留。"));
    if(next.private_residual_sr) {
        const auto timing_config=nr_private_sr_status::timing_config.load();
        const auto timing_instance=nr_private_sr_status::timing_instance.load();
        const auto timing_samples=nr_private_sr_status::timing_samples.load();
        if(nr_private_sr_status::gpu_timing_available.load()&&timing_samples&&timing_instance&&timing_config==g_config_generation.load())
            ImGui::Text(ui("Private SR GPU (owner %llu): median %.3f ms, P95 %.3f ms (%llu stable samples)","私有 SR GPU（实例 %llu）：中位 %.3f 毫秒、P95 %.3f 毫秒（%llu 个稳定样本）"),
                timing_instance,nr_private_sr_status::gpu_median_ms.load(),nr_private_sr_status::gpu_p95_ms.load(),timing_samples);
        else SettingsNote(ui("Private SR GPU timing is pending for this configuration.","当前配置的私有 SR GPU 计时尚不可用。"));
        SettingsNote(ui("GPU timing covers private Encode + Feature1 + Compose only; it excludes guides, native SR/RR, NR and whole-frame cost.",
            "GPU 计时仅覆盖私有 Encode、Feature1 和 Compose；不包含导引、原生 SR/RR、NR 与整帧开销。"));
        const auto state=nr_private_sr_status::state.load();
        if(state==nr_private_sr_status::State::Quarantined)SettingsNote(ui("Resources quarantined after an uncertain failure. Save F8, disable this experiment and restart.","异常后资源已隔离。请保存 F8，关闭此实验后重启。"));
        else if(state==nr_private_sr_status::State::OriginalInterface)SettingsNote(ui("The current native graphics interface could not be verified. The current NR result is retained; private SR was not adopted. Save F8 for diagnosis.","尚不能核对当前原生图形接口。已保留当前 NR 结果，未采用私有 SR；请保存 F8 供排查。"));
        else if(state==nr_private_sr_status::State::Phase)SettingsNote(ui("The native guide sampling phase is unproven. Private SR was not adopted; save F8 for diagnosis.","尚不能确认原生导引采样相位，未采用私有 SR；请保存 F8 供排查。"));
        else SettingsNote(ui("Selection, recording and submission are separate. F8 reports the current reason and adoption counts; these do not certify image quality.","勾选、录制与提交分别统计。F8 提供当前原因和采用次数，这些计数不代表画质验收通过。"));
    }
}
