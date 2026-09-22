// Appended to the shared OS/NGX service fixture, with actual extracted Core
// admission/retirement/tuning functions. This does not execute an opaque model.
int main(){
 unsigned cases=0;
 for(unsigned field=0;field<9;++field){
  const auto lookup_selected=[&](){if(field!=8)return lookup(640,360);
   EnterCriticalSection(&g_chain_cs);auto* c=FindOrCreateContext(&carriers[0],3,2,640,360,CodecColorMode::Automatic,guides,false);
   LeaveCriticalSection(&g_chain_cs);return c;};
  clear();guides={};auto* old=lookup_selected();check(old,"initial production admission");
  old->instance_id=700+field;const auto old_signature=old->tuning_signature;
  check(lookup_selected()==old,"same settings retain model identity");
  switch(field){
   case 0:g_config.intensity=1.7f;break;case 1:g_config.local_tone=.8f;break;
   case 2:g_config.local_structure=.7f;break;case 3:g_config.skin_structure=.6f;break;
   case 4:g_config.auto_mask=false;break;case 5:g_config.style=2;break;
   case 6:g_config.ui_correction=false;break;case 7:g_config.jitter_mode=NrJitterMode::Legacy;break;
   case 8:g_config.nr_input_filter=1;break;
  }
  const auto selected=g_config;const auto requested=CurrentNrTuningSignature();
  check(!SameNrTuningSignature(old_signature,requested),"changed effective model contract");
  check(!lookup_selected(),"live setting change defers fresh allocation until ready old Release");
  check(released.empty()&&g_pending_destroy_contexts[0].instance_id==700+field,
    "live update did not mutate or destroy old model under chain lock");
  check(SameNrTuningSignature(g_pending_destroy_contexts[0].tuning_signature,old_signature),
    "old pending model keeps immutable Create signature");
  check(nr_processing_runtime::Effective()==nr_processing::Start::After,"replacement wait retains native route");
  DrainPendingContextDestructions();auto* replacement=lookup_selected();
  check(replacement&&SameNrTuningSignature(replacement->tuning_signature,requested),"replacement takes complete current signature");
  const auto rebuilt=replacement->tuning_signature;
  clear();guides={};g_config=selected;auto* cold=lookup_selected();
  check(cold&&SameNrTuningSignature(cold->tuning_signature,rebuilt),"cold and live-recreated admission signatures agree");
  ++cases;
 }
 clear();guides={};auto* post=lookup(640,360);g_config.nr_input_filter=1;
 check(lookup(640,360)==post&&post->tuning_signature.input_filter==0,"pre input filter does not recreate an unaffected post model");++cases;
 clear();guides={};auto* current=lookup(640,360);current->guide_history.CommitOnEvaluate(guides,true);
 guides.motion_scale_x=.5f;check(lookup(640,360)==current,"mapping-only guide change keeps allocation");
 check(current->guide_history.NeedsReset(guides),"mapping change requires Reset");
 current->guide_history.CommitOnEvaluate(guides,false);check(current->guide_history.NeedsReset(guides),"failed Evaluate cannot consume guide Reset");
 current->guide_history.CommitOnEvaluate(guides,true);check(!current->guide_history.NeedsReset(guides),"only successful Evaluate commits guide mapping");++cases;
 for(unsigned storage=0;storage<3;++storage){
  clear();guides={};auto* old=lookup(640,360);old->instance_id=800+storage;
  if(storage==0)guides.motion_format=16;if(storage==1)guides.motion_bound_width=320;if(storage==2)guides.normalized_create_flags=1;
  check(!lookup(640,360),"guide storage, bound raster or Create flags retire the old instance");
  DrainPendingContextDestructions();check(lookup(640,360)!=nullptr,"cold-compatible guide allocation after retirement");++cases;
 }
 clear();guides={};auto* old=lookup(640,360);old->instance_id=900;check(!lookup(480,270),"work size change recreates");
 DrainPendingContextDestructions();auto* resized=lookup(480,270);check(resized&&resized->width==480&&resized->height==270,"new work raster explicit");++cases;
 std::printf("PASS model boundary CPU cases=%u; actual Core admission/signature/retirement functions, no model output or GPU equivalence claim\n",cases);
}
