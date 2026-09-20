"""Final product preferences overlay, after the historical colour adapters."""
from pathlib import Path

def runtime(s, h):
    once=h.once
    s=once(s,'#include "nr_shortcut_edge.h"','#include "nr_shortcut_edge.h"\n#include "nr_hotkeys.h"\n#include "nr_colour_preferences.h"','user controls includes')
    s=once(s,'struct ChainConfig {','''struct ChainConfig {
    unsigned master_virtual_key=117, image_virtual_key=118, feedback_virtual_key=119;
    float colour_priority_strength=nr_colour_preferences::PriorityDefault;
    float colour_conservative_strength=nr_colour_preferences::ConservativeDefault;''','user preferences')
    s=once(s,'    float color_strength = 1.0f;','    float color_strength = 0.5f;','new colour default')
    s=once(s,'    next.enabled = GetPrivateProfileIntW(L"NRBeforeSR", L"Enabled", 1, g_ini_path) != 0;',
        '''    next.master_virtual_key=GetPrivateProfileIntW(L"NRBeforeSR",L"MasterVirtualKey",117,g_ini_path);
    next.image_virtual_key=GetPrivateProfileIntW(L"NRBeforeSR",L"ImageVirtualKey",118,g_ini_path);
    next.feedback_virtual_key=GetPrivateProfileIntW(L"NRBeforeSR",L"FeedbackVirtualKey",119,g_ini_path);
    next.enabled = GetPrivateProfileIntW(L"NRBeforeSR", L"Enabled", 1, g_ini_path) != 0;''','shortcut load')
    s=once(s,'    next.color_strength = ReadIniFloat(L"ColorStrength", 1.0f);','''    // Preserve a pre-policy INI's explicit legacy permission, including OFF.
    wchar_t saved_mode[32]{},saved_permission[32]{};
    GetPrivateProfileStringW(L"NRBeforeSR",L"ColourLabMode",L"",saved_mode,32,g_ini_path);
    GetPrivateProfileStringW(L"NRBeforeSR",L"AllowUnverifiedHdrColor",L"",saved_permission,32,g_ini_path);
    if(!saved_mode[0] && saved_permission[0])next.colour_lab_mode=0;
    nr_colour_preferences::Load(next,ReadIniFloat(L"ColorStrength",std::nanf("")),
        ReadIniFloat(L"ColourPriorityStrength",std::nanf("")),
        ReadIniFloat(L"ColourConservativeStrength",std::nanf("")));''','colour request migration')
    s=once(s,'void NormalizeConfig(ChainConfig &config) {','void NormalizeConfig(ChainConfig &config) {\n    nr_hotkeys::Normalize(config);\n    nr_colour_preferences::Remember(config);','preference normalize')
    anchor='bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return '
    s=once(s,anchor,anchor+'''a.master_virtual_key==b.master_virtual_key && a.image_virtual_key==b.image_virtual_key &&
           a.feedback_virtual_key==b.feedback_virtual_key &&
           a.colour_priority_strength==b.colour_priority_strength &&
           a.colour_conservative_strength==b.colour_conservative_strength && ''','preference transactions')
    anchor='    record_light(WriteIniFloat(L"ColorStrength", config.color_strength));'
    s=once(s,anchor,anchor+'''
    record_write(WriteIniFloat(L"ColourPriorityStrength",config.colour_priority_strength));
    record_write(WriteIniFloat(L"ColourConservativeStrength",config.colour_conservative_strength));
    record_write(WriteIniInt(L"MasterVirtualKey",config.master_virtual_key));
    record_write(WriteIniInt(L"ImageVirtualKey",config.image_virtual_key));
    record_write(WriteIniInt(L"FeedbackVirtualKey",config.feedback_virtual_key));''','save user preferences')
    anchor='    g_config = next;'
    if s.count(anchor)!=2:raise ValueError('expected two config publication sites')
    s=s.replace(anchor,anchor+'\n    nr_hotkeys::Publish(g_config);')
    start=s.index('    static bool feedback_key_loaded=false;')
    end=s.index('    // Core feedback has no GPU-stage request',start)
    s=s[:start]+'''    const auto shortcut_bindings=nr_hotkeys::published.load(std::memory_order_acquire);
    static nr_hotkeys::Edge report_edge;
    const auto report_key=nr_hotkeys::Key(shortcut_bindings,nr_hotkeys::Report);
    const SHORT f8_state = report_key ? GetAsyncKeyState(static_cast<int>(report_key)) : 0;
    if(report_edge.Press(report_key,f8_state)) {
        nr_f8::RequestCore();nr_f8::OpenWindow();
        runtime->open_overlay(true,reshade::api::input_source::keyboard);
    }
'''+s[end:]
    s=once(s,'    static bool feedback_key_was_down = false;','','retired feedback edge')
    s=once(s,'    static bool stage_key_was_down = false;','    static nr_hotkeys::Edge image_edge;','image edge owner')
    s=once(s,'    const SHORT f7_state = GetAsyncKeyState(VK_F7);\n    const bool stage_key_is_down = (f7_state & 0x8000) != 0;',
        '''    const auto image_key=nr_hotkeys::Key(shortcut_bindings,nr_hotkeys::Image);
    const SHORT f7_state = image_key ? GetAsyncKeyState(static_cast<int>(image_key)) : 0;''','image key')
    s=once(s,'    const bool stage_pressed = nr_shortcut::Pressed(f7_state, stage_key_was_down);',
        '    const bool stage_pressed = image_edge.Press(image_key,f7_state);','image dispatch')
    s=once(s,'    stage_key_was_down = stage_key_is_down;','','image edge owns release')
    start=s.index('    static bool f6_was_down = false;')
    end=s.index('    UpdateEvaluateHookActivation();',start)
    s=s[:start]+'''    static nr_hotkeys::Edge master_edge;
    static ULONGLONG last_f6_toggle_ms = 0;
    const auto master_key=nr_hotkeys::Key(nr_hotkeys::Master);
    const SHORT f6_state=master_key?GetAsyncKeyState(static_cast<int>(master_key)):0;
    const bool f6_pressed=master_edge.Press(master_key,f6_state);
    const ULONGLONG now_ms=GetTickCount64();
    if(f6_pressed && now_ms-last_f6_toggle_ms>=250){last_f6_toggle_ms=now_ms;ToggleMasterFromHotkey();}
'''+s[end:]
    s=once(s,'ApplyConfig(next, "F6");','ApplyConfig(next, "master-shortcut");','accurate shortcut log')
    return s

def apply(stage: Path, inputs, helpers):
    root=stage/'core'
    p=root/'src/nr_before_sr.cpp'
    p.write_text(runtime(p.read_text(encoding='utf-8'),helpers),encoding='utf-8',newline='\n')
    p=root/'src/nr_recommended_picture.inl'
    s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'    next.color_strength = 1.0f;','    nr_colour_preferences::Reset(next);','explicit picture colour reset')
    p.write_text(s,encoding='utf-8',newline='\n')
    p=root/'src/nr_f8_sequence.inl'
    s=p.read_text(encoding='utf-8')
    start,end=helpers.definition(s,'        if(ImGui::TreeNode(use_chinese_ui?"快捷键设置":"Shortcut"))')
    s=s[:start]+s[end:]
    s=helpers.once(s,'        static int key=VK_F8;','','retired shortcut editor')
    p.write_text(s,encoding='utf-8',newline='\n')
    p=root/'config/nr_before_sr.ini'
    s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'ColorStrength=1.0','ColorStrength=0.5\nColourPriorityStrength=0.5\nColourConservativeStrength=1.0','template colour defaults')
    s=s.replace('; F6/F7 cannot be used as custom core-feedback shortcuts.',
        '; Shortcuts: 0=disabled, 112..123=F1..F12; duplicate keys are disabled.')
    s+='\n; Master NR / current picture; FeedbackVirtualKey remains the text report key.\nMasterVirtualKey=117\nImageVirtualKey=118\n'
    p.write_text(s,encoding='utf-8',newline='\n')
    for name in ('nr_hotkeys.h','nr_colour_preferences.h'):
        (root/'src'/name).write_bytes(inputs['src/'+name])
