#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include "nr_private_sr_guides.h"
namespace nr_private_sr_guides {
Result Record(nr_edit_dx12::Kernels* k,ID3D12GraphicsCommandList* list,const Input& i,
              const nr_edit_dx12::View& depth_out,const nr_edit_dx12::View& motion_out) {
    Result result;const auto p=Prepare(i);result.reason=p.reason;if(!p)return result;
    result.reason=Reason::Resource;
    if(!k||!list)return result;
    const auto& s=i.signature;
    if(i.depth.resource==i.motion.resource||i.depth.mip||i.motion.mip||depth_out.mip||motion_out.mip||
       (i.depth.format!=41&&i.depth.format!=54&&i.depth.format!=46&&i.depth.format!=21&&i.depth.format!=56)||
       (i.motion.format!=16&&i.motion.format!=34)||depth_out.format!=41||motion_out.format!=16||
       !nr_edit_dx12::ValidateRead(k,i.depth,unsigned(s.depth_physical_width),s.depth_physical_height)||
       !nr_edit_dx12::ValidateRead(k,i.motion,unsigned(s.motion_physical_width),s.motion_physical_height)||
       !nr_edit_dx12::ValidateRead(k,depth_out,i.work_width,i.work_height)||
       !nr_edit_dx12::ValidateRead(k,motion_out,i.work_width,i.work_height))return result;
    nr_edit_dx12::Work w{};w.kernel=nr_edit_dx12::Kernel::NormalizeGuides;w.recording=i.recording;
    w.width=i.work_width;w.height=i.work_height;w.constants=&p.constants;w.constant_bytes=sizeof(p.constants);
    w.reads[0]=i.depth;w.reads[1]=i.motion;w.writes[0]=depth_out;w.writes[1]=motion_out;
    result.dispatch=nr_edit_dx12::Record(k,list,w);
    if(!result.dispatch.recorded){result.reason=Reason::Dispatch;return result;}
    result.reason=Reason::Ready;result.source=i.source;result.motion_reference=i.motion_reference;result.recording=i.recording;
    result.normalized_guide_units_verified=true;return result;
}
}
