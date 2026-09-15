// Shared model panel: every visible value feeds this layer's Create signature.
void DrawUniformLayerControls(ChainConfig& next,bool& changed,bool& discrete) {
    const bool zh=UseChineseUi();
    SettingsSection(zh?"模型层数与参数":"Model layers and controls");
    SettingsNote(zh?"所有层使用上方同一工作分辨率。松开滑条时应用设置。":"All layers use the work resolution above. Settings apply when sliders are released.");
    if(next.nr_uniform_migrated)SettingsNote(zh?
        "旧辅助层比例已归档；各层现与第一层同尺寸，辅助层开销可能增加。当前为自定义配置。":
        "Legacy auxiliary scale is archived. All layers now match layer 1; auxiliary cost may increase. This is a custom configuration.");
    for(unsigned i=0;i<nr_layers::Count;++i) {
        ImGui::PushID(static_cast<int>(i));
        if(i) {
            auto& layer=next.nr_layers[i-1];
            char toggle[64];std::snprintf(toggle,sizeof(toggle),zh?"启用第 %u 层":"Enable layer %u",i+1);
            if(ImGui::Checkbox(toggle,&layer.enabled)) {
                if(layer.enabled)layer.initialized=true; // Constructor/migration supplied defaults; keep saved INI edits.
                changed=true;discrete=true;
            }
            if(!layer.enabled){ImGui::PopID();break;}
        }
        char title[64];std::snprintf(title,sizeof(title),zh?"第 %u 层":"Layer %u",i+1);
        if(ImGui::CollapsingHeader(title,ImGuiTreeNodeFlags_DefaultOpen)) {
            auto model=nr_layers::Read(next,i);bool edited=false;
            const char* styles=zh?"默认\0自然\0电影\0":"Default\0Natural\0Cinematic\0";
            int style=static_cast<int>(model.style);
            if(ImGui::Combo(zh?"风格":"Style",&style,styles)){model.style=static_cast<unsigned>(style);edited=true;discrete=true;}
            edited|=SettingsSliderFloat(zh?"强度":"Intensity","##intensity",&model.intensity,0.f,2.f,"%.2f");
            edited|=SettingsSliderFloat(zh?"局部结构":"Local structure","##structure",&model.local_structure,0.f,2.f,"%.2f");
            edited|=SettingsSliderFloat(zh?"局部色调":"Local tone","##tone",&model.local_tone,0.f,2.f,"%.2f");
            if(ImGui::Checkbox(zh?"自动皮肤遮罩":"Automatic skin mask",&model.auto_mask)){
                if(model.auto_mask&&model.skin_structure<0.f)model.skin_structure=0.f;
                edited=true;discrete=true;
            }
            ImGui::BeginDisabled(!model.auto_mask);
            edited|=SettingsSliderFloat(zh?"皮肤结构":"Skin structure","##skin",&model.skin_structure,model.auto_mask?0.f:-1.f,2.f,"%.2f");
            ImGui::EndDisabled();
            if(ImGui::Checkbox(zh?"文字与界面保护":"UI correction",&model.ui_correction)){edited=true;discrete=true;}
            if(ImGui::Button(zh?"重置本层":"Reset this layer")){model={};edited=true;discrete=true;}
            SettingsTooltip(zh?"仅恢复本层模型参数；不改变层数、工作分辨率和最终合成。":"Resets this layer's model controls only; keeps layer count, work resolution and final composition.");
            if(edited) {
                if(i){static_cast<nr_layers::Model&>(next.nr_layers[i-1])=model;next.nr_layers[i-1].initialized=true;}
                else nr_layers::WritePrimary(next,model);
                changed=true;
            }
        }
        ImGui::PopID();
    }
}
