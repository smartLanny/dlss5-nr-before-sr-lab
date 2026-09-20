// Flat product pages. All picture edits join DrawSettings' single transaction.
const char* NrUi(const char* en,const char* zh){return UseChineseUi()?zh:en;}
bool NrChoice(const char* id,unsigned& value,const char* const* labels,unsigned count) {
    bool edited=false;ImGui::PushID(id);
    const float width=std::max(1.f,(SettingsContentWidth()-(count-1)*ImGui::GetStyle().ItemSpacing.x)/count);
    for(unsigned i=0;i<count;++i){
        if(i)ImGui::SameLine();ImGui::PushID(static_cast<int>(i));
        const bool selected=value==i;
        if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_Header));
        if(ImGui::Button(labels[i],ImVec2(width,0))&&!selected){value=i;edited=true;}
        if(selected)ImGui::PopStyleColor();ImGui::PopID();
    }
    ImGui::PopID();return edited;
}
bool NrCombo(const char* label,const char* id,int& value,const char* options) {
    SettingsField(label);return ImGui::Combo(id,&value,options);
}
void DrawUnifiedColourState(const ChainConfig& next,unsigned generation,bool pending) {
    const auto sample=SnapshotColorPolicy();
    if(pending||sample.config_generation!=generation||sample.status!=NrColorPolicyStatus::Current){
        SettingsNote(pending?NrUi("Waiting for these settings to be used.","等待采用当前设置。"):NrColorPolicyStatusText(sample.status,UseChineseUi()));return;
    }
    ImGui::TextWrapped(NrUi("AI colour: request %.2f / recorded %.2f","AI 色彩：请求 %.2f／已录制 %.2f"),
        next.color_strength,sample.policy.effective_color_strength);
    if(!sample.policy.color_allowed)SettingsNote(NrUi("Colour is limited by the current policy; see Developer > Colour policy.","当前颜色策略限制了颜色采用，见开发者页“颜色策略”。"));
}
void DrawUnifiedLight(ChainConfig& next,bool before_sr,unsigned generation,
    nr_light_style::UndoState& undo,bool has_pending,bool& pending_undo,bool& changed,bool& discrete) {
    SettingsSection(NrUi("Light adjustment","光影调节"));
    auto method=nr_unified_ui::LightMethod(next);
    const char* methods[]={NrUi("Light / dark","普通亮暗"),NrUi("Regions · test","精细分区·实验"),NrUi("Edit types · test","修改类型·实验")};
    if(NrChoice("light-method",method,methods,3)){nr_unified_ui::SetLightMethod(next,method);changed=true;discrete=true;}
    SettingsNote(NrUi("These modes adjust NR's edits, not the whole game image. Final enhancement, AI colour and applicable safeguards still affect the result.",
        "三种方式只调节 NR 新增修改；最终增强、AI 色彩及适用的保护仍会影响结果。"));
    if(method==0){
        bool manual=false;
        manual|=LightStylePercent(NrUi("Brightening","增亮强度"),"##daily-bright",next.light_reflection,2.f);
        manual|=LightStylePercent(NrUi("Darkening","压暗强度"),"##daily-dark",next.light_structure,2.f);
        manual|=LightStylePercent(NrUi("Glow suppression","辉光抑制"),"##daily-glow",next.light_glow,1.f);
        if(manual){nr_light_style::MarkCustom(next);changed=true;}
        SettingsField(NrUi("Light preset","光影预设"));
        unsigned preset=next.light_preset;
        const char* names[]={NrUi("Original","原样"),NrUi("Natural","自然耐看"),NrUi("Less glow","减少辉光")};
        if(NrChoice("light-presets",preset,names,3)){
            nr_light_style::Write(next,nr_light_style::Select(preset,next.color_strength,next.light_control_mode));changed=true;discrete=true;
        }
        SettingsNote(NrUi("Presets change only lighting. AI colour stays as saved.","光影预设仅改光影，不改变 AI 色彩强度。"));
        if(nr_unified_ui::LegacyLightActive(next))SettingsNote(NrUi("Legacy detail settings are active; the controls may affect fine edits only. Inspect Developer > Legacy lighting.","旧光影细项正在生效，当前亮暗控件可能仅作用于细小修改；见开发者页“旧光影细分”。"));
        ImGui::BeginDisabled(has_pending||changed||!undo.Available(generation));
        if(ImGui::Button(NrUi("Undo lighting edit","撤销光影修改"))&&undo.Restore(next,generation)){pending_undo=true;changed=true;discrete=true;}
        ImGui::EndDisabled();
    }else if(method==1){
        const auto status=nr_regions::status.load(std::memory_order_acquire);
        const auto color=SnapshotColorPolicy();
        const bool colour_disabled=next.color_strength<=0.f ||
            (!has_pending&&!changed&&color.status==NrColorPolicyStatus::Current&&!color.policy.color_allowed);
        SettingsNote(status==3?NrUi("Basis: image edges, depth and specular guidance.","依据：图像边缘、深度与镜面导引。"):
            status==2?NrUi("Basis: image edges and depth; no specular guide.","依据：图像边缘与深度；无镜面导引。"):
            status==1?NrUi("Basis: image edges; engine guides unavailable.","依据：图像边缘；引擎导引不可用。"):
            status==4?NrUi("Region resources unavailable; original composition retained.","分区资源不可用，保留原合成。"):
            NrUi("Waiting for a supported frame; guide availability is unknown.","等待可用帧，导引状态尚未确认。"));
        const char* names[]={NrUi("Broad brightening","大范围增亮"),NrUi("Fine brightening","细节增亮"),
            NrUi("Broad darkening","大范围压暗"),NrUi("Fine darkening","细节压暗"),NrUi("Specular region","镜面区域")};
        if(ImGui::BeginTable("light-regions",3,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_BordersInnerH)){
            ImGui::TableSetupColumn(NrUi("Region","区域"));ImGui::TableSetupColumn(NrUi("Luma","明暗"));ImGui::TableSetupColumn(NrUi("Colour","颜色"));ImGui::TableHeadersRow();
            for(unsigned i=0;i<5;++i){
                ImGui::PushID(static_cast<int>(i));ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextWrapped("%s",names[i]);
                ImGui::BeginDisabled(i==4&&status!=3);
                for(unsigned c=0;c<2;++c){ImGui::TableSetColumnIndex(static_cast<int>(c+1));ImGui::SetNextItemWidth(-1.f);float percent=next.region_gains[i*2+c]*100.f;
                    ImGui::BeginDisabled(c==1&&colour_disabled);
                    if(ImGui::SliderFloat(c?"##colour":"##luma",&percent,0.f,400.f,"%.0f%%")){next.region_gains[i*2+c]=percent*.01f;changed=true;}
                    ImGui::EndDisabled();}
                ImGui::EndDisabled();ImGui::PopID();
            }ImGui::EndTable();
        }
        if(status!=3)SettingsNote(NrUi("Specular controls are unavailable without a verified specular guide.","未取得可信镜面导引，镜面控件暂不可用。"));
        ImGui::BeginDisabled(colour_disabled);
        changed|=LightStylePercent(NrUi("Colour-only edits","纯颜色改动"),"##region-pure",next.region_gains[10],4.f);ImGui::EndDisabled();
        if(colour_disabled)SettingsNote(NrUi("AI colour is zero or disallowed; regional colour controls are inactive.","AI 色彩为零或被策略限制，区域颜色调节暂不生效。"));
        SettingsNote(NrUi("These are guided regions of NR edits, not all of the game's independent lighting layers.","这里划分 NR 修改区域，并非取得游戏全部独立光照层。"));
    }else{
        const char* names[]={NrUi("Total edit gain","总改动倍率"),NrUi("Brightening edits","增亮修改"),NrUi("Darkening edits","压暗修改"),NrUi("Saturation edits","饱和度修改"),NrUi("Lightness edits","明度修改")};
        for(unsigned i=0;i<5;++i){ImGui::PushID(static_cast<int>(i));changed|=SettingsSliderFloat(names[i],"##edit-gain",&next.generic_gains[i],i?0.f:1.f,2.f,"%.2fx");ImGui::PopID();}
        SettingsNote(NrUi("Saturation edits scale only the change added by NR, not whole-image saturation. All 1x preserves the current edit.","饱和度修改只调节 NR 新增颜色变化，不是全画面饱和度。全部 1 倍保留当前修改。"));
    }
    SettingsSection(NrUi("Shared controls","公共设置"));
    changed|=SettingsSliderFloat(NrUi("Preserve original lighting","保留原始光照"),"##lighting-lock",&next.lighting_lock,0.f,1.f,"%.2f");
    SettingsTooltip(NrUi("Reduces AI-added light changes. Does not reduce model strength, inference count or work size.","减少 AI 新增明暗，不降低模型推理强度、次数或工作尺寸。"));
    ImGui::BeginDisabled(!before_sr);
    changed|=SettingsSliderFloat(NrUi("Edge protection","边缘保护"),"##edge-protection",&next.edge_guard,0.f,1.f,"%.2f");ImGui::EndDisabled();
    SettingsTooltip(NrUi("Attenuates edits near source edges; not a halo-removal filter.","减弱原图边缘附近的修改，不是抗光晕滤镜。"));
    if(!before_sr)SettingsNote(NrUi("Edge protection is supported on the pre-SR route; its saved value is retained.","当前路线不采用前置边缘保护，保存值仍保留。"));
    const auto sample=SnapshotColorPolicy();
    const auto reason=sample.policy.color_reason;
    const bool hdr_mapping=sample.config_generation==generation&&sample.status==NrColorPolicyStatus::Current&&
        (reason==NrColorReason::PostSrFp16UnitExposure||reason==NrColorReason::UnverifiedHdr||reason==NrColorReason::ExperimentalUnverifiedHdr||reason==NrColorReason::ColourLabPriority);
    ImGui::BeginDisabled(!hdr_mapping);float white=1.f/next.proxy_scale;
    if(SettingsSliderFloat(NrUi("Reference white","参考白亮度"),"##reference-white",&white,.5f,4.f,"%.2fx")){next.proxy_scale=1.f/white;changed=true;}ImGui::EndDisabled();
    SettingsTooltip(NrUi("Changes the HDR input seen by NR through the existing ProxyScale, once. Does not change display brightness or external HDR plugins.","通过现有 ProxyScale 一次调整模型所见 HDR 输入，不改显示器或外部 HDR 插件。"));
    if(!hdr_mapping)SettingsNote(NrUi("No current matching HDR-mapped input is confirmed; reference white is unavailable.","尚无当前配置采用 HDR 映射的记录，参考白暂不可编辑。"));
    if(ImGui::Button(NrUi("Reset current light method","重置当前光影方式"))){nr_unified_ui::ResetLightMethod(next);changed=true;discrete=true;}
    SettingsTooltip(NrUi("Resets only this method's gains and preview; keeps AI colour, common safeguards, reference white and the other methods.","仅重置当前方式的倍率并退出预览；保留 AI 色彩、公共保护、参考白及其他方式。"));
    int preview=static_cast<int>(method==1?next.region_preview:next.light_preview);
    ImGui::BeginDisabled(method==2);
    const char* choices=method==1?NrUi("Off\0All regions\0Broad bright\0Fine bright\0Broad dark\0Fine dark\0Specular\0Colour only\0","关闭\0全部区域\0大范围增亮\0细节增亮\0大范围压暗\0细节压暗\0镜面\0纯颜色\0"):
        NrUi("Off\0Four regions\0Final NR difference\0","关闭\0四分区\0最终 NR 修改差分\0");
    if(NrCombo(NrUi("Region preview","区域预览"),"##light-preview",preview,choices)){
        next.region_preview=method==1?static_cast<unsigned>(preview):0;next.light_preview=method==0?static_cast<unsigned>(preview):0;changed=true;discrete=true;
    }ImGui::EndDisabled();
    if(method==2)SettingsNote(NrUi("Edit-types mode has no region preview.","修改类型方式未提供区域预览。"));
}

void DrawUnifiedExperiments(ChainConfig& next,unsigned generation,bool pending,bool& changed,bool& discrete) {
    SettingsSection(NrUi("Colour comparisons","颜色对照"));
    int curve=next.colour_input_curve==1?1:0;
    if(NrCombo(NrUi("Model input","模型输入"),"##experiment-input",curve,NrUi("D21 input\0N2 (experimental)\0","D21 原输入\0N2（实验）\0"))){next.colour_input_curve=curve;changed=true;discrete=true;}
    int restore=nr_colour_restore::Normalize(next.colour_restore_mode);
    if(NrCombo(NrUi("Recovery","恢复方式"),"##experiment-restore",restore,NrUi("D21 composition\0Protected RGB (halo reports)\0Raw RGB (diagnostic)\0","D21 合成\0受保护 RGB（有亮边反馈）\0原始 RGB（诊断）\0"))){next.colour_restore_mode=restore;changed=true;discrete=true;}
    SettingsNote(NrUi("N2 changes model input/history. RGB changes recovery and has halo reports; neither is a proven replacement for the baseline.","N2 改变模型输入与历史；RGB 改变恢复方式且有亮边反馈。两者均是独立对照。"));
    DrawUnifiedColourState(next,generation,pending||changed);
    const auto sample=SnapshotColorPolicy();
    if(!pending&&!changed&&sample.config_generation==generation&&sample.status==NrColorPolicyStatus::Current){
        ImGui::TextWrapped(NrUi("Recorded: %s / %s","已录制：%s／%s"),nr_colour_restore::CurveName(sample.policy.recorded_input_curve),nr_colour_restore::Name(sample.policy.recorded_restore_mode));
        if(next.colour_input_curve!=sample.policy.recorded_input_curve)SettingsNote(NrUi("The selected input curve was not applied to the recorded frame.","所选输入曲线未用于最近记录的帧。"));
    }
    nr_face_runtime::DrawUi(next,changed);
    DrawEnergyGateOptions();
    SettingsSection(NrUi("RR compatibility candidate","RR 兼容候选"));
    bool missing=nr_post_settings::missing_projection.load(std::memory_order_acquire);
    if(ImGui::Checkbox(NrUi("Try post NR without a camera projection","RR 缺投影时尝试后置 NR"),&missing))
        nr_post_settings::RequestMissingProjection(missing);
    if(nr_post_settings::missing_projection_save_failed.load(std::memory_order_acquire))
        SettingsNote(NrUi("Could not save the RR experiment. The session keeps your request; check F8.","RR 实验设置未能保存；本次会话保留请求，请查看 F8。"));
    SettingsNote(NrUi("Off by default. Only when the current camera matrix is absent: keeps native RR, uses marked unknown depth and resets NR every frame. Flicker/detail and game support remain unverified. Other safety checks still apply; unreadable or unsupported matrices remain bypassed.",
        "默认关闭。仅在当前投影矩阵缺失时尝试：保留原生 RR，使用标记为未知的占位深度，NR 每帧重置历史。可能影响稳定性和细节，游戏效果待验证；其他安全检查不变，不可读或不支持的矩阵仍旁路。"));
    ImGui::TextWrapped(NrUi("Latest projection input: %s. Recorded conversion / placeholder passes: %llu / %llu. Save F8 to verify NR adoption.",
        "最近投影输入：%s。转换／占位录制次数：%llu／%llu。是否真正采用 NR 请查看 F8。"),
        nr_rr_depth::Name(nr_rr_depth::observations.input.load(std::memory_order_relaxed)),
        g_rr_depth_conversions.load(std::memory_order_relaxed),
        nr_rr_depth::observations.placeholder_recorded.load(std::memory_order_relaxed));
}

void DrawUnifiedDeveloper(ChainConfig& next,bool before_sr,bool& changed,bool& discrete) {
    SettingsSection(NrUi("Input mechanisms","输入机制"));
    ImGui::BeginDisabled(!before_sr);
    int filter=static_cast<int>(next.nr_input_filter);
    if(NrCombo(NrUi("Input filter","输入滤波"),"##input-filter",filter,NrUi("Exact Area\0Lanczos-2 AA (experimental)\0","精确面积\0Lanczos-2 抗混叠（实验）\0"))){next.nr_input_filter=static_cast<unsigned>(filter);changed=true;discrete=true;}
    ImGui::EndDisabled();
    if(!before_sr)SettingsNote(NrUi("Input filtering applies only before SR; value retained.","输入滤波仅用于前置，保存值保留。"));
    int curve=static_cast<int>(next.proxy_curve);
    if(NrCombo(NrUi("HDR mapping","HDR 映射"),"##hdr-map",curve,NrUi("Legacy shoulder\0Hybrid\0Reversible (experimental)\0","柔和压缩\0混合压缩\0可逆压缩（实验）\0"))){next.proxy_curve=static_cast<unsigned>(curve);changed=true;discrete=true;}
    changed|=ImGui::Checkbox(NrUi("Highlight mapping","高光映射"),&next.experimental_peak_shoulder);
    SettingsNote(NrUi("HDR mappings affect only applicable model inputs; they do not enable game HDR.","HDR 映射只影响适用的模型输入，不会开启游戏 HDR。"));
    int jitter=static_cast<int>(next.jitter_mode);
    if(NrCombo(NrUi("Jitter input","Jitter 输入"),"##jitter",jitter,NrUi("Off\0Verified game pixel jitter\0","不传递\0已验证的游戏像素抖动\0"))){next.jitter_mode=static_cast<NrJitterMode>(jitter);changed=true;discrete=true;}
    SettingsTooltip(NrUi("Experimental. Changes NR history, not the game's SR jitter; binding does not prove the model uses it.","实验选项；改变 NR 历史，不改游戏 SR 抖动。参数写入不等于模型一定采用。"));
    SettingsSection(NrUi("Colour policy","颜色策略"));
    int mode=static_cast<int>(nr_colour_lab::Normalize(next.colour_lab_mode));
    if(NrCombo(NrUi("Policy","颜色许可"),"##colour-policy",mode,NrUi("Legacy custom\0Colour priority\0Conservative HDR\0","旧版自定义\0颜色优先\0保守 HDR\0"))){next.colour_lab_mode=mode;changed=true;discrete=true;}
    if(nr_colour_lab::LegacyPermissionEditable(mode))
        changed|=ImGui::Checkbox(NrUi("Legacy custom: allow unverified HDR colour","旧版自定义：允许未验证 HDR 颜色"),&next.allow_unverified_hdr_color);
    else SettingsNote(mode==1?NrUi("Colour priority permits unverified HDR colour. The saved Legacy value is retained.","颜色优先允许未验证 HDR 颜色，旧版许可值仍保留。"):
        NrUi("Conservative policy limits unverified HDR colour. The saved Legacy value is retained.","保守策略限制未验证 HDR 颜色，旧版许可值仍保留。"));
    SettingsSection(NrUi("Recovery and composition","恢复与合成"));
    int order=nr_colour_restore::NormalizeScaledProxy(next.colour_scaled_proxy_mode);
    if(NrCombo(NrUi("Low-resolution input order","低分输入顺序"),"##scaled-proxy",order,NrUi("Scale source first\0Encode proxy first (no proven gain)\0","先缩放场景\0先编码 Proxy（未证实收益）\0"))){next.colour_scaled_proxy_mode=order;changed=true;discrete=true;}
    int edge=nr_colour_edge::Normalize(next.colour_edge_recovery);
    if(NrCombo(NrUi("Paired sampling","配对取样"),"##paired-edge",edge,NrUi("D21 bilinear\0Paired positive weights\0","D21 双线性\0配对正权重\0"))){next.colour_edge_recovery=edge;changed=true;discrete=true;}
    if(edge&&next.colour_restore_mode==0&&!next.colour_neutral_probe)SettingsNote(NrUi("D21 composition does not use paired recovery.","D21 合成不采用配对恢复。"));
    changed|=ImGui::Checkbox(NrUi("Independent chroma","独立色度"),&next.independent_chroma_composition);
    changed|=ImGui::Checkbox(NrUi("Low-grid luminance field","低分亮度场"),&next.low_grid_edit_field);
    changed|=ImGui::Checkbox(NrUi("Low-grid colour field","低分颜色场"),&next.low_grid_color_edit_field);
    const bool catmull=next.low_grid_edit_field&&!next.low_grid_color_edit_field&&next.color_strength<=0.000001f;
    ImGui::BeginDisabled(!catmull);changed|=ImGui::Checkbox(NrUi("Catmull-Rom luminance restore","Catmull-Rom 亮度恢复"),&next.catmull_edit_restore);ImGui::EndDisabled();
    if(!catmull)SettingsNote(NrUi("Catmull-Rom requires the luminance field, colour field off and AI colour zero.","Catmull-Rom 需亮度场开启、颜色场关闭且 AI 色彩为零。"));
    changed|=ImGui::Checkbox(NrUi("N=P zero-edit diagnostic","N=P 零修改诊断"),&next.colour_neutral_probe);
    changed|=ImGui::Checkbox(NrUi("Run NR but discard its result","运行 NR 但丢弃结果"),&next.identity_use_original);
    SettingsNote(NrUi("These are recovery experiments. A neutral diagnostic is not a quality/performance comparison; low-grid paths can add passes and storage.","这些是恢复实验。零修改诊断不能作为画质或性能对照；低分场可能增加处理和存储。"));
    const bool layered=before_sr||next.independent_chroma_composition;
    ImGui::BeginDisabled(!layered);
    changed|=ImGui::Checkbox(NrUi("Component gains from one NR result","单次 NR 分项强化"),&next.layered_enhancement);
    ImGui::BeginDisabled(!next.layered_enhancement);
    changed|=SettingsSliderFloat(NrUi("Broad tone","大范围明暗"),"##component-tone",&next.layer_tone,0.f,2.f,"%.2fx");
    changed|=SettingsSliderFloat(NrUi("Structure","结构"),"##component-structure",&next.layer_structure,0.f,2.f,"%.2fx");
    changed|=SettingsSliderFloat(NrUi("Brightening","增亮"),"##component-bright",&next.layer_bright,0.f,2.f,"%.2fx");
    changed|=SettingsSliderFloat(NrUi("Darkening","压暗"),"##component-dark",&next.layer_shadow,0.f,2.f,"%.2fx");
    ImGui::EndDisabled();ImGui::EndDisabled();
    if(!layered)SettingsNote(NrUi("Component gains require pre-SR or the shared independent-chroma compositor.","分项强化需前置或独立色度的共用合成路线。"));
    SettingsSection(NrUi("Safeguards","保护边界"));
    changed|=ImGui::Checkbox(NrUi("High-strength protection","高强度保护"),&next.high_strength_protection);
    changed|=ImGui::Checkbox(NrUi("Colour protection","色彩保护"),&next.color_protection);
    changed|=SettingsSliderFloat(NrUi("Luminance ratio limit","明暗变化上限"),"##luma-limit",&next.max_luma_ratio,1.f,8.f,"%.2f");
    ImGui::BeginDisabled(!before_sr);
    changed|=SettingsSliderFloat(NrUi("Detail stability","细节稳定保护"),"##detail-stability",&next.detail_stability,0.f,1.f,"%.2f");ImGui::EndDisabled();
    if(!before_sr)SettingsNote(NrUi("Detail stability applies before SR only.","细节稳定保护仅用于前置。"));
    SettingsSection(NrUi("Legacy lighting","旧光影细分"));
    SettingsNote(NrUi("These saved settings affect normal light/dark only. Non-neutral values remain visible on the Picture and Lighting pages.","以下保存值影响普通亮暗；非默认状态会在画面和光影页提示。"));
    int scope=static_cast<int>(next.light_control_mode);
    bool light_edit=NrCombo(NrUi("Daily slider scope","日常滑条作用范围"),"##legacy-scope",scope,NrUi("Fine edits only (legacy)\0All light/dark edits\0","仅细小变化（旧版）\0全部亮暗变化\0"));
    if(light_edit){next.light_control_mode=static_cast<unsigned>(scope);discrete=true;}
    light_edit|=LightStylePercent(NrUi("Broad brightening","大范围提亮"),"##legacy-broad",next.light_broad,2.f);
    light_edit|=LightStylePercent(NrUi("Broad darkening","大范围压暗"),"##legacy-dark",next.light_dark,2.f);
    if(light_edit){nr_light_style::MarkCustom(next);changed=true;}
    SettingsSection(NrUi("Compatibility and recording","兼容与记录"));
    int motion=static_cast<int>(next.present_mv_compensation);
    if(NrCombo(NrUi("Motion size compensation","运动向量尺寸补偿"),"##motion-compensation",motion,NrUi("Conservative automatic\0Force compensation\0","保守自动\0强制补偿\0"))){next.present_mv_compensation=static_cast<unsigned>(motion);changed=true;discrete=true;}
    SettingsTooltip(NrUi("Compatibility input only. Changes model history; compare motion and ghosting.","仅影响兼容入口；切换改变模型历史，需对比运动与拖影。"));
    changed|=ImGui::Checkbox(NrUi("Low-overhead descriptor recording (default on)","低开销描述符记录（默认开启）"),&next.descriptor_low_overhead);
    SettingsNote(next.descriptor_low_overhead!=NrDescriptorLowOverheadActive()?NrUi("Restart required to apply this descriptor setting.","描述符设置需重启游戏生效。"):
        NrDescriptorLowOverheadActive()?NrUi("Active: low-overhead recording.","实际：低开销记录。"):
        NrUi("Active: full diagnostic recording; higher resident overhead.","实际：完整诊断记录，常驻开销更高。"));
    DrawRenderControl();
    int warmup=static_cast<int>(next.warmup_after_create);
    if(SettingsSliderInt(NrUi("Warm-up frames","预热帧数"),"##warmup",&warmup,0,30,"%d")){next.warmup_after_create=static_cast<unsigned>(warmup);changed=true;}
}

void DrawUnifiedFeedback(reshade::api::effect_runtime* runtime,const ChainConfig& config,bool pending) {
    const bool busy=nr_f8::IsActive()||nr_f8::g_status.load()==nr_f8::Status::Packaging||nr_core_feedback::Busy();
    ImGui::BeginDisabled(busy);
    const bool row=SettingsContentWidth()>ImGui::GetFontSize()*42.f;
    if(ImGui::Button(NrUi("Text report F8","生成文字报告 F8")))nr_f8::RequestCore();
    SettingsTooltip(NrUi("Bounded TXT; settings unchanged; path copied when ready.","生成有界 TXT，不改画质设置；完成后复制路径。"));
    if(row)ImGui::SameLine();
    if(ImGui::Button(NrUi("Capture current view F7","采集当前画面 F7")))nr_f8::RequestFullscreen();
    SettingsTooltip(NrUi("Current-path full-screen images and available model stages, with a text report. Saved settings stay unchanged; stage capture resets/warms history and affects timing.","采集当前路径全屏画面、可用模型阶段和文字报告；保存设置不变，阶段采集会重置/预热历史并影响计时。"));
    if(row)ImGui::SameLine();
    if(ImGui::Button(NrUi("Capture comparison","采集对照")))nr_f8::RequestFullscreen(nr_f8::Comparison::ShareMatrix);
    SettingsTooltip(NrUi("Temporarily compares NR off and both orders at 50/75/100%. RR skips pre-SR groups. Restores settings; resets history and affects timing.","临时比较关闭 NR 与前后置 50/75/100%；RR 跳过前置组。结束恢复设置，会重置历史并影响帧率。"));
    ImGui::EndDisabled();
    if(nr_f8::IsActive()){SettingsNote(NrUi("Capture in progress; keep the camera still.","正在采集，请保持镜头不动。"));if(ImGui::Button(NrUi("Cancel and restore","取消并恢复")))nr_f8::Cancel();}
    else if(nr_f8::g_status.load()==nr_f8::Status::Packaging)SettingsNote(NrUi("Packaging images…","正在整理图像……"));
    else if(nr_f8::g_status.load()==nr_f8::Status::Complete)SettingsNote(NrUi("Image capture completed.","图像采集完成。"));
    else if(nr_f8::g_status.load()==nr_f8::Status::Partial||nr_f8::g_status.load()==nr_f8::Status::Failed)SettingsNote(NrUi("Capture incomplete; missing evidence is recorded.","采集未完整完成，缺失证据已记录。"));
    const auto text_status=g_feedback_status.load();
    if(text_status==1||text_status==2)SettingsNote(NrUi("Preparing text report…","正在生成文字报告……"));
    else if(text_status>=3)SettingsNote(nr_core_feedback::GetReportClipboardStatus()==nr_core_feedback::ReportClipboardStatus::Copied?
        NrUi("Report saved; path copied.","报告已保存，路径已复制。"):
        NrUi("Check report status in the detailed information below; clipboard delivery is not confirmed.","报告或剪贴板状态尚未确认，详见下方运行信息。"));
    SettingsSection(NrUi("Performance comparison","性能对照"));
    ImGui::BeginDisabled(nr_f8::IsActive());
    int probe=static_cast<int>(NrProbePolicySnapshot().mode);
    if(NrCombo(NrUi("Diagnostic sampling","诊断采样"),"##probe",probe,NrUi("Normal\0Paused\0Output transitions only\0","正常\0暂停\0仅输出状态切换\0")))SetNrProbePolicy(static_cast<nr_probe_policy::Mode>(probe));
    const auto timing=NrTimingPolicySnapshot();int wait=static_cast<int>(timing.wait.mode);
    if(NrCombo(NrUi("Wait strategy","等待方式"),"##wait",wait,NrUi("Event\0Legacy polling comparison\0","事件等待\0旧轮询对照\0")))SetNrWaitComparison(static_cast<unsigned>(wait));
    if(ImGui::Button(NrUi("Start a new timing segment","开始新计时段")))SetNrWaitComparison(static_cast<unsigned>(wait),true);
    ImGui::EndDisabled();
    SettingsNote(NrUi("Session-only; NR continues. Close the menu, hold the same scene for 30 seconds, then F8. Captures affect timing.","仅本次运行生效，NR 继续处理。关闭菜单后固定场景30秒，再按F8；图像采集会影响计时。"));
    SettingsSection(NrUi("Detailed captures","深度采集"));
    ImGui::BeginDisabled(busy||StageCaptureStatus()==1u||StageCaptureStatus()==2u);
    if(ImGui::Button(NrUi("Capture centre model stages","采集中央区域模型阶段")))nr_f8::RequestWithOptions({});ImGui::EndDisabled();
    SettingsNote(NrUi("Captures S/P/real N/E in the centre region with a paired report; resets/warms history. Pre-SR E is not the native SR output. Compatibility input captures available current-path layers instead.","采集中央区域原图、P、真实N和E并附报告，会重置/预热历史。前置E不是原生SR输出；兼容入口改为采集当前路径可用的各层。"));
    nr_replay::DrawUi();DrawFaceDiagnosticControls(runtime,config);
    SettingsSection(NrUi("Runtime information","详细运行信息"));
    ImGui::PushTextWrapPos(0.f);
    nr_dual_runtime::DrawDetailsUi(config.nr_passes==2,pending);
    ImGui::PushID("technical");DrawTechnicalDiagnostics(runtime);ImGui::PopID();
    ImGui::PopTextWrapPos();
    SettingsSection(NrUi("About","关于"));
    ImGui::TextWrapped("%s",kAddonVersion);
    ImGui::TextWrapped("%s",UseChineseUi()?kAddonAuthor:kAddonAuthorAscii);
    ImGui::TextWrapped("%s",kOfficialReleaseUrl);
    ImGui::TextWrapped("%s",NrUi("QQ group 2: 392308850","交流群（二群）：392308850"));
    SettingsNote(NrUi("F6: NR on/off · F8: text report · F7: current-path images · minus: screenshot. Deep comparisons may reset history and temporarily change parameters; completion/cancel restores saved values.",
        "F6：NR开关 · F8：文字报告 · F7：当前路径图像 · 减号：截图。深度对照可能重置历史、临时改变参数；完成或取消后恢复。"));
}
