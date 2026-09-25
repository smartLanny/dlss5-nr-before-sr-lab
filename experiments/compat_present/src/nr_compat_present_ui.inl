// Included inside Core, after SettingsField/Tooltip/Slider helpers.
// Exactly replaces the old two-button selector; no new Mode=3 is invented.
bool DrawProcessingModeBlocks(ChainConfig& next,bool rr_guarded,
    bool compatible_post_locked,int effective_mode) {
    const bool zh=UseChineseUi();bool changed=false;
    const char* labels[]={zh?"先增强，后放大":"Enhance first",zh?"先放大，后增强":"Upscale first",zh?"兼容后处理":"Compatibility post"};
    const float space=ImGui::GetStyle().ItemSpacing.x;
    const float column=std::max(1.f,(SettingsContentWidth()-space*2.f)/3.f);
    const bool stacked=column<ImGui::GetFontSize()*6.5f;
    ImGui::PushID("nr-processing-mode");
    for(unsigned option=0;option<3;++option){
        if(option&&!stacked)ImGui::SameLine();ImGui::PushID(static_cast<int>(option));
        // RR blocks only pre-reconstruction, not the whole three-choice row.
        const bool blocked=option==0&&(rr_guarded||compatible_post_locked);
        const int legacy=option==0?2:1;
        const bool selected=option==2?next.compat_present:(!next.compat_present&&legacy==effective_mode);
        if(blocked)ImGui::BeginDisabled();
        if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_Header));
        const bool clicked=ImGui::Button(labels[option],ImVec2(stacked?SettingsContentWidth():column,ImGui::GetFrameHeight()*1.25f));
        if(selected)ImGui::PopStyleColor();if(blocked)ImGui::EndDisabled();
        if(clicked&&!blocked){
            if(option==2&&!next.compat_present){next.compat_present=true;changed=true;}
            if(option!=2&&(next.compat_present||next.mode!=legacy)){next.compat_present=false;next.mode=legacy;changed=true;}
        }
        SettingsTooltip(option==0?(zh?"NR 处理场景输入后，游戏继续自己的 SR。":"NR modifies scene input before the game's SR."):
            option==1?(zh?"处理 SR／RR 刚输出的场景结果；游戏之后仍可能有后处理和界面绘制。":"NR modifies the scene result just produced by SR/RR; game post-processing and UI can follow."):
            (zh?"在当前游戏的呈现入口处理最终颜色，不依赖 SR／RR Evaluate。此候选仅原生 D3D12，先关闭 FG，并单独允许纯颜色实验；不是桌面全局滤镜，也不保证兼容最好。":"Processes current game output at the presentation boundary, independently of SR/RR Evaluate. This candidate requires native D3D12, FG off and explicit color-only consent. Not a desktop filter or guaranteed compatibility."));
        ImGui::PopID();
    }
    ImGui::PopID();return changed;
}
void DrawCompatPostStatus() {
    const bool zh=UseChineseUi();const auto s=nr_compat_present_runtime::Snapshot();
    ImGui::TextWrapped("%s: %s",zh?"兼容后处理":"Compatibility post",nr_compat_present::Name(s.status));
    SettingsNote(zh?"此候选为纯颜色输入：恒定深度、零运动、每帧重置历史。并非原生时域输入；高速运动、细节和字幕可能不稳定。"
        :"Color-only candidate: constant depth, zero motion, history reset every frame. Not native temporal input; motion, details and HUD may be unstable.");
    if(s.work_width)ImGui::TextWrapped(zh?"最近代次 %llu：%u × %u → NR %u × %u；请求 %u 遍。已提交 %llu，已完成 %llu。":"Last generation %llu: %u x %u -> NR %u x %u; requested %u passes. Submitted %llu, completed %llu.",
        static_cast<unsigned long long>(s.generation),s.source_width,s.source_height,s.work_width,s.work_height,s.passes,
        static_cast<unsigned long long>(s.submitted),static_cast<unsigned long long>(s.completed));
    if(s.timing_valid)ImGui::TextWrapped(zh?"最近已完成源帧 %llu：捕获 %.2f / 编码 %.2f / NR %.2f+%.2f+%.2f+%.2f / 合成 %.2f / 写回 %.2f / 总计 %.2f ms。":"Last completed source %llu: capture %.2f / encode %.2f / NR %.2f+%.2f+%.2f+%.2f / compose %.2f / blit %.2f / total %.2f ms.",
        static_cast<unsigned long long>(s.timing_source),s.capture_ms,s.encode_ms,s.nr_ms[0],s.nr_ms[1],s.nr_ms[2],s.nr_ms[3],s.compose_ms,s.blit_ms,s.total_ms);
    if(s.status!=nr_compat_present::Status::Off)SettingsNote(zh?"提交／GPU 完成不等于画质通过。出现旁路、闪烁或无变化，请按 F8 保存报告并上传 BUG。":"Submission/GPU completion is not visual validation. Save F8 and upload a bug report for bypass, flicker or no effect.");
}
void DrawCompatPostControls(ChainConfig& next,bool& changed,bool& discrete) {
    const bool zh=UseChineseUi();
    if(ImGui::Checkbox(zh?"允许纯颜色实验（无原生导引）":"Allow color-only experiment (no native guides)",&next.compat_color_only)){changed=discrete=true;}
    if(ImGui::Checkbox(zh?"本次运行已关闭帧生成 FG":"Frame Generation is OFF for this session",&next.compat_fg_off_confirmed)){changed=discrete=true;}
    SettingsNote(zh?"FG 确认只在本次进程有效，不保存为下次的已验证状态；观察到 FG 句柄仍会拒绝。原生导引／光流和可靠 FG 帧身份仍待接入，不会偷偷假造。":"FG confirmation is session-only, not saved as next-session proof. Observed FG handles still block. Native/optical-flow guides and reliable FG frame identity remain unconnected.");
    int percent=static_cast<int>(next.compat_percent);
    if(SettingsSliderInt(zh?"AI 渲染精度":"AI rendering precision","##compat-percent",&percent,50,100,"%d%%")){next.compat_percent=static_cast<unsigned>(percent);changed=true;}
    bool dual=next.nr_passes==2;
    if(ImGui::Checkbox(zh?"二次 AI 增强":"Second AI enhancement",&dual)){next.nr_passes=dual?2:1;changed=discrete=true;}
    if(dual){
        if(ImGui::Checkbox(zh?"完整同尺寸多 Pass（实验）":"Full-size multipass (experimental)",&next.nr_full_reference)){changed=discrete=true;}
        if(next.nr_full_reference){int passes=static_cast<int>(next.nr_full_passes);
            if(SettingsSliderInt(zh?"总 NR 次数":"Total NR passes","##compat-passes",&passes,2,4,"%d")){next.nr_full_passes=static_cast<unsigned>(passes);changed=true;}}
        else {
            static const unsigned num[]={1,3,2,1},den[]={1,4,3,2};const char* names[]={"100%","75%","2/3","50%"};
            for(unsigned i=0;i<4;++i){if(i)ImGui::SameLine();ImGui::PushID(static_cast<int>(i));
                if(ImGui::Button(names[i])){next.nr_second_numerator=num[i];next.nr_second_denominator=den[i];changed=discrete=true;}ImGui::PopID();}
            int ratio=static_cast<int>(100u*next.nr_second_numerator/std::max(next.nr_second_denominator,1u));
            if(SettingsSliderInt(zh?"辅层尺寸":"Auxiliary scale","##compat-aux",&ratio,25,100,"%d%%")){next.nr_second_numerator=static_cast<unsigned>(ratio);next.nr_second_denominator=100;changed=true;}
        }
    }
    SettingsNote(zh?"复用现有模型、亮暗、颜色与通用分层控制；本路径没有引擎分区或独立人脸保护掩码。100% 不缩小，低尺寸辅层只恢复匹配增量。":"Reuses model/light/color/generic controls; no engine-region or independent face-protection masks on this lane. Full size avoids scaling; reduced auxiliary input uses matched deltas.");
    DrawCompatPostStatus();
}
