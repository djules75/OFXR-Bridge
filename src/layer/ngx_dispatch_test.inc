// Only compiled into the native-ingress test executable; never shipping DLL.
struct DispatchParameters final:NVSDK_NGX_Parameter {
 bool present{true};int flags{NVSDK_NGX_DLSS_Feature_Flags_MVLowRes};
 unsigned width{1280},height{720},out_width{2560},out_height{1440};
 ID3D12Resource *output{},*motion{},*depth{};
 void Set(const char*, unsigned long long ) override {}
 NVSDK_NGX_Result Get(const char* key, unsigned long long* value) const override {
 return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 void Set(const char*, float ) override {}
 NVSDK_NGX_Result Get(const char* key, float* value) const override {
 return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 void Set(const char*, double ) override {}
 NVSDK_NGX_Result Get(const char* key, double* value) const override {
 return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 void Set(const char*, unsigned int ) override {}
 NVSDK_NGX_Result Get(const char* key, unsigned int* value) const override {
 if(present){if(!std::strcmp(key,NVSDK_NGX_Parameter_Width))*value=width;else if(!std::strcmp(key,NVSDK_NGX_Parameter_Height))*value=height;else if(!std::strcmp(key,NVSDK_NGX_Parameter_OutWidth))*value=out_width;else if(!std::strcmp(key,NVSDK_NGX_Parameter_OutHeight))*value=out_height;else return NVSDK_NGX_Result_FAIL_InvalidParameter;return NVSDK_NGX_Result_Success;}
 return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 void Set(const char*, int ) override {}
 NVSDK_NGX_Result Get(const char* key, int* value) const override {
 if(present&&!std::strcmp(key,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags)){*value=flags;return NVSDK_NGX_Result_Success;}
 return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 void Set(const char*, ID3D11Resource * ) override {}
 NVSDK_NGX_Result Get(const char* key, ID3D11Resource ** value) const override {
 return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 void Set(const char*, ID3D12Resource * ) override {}
 NVSDK_NGX_Result Get(const char* key, ID3D12Resource ** value) const override {
 if(!std::strcmp(key,NVSDK_NGX_Parameter_Output)&&output){*value=output;return NVSDK_NGX_Result_Success;}
 if(!std::strcmp(key,NVSDK_NGX_Parameter_MotionVectors)&&motion){*value=motion;return NVSDK_NGX_Result_Success;}
 if(!std::strcmp(key,NVSDK_NGX_Parameter_Depth)&&depth){*value=depth;return NVSDK_NGX_Result_Success;}
 return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 void Set(const char*, void * ) override {}
 NVSDK_NGX_Result Get(const char* key, void ** value) const override {
 return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 void Reset() override {present=false;}
};
std::uint64_t nested_dispatch_entry{}, native_dispatch_entry{}, nested_dispatch_exit{};
unsigned nested_native_slot{};
std::uint64_t native_dispatch_production{};
bool native_dispatch_fail{};
bool nested_reverse_dimensions{};
bool nested_forward_alias{};
const NVSDK_NGX_Handle* nested_alias{};
ID3D12GraphicsCommandList* nested_list_override{};
Feature native_dispatch_metadata{};
const NVSDK_NGX_Handle* direct_creation_handle{};
NVSDK_NGX_Result direct_creation_original(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,
    NVSDK_NGX_Parameter* parameters,NVSDK_NGX_Handle** output) {
    if(!output)return NVSDK_NGX_Result_FAIL_InvalidParameter;
    *output=const_cast<NVSDK_NGX_Handle*>(direct_creation_handle);
    if(parameters)parameters->Reset();
    return NVSDK_NGX_Result_Success;
}
NVSDK_NGX_Result forwarding_generic_original(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* params,void* callback) {
    return nested_native_slot==0?evaluate<0>(list,handle,params,callback):evaluate<1>(list,handle,params,callback);
}
NVSDK_NGX_Result nested_native_original(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter*,void*) {
    native_dispatch_entry=native_ingress::upscaler_observation().evaluations;
    {std::scoped_lock lock(state().mutex);
        native_dispatch_production=state().native_production_sequences.at(handle);
        native_dispatch_metadata=state().features.at(handle);}
    return native_dispatch_fail?NVSDK_NGX_Result_Fail:NVSDK_NGX_Result_Success;
}
NVSDK_NGX_Result nested_generic_original(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* params,void* callback) {
    nested_dispatch_entry=native_ingress::upscaler_observation().evaluations;
    if(nested_reverse_dimensions) {
        auto& map=*static_cast<DispatchParameters*>(const_cast<NVSDK_NGX_Parameter*>(params));
        map.present=true;
        std::swap(map.width,map.out_width);std::swap(map.height,map.out_height);
    }
    const auto* native_handle=nested_alias?nested_alias:handle;
    auto* native_list=nested_list_override?nested_list_override:list;
    const auto result=nested_forward_alias?evaluate<2>(native_list,native_handle,params,callback):
        nested_native_slot==0?evaluate<0>(native_list,native_handle,params,callback):
            evaluate<1>(native_list,native_handle,params,callback);
    // This is where an injector can copy/submit the native output, before the
    // surrounding generic API returns. Observations must already be native.
    nested_dispatch_exit=native_ingress::upscaler_observation().evaluations;
    return result;
}
int run_luke_ngx_dispatch_test(const wchar_t* path, bool engine = false) {
 const auto require=[](bool b,const char* message){if(!b)throw std::runtime_error(message);};
 try {
  test_kcd_production_identity=true;
  HMODULE fixture=LoadLibraryW(path);require(fixture!=nullptr,"fixture load");
  if(engine)require(!luke_dispatch_module_valid(fixture),"engine misidentified as REAL VR");
  else {
   require(luke_dispatch_module_valid(fixture),"REAL VR own NGX dispatch not recognized");
   require(luke_dispatch_module()==fixture,"custom filename REAL VR marker lookup");
  }
  require(!luke_dispatch_module_valid(GetModuleHandleW(L"kernel32.dll")),"unmarked unrelated module admitted");
  // Cache the actual callable entry points before hook installation.
  const auto make=reinterpret_cast<Create>(GetProcAddress(fixture,"NVSDK_NGX_D3D12_CreateFeature"));
  const auto eval=reinterpret_cast<Evaluate>(GetProcAddress(fixture,"NVSDK_NGX_D3D12_EvaluateFeature"));
  const auto retire=reinterpret_cast<Release>(GetProcAddress(fixture,"NVSDK_NGX_D3D12_ReleaseFeature"));
  DispatchParameters baseline;NVSDK_NGX_Handle* baseline_handle{};
  const auto baseline_count=native_ingress::upscaler_observation().evaluations;
  require(NVSDK_NGX_SUCCEED(make(nullptr,NVSDK_NGX_Feature_SuperSampling,&baseline,&baseline_handle)) &&
      NVSDK_NGX_SUCCEED(eval(nullptr,baseline_handle,&baseline,nullptr)),"baseline original cached dispatch");
  require(native_ingress::upscaler_observation().evaluations==baseline_count,
      "fixture did not reproduce unobserved REAL VR original dispatch");
  require(NVSDK_NGX_SUCCEED(retire(baseline_handle)),"baseline retirement");
  if(engine)install_dispatch<4>(fixture);else install_dispatch<3>(fixture);
  const auto slot=engine?4u:3u;
  require(state().modules[slot].retained!=nullptr&&!state().modules[slot].typed_implementation,"generic dispatch hook/type");
  DispatchParameters p;NVSDK_NGX_Handle* h{};
  require(NVSDK_NGX_SUCCEED(make(nullptr,NVSDK_NGX_Feature_SuperSampling,&p,&h)),"original create result");
  require(!p.present,"original parameter mutation suppressed");
  {std::scoped_lock lock(state().mutex);const auto& f=state().features.at(h);
   require(f.flags_known&&f.flags==p.flags&&f.width==1280&&f.out_height==1440&&f.creation_observed&&
       f.creation_source_identity==f.identity,"real creation contract lost after injector mutation");}
  const auto before=native_ingress::upscaler_observation().evaluations;
  require(NVSDK_NGX_SUCCEED(eval(nullptr,h,&p,nullptr)),"cached original evaluate result");
  require(native_ingress::upscaler_observation().evaluations==before+1,"cached REAL VR evaluation bypassed capture");
  require(state().native_production_sequences.at(h)==1,"generic-only SR did not allocate exact production");
  // No command list or guide resources exist here: these failed capture
  // attempts are still real SR productions and must not be compacted away.
  require(NVSDK_NGX_SUCCEED(eval(nullptr,h,&p,nullptr)) && state().native_production_sequences.at(h)==2,
      "failed guide capture reused production identity");
  const auto old_identity=state().features.at(h).identity;
  p.present=true;
  require(NVSDK_NGX_SUCCEED(make(nullptr,NVSDK_NGX_Feature_SuperSampling,&p,&h)) &&
      state().features.at(h).identity!=old_identity && !state().native_production_sequences.contains(h),
      "reused handle inherited previous native feature generation");
  require(NVSDK_NGX_SUCCEED(eval(nullptr,h,&p,nullptr)) && state().native_production_sequences.at(h)==1,
      "replacement feature did not begin its own production sequence");
  require(NVSDK_NGX_SUCCEED(retire(h))&&!state().features.contains(h),"native handle retirement");
  require(!state().native_production_sequences.contains(h),"native production stream survived feature release");
  p.present=true;require(NVSDK_NGX_SUCCEED(make(nullptr,NVSDK_NGX_Feature_FrameGeneration,&p,&h)),"FG creation");
  require(NVSDK_NGX_SUCCEED(eval(nullptr,h,&p,nullptr)),"FG passthrough");
  require(native_ingress::upscaler_observation().evaluations==before+3 && !state().native_production_sequences.contains(h),"generic FG guessed as SR");
  require(NVSDK_NGX_SUCCEED(retire(h)),"FG retirement");
  // A missed creation at a generic injector must not guess feature type.
  require(NVSDK_NGX_SUCCEED(eval(nullptr,h,&p,nullptr)),"unknown generic passthrough");
  require(native_ingress::upscaler_observation().evaluations==before+3 && !state().native_production_sequences.contains(h),"unknown generic guessed as SR");
  // C-callback evaluation has a separate public entry and may bypass the legacy body.
  const auto eval_c=reinterpret_cast<Evaluate>(GetProcAddress(fixture,"NVSDK_NGX_D3D12_EvaluateFeature_C"));
  require(eval_c!=nullptr,"C callback export");
  p.present=true;require(NVSDK_NGX_SUCCEED(make(nullptr,NVSDK_NGX_Feature_SuperSampling,&p,&h)),"C SR creation");
  const auto before_c=native_ingress::upscaler_observation().evaluations;
  const auto last_c=reinterpret_cast<void*(*)()>(GetProcAddress(fixture,"FixtureLastCCallback"));
  auto* callback=reinterpret_cast<void*>(std::uintptr_t{0x77});
  require(last_c&&NVSDK_NGX_SUCCEED(eval_c(nullptr,h,&p,callback)),"C callback original result");
  require(last_c()==callback,"C callback argument changed");
  require(native_ingress::upscaler_observation().evaluations==before_c+1,"public C evaluation bypassed capture");
  require(state().native_production_sequences.at(h)==1,"public C SR production missing");
  require(NVSDK_NGX_SUCCEED(retire(h)),"C retirement");
  p.present=true;require(NVSDK_NGX_SUCCEED(make(nullptr,NVSDK_NGX_Feature_FrameGeneration,&p,&h)),"C FG create");
  require(NVSDK_NGX_SUCCEED(eval_c(nullptr,h,&p,callback)),"C FG passthrough");
  require(native_ingress::upscaler_observation().evaluations==before_c+1,"C FG guessed as SR");
  require(NVSDK_NGX_SUCCEED(retire(h)),"C FG retirement");
  require(NVSDK_NGX_SUCCEED(eval_c(nullptr,h,&p,callback)),"C unknown passthrough");
  require(native_ingress::upscaler_observation().evaluations==before_c+1,"C unknown guessed as SR");
  for(unsigned native_slot=0;native_slot<2;++native_slot) {
   const auto type=native_slot==0?NVSDK_NGX_Feature_SuperSampling:NVSDK_NGX_Feature_RayReconstruction;
   p.present=true;require(NVSDK_NGX_SUCCEED(make(nullptr,type,&p,&h)),"nested SR/RR create");
   const auto saved_generic=state().modules[slot].evaluate;
   const auto saved_native=state().modules[native_slot].evaluate;
   const auto saved_typed=state().modules[native_slot].typed_implementation;
   nested_native_slot=native_slot;
   state().modules[slot].evaluate=&nested_generic_original;
   state().modules[native_slot].evaluate=&nested_native_original;
   state().modules[native_slot].typed_implementation=true;
   const auto before_nested=native_ingress::upscaler_observation().evaluations;
   require(NVSDK_NGX_SUCCEED(eval(nullptr,h,&p,callback)),"nested cached original result");
   require(nested_dispatch_entry==before_nested,"guide capture still occurs at outer injector boundary");
   require(native_dispatch_entry==before_nested+1 && nested_dispatch_exit==before_nested+1,
       "native SR/RR observation unavailable before injector copies output or duplicated");
   require(native_dispatch_production==1 && state().native_production_sequences.at(h)==1,
       "nested generic/native dispatch allocated more than one SR production");
   native_dispatch_fail=true;
   require(NVSDK_NGX_FAILED(eval(nullptr,h,&p,callback)) && native_dispatch_production==2 &&
       state().native_production_sequences.at(h)==2,"failed native SR result or production identity lost");
   native_dispatch_fail=false;
   require(NVSDK_NGX_SUCCEED(eval(nullptr,h,&p,callback)) && native_dispatch_production==3 &&
       state().native_production_sequences.at(h)==3,"native failure compacted out of production order");
   state().modules[slot].evaluate=saved_generic;
   state().modules[native_slot].evaluate=saved_native;
   state().modules[native_slot].typed_implementation=saved_typed;
   require(NVSDK_NGX_SUCCEED(retire(h)),"nested SR/RR retirement");
  }

  // Cached public dispatch replaces the opaque handle and reverses the
  // creation-only dimension keys before the actual native SR/RR callback.
  // Use real command-list identities: metadata cannot join another list.
  ComPtr<IDXGIFactory6> factory;ComPtr<IDXGIAdapter> warp;
  ComPtr<ID3D12Device> device;
  require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))&&
      SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))&&
      SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))),
      "metadata invocation WARP device");
  std::array<ComPtr<ID3D12CommandAllocator>,2> allocators;
  std::array<ComPtr<ID3D12GraphicsCommandList>,2> lists;
  for(unsigned index=0;index<lists.size();++index)
   require(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocators[index])))&&
       SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators[index].Get(),nullptr,
           IID_PPV_ARGS(&lists[index]))),"metadata invocation command lists");
  for(unsigned native_slot=0;native_slot<2;++native_slot) {
   const auto type=native_slot==0?NVSDK_NGX_Feature_SuperSampling:NVSDK_NGX_Feature_RayReconstruction;
   alignas(16) std::array<unsigned char,16> alias_storage{},wrong_list_storage{},direct_storage{};
   const auto* alias=reinterpret_cast<const NVSDK_NGX_Handle*>(alias_storage.data());
   const auto* wrong_list_alias=reinterpret_cast<const NVSDK_NGX_Handle*>(wrong_list_storage.data());
   const auto* direct_handle=reinterpret_cast<const NVSDK_NGX_Handle*>(direct_storage.data());
   p.present=true;p.width=1672;p.height=1368;p.out_width=3344;p.out_height=2736;
   require(NVSDK_NGX_SUCCEED(make(lists[0].Get(),type,&p,&h)),"alias public creation");
   const auto created=state().features.at(h);
   const auto saved_generic=state().modules[slot].evaluate;
   const auto saved_native=state().modules[native_slot].evaluate;
   const auto saved_typed=state().modules[native_slot].typed_implementation;
   const auto saved_forwarder=state().modules[2].evaluate;
   const auto saved_forwarder_typed=state().modules[2].typed_implementation;
   state().modules[slot].evaluate=&nested_generic_original;
   state().modules[native_slot].evaluate=&nested_native_original;
   state().modules[native_slot].typed_implementation=true;
   state().modules[2].evaluate=&forwarding_generic_original;
   state().modules[2].typed_implementation=false;
   nested_native_slot=native_slot;nested_alias=alias;nested_reverse_dimensions=true;
   // Recover the raw native handle before any enclosing creation receipt.
   // Later adopting a proven receipt cannot reuse this unproved generation.
   p.present=true;p.width=3344;p.height=2736;p.out_width=1672;p.out_height=1368;
   require(NVSDK_NGX_SUCCEED(native_slot==0?evaluate<0>(lists[0].Get(),alias,&p,callback):
       evaluate<1>(lists[0].Get(),alias,&p,callback)),"unobserved native alias recovery");
   require(!native_dispatch_metadata.creation_observed&&native_dispatch_metadata.creation_source_identity==0&&
       native_dispatch_production==1,"late native evaluation fabricated an observed creation");
   const auto unproved_identity=native_dispatch_metadata.identity;
   p.width=1672;p.height=1368;p.out_width=3344;p.out_height=2736;
   const auto before_alias=native_ingress::upscaler_observation().evaluations;
   require(NVSDK_NGX_SUCCEED(eval(lists[0].Get(),h,&p,callback)),"cached native alias result");
   require(p.width==3344&&p.height==2736&&p.out_width==1672&&p.out_height==1368,
       "original callback did not invert live creation-only keys");
   require(native_dispatch_entry==before_alias+1&&nested_dispatch_exit==before_alias+1,
       "native alias observation lost its boundary or duplicated");
   require(native_dispatch_metadata.creation_observed&&native_dispatch_metadata.width==1672&&
       native_dispatch_metadata.height==1368&&native_dispatch_metadata.out_width==3344&&
       native_dispatch_metadata.out_height==2736&&native_dispatch_metadata.identity!=created.identity&&
       native_dispatch_metadata.creation_source_identity==created.identity&&
       native_dispatch_metadata.identity!=unproved_identity,
       "typed alias trusted reversed evaluation dimensions instead of exact public creation");
   require(native_dispatch_production==1&&state().native_production_sequences.at(alias)==1,
       "public/native alias allocated wrong production identity");
   const auto first_alias_identity=native_dispatch_metadata.identity;

   // A proven new public creation can reuse a raw native alias. Refresh its
   // contract and generation rather than freezing the first preset forever.
   p.present=true;p.width=1559;p.height=1705;p.out_width=3056;p.out_height=3344;
   require(NVSDK_NGX_SUCCEED(make(lists[0].Get(),type,&p,&h)),"replacement public creation");
   const auto replacement=state().features.at(h);
   nested_forward_alias=true; // public A -> known inherited generic B -> typed B
   require(NVSDK_NGX_SUCCEED(eval(lists[0].Get(),h,&p,callback)),"replacement native alias result");
   require(native_dispatch_metadata.creation_observed&&native_dispatch_metadata.width==1559&&
       native_dispatch_metadata.out_width==3056&&native_dispatch_metadata.out_height==3344&&
       native_dispatch_metadata.creation_source_identity==replacement.identity&&
       native_dispatch_metadata.identity!=first_alias_identity&&native_dispatch_production==1,
       "reused native alias retained the previous creation generation/preset");
   nested_forward_alias=false;

   // A nested native call on another real list cannot inherit that receipt.
   nested_alias=wrong_list_alias;nested_list_override=lists[1].Get();
   p.present=true;p.width=1672;p.height=1368;p.out_width=3344;p.out_height=2736;
   require(NVSDK_NGX_SUCCEED(eval(lists[0].Get(),h,&p,callback)),"different native list result");
   require(!native_dispatch_metadata.creation_observed&&!native_dispatch_metadata.width&&
       !native_dispatch_metadata.out_width&&native_dispatch_metadata.flags_known&&
       ngx_display_dimension(native_dispatch_metadata.out_width,&p,NVSDK_NGX_Parameter_OutWidth,
           3344,native_dispatch_metadata.creation_observed)==3344,
       "different-list native handle inherited or promoted an unproved display contract");
   // An independently observed native creation is not an alias of outer A.
   const auto saved_direct_create=state().modules[native_slot].create;
   state().modules[native_slot].create=&direct_creation_original;direct_creation_handle=direct_handle;
   p.present=true;p.width=2048;p.height=1080;p.out_width=4096;p.out_height=2160;
   NVSDK_NGX_Handle* created_direct{};
   require(NVSDK_NGX_SUCCEED(native_slot==0?create<0>(lists[0].Get(),type,&p,&created_direct):
       create<1>(lists[0].Get(),type,&p,&created_direct))&&created_direct==direct_handle,
       "independent native production creation");
   state().modules[native_slot].create=saved_direct_create;direct_creation_handle=nullptr;
   const auto direct=state().features.at(direct_handle);
   nested_alias=direct_handle;nested_list_override=nullptr;nested_forward_alias=true;
   p.present=true;p.width=1672;p.height=1368;p.out_width=3344;p.out_height=2736;
   require(NVSDK_NGX_SUCCEED(eval(lists[0].Get(),h,&p,callback))&&
       native_dispatch_metadata.creation_source_identity==direct.identity&&
       native_dispatch_metadata.identity==direct.identity&&native_dispatch_metadata.out_width==4096&&
       native_dispatch_metadata.out_height==2160,"outer receipt overwrote independently observed native creation");
   nested_forward_alias=false;
   nested_alias=nullptr;nested_list_override=nullptr;nested_reverse_dimensions=false;
   state().modules[slot].evaluate=saved_generic;
   state().modules[native_slot].evaluate=saved_native;
   state().modules[native_slot].typed_implementation=saved_typed;
   state().modules[2].evaluate=saved_forwarder;
   state().modules[2].typed_implementation=saved_forwarder_typed;
   require(NVSDK_NGX_SUCCEED(retire(h)),"alias public retirement");
   {std::scoped_lock lock(state().mutex);
       state().features.erase(alias);state().features.erase(wrong_list_alias);state().features.erase(direct_handle);
       state().native_production_sequences.erase(alias);state().native_production_sequences.erase(wrong_list_alias);
       state().native_production_sequences.erase(direct_handle);}
  }
  for(auto& list:lists)require(SUCCEEDED(list->Close()),"close metadata invocation list");
  std::cout<<"Cached SR/RR aliases: original creation through exact native-list invocation, reversed keys/preset reuse isolation PASS\n";
  std::cout<<"Nested generic/native SR and RR: exact one native observation before injector copy PASS\n";
  std::cout<<"KCD production identity: generic/native ownership, failed captures/native calls, feature isolation PASS\n";
  std::cout<<"REAL VR cached NGX dispatch: original create/evaluate/release, pre-mutation SR metadata, FG/unknown isolation PASS\n";
  // Hooked fixture is pinned until process exit, matching shipping hook lifetime.
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

struct ActualNgxCaptureContext {
    Evaluate evaluate{};
    ID3D12GraphicsCommandList* list{};
    ID3D12CommandQueue* queue{};
    const NVSDK_NGX_Handle* handle{};
    DispatchParameters* parameters{};
    std::shared_ptr<const DlssMotionVectorFrame> frame;
};
ActualNgxCaptureContext* actual_ngx_capture_context{};
void actual_ngx_capture_observer(void* list,std::uint32_t feature,std::uint32_t frame,
    std::uint32_t viewport,const void*) {
    auto& c=*actual_ngx_capture_context;
    const auto camera=native_ingress::streamline_camera::current();
    if(list!=c.list||feature!=sl1::eFeatureDLSS||!camera.valid||camera.frame!=frame||camera.viewport!=viewport)
        throw std::runtime_error("actual NGX capture lost its original Streamline production camera");
    const auto before=dlss_motion_vector_statistics().published;
    if(NVSDK_NGX_FAILED(c.evaluate(c.list,c.handle,c.parameters,nullptr)))
        throw std::runtime_error("actual NGX capture changed original evaluation result");
    const auto guides=resolve_dlss_motion_vectors(1,c.queue);
    if(dlss_motion_vector_statistics().published!=before+1||!guides||!guides->eyes[0])
        throw std::runtime_error("actual NGX capture did not publish exact current guides");
    c.frame=guides->eyes[0];
    if(c.frame->camera_frame_token!=frame||!c.frame->camera_data_valid||!c.frame->camera_matrices_valid)
        throw std::runtime_error("actual NGX publication borrowed another production camera");
}
int run_actual_ngx_capture_test(const wchar_t* realvr,const wchar_t* streamline) {
    const auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto check=[&](HRESULT result,const char* message){require(SUCCEEDED(result),message);};
    try {
        const auto vr_module=LoadLibraryW(realvr),sl_module=LoadLibraryW(streamline);
        require(vr_module&&sl_module&&native_ingress::luke_detected(),"actual NGX capture fixtures");
        const auto observer=reinterpret_cast<void(*)(decltype(&actual_ngx_capture_observer))>(
            GetProcAddress(sl_module,"TestObserver"));
        const auto set_constants=reinterpret_cast<decltype(&sl1::slSetConstants)>(GetProcAddress(sl_module,"slSetConstants"));
        const auto evaluate_sl=reinterpret_cast<decltype(&sl1::slEvaluateFeature)>(GetProcAddress(sl_module,"slEvaluateFeature"));
        require(observer&&set_constants&&evaluate_sl,"actual NGX capture original Streamline exports");
        observer(&actual_ngx_capture_observer);native_ingress::poll_streamline();
        require(native_ingress::streamline_camera::hooked(),"actual NGX capture camera ABI hook");
        install_dispatch<3>(vr_module);
        const auto create_feature=reinterpret_cast<Create>(GetProcAddress(vr_module,"NVSDK_NGX_D3D12_CreateFeature"));
        const auto evaluate_feature=reinterpret_cast<Evaluate>(GetProcAddress(vr_module,"NVSDK_NGX_D3D12_EvaluateFeature"));
        require(create_feature&&evaluate_feature,"actual NGX capture original cached NGX exports");
        ComPtr<ID3D12Debug> debug;
        if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
        ComPtr<IDXGIFactory6> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"actual capture factory");
        check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"actual capture WARP");
        check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"actual capture device");
        D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;
        check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"actual capture queue");
        check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"actual capture fence");
        HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(done!=nullptr,"actual capture event");
        UINT64 fence_value{};
        const auto wait=[&] {
            check(queue->Signal(fence.Get(),++fence_value),"actual capture submit fence");
            check(fence->SetEventOnCompletion(fence_value,done),"actual capture notify");
            require(WaitForSingleObject(done,10000)==WAIT_OBJECT_0,"actual capture GPU timeout");
        };
        const auto make_list=[&](ComPtr<ID3D12CommandAllocator>& allocator,ComPtr<ID3D12GraphicsCommandList>& list) {
            check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"actual capture allocator");
            check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"actual capture list");
        };
        // Only the real Execute of an observed producer list establishes the
        // Luke context. Queue creation alone and layer-private queues cannot.
        native_ingress::configure_queue(queue.Get());
        ComPtr<ID3D12CommandAllocator> bootstrap_allocator;ComPtr<ID3D12GraphicsCommandList> bootstrap_list;
        make_list(bootstrap_allocator,bootstrap_list);native_ingress::observe_luke_production(bootstrap_list.Get());
        check(bootstrap_list->Close(),"actual capture bootstrap close");
        ID3D12CommandList* bootstrap[]{bootstrap_list.Get()};queue->ExecuteCommandLists(1,bootstrap);wait();
        ComPtr<ID3D12Device> submitted_owner;ComPtr<ID3D12CommandQueue> submitted_queue;
        require(native_ingress::query_luke_context(submitted_owner.GetAddressOf(),submitted_queue.GetAddressOf())&&
            d3d_owner::same(submitted_owner.Get(),device.Get())&&same(submitted_queue.Get(),queue.Get()),
            "actual capture did not retain its original executed native producer");

        const auto texture=[&](unsigned width,unsigned height,DXGI_FORMAT format,
            D3D12_RESOURCE_STATES initial,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE) {
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;
            desc.Format=format;desc.Flags=flags;ComPtr<ID3D12Resource> resource;
            check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,initial,nullptr,IID_PPV_ARGS(&resource)),"actual capture texture");
            return resource;
        };
        auto color=texture(512,32,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto motion=texture(7,3,DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto depth=texture(7,3,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        DispatchParameters parameters;parameters.width=7;parameters.height=3;
        parameters.out_width=512;parameters.out_height=32;
        parameters.flags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        parameters.output=color.Get();parameters.motion=motion.Get();parameters.depth=depth.Get();
        NVSDK_NGX_Handle* handle{};
        require(NVSDK_NGX_SUCCEED(create_feature(bootstrap_list.Get(),NVSDK_NGX_Feature_SuperSampling,&parameters,&handle)),"actual native SR creation");
        parameters.present=true;
        {std::scoped_lock lock(state().mutex);state().queue=queue;state().active=true;}
        configure_dlss_motion_vector_tracking(true);configure_luke_color_origin_extent(512,32);
        require(embedded::afw_snapshot().desired.mode!=0,"actual capture test requires its AFW defaults");
        ActualNgxCaptureContext context{evaluate_feature,nullptr,queue.Get(),handle,&parameters};
        actual_ngx_capture_context=&context;
        const auto transition=[](ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource=resource;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore=before;barrier.Transition.StateAfter=after;return barrier;
        };
        std::vector<std::shared_ptr<const DlssMotionVectorFrame>> retained;
        for(unsigned n=1;n<=3;++n) {
            ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
            make_list(allocator,list);context.list=list.Get();
            std::array<ComPtr<ID3D12Resource>,2> uploads;
            for(unsigned index=0;index<2;++index) {
                auto* resource=index?depth.Get():motion.Get();const auto desc=resource->GetDesc();
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};
                device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
                D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
                D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
                buffer.Width=bytes;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;
                buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,IID_PPV_ARGS(&uploads[index])),"actual capture input upload");
                void* data{};check(uploads[index]->Map(0,nullptr,&data),"actual capture upload map");
                std::memset(data,0,static_cast<std::size_t>(bytes));
                const float values[]{index?.125F*n:.125F+.01F*n,-.25F};
                const unsigned pixel_bytes=index?4:8;
                for(unsigned y=0;y<3;++y)for(unsigned x=0;x<7;++x)
                    std::memcpy(static_cast<char*>(data)+footprint.Offset+y*footprint.Footprint.RowPitch+x*pixel_bytes,values,pixel_bytes);
                uploads[index]->Unmap(0,nullptr);
                auto before=transition(resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
                list->ResourceBarrier(1,&before);
                D3D12_TEXTURE_COPY_LOCATION source{},destination{};source.pResource=uploads[index].Get();
                source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;source.PlacedFootprint=footprint;
                destination.pResource=resource;destination.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                list->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
                auto after=transition(resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                list->ResourceBarrier(1,&after);
            }
            sl1::Constants constants{};
            for(auto* matrix:{&constants.cameraViewToClip,&constants.clipToCameraView,&constants.clipToPrevClip,&constants.prevClipToClip}) {
                std::memset(matrix,0,sizeof(*matrix));
                auto* values=reinterpret_cast<float*>(matrix);values[0]=values[5]=values[10]=values[15]=1;
            }
            constants.cameraPos={float(n),0,0};constants.cameraRight={1,0,0};constants.cameraUp={0,1,0};constants.cameraFwd={0,0,-1};
            constants.cameraNear=.1F;constants.cameraFar=100;constants.cameraFOV=1;
            constants.depthInverted=static_cast<decltype(constants.depthInverted)>(1);
            require(set_constants(constants,n,0)&&evaluate_sl(reinterpret_cast<sl1::CommandBuffer*>(list.Get()),sl1::eFeatureDLSS,n,0),
                "actual capture original Streamline results");
            require(context.frame&&context.frame->serial==n&&context.frame->camera_frame_token==n&&
                context.frame->motion_retention&&context.frame->depth_retention&&
                d3d_owner::same(context.frame->motion_snapshot_device.Get(),device.Get())&&
                d3d_owner::same(context.frame->depth_snapshot_device.Get(),device.Get())&&
                same(context.frame->producer_queue.Get(),queue.Get())&&same(context.frame->producer_command_list.Get(),list.Get()),
                "actual capture lost one of its queried owners, exact list/queue or owned snapshots");
            check(list->Close(),"actual capture production close");ID3D12CommandList* submitted[]{list.Get()};
            queue->ExecuteCommandLists(1,submitted);wait();
            std::uint32_t token{};check(read_luke_color_origin(queue.Get(),color.Get(),&token,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,5000),
                "actual capture completed native color marker read");
            require(token==context.frame->color_origin_token,"actual capture color marker did not carry its exact publication");
            if(n==3) {
                const auto joined=resolve_luke_guide_token(token,queue.Get(),device.Get());
                require(joined&&joined->publication==context.frame->publication&&joined->producer_fence_value&&
                    joined->previous_opposite_guide&&joined->previous_opposite_guide->camera_frame_token==2&&
                    joined->previous_same_native_camera&&joined->previous_same_native_camera->token==1,
                    "actual capture submission did not retain exact fenced current/N-1/N-2 cameras");
            }
            retained.push_back(context.frame);
        }
        require(dlss_motion_vector_statistics().published==3,"actual capture count changed during native callbacks");
        {std::scoped_lock lock(state().mutex);state().active=false;state().queue.Reset();}
        native_ingress::stop_queue(queue.Get());configure_dlss_motion_vector_tracking(false);
        retire_disabled_dlss_motion_vector_resources();configure_luke_color_origin_extent(0,0);
        observer(nullptr);actual_ngx_capture_context=nullptr;CloseHandle(done);
        ComPtr<ID3D12InfoQueue> info;
        if(SUCCEEDED(device.As(&info)))for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T bytes{};info->GetMessage(i,nullptr,&bytes);std::vector<char> storage(bytes);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());check(info->GetMessage(i,message,&bytes),"actual capture debug message");
            require(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR&&message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,
                "actual capture D3D12 debug error");
        }
        std::cout<<"Actual native NGX capture: original scoped SL camera/NGX callback, four queried GPU owners, three owned publications, exact CRC colors and submission histories PASS\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
