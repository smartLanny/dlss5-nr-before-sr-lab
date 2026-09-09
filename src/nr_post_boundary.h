#pragma once
#include <cstdint>
#include <limits>

// Recording observations, not a resource-state tracker or permission to issue
// barriers. ReShade's event omits native split flags and subresource numbers.
namespace nr_post_boundary {
inline void Increment(std::uint32_t &v) { if(v!=std::numeric_limits<std::uint32_t>::max()) ++v; }
struct Native {
    std::uint32_t callbacks=0,matches=0,other_lists=0;
    std::uint32_t first_before=0,last_after=0;
    void Barrier(bool same_list,bool target,std::uint32_t before,std::uint32_t after) {
        Increment(callbacks);
        if(!target)return;
        if(!same_list){Increment(other_lists);return;}
        if(!matches)first_before=before;
        Increment(matches);last_after=after;
    }
};
struct Window {
    std::uint64_t frame=0,feature=0,tick=0;
    std::uint32_t config=0,barriers=0,read_barriers=0,write_barriers=0;
    std::uint32_t copy_reads=0,copy_writes=0,render_target_binds=0;
    std::uint32_t first_before=0,first_after=0,last_after=0;
    Native native;
    void Barrier(std::uint32_t before,std::uint32_t after) {
        if(!barriers){first_before=before;first_after=after;}
        Increment(barriers);last_after=after;
        if(after & (0xC0u|0x800u))Increment(read_barriers);
        if(after & (0x8u|0x4u|0x400u))Increment(write_barriers);
    }
    void Copy(bool source,bool destination) {
        if(source)Increment(copy_reads);
        if(destination)Increment(copy_writes);
    }
};
}
