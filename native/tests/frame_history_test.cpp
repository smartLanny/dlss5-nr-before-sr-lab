#include "../quickbar/frame_history.hpp"
#include "../shared/nrui_api.h"
#include <cassert>
int main(){
 quickbar::FrameHistory h;h.observe(1);for(int i=1;i<=120;++i)h.observe(1.+i/60.);assert(h.count==120);assert(std::abs(h.fps()-60)<.01);
 h.observe(3.1);assert(h.ordered()[120]>99); // Keep genuine stutters.
 for(int i=1;i<=600;++i){h.observe(3.1+i/120.);}assert(h.count==240);assert(std::abs(h.fps()-120)<.01);
 h.observe(20);assert(h.count==0);h.observe(21,false);assert(h.count==0);h.observe(22);assert(h.count==0);
 h.observe(NAN);assert(h.last==0);return 0;
}
