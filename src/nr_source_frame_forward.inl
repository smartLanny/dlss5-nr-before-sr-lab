// Include in Core's private namespace before native command hooks.
struct NgxHandle;
namespace nr_source_runtime {
bool PrivateSrInputReadState(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned&);
void OnExecute(ID3D12CommandQueue*,unsigned,ID3D12CommandList*const*,bool);
void OnReset(ID3D12GraphicsCommandList*,bool);
void OnCopy(ID3D12GraphicsCommandList*,ID3D12Resource*,ID3D12Resource*,bool);
void OnWrite(ID3D12GraphicsCommandList*,ID3D12Resource*);
void OnDestroyResource(ID3D12Resource*);
void OnBarrier(ID3D12GraphicsCommandList*,unsigned,const D3D12_RESOURCE_BARRIER*);
void OnEnhancedBarrier(ID3D12GraphicsCommandList*);
void OnDraw(ID3D12GraphicsCommandList*);
void OnClear(ID3D12GraphicsCommandList*,ID3D12Resource*,bool);
void OnClearRtv(ID3D12GraphicsCommandList*,D3D12_CPU_DESCRIPTOR_HANDLE,bool);
unsigned long long ObservedRtvResource(reshade::api::command_list*,unsigned long long);
void RetireFeature(const NgxHandle*,unsigned long long);
void RetireSwapchain(reshade::api::swapchain*);
void Register(bool);
bool MayObserveSubmission();
bool HasPendingSubmission(reshade::api::command_list*);
bool InlineAllowed(unsigned);
void MarkPrivateRecorded();
nr_source_frame::Source CurrentSource();
unsigned long long CurrentRetryToken();
unsigned long long PresentRetryToken();
bool TryPresentObservation(nr_source_frame::PresentObservation&);
}
