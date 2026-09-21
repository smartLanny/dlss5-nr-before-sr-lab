// Uses existing DrawSettings -> ApplyConfig transaction; no second writer.
bool DrawEditExperimentSettings(ChainConfig& next) {
    const bool zh=UseChineseUi();bool changed=false;
    SettingsSection(zh?"实验：重建后应用编辑层":"Experimental edit placement");
    int mode=static_cast<int>(next.edit_experiment.mode);
    const char* choices=zh?"关闭，保留原链路\0Deferred B：独立 DLSS 放大修改层\0RR：低分 NR 编辑绕过重建\0":
        "Off: retain existing route\0Deferred B: independent DLSS edit SR\0RR: carry low-resolution NR edit\0";
    SettingsField(zh?"处理方式（测试中）":"Placement (experimental)");
    if(ImGui::Combo("##nr_edit_mode",&mode,choices)){next.edit_experiment.mode=static_cast<unsigned>(mode);changed=true;}
    if(mode){
        SettingsNote(zh?"保存的前置/后置选项不会被改写。复用现有前置精度和多层设置；准入失败时保留原链路。":"Saved placement is unchanged. Reuses pre-SR precision and existing passes; failed preflight retains the legacy route.");
        if(mode==2)changed|=ImGui::Checkbox(zh?"有可靠投影和相位时使用历史##rrhist":"Use history with verified camera/phase##rrhist",&next.edit_experiment.rr_history);
        int budget=next.edit_experiment.budget_mib<=256?0:next.edit_experiment.budget_mib<=512?1:2;
        if(ImGui::Combo(zh?"实验纹理预算##editbudget":"Experiment texture budget##editbudget",&budget,"256 MiB\000512 MiB\0001024 MiB\0")){
            constexpr unsigned values[]={256,512,1024};next.edit_experiment.budget_mib=values[budget];changed=true;}
    }
    if(mode==2){
        const auto history=nr_edit_core::rr_history_status.load();
        const char* status=history==4?(zh?"时域编辑历史已绑定":"Temporal edit history bound"):
            history==2?(zh?"空间编辑层：缺少可靠投影或相位":"Spatial carry: projection/phase unavailable"):
            history==3?(zh?"时域冷启动，当前不读取旧编辑":"Cold history: no reuse this frame"):
                       (zh?"历史关闭或尚无有效样本":"History disabled/no valid sample");
        ImGui::TextWrapped("%s",status);
    }
    ImGui::TextWrapped("%s",nr_edit_core::reason.load(std::memory_order_acquire));
    ImGui::Text(zh?"合成录制 %llu；提交 %llu；完成并释放 %llu":"Compositions %llu; submitted %llu; completed/released %llu",
        static_cast<unsigned long long>(nr_edit_core::composed.load()),static_cast<unsigned long long>(nr_edit_core::submitted.load()),
        static_cast<unsigned long long>(nr_edit_core::completed.load()));
    ImGui::Text("R %ux%u -> O %ux%u; textures %.1f MiB",nr_edit_core::reported_r_width.load(),nr_edit_core::reported_r_height.load(),
        nr_edit_core::reported_o_width.load(),nr_edit_core::reported_o_height.load(),double(nr_edit_core::allocated_bytes.load())/1048576.0);
    SettingsNote(zh?"录制/提交不等于画面验收。先关闭插帧，对比 A、B、完整 C 与同尺寸低分 C。":"Recording is not image validation. First test without FG; compare A, B, full C and same-N reduced C.");
    next.edit_experiment=nr_edit_policy::Sanitize(next.edit_experiment);return changed;
}
