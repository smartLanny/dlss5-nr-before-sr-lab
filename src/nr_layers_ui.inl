// One layer-count entry; disabled layers retain their complete parameter set.
#include "nr_unified_ui_policy.h"

const char* NrNetworkUiName(int request,bool zh) {
    switch(request) {
    case nr_network_policy::Inherit:return zh?"跟随全局":"Follow global";
    case nr_network_policy::RuntimeDefault:return zh?"运行库默认":"Runtime default";
    case nr_network_policy::Original:return zh?"原始网络（Original）":"Original / HT";
    case nr_network_policy::Optimized:return zh?"优化网络（Small）":"Optimized / Small";
    default:return zh?"未识别的已保存值":"Unrecognized saved value";
    }
}

bool DrawNrNetworkChoice(const char* id,int& request,bool allow_inherit) {
    const bool zh=UseChineseUi();
    const bool known=allow_inherit?nr_network_policy::IsValidOverride(request):
        nr_network_policy::IsValidRequest(request);
    const int offset=allow_inherit?1:0,unknown_index=allow_inherit?4:3;
    int selected=known?request+offset:unknown_index;
    const char* choices=allow_inherit?
        (zh?(known?"跟随全局\0运行库默认\0原始网络（Original）\0优化网络（Small）\0":
                    "跟随全局\0运行库默认\0原始网络（Original）\0优化网络（Small）\0未识别的已保存值\0"):
             (known?"Follow global\0Runtime default\0Original / HT\0Optimized / Small\0":
                    "Follow global\0Runtime default\0Original / HT\0Optimized / Small\0Unrecognized saved value\0")):
        (zh?(known?"运行库默认\0原始网络（Original）\0优化网络（Small）\0":
                    "运行库默认\0原始网络（Original）\0优化网络（Small）\0未识别的已保存值\0"):
             (known?"Runtime default\0Original / HT\0Optimized / Small\0":
                    "Runtime default\0Original / HT\0Optimized / Small\0Unrecognized saved value\0"));
    if(!ImGui::Combo(id,&selected,choices)||selected<0||selected>=unknown_index)return false;
    const int chosen=selected-offset;
    if(request==chosen)return false;
    request=chosen;
    return true;
}

void DrawNrNetworkRuntimeInfo(const ChainConfig& config) {
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
    const bool zh=UseChineseUi();
    const auto runtime=NrNetworkRuntimeSnapshot();
    if(runtime.module_address&&runtime.version.known)
        ImGui::TextDisabled(zh?"当前运行库：%u.%u.%u.%u":"Loaded runtime: %u.%u.%u.%u",
            runtime.version.value[0],runtime.version.value[1],runtime.version.value[2],runtime.version.value[3]);
    const unsigned count=nr_layers::Active(config.nr_layers);
    unsigned invalid=count,unavailable=count,fallback=count;
    bool explicit_request=false;
    for(unsigned i=0;i<count;++i) {
        const int request=nr_layers::RequestedNetwork(config,i);
        const auto state=nr_network_policy::Assess(request,runtime.family,runtime.evidence);
        explicit_request|=request!=nr_network_policy::RuntimeDefault;
        if(!state.valid&&invalid==count)invalid=i;
        if(state.unavailable&&unavailable==count)unavailable=i;
        if(state.possible_fallback&&fallback==count)fallback=i;
    }
    if(invalid<count)
        ImGui::TextWrapped(zh?"第 %u 层的网络编号未识别，无法采用。请求已保留；请重新选择并按 F8 反馈。":
            "Layer %u has an unrecognized network ID and cannot use it. The request is retained; choose a network and save F8 feedback.",invalid+1);
    else if(!runtime.module_address)
        SettingsNote(zh?"网络请求已保留；运行库加载后显示支持信息。":
            "Network request retained; support information appears after the runtime loads.");
    else if(unavailable<count)
        ImGui::TextWrapped(zh?"第 %u 层请求 Small，但已核对的运行库只有 Original。请求已保留；请选择 Original，或更换支持 Small 的运行库后重启。请按 F8 反馈。":
            "Layer %u requests Small, but the identified runtime contains Original only. Request retained; choose Original, or install a Small-capable runtime and restart. Save F8 feedback.",unavailable+1);
    else if(fallback<count)
        ImGui::TextWrapped(zh?"第 %u 层请求 Small；当前版本信息提示可能仍使用 Original，尚未确认。切换无效请按 F8 反馈。":
            "Layer %u requests Small; version information suggests it may still use Original. This is unconfirmed; save F8 if switching has no effect.",fallback+1);
    else if(explicit_request&&runtime.family==nr_network_policy::Family::Unknown)
        SettingsNote(zh?"此运行库对所选网络的支持尚未确认。请求已保留；切换无效请按 F8 反馈。":
            "This runtime's support for the selected network is unconfirmed. Request retained; save F8 if switching has no effect.");
    SettingsTooltip(zh?"这里显示网络请求与运行库信息。是否按预期切换，还需结合反馈记录和画面对照确认。":
        "These are network requests and runtime information. Confirm the intended switch with feedback records and a picture comparison.");
#else
    (void)config;
#endif
}

void DrawNrNetworkControls(ChainConfig& next,bool& changed,bool& discrete) {
    const bool zh=UseChineseUi();
    SettingsField(zh?"全局模型网络":"Global model network");
    if(DrawNrNetworkChoice("##nr-network-global",next.nr_network_preset,false)) {changed=true;discrete=true;}
    SettingsTooltip(zh?
        "供选择‘跟随全局’的层使用。运行库默认由当前 DLL 决定，升级 DLL 后可能改变；要固定原始网络请选择 Original。Small 是 Optimized 网络。切换会保留分辨率、模型强度和调色设置，重建必要实例时可能短暂停顿。":
        "Used by layers set to Follow global. Runtime default can change with a DLL upgrade; choose Original to pin the original network. Small is the Optimized network. Switching keeps work size, model strength and colour settings; rebuilding required instances may pause briefly.");
    if(!nr_network_policy::IsValidRequest(next.nr_network_preset))
        ImGui::TextDisabled(zh?"保留的全局网络编号：%d":"Retained global network ID: %d",next.nr_network_preset);
    DrawNrNetworkRuntimeInfo(next);
    const auto count=nr_layers::Active(next.nr_layers);
    bool different_requests=false;
    for(unsigned i=1;i<count;++i)
        different_requests|=nr_layers::RequestedNetwork(next,i)!=nr_layers::RequestedNetwork(next,0);
    if(different_requests)SettingsNote(zh?
        "各层网络请求不同；混搭尚待游戏验证，可先让所有层跟随全局作对照。":
        "Layers request different networks; mixed networks need game validation. Compare with all layers following global first.");
}

void DrawUniformLayerCount(ChainConfig& next,bool& changed,bool& discrete) {
    const bool zh=UseChineseUi();
    SettingsField(zh?"模型层数":"Model layers");
    ImGui::PushID("nr-layer-count");
    const float width=std::max(1.f,(SettingsContentWidth()-4.f*ImGui::GetStyle().ItemSpacing.x)/5.f);
    const auto active=nr_layers::Active(next.nr_layers);
    for(unsigned count=1;count<=nr_layers::Count;++count) {
        if(count>1)ImGui::SameLine();
        char label[8];std::snprintf(label,sizeof(label),"%u",count);
        if(active==count)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_Header));
        if(ImGui::Button(label,ImVec2(width,0))&&active!=count){nr_unified_ui::SetLayers(next,count);changed=true;discrete=true;}
        if(active==count)ImGui::PopStyleColor();
    }
    ImGui::PopID();
}
void NrLayerField(const char* label,bool table) {
    if(table){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::AlignTextToFramePadding();}
    ImGui::TextWrapped("%s",label);
    if(table)ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.f);
}
void DrawUniformLayerControls(ChainConfig& next,bool& changed,bool& discrete) {
    const bool zh=UseChineseUi();
    SettingsSection(zh?"模型与层数":"Model parameters per layer");
    SettingsTooltip(zh?"各层共用工作尺寸，可独立选择网络与模型参数；松手或提交输入后应用。减少层数会保留后续层设置。":"Layers share one work size, with independent networks and model controls. Apply on release or input commit. Reducing layers retains their settings.");
    if(next.nr_uniform_migrated)SettingsNote(zh?"当前层数设置为自定义。":"Legacy auxiliary scales are archived. Uniform sizing changes their workload; this is a custom configuration.");
    const auto count=nr_layers::Active(next.nr_layers);
    for(unsigned i=0;i<count;++i) {
        ImGui::PushID(static_cast<int>(i));
        char title[64];std::snprintf(title,sizeof(title),zh?"第 %u 层":"Layer %u",i+1);
        SettingsSection(title);
        auto model=nr_layers::Read(next,i);bool edited=false;
        const bool table=SettingsContentWidth()>=ImGui::GetFontSize()*25.f &&
            ImGui::BeginTable("layer-fields",2,ImGuiTableFlags_SizingStretchProp);
        if(table){ImGui::TableSetupColumn("name",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*9.f);ImGui::TableSetupColumn("value",ImGuiTableColumnFlags_WidthStretch);}
        NrLayerField(zh?"模型网络":"Model network",table);
        if(DrawNrNetworkChoice("##nr-layer-network",model.network_preset,true)){edited=true;discrete=true;}
        SettingsTooltip(zh?
            "跟随全局会采用上方选择；运行库默认则由本层单独交给 DLL 决定。修改只重建必要的模型并重置相关历史，不自动改变本层强度或最终调色。":
            "Follow global uses the choice above. Runtime default delegates this layer independently to the DLL. Changes rebuild required models and reset affected history, while preserving this layer's strength and final colour controls.");
        const int requested=nr_network_policy::Resolve(next.nr_network_preset,model.network_preset);
        ImGui::TextDisabled(zh?"本层请求：%s":"Layer request: %s",NrNetworkUiName(
            nr_network_policy::IsValidRequest(requested)?requested:nr_network_policy::Unknown,zh));
        if(!nr_network_policy::IsValidOverride(model.network_preset))
            ImGui::TextDisabled(zh?"保留的网络编号：%d":"Retained network ID: %d",model.network_preset);
        NrLayerField(zh?"风格":"Style",table);
        const char* stylesEn[]={"Default","Natural","Cinematic"};
        const char* stylesZh[]={"默认","自然","电影"};
        // Keep drag preview separate from the config transaction: crossing
        // segments must not recreate models until the mouse is released.
        static int drag_layer=-1;
        static unsigned drag_style=0;
        const float segment_width=std::max(1.f,(SettingsContentWidth()-2.f*ImGui::GetStyle().ItemSpacing.x)/3.f);
        for(unsigned style=0;style<3;++style) {
            if(style)ImGui::SameLine();
            const unsigned selected=drag_layer==static_cast<int>(i)?drag_style:model.style;
            if(selected==style)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_Header));
            const bool clicked=ImGui::Button(zh?stylesZh[style]:stylesEn[style],ImVec2(segment_width,0));
            if(selected==style)ImGui::PopStyleColor();
            if(ImGui::IsItemActivated()){drag_layer=static_cast<int>(i);drag_style=style;}
            if(drag_layer==static_cast<int>(i)&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))drag_style=style;
            if(clicked&&drag_layer<0&&model.style!=style){model.style=style;edited=true;discrete=true;}
        }
        if(drag_layer==static_cast<int>(i)&&!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if(model.style!=drag_style){model.style=drag_style;edited=true;discrete=true;}
            drag_layer=-1;
        }

        NrLayerField(zh?"模型强度":"Model strength",table);
        edited|=ImGui::SliderFloat("##intensity",&model.intensity,0.f,2.f,"%.2f");
        SettingsTooltip(zh?"传给本层 NR 模型的强度；不是最终额外增强。修改后仅重建必要实例。":"Strength sent to this NR layer, distinct from final enhancement. Changes rebuild only required instances.");
        NrLayerField(zh?"局部结构":"Local structure",table);
        edited|=ImGui::SliderFloat("##structure",&model.local_structure,0.f,2.f,"%.2f");
        SettingsTooltip(zh?"本层模型的局部结构参数，影响推理；不同于光影页对已有明暗细节的倍率。":"Local structure passed to this layer. Distinct from scaling existing light/detail edits on Lighting.");
        NrLayerField(zh?"局部色调":"Local tone",table);
        edited|=ImGui::SliderFloat("##tone",&model.local_tone,0.f,2.f,"%.2f");
        SettingsTooltip(zh?"本层模型的局部色调参数；不是最终 AI 色彩采用量，也不是全画面饱和度。":"Local tone passed to this layer; not final AI colour amount or global saturation.");
        NrLayerField(zh?"皮肤保护":"Native skin mask",table);
        if(ImGui::Checkbox("##native-mask",&model.auto_mask)) {
            if(model.auto_mask&&model.skin_structure<0.f)model.skin_structure=nr_layers::Model{}.skin_structure;
            edited=true;discrete=true;
        }
        SettingsTooltip(zh?"NR 模型自带的皮肤遮罩，不启动独立人脸检测；下方皮肤结构默认 0.4。":"NR's native skin mask; no separate face detector. Skin structure defaults to 0.4.");
        NrLayerField(zh?"皮肤结构":"Skin structure",table);
        ImGui::BeginDisabled(!model.auto_mask);
        edited|=ImGui::SliderFloat("##skin",&model.skin_structure,model.auto_mask?0.f:-1.f,2.f,"%.2f");
        ImGui::EndDisabled();
        NrLayerField(zh?"文字保护":"Text / UI protection",table);
        if(ImGui::Checkbox("##ui-correction",&model.ui_correction)){edited=true;discrete=true;}
        if(table)ImGui::EndTable();
        if(ImGui::Button(zh?"重置本层":"Reset this layer")){model={};edited=true;discrete=true;}
        SettingsTooltip(zh?"仅重置本层：网络跟随全局，强度 1.5、结构/色调 1、皮肤 0.4、原生遮罩和 UI 保护开。保留全局网络、层数、工作尺寸及最终效果。":"Only this layer: network follows global, strength 1.5, structure/tone 1, skin 0.4, native mask/UI on. Keeps global network, layer count, work size and final composition.");
        if(edited){if(i){static_cast<nr_layers::Model&>(next.nr_layers[i-1])=model;next.nr_layers[i-1].initialized=true;}else nr_layers::WritePrimary(next,model);changed=true;}
        ImGui::PopID();
    }
}
