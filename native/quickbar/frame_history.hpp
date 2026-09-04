// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
namespace quickbar {
// Observations are ReShade callbacks, NOT a displayed-FPS or generated-frame counter.
struct FrameHistory {
    static constexpr std::size_t capacity=240;
    std::array<float,capacity> ms{};
    std::size_t cursor=0, count=0;
    double last=0;
    void reset(){ cursor=count=0; last=0; ms.fill(0); }
    void observe(double seconds, bool foreground=true){
        if(!foreground || !std::isfinite(seconds)){ reset(); return; }
        if(last==0){last=seconds; return;}
        double delta=(seconds-last)*1000.0; last=seconds;
        // A backwards clock or >10 s discontinuity starts a new window.
        if(delta<=0 || delta>10000){reset();last=seconds;return;}
        ms[cursor]=static_cast<float>(delta);cursor=(cursor+1)%capacity;
        if(count<capacity)++count;
    }
    float mean_ms() const { double total=0;for(std::size_t i=0;i<count;++i)total+=ms[i];return count?float(total/count):0; }
    float fps() const { const float m=mean_ms();return m>0?1000.f/m:0; }
    std::array<float,capacity> ordered()const {std::array<float,capacity> out{};std::size_t start=count==capacity?cursor:0;for(std::size_t i=0;i<count;++i)out[i]=ms[(start+i)%capacity];return out;}
};
}
