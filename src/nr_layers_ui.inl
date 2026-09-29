// One layer-count entry; disabled layers retain their complete parameter set.
#include "nr_unified_ui_policy.h"
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
    SettingsTooltip(zh?"各层同一工作尺寸；松手或提交输入后应用。减少层数会保留后续层设置。":"All layers share one work size; apply on release or input commit. Reducing layers retains their settings.");
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
        SettingsTooltip(zh?"仅重置本层：强度 1.5、结构/色调 1、皮肤 0.4、原生遮罩和 UI 保护开。不改层数、工作尺寸及最终效果。":"Only this layer: strength 1.5, structure/tone 1, skin 0.4, native mask/UI on. Keeps layer count, work size and final composition.");
        if(edited){if(i){static_cast<nr_layers::Model&>(next.nr_layers[i-1])=model;next.nr_layers[i-1].initialized=true;}else nr_layers::WritePrimary(next,model);changed=true;}
        ImGui::PopID();
    }
}
