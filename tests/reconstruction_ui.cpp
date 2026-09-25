// Exercise the actual production UI with deterministic input/layout services.
// This is a host contract test, not a rendered ReShade or game screenshot.
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "nr_reconstruction_policy.h"
struct ImVec2 {float x,y;ImVec2(float a=0,float b=0):x(a),y(b){}};
struct ImVec4 {};
enum {ImGuiCol_Button,ImGuiCol_Header};
struct ChainConfig {int reconstruction_mode=0;bool near_black_chroma_guard=false;std::array<unsigned,32> unchanged{};};
enum class NrColorPolicyStatus {Current,Stale};
struct Policy {int recorded_reconstruction_mode=0;bool recorded_near_black_guard=false;
    nr_reconstruction::Reason reconstruction_reason=nr_reconstruction::Reason::Off,near_black_reason=nr_reconstruction::Reason::Off;};
struct Snapshot {unsigned config_generation=7;NrColorPolicyStatus status=NrColorPolicyStatus::Current;Policy policy;} snapshot;
Snapshot SnapshotColorPolicy(){return snapshot;}
static bool chinese=true;static float available=400;
const char* NrUi(const char* en,const char* zh){return chinese?zh:en;}
struct ButtonRecord {std::string label;float width,height;bool selected;};
static std::string action;static std::vector<std::string> text,tips,ids,checks;
static std::vector<ButtonRecord> buttons;static int styleDepth=0,sameLines=0,clicks=0;static unsigned assertions=0;
void check(bool value,const char* why){++assertions;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
float SettingsContentWidth(){return available;}
void SettingsSection(const char* s){text.emplace_back(s);}
void SettingsNote(const char* s){text.emplace_back(s);}
void SettingsTooltip(const char* s){tips.emplace_back(s);}
namespace ImGui {
struct Style {ImVec2 ItemSpacing{8,4};} style;
const Style& GetStyle(){return style;}
float GetFrameHeight(){return 24.f;}
ImVec4 GetStyleColorVec4(int){return {};}
void PushStyleColor(int,ImVec4){++styleDepth;}
void PopStyleColor(){check(styleDepth>0,"unbalanced style pop");--styleDepth;}
void PushID(const char* s){ids.emplace_back(s);}
void PushID(int x){ids.push_back(std::to_string(x));}
void PopID(){check(!ids.empty(),"unbalanced ID pop");ids.pop_back();}
void SameLine(){++sameLines;}
bool Checkbox(const char* label,bool* value){
 checks.emplace_back(label);const std::string s=label;
 const bool hit=(action=="detail"&&s.find("##reconstruction-enable")!=s.npos)||
    (action=="noise"&&s.find("##reconstruction-dark-noise")!=s.npos);
 if(hit){*value=!*value;++clicks;}return hit;
}
bool Button(const char* label,ImVec2 size){
 buttons.push_back({label,size.x,size.y,styleDepth>0});
 check(ids.size()==2&&ids.front()=="nr-reconstruction-quality","quality button stable ID scope");
 bool hit=action=="quality"+ids.back();if(hit)++clicks;return hit;
}
void TextWrapped(const char* format,...){char out[1600];va_list args;va_start(args,format);std::vsnprintf(out,sizeof(out),format,args);va_end(args);text.emplace_back(out);}
}
#include "nr_reconstruction_ui.inl"
static void reset(const char* event="") {action=event;text.clear();tips.clear();ids.clear();checks.clear();buttons.clear();styleDepth=sameLines=clicks=0;snapshot={};}
static bool has(const std::vector<std::string>& rows,const std::string& needle){for(auto& s:rows)if(s.find(needle)!=s.npos)return true;return false;}
static ChainConfig config(int mode,bool guard){ChainConfig c;c.reconstruction_mode=mode;c.near_black_chroma_guard=guard;for(unsigned i=0;i<c.unchanged.size();++i)c.unchanged[i]=0xFA000000+i*13;return c;}
static void isolated(const ChainConfig& before,const ChainConfig& after){check(before.unchanged==after.unchanged,"unrelated settings changed");check(styleDepth==0&&ids.empty(),"UI stack leak");}
int main(){
 for(bool zh:{false,true}) {chinese=zh;
  for(int mode:{0,1,2})for(bool guard:{false,true}) {
   reset();auto c=config(mode,guard);const auto before=c;bool changed=false,discrete=false;
   DrawReconstructionControls(c,7,false,changed,discrete);isolated(before,c);
   check(c.reconstruction_mode==mode&&c.near_black_chroma_guard==guard&&!changed&&!discrete,"viewing rewrites settings");
   check(checks.size()==2,"independent option count");
   check(buttons.size()==(mode?2u:0u)&&sameLines==(mode?1:0),"exactly two adjacent quality buttons");
   if(mode){check(buttons[0].width==196&&buttons[1].width==196&&buttons[0].height==buttons[1].height,"equal row geometry");check(buttons[mode-1].selected&&!buttons[2-mode].selected,"saved selected state");}
   check(has(tips,zh?"人脸、身体":"faces, bodies"),"noise tooltip misses people");
   check(has(tips,zh?"不一定能消除":"may remain"),"tooltip overpromises noise removal");
   check(has(tips,zh?"保留当前亮度和光影":"Keeps the selected lighting"),"preserved lighting not explained");
   check(!has(text,"source-guided-paired")&&!has(text,"recorded-not-pixel"),"technical diagnostics leaked to UI");
   reset("noise");c=before;changed=discrete=false;DrawReconstructionControls(c,7,false,changed,discrete);isolated(before,c);
   check(c.reconstruction_mode==mode&&c.near_black_chroma_guard!=guard&&changed&&discrete&&clicks==1,"noise toggle affects detail or missing transaction");
   reset("detail");c=before;changed=discrete=false;DrawReconstructionControls(c,7,false,changed,discrete);isolated(before,c);
   check(c.reconstruction_mode==(mode?0:1)&&c.near_black_chroma_guard==guard&&changed&&discrete&&clicks==1,"detail toggle affects noise or default");
   for(int target:{1,2})if(mode){auto a="quality"+std::to_string(target);reset(a.c_str());c=before;changed=discrete=false;DrawReconstructionControls(c,7,false,changed,discrete);isolated(before,c);
    check(c.reconstruction_mode==target&&c.near_black_chroma_guard==guard&&changed==(target!=mode)&&discrete==changed&&clicks==1,"quality choice couples options or rewrites unchanged state");}
  }
  // Disabled default and explicit legacy values are not silently normalized.
  reset();auto c=config(73,true);bool changed=false,discrete=false;DrawReconstructionControls(c,7,false,changed,discrete);check(c.reconstruction_mode==73&&c.near_black_chroma_guard&&!changed&&!discrete,"view normalized invalid saved value");
  for(int state:{0,1,2,3}){reset();c=config(2,true);changed=state==3;discrete=false;snapshot.config_generation=state==1?8:7;snapshot.status=state==2?NrColorPolicyStatus::Stale:NrColorPolicyStatus::Current;
   DrawReconstructionControls(c,7,state==0,changed,discrete);
   check(!has(text,zh?"最近处理记录":"Latest processing record"),"stale/request-only settings reported as current");check(has(text,zh?"等待当前设置":"Waiting for a processing record"),"missing honest pending state");check(changed==(state==3),"view cleared caller change state");}
  for(int r=0;r<=10;++r){reset();auto reason=static_cast<nr_reconstruction::Reason>(r);const auto* msg=ReconstructionUiReason(reason);check(msg&&std::string(msg).size()>3,"empty reason");if(zh)check(std::string(msg).find('-')==std::string::npos,"technical reason name exposed");}
  for(float width:{120.f,220.f,800.f}){reset();available=width;c=config(2,false);changed=discrete=false;DrawReconstructionControls(c,7,false,changed,discrete);
   check(buttons.size()==2&&sameLines==1&&buttons[0].width==buttons[1].width&&buttons[0].width*2+8<=width,"narrow/wide row overflow");}
  available=400;
 }
 std::printf("PASS reconstruction UI: %u assertions; bilingual actual UI functions, deterministic host input/layout; not rendered/game verification\n",assertions);
}
