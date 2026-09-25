#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include "nr_present_mapping.h"

// Pure admission policy for a native reconstruction receipt captured before
// the native call and acknowledged only after that call succeeds. Resource and
// fence ownership remains in nr_source_transport.inl.
namespace nr_late_present {
using Id = std::uint64_t;

enum class Decision : unsigned {
    None, Accepted, KnownGenerated, KnownFgConsumed, KnownInlineProcessed,
    NoReceipt, NativeResultPending, NativeFailed, Unsubmitted, WrongDevice,
    WrongExtent, WrongConfig, Expired, AlreadyConsumed, Ambiguous
};

inline const char* Name(Decision value) noexcept {
    switch(value){
    case Decision::None:return "none";
    case Decision::Accepted:return "accepted";
    case Decision::KnownGenerated:return "known-generated";
    case Decision::KnownFgConsumed:return "known-FG-consumed";
    case Decision::KnownInlineProcessed:return "known-inline-processed";
    case Decision::NoReceipt:return "no-native-receipt";
    case Decision::NativeResultPending:return "native-result-pending";
    case Decision::NativeFailed:return "native-failed";
    case Decision::Unsubmitted:return "receipt-unsubmitted";
    case Decision::WrongDevice:return "receipt-device-mismatch";
    case Decision::WrongExtent:return "receipt-output-extent-mismatch";
    case Decision::WrongConfig:return "receipt-config-mismatch";
    case Decision::Expired:return "receipt-expired";
    case Decision::AlreadyConsumed:return "receipt-already-consumed";
    case Decision::Ambiguous:return "ambiguous-native-producers";
    }
    return "invalid";
}

struct Candidate {
    Id stream=0,epoch=0,sequence=0,latest_sequence=0;
    Id opportunity=0,latest_device_opportunity=0;
    Id device=0,producer_queue=0,producer_fence_value=0;
    unsigned config_generation=0;
    unsigned output_width=0,output_height=0;
    bool recorded=false,native_result_known=false,native_succeeded=false;
    bool submitted=false,consumed=false,inline_processed=false,fg_consumed=false;
    bool poisoned=false;
};

struct Request {
    Id device=0;
    unsigned output_width=0,output_height=0;
    unsigned config_generation=0;
    bool known_generated=false,known_fg_consumed=false,known_inline_processed=false;
    bool automatic_full_viewport_scale=true;
};

struct Selection {
    std::size_t index=std::numeric_limits<std::size_t>::max();
    Decision decision=Decision::NoReceipt;
    unsigned age=0,device_age=0;
    explicit operator bool() const noexcept {return decision==Decision::Accepted;}
};

inline Selection Select(const Request& request,const Candidate* candidates,std::size_t count) noexcept {
    constexpr auto invalid=std::numeric_limits<std::size_t>::max();
    if(request.known_generated)return {invalid,Decision::KnownGenerated,0,0};
    if(request.known_fg_consumed)return {invalid,Decision::KnownFgConsumed,0,0};
    if(request.known_inline_processed)return {invalid,Decision::KnownInlineProcessed,0,0};
    if(!candidates||!count)return {invalid,Decision::NoReceipt,0,0};

    Decision refused=Decision::NoReceipt;
    // Preserve the old exact-extent route before considering NEW inferred
    // candidates. A scaled ambiguity must not hide a later exact candidate.
    const unsigned tiers=request.automatic_full_viewport_scale?2u:1u;
    for(unsigned tier=0;tier<tiers;++tier) {
    std::size_t selected=invalid;
    unsigned selected_age=0,selected_device_age=0;
    for(std::size_t i=0;i<count;++i){const auto& c=candidates[i];
        if(!c.recorded)continue;
        if(c.poisoned){refused=Decision::NativeFailed;continue;}
        if(!c.native_result_known){refused=Decision::NativeResultPending;continue;}
        if(!c.native_succeeded){refused=Decision::NativeFailed;continue;}
        if(!c.submitted||!c.producer_queue||!c.producer_fence_value){refused=Decision::Unsubmitted;continue;}
        if(c.device!=request.device){refused=Decision::WrongDevice;continue;}
        const auto mapping=nr_present_mapping::Map({c.output_width,c.output_height},
            {request.output_width,request.output_height},request.automatic_full_viewport_scale);
        if(!mapping) {
            refused=Decision::WrongExtent;continue;
        }
        if(mapping.scaled!=(tier!=0))continue;
        if(request.config_generation&&c.config_generation!=request.config_generation){refused=Decision::WrongConfig;continue;}
        if(c.consumed||c.inline_processed||c.fg_consumed){refused=Decision::AlreadyConsumed;continue;}
        if(!c.stream||!c.epoch||!c.sequence||c.latest_sequence<c.sequence||c.latest_sequence-c.sequence>1||
           !c.opportunity||c.latest_device_opportunity<c.opportunity||c.latest_device_opportunity-c.opportunity>1){
            refused=Decision::Expired;continue;
        }
        const unsigned age=static_cast<unsigned>(c.latest_sequence-c.sequence);
        const unsigned device_age=static_cast<unsigned>(c.latest_device_opportunity-c.opportunity);
        if(selected==invalid){selected=i;selected_age=age;selected_device_age=device_age;continue;}
        const auto& current=candidates[selected];
        if(current.stream==c.stream&&current.epoch==c.epoch){
            if(c.sequence>current.sequence){selected=i;selected_age=age;selected_device_age=device_age;}
            continue;
        }
        // Backbuffer provenance is diagnostic at Present, so two distinct
        // producer streams with equally shape-compatible receipts cannot be
        // disambiguated safely by guessed recency or callback order.
        return {invalid,Decision::Ambiguous,0,0};
    }
    if(selected!=invalid)return {selected,Decision::Accepted,selected_age,selected_device_age};
    }
    return {invalid,refused,0,0};
}
} // namespace nr_late_present
