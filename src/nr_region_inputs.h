#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nr_region {
constexpr unsigned kVersion=1, kMaxHistory=8;
enum class Route : unsigned { Invalid, BeforeSr, AfterSr, AfterRr };
enum class ColorDomain : unsigned { Unknown, ModelEncoded709, Linear709, Encoded709, Pq2020 };
enum class Source : unsigned { None, BeforeNrDetector, VerifiedGameFace, ModelDifference, VerifiedGameReflection };
enum class Status : unsigned { Off, Waiting, Available, NoFace, Stale, ContractChanged, MissingMotion,
    MissingDepth, UntrustedSource, LowConfidence, InvalidExtent, WorkerUnavailable, ResourceUnavailable };
struct Frame {
    std::uint64_t context=0, epoch=0, frame=0;
    unsigned width=0, height=0, x=0, y=0, model_width=0, model_height=0;
    Route route=Route::Invalid;
    ColorDomain color=ColorDomain::Unknown;
    unsigned exposure_contract=0;
    bool Valid() const {
        return context && epoch && frame && width && height && width<=16384 && height<=16384 &&
            model_width && model_height && model_width<=16384 && model_height<=16384 &&
            x<=16384-width && y<=16384-height && route>=Route::BeforeSr && route<=Route::AfterRr &&
            color>=ColorDomain::ModelEncoded709 && color<=ColorDomain::Pq2020;
    }
};
inline bool SameContract(const Frame& a,const Frame& b) {
    return a.Valid()&&b.Valid()&&a.context==b.context&&a.epoch==b.epoch&&a.width==b.width&&
        a.height==b.height&&a.x==b.x&&a.y==b.y&&a.model_width==b.model_width&&a.model_height==b.model_height&&
        a.route==b.route&&a.color==b.color&&a.exposure_contract==b.exposure_contract;
}
struct Input {
    unsigned version=kVersion;
    Source source=Source::None;
    Frame reference{};
    float confidence=0;
    // Every intermediate motion/depth step must be validated. This is not
    // inferred from age, a non-null pointer, roughness, brightness or skin hue.
    unsigned proven_history_steps=0;
    bool coordinates_verified=false, producer_verified=false;
    Status For(const Frame& current,bool reflection=false) const {
        if(version!=kVersion||!coordinates_verified||!reference.Valid()||!current.Valid())return Status::InvalidExtent;
        if(!SameContract(reference,current))return Status::ContractChanged;
        if(!std::isfinite(confidence)||confidence<.6f||confidence>1)return Status::LowConfidence;
        if(reflection ? source!=Source::VerifiedGameReflection||!producer_verified :
            (source!=Source::BeforeNrDetector && (source!=Source::VerifiedGameFace||!producer_verified)))return Status::UntrustedSource;
        if(current.frame<reference.frame||current.frame-reference.frame>kMaxHistory)return Status::Stale;
        if(current.frame-reference.frame!=proven_history_steps)return Status::MissingMotion;
        return Status::Available;
    }
};
// All selection inputs are measured from the fixed original/proxy/neural
// triplet, before user gains. The returned weights never inspect settings.
struct Selection { float bright=0,dark=0,broad=0,detail=0; };
inline float Smooth(float low,float high,float x) {
    const float t=std::clamp((x-low)/(high-low),0.f,1.f);return t*t*(3-2*t);
}
inline Selection Select(float raw_luma_log,float broad_luma_log) {
    if(!std::isfinite(raw_luma_log)||!std::isfinite(broad_luma_log))return {};
    const float magnitude=std::abs(raw_luma_log);
    const float active=Smooth(.002f,.02f,magnitude);
    const float broad=std::clamp(std::abs(broad_luma_log)/std::max(magnitude,.002f),0.f,1.f);
    return {raw_luma_log>0?active:0,raw_luma_log<0?active:0,broad,1-broad};
}
// C1 guard: the same inference at neutral regional/extra gains bounds the
// final face edit. No second inference or reconstruction of model parameters.
inline float FaceEdit(float candidate,float same_nr_one,float mask,bool cap,float original_protection) {
    if(!std::isfinite(candidate)||!std::isfinite(same_nr_one)||!std::isfinite(mask)||!std::isfinite(original_protection))return 0;
    mask=std::clamp(mask,0.f,1.f);
    if(cap) {
        const float bounded=std::clamp(candidate,std::min(0.f,same_nr_one),std::max(0.f,same_nr_one));
        candidate=mask==1?bounded:candidate+(bounded-candidate)*mask;
    }
    return candidate*(1-mask*std::clamp(original_protection,0.f,1.f));
}
}
