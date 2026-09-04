// Compiled AFTER nr_before_sr.cpp in the same translation unit.
// The core keeps ownership of validation, F8 capture exclusion, history and persistence.
#include "nrui_api.h"
namespace {
struct NrUiLock {
    bool held = TryEnterCriticalSection(&g_chain_cs) != 0;
    ~NrUiLock(){ if(held) LeaveCriticalSection(&g_chain_cs); }
};
}
extern "C" __declspec(dllexport) uint32_t NRUI_CALL NRUI_GetStateV1(NrUiStateV1 *out) {
    if(!out || out->size!=sizeof(NrUiStateV1) || out->version!=1) return NRUI_INVALID;
    NrUiLock lock; if(!lock.held) return NRUI_BUSY;
    NrUiStateV1 s{};s.size=sizeof(s);s.version=1;
    s.generation=g_config_generation.load(std::memory_order_relaxed);
    s.enabled=g_config.enabled;s.mode=g_config.mode;s.style=g_config.style;
    s.auto_mask=g_config.auto_mask;s.work_mode=g_config.work_mode;
    s.strength=g_config.transfer_strength;s.custom_work_scale=g_config.custom_work_scale;
    s.bypass_reason=static_cast<uint32_t>(g_last_bypass_reason.load(std::memory_order_relaxed));
    s.capture_busy=nr_f8::g_config_hold.load(std::memory_order_acquire)?1u:0u;
    s.submitted_count=g_nr_transfer_submitted_frames.load(std::memory_order_relaxed);
    *out=s;return NRUI_OK;
}
extern "C" __declspec(dllexport) uint32_t NRUI_CALL NRUI_SetValueV1(uint32_t key,float value,uint32_t expected) {
    if(!std::isfinite(value))return NRUI_INVALID;
    NrUiLock lock;if(!lock.held || nr_f8::g_config_hold.load(std::memory_order_acquire))return NRUI_BUSY;
    if(expected!=g_config_generation.load(std::memory_order_relaxed))return NRUI_CONFLICT;
    ChainConfig next=g_config;
    switch(key){
    case NRUI_ENABLED: if(value!=0 && value!=1)return NRUI_INVALID;next.enabled=value!=0;break;
    case NRUI_MODE: if(value!=1 && value!=2)return NRUI_INVALID;next.mode=static_cast<int>(value);break;
    case NRUI_STRENGTH: if(value<0 || value>4)return NRUI_INVALID;next.transfer_strength=next.post_transfer_strength=value;break;
    case NRUI_STYLE: if(value<0 || value>2 || std::floor(value)!=value)return NRUI_INVALID;next.style=static_cast<unsigned>(value);break;
    case NRUI_MASK: if(value!=0 && value!=1)return NRUI_INVALID;next.auto_mask=value!=0;break;
    case NRUI_WORK_MODE: if(value<0 || value>5 || std::floor(value)!=value)return NRUI_INVALID;next.work_mode=static_cast<int>(value);break;
    default:return NRUI_UNSUPPORTED;
    }
    const bool applied=ApplyConfig(next,"Quickbar public API",true);
    if(!applied && nr_f8::g_config_hold.load(std::memory_order_acquire))return NRUI_BUSY;
    return g_config_persistence_failed.load(std::memory_order_acquire)?NRUI_IO_ERROR:NRUI_OK;
}
