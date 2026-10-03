// Flat product pages. All picture edits join DrawSettings' single transaction.
const char* NrUi(const char* en,const char* zh){return UseChineseUi()?zh:en;}
bool NrChoice(const char* id,unsigned& value,const char* const* labels,unsigned count,
              const char* const* tooltips=nullptr) {
    bool edited=false;ImGui::PushID(id);
    const float width=std::max(1.f,(SettingsContentWidth()-(count-1)*ImGui::GetStyle().ItemSpacing.x)/count);
    for(unsigned i=0;i<count;++i){
        if(i)ImGui::SameLine();ImGui::PushID(static_cast<int>(i));
        const bool selected=value==i;
        if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_Header));
        if(ImGui::Button(labels[i],ImVec2(width,0))&&!selected){value=i;edited=true;}
        if(selected)ImGui::PopStyleColor();
        if(tooltips)SettingsTooltip(tooltips[i]);
        ImGui::PopID();
    }
    ImGui::PopID();return edited;
}
bool NrCombo(const char* label,const char* id,int& value,const char* options) {
    SettingsField(label);return ImGui::Combo(id,&value,options);
}
void DrawKnownReShadeHotkeyConflicts(reshade::api::effect_runtime* runtime,const ChainConfig& config) {
    if(!runtime)return;
    const char* config_keys[]={"KeyOverlay","KeyScreenshot","KeyEffects","KeyNextPreset","KeyPreviousPreset","KeyReload","KeyFPS","KeyFrameTime"};
    const char* labels[]={NrUi("menu","菜单"),NrUi("screenshot","截图"),NrUi("effects","效果开关"),NrUi("next preset","下一预设"),NrUi("previous preset","上一预设"),NrUi("reload","重新加载"),NrUi("FPS display","帧率显示"),NrUi("frame-time display","帧时间显示")};
    const char* actions[]={NrUi("NR toggle","NR 开关"),NrUi("Current-view capture","当前画面采集"),NrUi("Text report","文字报告"),NrUi("Screenshot","保存截图")};
    const unsigned keys[]={config.master_virtual_key,config.image_virtual_key,config.feedback_virtual_key,config.screenshot_virtual_key};
    // The SDK reads the current runtime's cached config. Its integer getter
    // yields the base key of the key/modifier array; saved bindings stay intact.
    for(unsigned j=0;j<sizeof(config_keys)/sizeof(config_keys[0]);++j){
        unsigned other=0;if(!reshade::get_config_value(runtime,"INPUT",config_keys[j],other))continue;
        for(unsigned i=0;i<nr_hotkeys::Count;++i)if(nr_hotkeys::Overlaps(static_cast<nr_hotkeys::Action>(i),keys[i],other))
            ImGui::TextWrapped(NrUi("%s uses %s, also used by ReShade (%s).","%s 绑定 %s，与 ReShade 的%s重叠。"),actions[i],nr_hotkeys::Name(keys[i],UseChineseUi()),labels[j]);
    }
}
void DrawUnifiedColourState(const ChainConfig& next,unsigned generation,bool pending) {
    const auto sample=SnapshotColorPolicy();
    if(pending||sample.config_generation!=generation||sample.status!=NrColorPolicyStatus::Current){
        SettingsNote(pending?NrUi("Preparing: waiting for new settings.","准备中：等待新设置。"):NrColorPolicyStatusText(sample.status,UseChineseUi()));return;
    }
    SettingsNote(NrUi("AI colour: recent processing recorded.","AI 色彩：有近期处理记录。"));
    char colour_hint[128]{};std::snprintf(colour_hint,sizeof(colour_hint),NrUi("Requested %.2f; recorded coefficient %.2f. This does not prove visible colour.","请求 %.2f；录制系数 %.2f。不代表画面已呈现可见色彩。"),next.color_strength,sample.policy.effective_color_strength);
    SettingsHelp(colour_hint);
    if(!sample.policy.color_allowed)SettingsNote(NrUi("Colour is limited by the current input or saved legacy permission; requested strength is not the applied amount.","AI 色彩未生效：当前受限。"));
}
void DrawUnifiedLight(ChainConfig& next,bool before_sr,unsigned generation,
    nr_light_style::UndoState& undo,bool has_pending,bool& pending_undo,bool& changed,bool& discrete) {
    SettingsSection(NrUi("Light adjustment","光影调节"));
    auto method=nr_unified_ui::LightMethod(next);
    const char* methods[]={NrUi("Light / dark","普通亮暗"),NrUi("Regions · test","精细分区·实验"),NrUi("Edits · test","修改类型·实验")};
    if(NrChoice("light-method",method,methods,3)){nr_unified_ui::SetLightMethod(next,method);changed=true;discrete=true;}
    SettingsTooltip(NrUi("These modes adjust NR's edits, not the whole game image. Final enhancement, AI colour and applicable safeguards still affect the result.",
        "三种方式只调节 NR 新增修改；最终增强、AI 色彩及适用的保护仍会影响结果。"));
    if(method==0){
        bool manual=false;
        const bool detail_scope=next.light_control_mode==nr_light_style::DetailLayers;
        manual|=LightStylePercent(detail_scope?NrUi("Fine brightening","细节增亮"):NrUi("Brightening","增亮强度"),"##daily-bright",next.light_reflection,2.f);
        SettingsTooltip(NrUi("Scales NR-added brightening. In the current whole-light scope this includes broad and fine changes; it is not whole-image brightness.","调整 NR 新增提亮。全部亮暗范围下包含大范围和细节；旧版细节范围下仅调整细节。不是全画面亮度。"));
        manual|=LightStylePercent(detail_scope?NrUi("Fine darkening","细节压暗"):NrUi("Darkening","压暗强度"),"##daily-dark",next.light_structure,2.f);
        SettingsTooltip(NrUi("Scales NR-added darkening, using the same scope as Brightening. It does not darken the original game image directly.","调整 NR 新增压暗，作用范围与增亮相同，不直接压暗游戏原图。"));
        SettingsSection(NrUi("Broad changes","大范围变化"));
        float broad=nr_unified_ui::BroadBright(next);
        if(LightStylePercent(NrUi("Broad brightening","大范围增亮"),"##daily-broad-bright",broad,2.f)){
            nr_unified_ui::SetBroadBright(next,broad);manual=true;
        }
        SettingsTooltip(NrUi("One effective multiplier combines old broad brightening and glow suppression. 100% keeps the broad edit; 0 removes it. Viewing preserves old values; editing replaces the two factors with this amount. No halo detector is used.","100% 保留大范围增亮，0% 去掉。打开菜单不改旧值；调节后合并原有两个数值。不识别真实光晕。"));
        manual|=LightStylePercent(NrUi("Broad darkening","大范围压暗"),"##daily-broad-dark",next.light_dark,2.f);
        SettingsTooltip(NrUi("Additional multiplier for broad NR darkening. In whole-light scope it follows Darkening; fine edits keep their existing amount.","单独调整大范围的 NR 压暗；全部亮暗范围下还受上方压暗强度影响，细小修改不受此额外倍率影响。"));
        if(manual){nr_light_style::MarkCustom(next);changed=true;}
        SettingsField(NrUi("Light preset","光影预设"));
        unsigned preset=next.light_preset;
        const char* names[]={NrUi("Original","原样"),NrUi("Natural","自然耐看"),NrUi("Less glow","减少辉光")};
        if(NrChoice("light-presets",preset,names,3)){
            nr_light_style::Write(next,nr_light_style::Select(preset,next.color_strength,next.light_control_mode));changed=true;discrete=true;
        }
        SettingsTooltip(NrUi("Presets change only these lighting coefficients and retain the saved scope. AI colour and legacy component gains stay as saved.","预设仅改这组光影系数，保留作用范围；不修改 AI 色彩及旧分项倍率。“减少辉光”通过减少大范围增亮实现。"));
    }else if(method==1){
        const auto status=nr_regions::status.load(std::memory_order_acquire);
        const auto color=SnapshotColorPolicy();
        const bool colour_disabled=next.color_strength<=0.f ||
            (!has_pending&&!changed&&color.status==NrColorPolicyStatus::Current&&!color.policy.color_allowed);
        SettingsNote(status==3?NrUi("Basis: image edges, depth and specular guidance.","依据：图像边缘、深度与镜面导引。"):
            status==2?NrUi("Basis: image edges and depth; no specular guide.","依据：图像边缘与深度；无镜面导引。"):
            status==1?NrUi("Basis: image edges; engine guides unavailable.","依据：图像边缘；引擎导引不可用。"):
            status==4?NrUi("Region resources unavailable; original composition retained.","未生效：分区资源不可用。"):
            NrUi("Waiting for a supported frame; guide availability is unknown.","准备中：等待可用画面。"));
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
        if(status!=3)SettingsNote(NrUi("Specular controls are unavailable without a verified specular guide.","镜面调节未生效：缺少导引。"));
        ImGui::BeginDisabled(colour_disabled);
        changed|=LightStylePercent(NrUi("Colour-only edits","纯颜色改动"),"##region-pure",next.region_gains[10],4.f);ImGui::EndDisabled();
        if(colour_disabled)SettingsNote(NrUi("AI colour is zero or disallowed; regional colour controls are inactive.","区域颜色未生效：AI 色彩受限。"));
        SettingsHelp(NrUi("These are regions of NR changes, not independent game lighting layers.","这里只区分 NR 改动区域，不是游戏的独立光照层。"));
    }else{
        const char* names[]={NrUi("Total edit gain","总改动倍率"),NrUi("Brightening edits","增亮修改"),NrUi("Darkening edits","压暗修改"),NrUi("Saturation edits","饱和度修改"),NrUi("Lightness edits","明度修改")};
        for(unsigned i=0;i<5;++i){ImGui::PushID(static_cast<int>(i));changed|=SettingsSliderFloat(names[i],"##edit-gain",&next.generic_gains[i],i?0.f:1.f,2.f,"%.2fx");ImGui::PopID();}
        SettingsNote(NrUi("Only NR-added colour changes.","只调整 NR 新增颜色。"));
        SettingsHelp(NrUi("This scales NR's added colour, not whole-image saturation. All 1x preserves the current edit.",
            "仅缩放 NR 新增的颜色变化，不改变全画面饱和度；全部设为 1 倍会保留当前修改。"));
    }
    // Legacy algorithms remain visible only while requested, in the same page
    // as the controls with which they can combine. No hidden cross-page gain.
    if(nr_unified_ui::LegacyLightActive(next)) {
        SettingsSection(NrUi("Saved legacy lighting combination","已保存的旧版光影组合"));
        SettingsNote(NrUi("Additional legacy settings are enabled. Values are retained until you explicitly leave this combination.","旧光影组合仍开启。"));
        if(next.light_control_mode==nr_light_style::DetailLayers){
            SettingsNote(method==0?NrUi("Ordinary sliders currently affect fine edits only.","普通亮暗的主滑条当前只影响细节。"):
                NrUi("Saved fine-only scope is inactive in this method.","保存的仅细节范围不参与当前方式。"));
        }
        if(next.layered_enhancement){
            const bool supported=before_sr||next.independent_chroma_composition;
            ImGui::BeginDisabled(!supported);
            changed|=SettingsSliderFloat(NrUi("Broad tone multiplier","大范围明暗倍率"),"##component-tone",&next.layer_tone,0.f,2.f,"%.2fx");
            SettingsTooltip(NrUi("Old decomposition: scales broad bright and dark edits together. Its classification differs from ordinary lighting.","旧分解算法同时调整大范围增亮和压暗，其分区计算与普通亮暗不同。"));
            changed|=SettingsSliderFloat(NrUi("Fine detail multiplier","明暗细节倍率"),"##component-structure",&next.layer_structure,0.f,2.f,"%.2fx");
            SettingsTooltip(NrUi("Scales fine light/dark edits; not the model's Local structure parameter. No extra NR evaluation.","调整细小明暗变化，不是模型的局部结构参数，不增加 NR 推理次数。"));
            changed|=SettingsSliderFloat(NrUi("Legacy brightening multiplier","旧分项增亮倍率"),"##component-bright",&next.layer_bright,0.f,2.f,"%.2fx");
            SettingsTooltip(NrUi("Multiplies both broad and fine positive edits after the old split. Can compound with the selected lighting method.","在旧分解基础上同时放大大范围与细节增亮，可能与当前光影方式继续叠加。"));
            changed|=SettingsSliderFloat(NrUi("Legacy darkening multiplier","旧分项压暗倍率"),"##component-dark",&next.layer_shadow,0.f,2.f,"%.2fx");
            SettingsTooltip(NrUi("Multiplies both broad and fine negative edits after the old split. Additional limits still apply.","在旧分解基础上同时放大大范围与细节压暗，最终仍受适用保护和上限约束。"));
            ImGui::EndDisabled();
            if(!supported)SettingsNote(NrUi("Legacy component gains are not used by this route; their values are retained.","旧倍率未生效：当前路线不支持。"));
        }
        if(ImGui::Button(NrUi("Use only unified lighting","只使用统一光影控制"))){
            nr_unified_ui::UseUnifiedLight(next);changed=true;discrete=true;
        }
        SettingsTooltip(NrUi("Disables legacy component gains and returns ordinary sliders to all light/dark edits. Keeps their saved amounts, model settings and colour. May change the picture; Undo lighting restores the previous combination.","关闭旧分项倍率，并将普通滑条切回全部亮暗；保留旧倍率数值、当前方式、模型与颜色。画面可能改变，可撤销光影修改恢复原组合。"));
    }
    if(method==0||undo.Available(generation)){
        ImGui::BeginDisabled(has_pending||changed||!undo.Available(generation));
        if(ImGui::Button(NrUi("Undo lighting edit","撤销光影修改"))&&undo.Restore(next,generation)){pending_undo=true;changed=true;discrete=true;}
        ImGui::EndDisabled();
        SettingsTooltip(NrUi("Restores the last ordinary/legacy lighting edit only, including a legacy-combination exit. Later unrelated settings invalidate undo; AI colour is never restored by this action.","仅恢复最近一次普通/旧版光影修改，包含退出旧组合。之后修改其他设置会使撤销失效；不会回写 AI 色彩。"));
    }
    if(next.light_preview||next.region_preview||next.face_preview){
        if(ImGui::Button(NrUi("Exit all previews","退出所有预览"))){next.light_preview=next.region_preview=next.face_preview=0;changed=true;discrete=true;}
    }
    SettingsSection(NrUi("Shared controls","公共设置"));
    changed|=SettingsSliderFloat(NrUi("Preserve original lighting","保留原始光照"),"##lighting-lock",&next.lighting_lock,0.f,1.f,"%.2f");
    SettingsTooltip(NrUi("Reduces AI-added light changes. Does not reduce model strength, inference count or work size.","减少 AI 新增明暗，不降低模型推理强度、次数或工作尺寸。"));
    ImGui::BeginDisabled(!before_sr);
    changed|=SettingsSliderFloat(NrUi("Edge protection","边缘保护"),"##edge-protection",&next.edge_guard,0.f,1.f,"%.2f");ImGui::EndDisabled();
    SettingsTooltip(NrUi("Attenuates edits near source edges; not a halo-removal filter.","减弱原图边缘附近的修改，不是抗光晕滤镜。"));
    if(!before_sr)SettingsNote(NrUi("Edge protection is supported on the pre-SR route; its saved value is retained.","边缘保护未生效：仅支持前置。"));
    const auto sample=SnapshotColorPolicy();
    const auto reason=sample.policy.color_reason;
    const bool hdr_mapping=sample.config_generation==generation&&sample.status==NrColorPolicyStatus::Current&&
        (reason==NrColorReason::PostSrFp16UnitExposure||reason==NrColorReason::UnverifiedHdr||reason==NrColorReason::ExperimentalUnverifiedHdr||reason==NrColorReason::ColourLabPriority);
    ImGui::BeginDisabled(!hdr_mapping);float white=1.f/next.proxy_scale;
    if(SettingsSliderFloat(NrUi("Reference white","参考白亮度"),"##reference-white",&white,.5f,4.f,"%.2fx")){next.proxy_scale=1.f/white;changed=true;}ImGui::EndDisabled();
    SettingsTooltip(NrUi("Changes the HDR input seen by NR through the existing ProxyScale, once. Does not change display brightness or external HDR plugins.","通过现有 ProxyScale 一次调整模型所见 HDR 输入，不改显示器或外部 HDR 插件。"));
    if(!hdr_mapping)SettingsNote(NrUi("No current matching HDR-mapped input is confirmed; reference white is unavailable.","参考白未生效：HDR 映射待确认。"));
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

#if defined(NR_HAS_TEMPORAL_RUNTIME)
#include "nr_temporal_ui.inl"
#if defined(NR_HAS_PRIVATE_RESIDUAL_SR)
#include "nr_private_sr_ui.inl"
#include "nr_spatial_experiment_ui.inl"
#endif
#endif
#include "nr_reconstruction_ui.inl"
void DrawUnifiedExperiments(ChainConfig& next,unsigned generation,bool pending,bool& changed,bool& discrete) {
    DrawReconstructionControls(next,generation,pending,changed,discrete);
    DrawSpatialExperiment(next,changed,discrete);
    SettingsSection(NrUi("Colour comparisons","颜色对照"));
    SettingsField(NrUi("Model input","模型输入"));
    unsigned curve=next.colour_input_curve==1?1u:0u;
    const char* input_labels[]={NrUi("Original","原输入"),NrUi("N2 (test)","N2（实验）")};
    const char* input_tips[]={
        NrUi("Uses the original D21 input mapping. Switching input mapping resets model history; it does not change the recovery choice.",
             "使用原有 D21 模型输入映射。切换输入映射会重置模型历史，不会联动恢复方式。"),
        NrUi("Uses the experimental N2 input mapping and resets model history. For comparison; it is not a verified quality upgrade.",
             "改用实验性的 N2 输入映射，并重置模型历史。用于对照，尚未证明画质更好。")};
    if(NrChoice("##experiment-input",curve,input_labels,2,input_tips)){
        next.colour_input_curve=static_cast<int>(curve);changed=true;discrete=true;
    }
    SettingsField(NrUi("Recovery","恢复方式"));
    unsigned restore=static_cast<unsigned>(nr_colour_restore::Normalize(next.colour_restore_mode));
    const char* recovery_labels[]={NrUi("Default","默认合成"),NrUi("Protected RGB","保护 RGB"),NrUi("Raw RGB","原始 RGB")};
    const char* recovery_tips[]={
        NrUi("Uses the original D21 output composition. Keeps the selected model input and its history.",
             "使用原有 D21 输出合成方式，保留所选模型输入及其历史。"),
        NrUi("Compares protected RGB recovery. Halo regressions have been reported; this is not a recommended upgrade. Does not change model input/history.",
             "对照带保护的 RGB 恢复。已有亮边回归反馈，不代表推荐升级；不改变模型输入及其历史。"),
        NrUi("Diagnostic raw RGB recovery bypasses artistic attenuation, while GPU safety remains active. Not the normal final picture; it does not change model input/history.",
             "诊断用原始 RGB 恢复，绕过审美衰减，仍保留 GPU 安全检查。不代表正常最终画面；不改变模型输入及其历史。")};
    if(NrChoice("##experiment-restore",restore,recovery_labels,3,recovery_tips)){
        next.colour_restore_mode=static_cast<int>(restore);changed=true;discrete=true;
    }
    DrawUnifiedColourState(next,generation,pending||changed);
    const auto sample=SnapshotColorPolicy();
    if(!pending&&!changed&&sample.config_generation==generation&&sample.status==NrColorPolicyStatus::Current){
        ImGui::TextWrapped(NrUi("Recorded: %s / %s","已录制：%s／%s"),nr_colour_restore::CurveName(sample.policy.recorded_input_curve),nr_colour_restore::Name(sample.policy.recorded_restore_mode));
        if(next.colour_input_curve!=sample.policy.recorded_input_curve)SettingsNote(NrUi("The selected input curve was not applied to the recorded frame.","输入曲线未生效：本帧未采用。"));
    }
    nr_face_runtime::DrawUi(next,changed);
#if defined(NR_HAS_TEMPORAL_RUNTIME)
    DrawTemporalOptions(next,changed,discrete);
#endif
#if defined(NR_HAS_PRIVATE_RESIDUAL_SR)
    DrawPrivateResidualSr(next,changed,discrete);
#endif
    DrawEnergyGateOptions();
    SettingsSection(NrUi("RR compatibility candidate","游戏 RR 后增强"));
    bool missing=nr_post_settings::missing_projection.load(std::memory_order_acquire);
    if(ImGui::Checkbox(NrUi("Try NR after RR###Try post NR without a camera projection","RR 后缺投影时尝试 NR###RR 缺投影时尝试后置 NR"),&missing))
        nr_post_settings::RequestMissingProjection(missing);
    if(nr_post_settings::missing_projection_save_failed.load(std::memory_order_acquire))
        SettingsNote(NrUi("Could not save the RR experiment. The session keeps your request; check F8.","RR 实验未保存：本次仍使用。"));
    SettingsTooltip(NrUi("Try NR when camera projection is missing. Resets NR history each frame and may flicker. Unsupported inputs keep native RR; your saved choice is retained.",
        "缺少相机投影时尝试 NR；每帧重置历史，可能增加闪烁。输入不支持时保留原生 RR，沿用已保存的开关。"));
    ImGui::TextWrapped(NrUi("Projection: %s. Conversion / placeholder recordings: %llu / %llu.",
        "投影：%s。转换／占位录制：%llu／%llu。"),
        nr_rr_depth::Name(nr_rr_depth::observations.input.load(std::memory_order_relaxed)),
        g_rr_depth_conversions.load(std::memory_order_relaxed),
        nr_rr_depth::observations.placeholder_recorded.load(std::memory_order_relaxed));
    SettingsTooltip(NrUi("Recording counts do not prove NR use. Check F8 for adoption details.",
        "计数只表示录制次数，实际是否采用 NR 请查看 F8。"));
}

void DrawUnifiedDeveloper(ChainConfig& next,bool before_sr,bool& changed,bool& discrete,reshade::api::effect_runtime* runtime=nullptr) {
#if defined(NR_HAS_TEMPORAL_RUNTIME)
    SettingsSection(NrUi("Temporal comparison","时域对照"));
    if(ImGui::Checkbox(NrUi("Legacy fixed residual weight 0.5","残差采用旧固定权重 0.5"),&next.temporal_legacy)){changed=discrete=true;}
    SettingsNote(NrUi("Default uses real source intervals: reliability 0.65, half-life parameter 60 ms; gaps above 250 ms reseed.",
        "默认按真实源帧间隔计算：可靠性 0.65、半衰期参数 60ms；超过 250ms 重新建立历史。"));
#endif
    SettingsSection(NrUi("Input mechanisms","输入机制"));
    ImGui::BeginDisabled(!before_sr);
    int filter=static_cast<int>(next.nr_input_filter);
    if(NrCombo(NrUi("Input filter","输入滤波"),"##input-filter",filter,NrUi("Exact Area\0Lanczos-2 AA (experimental)\0","精确面积\0Lanczos-2 抗混叠（实验）\0"))){next.nr_input_filter=static_cast<unsigned>(filter);changed=true;discrete=true;}
    ImGui::EndDisabled();
    if(!before_sr)SettingsNote(NrUi("Input filtering applies only before SR; value retained.","输入滤波未生效：仅支持前置。"));
    int curve=static_cast<int>(next.proxy_curve);
    if(NrCombo(NrUi("HDR mapping","HDR 映射"),"##hdr-map",curve,NrUi("Legacy shoulder\0Hybrid\0Reversible (experimental)\0","柔和压缩\0混合压缩\0可逆压缩（实验）\0"))){next.proxy_curve=static_cast<unsigned>(curve);changed=true;discrete=true;}
    changed|=ImGui::Checkbox(NrUi("Highlight mapping","高光映射"),&next.experimental_peak_shoulder);
    SettingsNote(NrUi("HDR mappings affect only applicable model inputs; they do not enable game HDR.","HDR 映射只影响适用的模型输入，不会开启游戏 HDR。"));
    int jitter=static_cast<int>(next.jitter_mode);
    if(NrCombo(NrUi("Jitter input","Jitter 输入"),"##jitter",jitter,NrUi("Off\0Verified game pixel jitter\0","不传递\0已验证的游戏像素抖动\0"))){next.jitter_mode=static_cast<NrJitterMode>(jitter);changed=true;discrete=true;}
    SettingsTooltip(NrUi("Experimental. Changes NR history, not the game's SR jitter; binding does not prove the model uses it.","实验选项；改变 NR 历史，不改游戏 SR 抖动。参数写入不等于模型一定采用。"));
    SettingsSection(NrUi("Shortcuts","快捷键"));
    const char* shortcut_names[]={NrUi("Toggle NR","启用／关闭 NR"),NrUi("Capture current view","采集当前画面"),NrUi("Text report","生成文字报告"),NrUi("Save screenshot","保存截图")};
    static bool shortcut_conflict=false;
    int shortcut_preset=static_cast<int>(nr_hotkeys::PresetIndex(next));
    if(NrCombo(NrUi("Shortcut preset","快捷键预设"),"##shortcut-preset",shortcut_preset,
        NrUi("Custom (retained)\0Default: F6 / F7 / F8 / Minus\0F9 / F10 / F11 / Minus\0ScrollLock / Pause / PageDown / Minus\0All off\0",
             "自定义（保留）\0默认：F6 / F7 / F8 / 减号\0F9 / F10 / F11 / 减号\0ScrollLock / Pause / PageDown / 减号\0全部关闭\0")) && shortcut_preset>0){
        if(nr_hotkeys::ApplyPreset(next,static_cast<unsigned>(shortcut_preset-1))){shortcut_conflict=false;changed=true;discrete=true;}
    }
    SettingsTooltip(NrUi("Applies the four actions in the order shown below, only when you select a preset. Custom saved bindings are kept until you edit them. All off also disables both minus screenshot keys; buttons on Feedback still work.",
        "仅点选预设时，按下方四项顺序一起应用；未点选时保留已保存绑定。全部关闭也停用主键盘和小键盘的减号截图，反馈页按钮仍可使用。"));
    const unsigned shortcut_values[]={next.master_virtual_key,next.image_virtual_key,next.feedback_virtual_key,next.screenshot_virtual_key};
    for(unsigned i=0;i<nr_hotkeys::Count;++i){
        ImGui::PushID(static_cast<int>(i));
        SettingsField(shortcut_names[i]);
        if(ImGui::BeginCombo("##shortcut",nr_hotkeys::Name(shortcut_values[i],UseChineseUi()))){
            for(const auto& choice:nr_hotkeys::Choices){
                if(!nr_hotkeys::Allowed(static_cast<nr_hotkeys::Action>(i),choice.key))continue;
                if(ImGui::Selectable(UseChineseUi()?choice.zh:choice.en,shortcut_values[i]==choice.key)){
                    shortcut_conflict=!nr_hotkeys::Set(next,static_cast<nr_hotkeys::Action>(i),choice.key);
                    if(!shortcut_conflict){changed=true;discrete=true;}
                }
            }
            ImGui::EndCombo();
        }
        SettingsTooltip(NrUi("Off disables this action only. Release a rebound key before pressing. Minus also covers keypad minus. Known ReShade overlaps are shown, including modifier bindings. Game bindings are not scanned; Home is often a menu key.",
            "关闭只停用此快捷键；改键后松开再按。减号截图也兼容小键盘减号。会提示已知 ReShade 同键绑定，含组合键；不扫描游戏绑定，Home 常用于菜单。"));
        ImGui::PopID();
    }
    if(shortcut_conflict)SettingsNote(NrUi("Key already assigned. Choose another key or disable its existing action first.","此按键已有用途，请换一个，或先关闭原绑定。"));
    DrawKnownReShadeHotkeyConflicts(runtime,next);
    if(ImGui::Button(NrUi("Reset shortcuts","恢复默认快捷键"))){nr_hotkeys::ApplyPreset(next,0);shortcut_conflict=false;changed=true;discrete=true;}
    SettingsNote(NrUi("Each action can be turned off. Defaults: F6 NR, F7 current-view capture, F8 text report, Minus screenshot.",
        "每项均可关闭。默认 F6 开关 NR、F7 采集当前画面、F8 文字报告、减号保存截图。"));
    SettingsSection(NrUi("Recovery and composition","恢复与合成"));
    int order=nr_colour_restore::NormalizeScaledProxy(next.colour_scaled_proxy_mode);
    if(NrCombo(NrUi("Low-resolution input order","低分输入顺序"),"##scaled-proxy",order,NrUi("Scale source first\0Encode proxy first (no proven gain)\0","先缩放场景\0先编码 Proxy（未证实收益）\0"))){next.colour_scaled_proxy_mode=order;changed=true;discrete=true;}
    int edge=nr_colour_edge::Normalize(next.colour_edge_recovery);
    if(NrCombo(NrUi("Low-resolution RGB sampling","低分 RGB 恢复取样"),"##paired-edge",edge,NrUi("Standard interpolation (D21)\0Edge-guided pair (experimental)\0","普通插值（D21）\0边缘引导配对（实验）\0"))){next.colour_edge_recovery=edge;changed=true;discrete=true;}
    SettingsTooltip(NrUi("D21 interpolates nearby samples. The experiment uses identical nonnegative source-guided weights for model input/output to reduce cross-edge mixing; it may weaken edits. This is not dual-NR HDR or temporal denoising; equal-size input keeps ordinary sampling.","D21 使用附近像素插值；实验按原图为模型输入和输出使用相同非负权重，尝试减少跨边缘混色，也可能减弱增强。不是双层 HDR 开关或时域防闪；同尺寸时保留普通取样。"));
    if(edge&&next.colour_restore_mode==0&&!next.colour_neutral_probe)SettingsNote(NrUi("D21 composition does not use paired recovery.","配对恢复未生效：D21 合成。"));
    changed|=ImGui::Checkbox(NrUi("Independent chroma","独立色度"),&next.independent_chroma_composition);
    changed|=ImGui::Checkbox(NrUi("Low-grid luminance field","低分亮度场"),&next.low_grid_edit_field);
    changed|=ImGui::Checkbox(NrUi("Low-grid colour field","低分颜色场"),&next.low_grid_color_edit_field);
    const bool catmull=next.low_grid_edit_field&&!next.low_grid_color_edit_field&&next.color_strength<=0.000001f;
    ImGui::BeginDisabled(!catmull);changed|=ImGui::Checkbox(NrUi("Catmull-Rom luminance restore","Catmull-Rom 亮度恢复"),&next.catmull_edit_restore);ImGui::EndDisabled();
    if(!catmull)SettingsNote(NrUi("Catmull-Rom requires the luminance field, colour field off and AI colour zero.","Catmull-Rom 需亮度场开启、颜色场关闭且 AI 色彩为零。"));
    changed|=ImGui::Checkbox(NrUi("N=P zero-edit diagnostic","N=P 零修改诊断"),&next.colour_neutral_probe);
    changed|=ImGui::Checkbox(NrUi("Run NR but discard its result","运行 NR 但丢弃结果"),&next.identity_use_original);
    SettingsNote(NrUi("These are recovery experiments. A neutral diagnostic is not a quality/performance comparison; low-grid paths can add passes and storage.","恢复实验仍待画质验证。"));
    SettingsSection(NrUi("Safeguards","保护边界"));
    changed|=ImGui::Checkbox(NrUi("Limit extra enhancement","额外增强软限制"),&next.high_strength_protection);
    SettingsTooltip(NrUi("Soft-limits extra luminance amplification above the base amount; legacy composition also limits extra chroma. It does not guarantee that multi-layer NR at 1x cannot be oversaturated. Existing numeric/gamut safety remains.","软限制超过原量的额外明暗放大，旧合成也限制额外颜色放大；不保证最终增强为 1 时多层本身不会过艳。数值与色域安全仍保留。"));
    changed|=ImGui::Checkbox(NrUi("Keep extra enhancement from boosting colour","防止额外增强放大颜色"),&next.color_protection);
    SettingsTooltip(NrUi("Prevents the final enhancement gain from multiplying colour again in coupled composition. Independent colour consumers do not divide by that gain. This is not a general saturation limiter and does not change HDR permission.","在明暗与颜色耦合的合成中，避免最终增强重复放大颜色；独立颜色分支不再除以该增益。不是全局饱和度上限，也不改变 HDR 许可。"));
    changed|=SettingsSliderFloat(NrUi("Luminance ratio limit","明暗变化上限"),"##luma-limit",&next.max_luma_ratio,1.f,8.f,"%.2f");
    ImGui::BeginDisabled(!before_sr);
    changed|=SettingsSliderFloat(NrUi("Detail stability","细节稳定保护"),"##detail-stability",&next.detail_stability,0.f,1.f,"%.2f");ImGui::EndDisabled();
    if(!before_sr)SettingsNote(NrUi("Detail stability applies before SR only.","细节稳定保护仅用于前置。"));
    SettingsSection(NrUi("Compatibility and recording","兼容与记录"));
    int motion=static_cast<int>(next.present_mv_compensation);
    if(NrCombo(NrUi("Compatibility motion-unit conversion","兼容入口运动量换算"),"##motion-compensation",motion,NrUi("Conservative automatic\0Force compensation\0","保守自动\0强制补偿\0"))){next.present_mv_compensation=static_cast<unsigned>(motion);changed=true;discrete=true;}
    SettingsTooltip(NrUi("Compatibility input only: forced mode rebases existing motion scales from the captured image to the model raster. It neither creates motion nor selects native MV, AMDOF or NVOF. Conservative keeps established units. Changing it rebuilds this compatibility context/history; do not force it merely because sizes differ.","仅兼容入口：强制时将已有运动量从捕获画面尺寸换算到模型尺寸，不生成运动，也不选择原生 MV、AMDOF 或 NVOF。保守模式保留现有单位。切换会更新兼容实例和历史，不能仅因尺寸不同就认定需要强制。"));
    changed|=ImGui::Checkbox(NrUi("Low-overhead descriptor recording (default on)","低开销描述符记录（默认开启）"),&next.descriptor_low_overhead);
    SettingsNote(next.descriptor_low_overhead!=NrDescriptorLowOverheadActive()?NrUi("Restart required to apply this descriptor setting.","描述符设置需重启游戏生效。"):
        NrDescriptorLowOverheadActive()?NrUi("Active: low-overhead recording.","实际：低开销记录。"):
        NrUi("Active: full diagnostic recording; higher resident overhead.","实际：完整诊断记录，常驻开销更高。"));
    DrawRenderControl();
    int warmup=static_cast<int>(next.warmup_after_create);
    if(SettingsSliderInt(NrUi("Warm-up frames","预热帧数"),"##warmup",&warmup,0,30,"%d")){next.warmup_after_create=static_cast<unsigned>(warmup);changed=true;}
}

void DrawUnifiedFeedback(reshade::api::effect_runtime* runtime,const ChainConfig& config,bool pending) {
#if NR_HAS_ACCEPTANCE
    nr_acceptance::Draw(runtime,pending);
    ImGui::BeginDisabled(nr_acceptance::Active());
#endif
    const bool busy=nr_f8::IsActive()||nr_f8::g_status.load()==nr_f8::Status::Packaging||nr_core_feedback::Busy();
    ImGui::BeginDisabled(busy);
    const bool row=SettingsContentWidth()>ImGui::GetFontSize()*42.f;
    char report_label[96]{},image_label[96]{};
    std::snprintf(report_label,sizeof(report_label),NrUi("Text report (%s)###report","生成文字报告（%s）###report"),nr_hotkeys::Name(nr_hotkeys::Key(nr_hotkeys::Report),UseChineseUi()));
    std::snprintf(image_label,sizeof(image_label),NrUi("Capture current view (%s)###capture","采集当前画面（%s）###capture"),nr_hotkeys::Name(nr_hotkeys::Key(nr_hotkeys::Image),UseChineseUi()));
    if(ImGui::Button(report_label))nr_f8::RequestCore();
    SettingsTooltip(NrUi("Bounded TXT; settings unchanged; path copied when ready.","生成有界 TXT，不改画质设置；完成后复制路径。"));
    if(row)ImGui::SameLine();
    if(ImGui::Button(image_label))nr_f8::RequestFullscreen();
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
    SettingsNote(NrUi("Captures centre model stages; affects history and timing.","采集中央模型阶段；会影响历史和计时。"));
    SettingsHelp(NrUi("Captures S/P/real N/E with a report and resets/warms history. Pre-SR E is not native SR output; compatibility captures available current-path layers.",
        "采集中央原图、P、真实 N、E 和报告，并重置／预热历史。前置 E 不是原生 SR 输出；兼容入口采集当前路径可用层。"));
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
    SettingsNote(NrUi("Change or disable shortcuts on Developer. Deep comparisons may reset history and temporarily change parameters; completion/cancel restores saved values.",
        "可在开发者页修改或关闭快捷键。深度对照可能重置历史、临时改变参数；完成或取消后恢复。"));
#if NR_HAS_ACCEPTANCE
    ImGui::EndDisabled();
#endif
}
