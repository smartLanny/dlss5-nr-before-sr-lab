#pragma once
#include <atomic>
#include "nr_shortcut_edge.h"

// Publish all four bindings together. Saved virtual-key values stay stable;
// the UI's list index is never persisted. No per-frame INI IO.
namespace nr_hotkeys {
enum Action : unsigned { Master, Image, Report, Screenshot, Count };
constexpr unsigned Defaults[Count]={117,118,119,189};
struct Choice { unsigned key; const char* en; const char* zh; };
inline constexpr Choice Choices[]={
    {0,"Off","关闭"},
    {112,"F1","F1"},{113,"F2","F2"},{114,"F3","F3"},{115,"F4","F4"},
    {116,"F5","F5"},{117,"F6","F6"},{118,"F7","F7"},{119,"F8","F8"},
    {120,"F9","F9"},{121,"F10","F10"},{122,"F11","F11"},{123,"F12","F12"},
    {36,"Home","Home"},{35,"End","End"},{45,"Insert","Insert"},{46,"Delete","Delete"},
    {33,"PageUp","PageUp"},{34,"PageDown","PageDown"},
    {145,"ScrollLock","ScrollLock"},{19,"Pause","Pause"},{189,"Minus (-)","减号 (-)"}
};
constexpr unsigned ChoiceCount=sizeof(Choices)/sizeof(Choices[0]);
inline unsigned Index(unsigned key) {
    for(unsigned i=0;i<ChoiceCount;++i)if(Choices[i].key==key)return i;
    return 0;
}
inline bool Valid(unsigned key) { return key==0 || Choices[Index(key)].key==key; }
inline bool Allowed(Action action,unsigned key) {
    return action<Count && Valid(key) && (key!=189 || action==Screenshot);
}
inline bool Overlaps(Action action,unsigned key,unsigned other_key) {
    return key && other_key && (key==other_key || (action==Screenshot && key==189 && other_key==109));
}
inline unsigned Pack(unsigned master,unsigned image,unsigned report,unsigned screenshot=189) {
    return master | (image<<8) | (report<<16) | (screenshot<<24);
}
inline std::atomic<unsigned> published{Pack(117,118,119,189)};
inline unsigned Key(unsigned packed,Action action) { return action<Count ? (packed>>(8*action))&255u : 0; }
inline unsigned Key(Action action) { return Key(published.load(std::memory_order_acquire),action); }
inline const char* Name(unsigned key,bool chinese=false) {
    const auto& choice=Choices[Index(key)];return chinese?choice.zh:choice.en;
}
template<class C> void Normalize(C& c) {
    unsigned* keys[]={&c.master_virtual_key,&c.image_virtual_key,&c.feedback_virtual_key,&c.screenshot_virtual_key};
    for(unsigned i=0;i<Count;++i) {
        // An invalid saved value must not silently activate a default shortcut.
        if(!Allowed(static_cast<Action>(i),*keys[i]))*keys[i]=0;
        for(unsigned j=0;j<i;++j)if(*keys[i] && *keys[i]==*keys[j])*keys[i]=0;
    }
}
template<class C> void Publish(const C& c) {
    published.store(Pack(c.master_virtual_key,c.image_virtual_key,c.feedback_virtual_key,c.screenshot_virtual_key),std::memory_order_release);
}
template<class C> bool Set(C& c,Action action,unsigned key) {
    if(!Allowed(action,key))return false;
    unsigned* keys[]={&c.master_virtual_key,&c.image_virtual_key,&c.feedback_virtual_key,&c.screenshot_virtual_key};
    for(unsigned i=0;i<Count;++i)if(i!=action && key && *keys[i]==key)return false;
    *keys[action]=key;return true;
}
inline constexpr unsigned Presets[][Count]={
    {117,118,119,189}, {120,121,122,189}, {145,19,34,189}, {0,0,0,0}
};
template<class C> bool ApplyPreset(C& c,unsigned preset) {
    if(preset>=sizeof(Presets)/sizeof(Presets[0]))return false;
    c.master_virtual_key=Presets[preset][Master];c.image_virtual_key=Presets[preset][Image];
    c.feedback_virtual_key=Presets[preset][Report];c.screenshot_virtual_key=Presets[preset][Screenshot];return true;
}
template<class C> unsigned PresetIndex(const C& c) {
    for(unsigned i=0;i<sizeof(Presets)/sizeof(Presets[0]);++i)
        if(c.master_virtual_key==Presets[i][Master] && c.image_virtual_key==Presets[i][Image] &&
           c.feedback_virtual_key==Presets[i][Report] && c.screenshot_virtual_key==Presets[i][Screenshot])return i+1;
    return 0; // Custom bindings remain untouched.
}
template<class Getter> short State(unsigned key,Action action,Getter get) {
    if(!key)return 0;
    unsigned short state=static_cast<unsigned short>(get(key));
    // Preserve the historical keypad-minus alias only for the default Minus
    // screenshot binding. No other action may own either minus virtual key.
    if(action==Screenshot && key==189)state|=static_cast<unsigned short>(get(109));
    return static_cast<short>(state&0x8001u);
}
struct Edge {
    unsigned binding=~0u; bool down=false,armed=false;
    bool Press(unsigned key,short state) {
        const bool held=(static_cast<unsigned short>(state)&0x8000u)!=0;
        // A newly assigned key must first be released. Changing shortcuts while
        // holding a key never toggles NR or starts a capture.
        if(binding!=key){binding=key;down=held;armed=!held;return false;}
        if(!armed){if(!held)armed=true;down=held;return false;}
        const bool pressed=key && nr_shortcut::Pressed(state,down);
        down=held;return pressed;
    }
};
}
