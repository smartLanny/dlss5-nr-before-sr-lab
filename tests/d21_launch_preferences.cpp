#include "nr_launch_preferences.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

int main() {
    using namespace nr_launch_preferences;
    unsigned checks=0;
    for(unsigned saved:{Before,After,Present}) {
        assert(Select(saved,Before,0)==saved);++checks;
        for(unsigned live:{Before,After,Present}) {
            assert(Select(saved,live,99)==live);++checks;
        }
    }
    for(unsigned invalid:{3u,UINT32_MAX}) {
        assert(Select(invalid,Present,0)==Before);++checks;
        assert(Select(invalid,After,7)==After);++checks;
        assert(Select(invalid,UINT32_MAX,7)==Before);++checks;
    }
    assert(LegacyMode(Before,1)==2);++checks;
    assert(LegacyMode(After,2)==1);++checks;
    assert(LegacyMode(Present,2)==2&&LegacyMode(Present,1)==1);++checks;
    std::printf("PASS %u saved-route launch preference assertions; config-only CPU test\n",checks);
}
