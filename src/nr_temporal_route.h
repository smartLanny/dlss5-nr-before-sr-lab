#pragma once

namespace nr_temporal_route {
// Entry is supplied by the actual compositor seam, not inferred from a
// Feature ID. In particular, PostRr is reached only after successful native RR.
enum class Entry : unsigned { PostSr, PreSr, PostRr };
struct Evidence {
    unsigned feature=0, mode=0, backend=0, scope=0;
    bool flags_known=false, hdr=false, sdr_codec=false;
    bool native_output_snapshot=false, packed_original=false;
};
enum class Decision : unsigned { Supported, Domain, Route, Scope, Motion };
constexpr Decision Admit(Entry entry,const Evidence& e) {
    if(e.hdr||!e.sdr_codec)return Decision::Domain;
    if(entry==Entry::PostSr)
        return e.feature==1&&e.mode==1&&e.native_output_snapshot
            ?Decision::Supported:Decision::Route;
    // New routes require affirmative flags, not an absent HDR bit in an
    // unknown Create observation. HDR needs a different accumulator contract.
    if(!e.flags_known)return Decision::Domain;
    if(entry==Entry::PreSr){
        if(e.feature!=1||e.mode!=2||e.native_output_snapshot||!e.packed_original)
            return Decision::Route;
        if(e.scope)return Decision::Scope;
        // A pre-SR source can be jittered even when the MV flag is not. Its
        // inter-frame raster phase is not established by a copied MV texture.
        if(e.backend!=2)return Decision::Motion;
        return Decision::Supported;
    }
    if(entry==Entry::PostRr){
        if(e.feature!=13||e.mode!=1||!e.native_output_snapshot)return Decision::Route;
        // RR model fallback/placeholder guides are not temporal motion proof.
        if(e.backend!=2)return Decision::Motion;
        return Decision::Supported;
    }
    return Decision::Route;
}
}
