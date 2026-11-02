// Appended after the unchanged parent-retirement CPU fixture, whose original
// main is renamed and still executed. Includes actual compaction/admission.
#include "nr_residency_policy.h"
#include <iterator>
bool g_nr_residency_drain_pending=false;
std::atomic<unsigned> g_nr_context_releases_in_progress{0};
std::atomic<unsigned long long> g_nr_create_release_deferrals{0};
namespace nr_processing_runtime { unsigned blocks=0;void BlockSource(){++blocks;} }
enum class BypassReason {LifecycleIsolation,ContextReleasePending};
void SetBypassReason(BypassReason){}
#include "compaction.inl"
void compact_setup(){
 clear();g_nr_residency_drain_pending=false;g_nr_context_releases_in_progress=0;
 auto& root=g_contexts[7];root.sr_handle=&carriers[0];root.instance_id=101;root.feature_generation=9;
 for(unsigned i=0;i<4;++i){auto& child=g_contexts[i];child.sr_handle=root.sr_handle;child.layer_index=i+1;child.parent_instance_id=101;child.instance_id=102+i;child.feature_generation=9;}
 auto& other=g_contexts[12];other.sr_handle=&carriers[1];other.instance_id=201;other.feature_generation=10;other.mode=2;
}
int main(int argc,char** argv){
 check(OriginalParentMain(argc,argv)==0,"inherited parent retirement checks");
 compact_setup();ReconcileNrResidencyLocked(5,5,false);
 check(!g_nr_residency_drain_pending&&!g_contexts[7].retired,"F6 same selection discards warm cache");
 ReconcileNrResidencyLocked(5,1,false);
 check(g_nr_residency_drain_pending&&g_contexts[7].retired&&g_contexts[12].retired,"downsize did not retire complete old owner closure");
 check(g_contexts[0].retired&&g_contexts[3].retired,"downsize did not mark children while OFF");
 ReconcileNrResidencyLocked(1,5,false);
 check(g_nr_residency_drain_pending&&g_contexts[7].retired,"quick re-enable undid retirement");
 // Complete children and all other roots. The remaining original root is
 // still recording: a NEW carrier must not bypass the global compaction.
 g_contexts[7].recording_command_lists=1;
 CollectRetiredContextsLocked();for(auto& c:g_pending_destroy_contexts)c={};
 check(DeferNrCreationForReleaseLocked(&carriers[2]),"cross-carrier Create escaped old in-flight compaction");
 g_contexts[7].recording_command_lists=0;
 check(DeferNrCreationForReleaseLocked(&carriers[2]),"queued outside-lock Release incorrectly treated as already released");
 check(scheduled(101)==1,"ready old root not queued");
 for(auto& c:g_pending_destroy_contexts)c={};
 g_nr_context_releases_in_progress=1;
 check(DeferNrCreationForReleaseLocked(&carriers[2]),"outside-lock Release overlap admitted");
 g_nr_context_releases_in_progress=0;
 check(!DeferNrCreationForReleaseLocked(&carriers[2])&&!g_nr_residency_drain_pending,"completed global drain never resumed");
 compact_setup();ReconcileNrResidencyLocked(5,5,true);
 check(g_nr_residency_drain_pending&&g_contexts[7].retired&&g_contexts[12].retired,"explicit route change kept old modes");
 // Every failure remains a stop, not an eviction permission.
 for(unsigned reason=0;reason<3;++reason){compact_setup();ReconcileNrResidencyLocked(5,1,false);
  if(reason==0)g_contexts[7].create_dependencies_quarantined=true;
  if(reason==1)g_contexts[7].lifecycle_dependencies_quarantined=true;
  if(reason==2)g_lifecycle_tracking_uncertain=true;
  check(DeferNrCreationForReleaseLocked(&carriers[2])&&g_nr_residency_drain_pending,"unknown/quarantine closure permitted compaction Create");
 }
 clear();g_nr_residency_drain_pending=false;ReconcileNrResidencyLocked(5,1,false);
 check(!g_nr_residency_drain_pending,"empty session forced nonexistent drain");
 std::puts("PASS global compaction: same-F6, OFF downsize, explicit route, cross-carrier in-flight, pending, outside-lock Release, rapid reselect and quarantine");
}
