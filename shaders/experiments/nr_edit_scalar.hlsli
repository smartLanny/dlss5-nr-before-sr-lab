// Scalar reference compiled both as C++ and HLSL. No vendor code or model data.
#ifndef NR_EDIT_SCALAR_MATH
#define NR_EDIT_SCALAR_MATH
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
namespace nr_edit_math {
#define NR_EDIT_INLINE inline
NR_EDIT_INLINE bool NrEditFinite(float x) {return std::isfinite(x);}
NR_EDIT_INLINE float NrEditAbs(float x) {return std::fabs(x);}
NR_EDIT_INLINE float NrEditMin(float a,float b) {return std::min(a,b);}
NR_EDIT_INLINE float NrEditMax(float a,float b) {return std::max(a,b);}
NR_EDIT_INLINE float NrEditClamp(float x,float a,float b) {return std::clamp(x,a,b);}
#else
#define NR_EDIT_INLINE
bool NrEditFinite(float x) {return isfinite(x);}
float NrEditAbs(float x) {return abs(x);}
float NrEditMin(float a,float b) {return min(a,b);}
float NrEditMax(float a,float b) {return max(a,b);}
float NrEditClamp(float x,float a,float b) {return clamp(x,a,b);}
#endif
NR_EDIT_INLINE float NrEditDepthWeight(float expected_previous_z,float stored_previous_z,
                                      float confidence,float absolute_tolerance,float relative_tolerance) {
    if(!NrEditFinite(expected_previous_z)||!NrEditFinite(stored_previous_z)||
       !NrEditFinite(confidence)||!NrEditFinite(absolute_tolerance)||
       !NrEditFinite(relative_tolerance)||expected_previous_z<=0.0f||
       stored_previous_z<=0.0f||absolute_tolerance<0.0f||relative_tolerance<0.0f)return 0.0f;
    float tolerance=NrEditMax(absolute_tolerance,relative_tolerance*
                             NrEditMax(expected_previous_z,stored_previous_z));
    if(!NrEditFinite(tolerance))return 0.0f;
    return NrEditAbs(expected_previous_z-stored_previous_z)<=tolerance?
        NrEditClamp(confidence,0.0f,1.0f):0.0f;
}
NR_EDIT_INLINE float NrEditAccumulate(float current_delta,float old_delta,
        float confidence,float new_sample_rate,float current_min,float current_max) {
    if(!NrEditFinite(current_delta))return 0.0f;
    if(!NrEditFinite(old_delta)||!NrEditFinite(confidence)||!NrEditFinite(new_sample_rate)||
       !NrEditFinite(current_min)||!NrEditFinite(current_max)||current_min>current_max)return current_delta;
    float c=NrEditClamp(confidence,0.0f,1.0f);
    float alpha=NrEditMax(NrEditClamp(new_sample_rate,0.01f,1.0f),1.0f-c);
    float history=NrEditClamp(old_delta,current_min,current_max);
    float value=history+(current_delta-history)*alpha;
    return NrEditFinite(value)?value:current_delta;
}
NR_EDIT_INLINE float NrEditViewZ(float hardware_depth,float projection_a,float projection_b) {
    if(!NrEditFinite(hardware_depth)||!NrEditFinite(projection_a)||!NrEditFinite(projection_b)||
       hardware_depth<0.0f||hardware_depth>1.0f)return 0.0f;
    float denominator=hardware_depth-projection_a;
    if(NrEditAbs(denominator)<1e-8f)return 0.0f;
    float z=projection_b/denominator;
    return NrEditFinite(z)&&z>0.0f?z:0.0f;
}
struct NrEditPacked {
    float carrier;
    float tail;
    bool valid;
};
// Stable, frame-global affine carrier. The spatial tail preserves HDR range
// instead of making the neural inverse singular. Range changes reset private SR.
NR_EDIT_INLINE NrEditPacked NrEditPack(float delta,float range) {
    NrEditPacked p;p.carrier=0.5f;p.tail=0.0f;p.valid=false;
    if(!NrEditFinite(delta)||!NrEditFinite(range)||range<=1e-6f||range>1e6f)return p;
    float core=NrEditClamp(delta,-range,range);
    p.carrier=0.5f+0.5f*(core/range);p.tail=delta-core;p.valid=NrEditFinite(p.tail);
    return p;
}
NR_EDIT_INLINE float NrEditUnpack(float carrier,float spatial_tail,float range) {
    if(!NrEditFinite(carrier)||!NrEditFinite(spatial_tail)||!NrEditFinite(range)||
       range<=1e-6f||range>1e6f)return 0.0f;
    float value=(2.0f*NrEditClamp(carrier,0.0f,1.0f)-1.0f)*range+spatial_tail;
    return NrEditFinite(value)?value:0.0f;
}
#ifdef __cplusplus
} // namespace nr_edit_math
#endif
#undef NR_EDIT_INLINE
#endif
