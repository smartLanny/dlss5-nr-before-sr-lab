void DrawSpatialExperiment(ChainConfig& next,bool& changed,bool& discrete) {
    SettingsSection(NrUi("Denoising comparison (test)","降噪对照（实验）"));
    SettingsField(NrUi("Method","方式"));
    unsigned mode=nr_spatial_experiment::Normalize(next.spatial_experiment_mode);
    const char* labels[]={NrUi("Off","关闭"),NrUi("Guided recovery","引导恢复"),NrUi("Local denoising","局部抑噪")};
    const char* tips[]={
        NrUi("No extra spatial processing.","不增加空间处理。"),
        NrUi("Experimental: area-average the NR edit at the lower work resolution, then recover it with four game-image-guided taps. May weaken fine lines.",
             "实验：将 NR 修改压到较低工作分辨率，再由游戏画面引导四点恢复；可能减弱细线。"),
        NrUi("Experimental: average each frame's NR edit over a guided 3×3 neighbourhood. Can reduce isolated speckles but may soften fine lines.",
             "实验：按原画面引导，对本帧 NR 修改作 3×3 局部均值；可压低孤立彩点，也可能减弱细线。")};
    if(NrChoice("spatial-experiment",mode,labels,3,tips)){
        next.spatial_experiment_mode=mode;changed=discrete=true;
    }
    if(!mode)return;
    if(next.private_residual_sr||next.temporal_backend){
        SettingsNote(NrUi("Not applied: turn off private SR and temporal stabilization first.",
            "未生效：请先关闭私有 SR 和残差防闪。"));return;
    }
    if(nr_processing_runtime::Effective()!=nr_processing::Start::After){
        SettingsNote(NrUi("Not applied: needs NR after game SR or RR.","未生效：需在游戏 SR 或 RR 后处理。"));return;
    }
    const auto& s=nr_spatial_experiment::status;
    if(changed||s.configuration.load()!=g_config_generation.load()){
        SettingsNote(NrUi("Preparing: waiting for this setting.","准备中：等待当前设置。"));return;
    }
    using R=nr_spatial_experiment::Reason;
    const auto reason=static_cast<R>(s.reason.load());
    const char* note=reason==R::Submitted?NrUi("Submitted; picture use is unverified.","已提交；画面采用待确认。"):
        reason==R::Recorded?NrUi("Recorded; waiting for GPU submission.","已录制；等待提交。"):
        reason==R::Conflict?NrUi("Not applied: conflicts with another recovery mode.","未生效：与其他恢复方式冲突。"):
        reason==R::NeedLowerScale?NrUi("Not applied: guided recovery needs lower NR work scale; try 50%.","未生效：引导恢复需降低 NR 工作比例，可先试 50%。"):
        reason==R::UnsupportedFormat?NrUi("Not applied: this output format is unsupported.","未生效：当前输出格式不支持。"):
        reason==R::UnsupportedExtent?NrUi("Not applied: picture size is unsupported.","未生效：当前画面尺寸不支持。"):
        reason==R::BudgetCap?NrUi("Not applied: scratch exceeds its memory limit.","未生效：处理所需显存超出上限。"):
        reason==R::BudgetHeadroom?NrUi("Not applied yet: graphics memory is low.","暂未生效：显存余量不足。"):
        reason==R::MemoryUnknown?NrUi("Not applied: memory information unavailable.","未生效：显存信息未确认。"):
        reason==R::Allocation||reason==R::Pipeline?NrUi("Not applied: processing resources unavailable.","未生效：处理资源不可用。"):
        reason==R::Busy?NrUi("Preparing: waiting for the previous frame.","准备中：等待上一帧完成。"):
        reason==R::Discarded?NrUi("Not applied: GPU recording was discarded.","未生效：本次录制已取消。"):
        NrUi("Waiting for supported game SR or RR.","等待可用的游戏 SR 或 RR。");
    SettingsNote(note);
    SettingsHelp(NrUi("Both options are independent same-frame experiments. They do not add motion or change the saved private SR and temporal settings. F8 records the exact state.",
        "两种方式都只看同一帧，不生成运动信息，也不改私有 SR、防闪的保存值。F8 记录具体状态。"));
}
