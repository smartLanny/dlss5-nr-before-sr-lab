#include "SceneMeasurements.h"
namespace scene_bench {
enum class Mode {Functional,Capture,Perf};
Mode mode=Mode::Functional;
unsigned warm=2,observed=1;
bool Functional(){return mode==Mode::Functional;}
bool Capture(){return mode==Mode::Capture;}
bool Perf(){return mode==Mode::Perf;}
unsigned Total(){return warm+observed;}
scene_measurements::Recorder measurements;
scene_measurements::Recorder* active_measurement=nullptr;
std::vector<std::string> capture_files;
void Consume(ID3D12Fence* fence,unsigned long long value,bool reset_proven){
 if(!active_measurement)return;
 queue_test::Check(active_measurement->Consume(fence,value,reset_proven),"measurement only after actual exact completed fence and successful Reset");
 active_measurement=nullptr;
}
}
