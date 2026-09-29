#ifndef NR_TEMPORAL_STATUS_HEADER
#define NR_TEMPORAL_STATUS_HEADER "../src/nr_temporal_status.h"
#endif
#include NR_TEMPORAL_STATUS_HEADER
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
int main(){
 nr_temporal_status::AdmissionLogLimiter gate;unsigned count=0;
 for(unsigned frame=0;frame<100;++frame)for(unsigned reason=0;reason<32;++reason)
  count+=gate.Observe(1,reason);
 assert(count==18); // 2..6, 9..12, 14..22
 for(unsigned reason=0;reason<32;++reason)
  assert(gate.Observe(2,reason)==nr_temporal_status::AdmissionLogLimiter::Actionable(reason));
 assert(!gate.Observe(1,5));assert(!gate.Observe(2,5));assert(!gate.Observe(2,~0u));
 nr_temporal_status::AdmissionLogLimiter concurrent;std::atomic<unsigned> emitted{0};
 std::vector<std::thread> workers;
 for(unsigned t=0;t<8;++t)workers.emplace_back([&]{for(unsigned i=0;i<1000;++i){
  emitted.fetch_add(concurrent.Observe(7,3));emitted.fetch_add(concurrent.Observe(7,17));}});
 for(auto& worker:workers)worker.join();assert(emitted==2);
 nr_temporal_status::DetailLogLimiter detail;using D=nr_temporal_status::Detail;
 assert(!detail.Observe(1,D::None));
 assert(detail.Observe(1,D::MissingCandidate));assert(!detail.Observe(1,D::MissingCandidate));
 assert(detail.Observe(1,D::AmdContinuity));assert(detail.Observe(2,D::MissingCandidate));
 assert(!detail.Observe(1,D::TypedCandidate));
 nr_temporal_status::DetailLogLimiter concurrent_detail;std::atomic<unsigned> detail_emitted{0};workers.clear();
 for(unsigned t=0;t<8;++t)workers.emplace_back([&]{for(unsigned i=0;i<1000;++i)
  detail_emitted.fetch_add(concurrent_detail.Observe(7,D::AmdSnapshot));});
 for(auto& worker:workers)worker.join();assert(detail_emitted==1);
 std::puts("PASS reason bitset: 18 reasons/config, alternating suppression, stale-config suppression, concurrent once-only");
}
