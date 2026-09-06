#include "nr_shortcut_edge.h"
#include <cassert>
#include <cstdio>
int main() {
    unsigned actions=0;bool down=false;
    const auto sample=[&](unsigned short raw) {
        if(nr_shortcut::Pressed(static_cast<short>(raw),down)) ++actions;
        down=(raw & 0x8000u)!=0;
    };
    sample(0);sample(0x8001);
    for(unsigned i=0;i<5000;++i)sample(i%2?0x8000:0x8001);
    assert(actions==1);sample(0);sample(1);assert(actions==2);
    sample(0);sample(0x8000);sample(0);sample(0x8001);assert(actions==4);
    sample(0);sample(0x7ffe);assert(actions==4);
    std::puts("shortcut physical-edge: held/repeat/consumed-low-bit/short-tap PASS");
}
