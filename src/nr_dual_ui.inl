namespace nr_dual_runtime {
void DrawUi(ChainConfig& next,bool& changed,bool& discrete) {
    const bool zh=UseChineseUi();
    ImGui::SeparatorText(zh?"双次 NR 叠加（实验）":"Dual NR passes (experimental)");
    bool enabled=next.nr_passes==2;
    if(ImGui::Checkbox(zh?"启用第二次 NR":"Enable second NR pass",&enabled)){next.nr_passes=enabled?2:1;changed=true;discrete=true;}
    if(!enabled)return;
    float percent=100.f*next.nr_second_numerator/next.nr_second_denominator;
    if(SettingsSliderFloat(zh?"第二层处理比例":"Second-pass processing scale","##dual-scale",&percent,25.f,100.f,"%.0f%%")){
        next.nr_second_numerator=static_cast<unsigned>(std::lround(percent));next.nr_second_denominator=100;changed=true;
    }
    const unsigned n[]={1,3,2,1},d[]={1,4,3,2};const char* labels[]={"100%","75%","2/3","50%"};
    for(unsigned i=0;i<4;++i){if(i)ImGui::SameLine();if(ImGui::Button(labels[i])){next.nr_second_numerator=n[i];next.nr_second_denominator=d[i];changed=true;discrete=true;}}
    SettingsNote(zh?"100% 为同尺寸双 Pass 参考；低比例让第二次 NR 看完整小图，再恢复其新增改动。独立历史，最终仍只执行原有的一次 SR；不会按负载自动调档。":"100% is the same-size dual-pass reference. Reduced scales process the whole smaller scene, then restore the matching edit. Independent histories, one native SR; no automatic scaling.");
    const auto value=status.load();
    const char* cn[]={"关闭","第一层暖机中","第二层暖机中","等待第二层创建完成","已录制双次 NR","导引暂不支持，当前使用单次 NR","资源或预算不足，当前使用单次 NR","等待有序队列","等待可安全复用的资源","第二层调用失败；需重启游戏"};
    const char* en[]={"Off","Primary warming","Secondary warming","Waiting for Create completion","Two NR passes recorded","Guides unavailable; single NR retained","Resources unavailable; single NR retained","Waiting for ordered queue","Waiting for reusable resources","Secondary failure; restart required"};
    ImGui::TextWrapped("%s",zh?cn[static_cast<unsigned>(value)]:en[static_cast<unsigned>(value)]);
    ImGui::TextWrapped(zh?"实际尺寸：%u × %u → %u × %u；第二层成功录制 %llu 次。":"Actual sizes: %u x %u -> %u x %u; secondary recordings: %llu.",primary_width.load(),primary_height.load(),second_width.load(),second_height.load(),second_evaluations.load());
    ImGui::TextWrapped(zh?"辅助纹理：%.1f MiB（全池上限 1024 MiB；不含模型内部显存）。":"Auxiliary textures: %.1f MiB (pool limit 1024 MiB; excludes model internals).",double(g_dual_allocated_bytes.load())/(1024*1024));
    const unsigned count=time_samples.load(std::memory_order_acquire);
    if(count&&time_config.load(std::memory_order_acquire)==g_config_generation.load()) {
        ImGui::TextWrapped(zh?"GPU 稳定样本 %u：链路中位 %.2f ms / P95 %.2f ms；第二层 %.2f ms。":"Stable GPU samples %u: chain median %.2f ms / P95 %.2f ms; NR2 %.2f ms.",count,time_median[5].load(),time_p95[5].load(),time_median[2].load());
        SettingsNote(zh?"此处只计 NR 与 SR 链路，不代表游戏整帧耗时；切换后的暖机不纳入稳定窗口。":"This measures the NR/SR chain, not the whole game frame; switch warmup is excluded.");
    }
    if(ImGui::Button(zh?"采集双次 NR 对照（F7）":"Capture dual NR comparison (F7)"))RequestStageCapture();
    SettingsNote(zh?"双次 NR 生效时，F7 额外保存同一帧的 P1、L1、L2、第二层导引及最终合成到文档 / DLSS5-Dual-Capture。只记录观察帧，不是完整历史重放。":"When both passes are active, F7 also saves P1, L1, L2, bound NR2 guides and final composition in Documents / DLSS5-Dual-Capture. One observed frame, not a complete history replay.");
    ImGui::Text(zh?"双次采集状态：%u（0未开始 / 1等待 / 2已导出 / 3失败）":"Dual capture status: %u (0 idle / 1 pending / 2 exported / 3 failed)",nr_dual_capture::status.load());
    if(value==Status::UnsupportedGuides||value==Status::ResourceBudget||value==Status::Failed)
        SettingsNote(zh?"请用 F8 上传反馈；画质和性能需在游戏中对比。":"Use F8 to upload feedback; compare quality and performance in the game.");
}
}
