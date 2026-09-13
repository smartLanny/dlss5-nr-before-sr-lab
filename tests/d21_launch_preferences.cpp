#include "nr_launch_preferences.h"
#include <initializer_list>
#include <cassert>
#include <cstdint>
#include <cstdio>
int main() {
 using namespace nr_launch_preferences;
 unsigned checks=0;
 for(unsigned saved:{0u,1u,2u,3u,UINT32_MAX}) {
  assert(Select(saved,Present,false,0)==Before);++checks;
  assert(Select(saved,Before,true,0)==(Valid(saved)?saved:Before));++checks;
  for(unsigned clicked:{Before,After,Present}) {
   for(bool remember:{false,true}) {
    // A click must not be undone by the next 500-ms disk config read.
    assert(Select(saved,clicked,remember,99)==clicked);++checks;
   }
  }
 }
 for(unsigned n=0;n<12;++n){assert(Select(Present,Present,false,0)==Before);++checks;}
 assert(Select(After,Before,true,0)==After);++checks;
 assert(Select(Present,Before,true,0)==Present);++checks;
 assert(Select(Present,UINT32_MAX,false,1)==Before);++checks;
 assert(LegacyMode(Before,1)==2&&LegacyMode(After,2)==1);++checks;
 assert(LegacyMode(Present,2)==2&&LegacyMode(Present,1)==1);++checks;
 for(unsigned live:{Before,After,Present})for(bool remember:{false,true}) {
  assert(Select(Present,live,remember,0,true)==live);++checks;
 }
 assert(Select(After,UINT32_MAX,true,0,true)==Before);++checks;
 // A new process may choose the persisted manual route; the next poll keeps
 // that route even if the checkbox was unchecked in the current process.
 assert(Select(Before,Select(After,Before,true,0),false,0,true)==After);++checks;
 std::printf("PASS %u launch preference assertions; config-only CPU test\n",checks);
}
