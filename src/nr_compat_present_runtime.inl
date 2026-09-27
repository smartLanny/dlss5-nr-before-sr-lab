// Included once inside the Core private namespace after existing NR/codec/lifetime definitions.
// Generated source staging copies all companion files; this is not a standalone addon.
#include "nr_compat_present_shader.h"
namespace nr_compat_present_runtime {
std::atomic<unsigned long long> last_recorded_tick{0};
#include "nr_compat_present_resources.inl"
namespace nr_present_capture {bool Retire(Context&);}
#if NR_HAS_TEMPORAL_RUNTIME
bool ReleaseTemporal(Context&);
void EndTemporalRecording(Context&,Flight&);
#endif
#include "nr_compat_present_models.inl"
#include "nr_present_capture.inl"
bool RequestCapture(){return nr_fg_runtime::CapturePreferred()?nr_fg_capture::RequestCapture():nr_present_capture::RequestCapture();}
#if NR_HAS_TEMPORAL_RUNTIME
#include "nr_present_temporal.inl"
#endif
#include "nr_compat_present_events.inl"
} // namespace nr_compat_present_runtime
