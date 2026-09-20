#pragma once
#include <atomic>
#include "nr_shortcut_edge.h"

// Function keys or Disabled. Publish the complete set atomically: swapping two
// keys cannot momentarily let one press own two actions. No per-frame INI IO.
namespace nr_hotkeys {
enum Action : unsigned { Master, Image, Report };
constexpr unsigned Defaults[3]={117,118,119};
inline bool Valid(unsigned key) { return key==0 || (key>=112 && key<=123); }
inline unsigned Pack(unsigned master,unsigned image,unsigned report) {
    return master | (image<<8) | (report<<16);
}
inline std::atomic<unsigned> published{Pack(117,118,119)};
inline unsigned Key(unsigned packed,Action action) { return (packed>>(8*action))&255u; }
inline unsigned Key(Action action) { return Key(published.load(std::memory_order_acquire),action); }
inline const char* Name(unsigned key) {
    static constexpr const char* names[]={"Off","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    return names[key>=112 && key<=123 ? key-111 : 0];
}
template<class C> void Normalize(C& c) {
    unsigned* keys[]={&c.master_virtual_key,&c.image_virtual_key,&c.feedback_virtual_key};
    for(unsigned i=0;i<3;++i) {
        if(!Valid(*keys[i]))*keys[i]=Defaults[i];
        // Malformed saved duplicate bindings are disabled and shown as such.
        for(unsigned j=0;j<i;++j)if(*keys[i] && *keys[i]==*keys[j])*keys[i]=0;
    }
}
template<class C> void Publish(const C& c) {
    published.store(Pack(c.master_virtual_key,c.image_virtual_key,c.feedback_virtual_key),std::memory_order_release);
}
template<class C> bool Set(C& c,Action action,unsigned key) {
    if(!Valid(key))return false;
    unsigned* keys[]={&c.master_virtual_key,&c.image_virtual_key,&c.feedback_virtual_key};
    for(unsigned i=0;i<3;++i)if(i!=action && key && *keys[i]==key)return false;
    *keys[action]=key;return true;
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
