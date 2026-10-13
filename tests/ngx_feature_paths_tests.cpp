#include "xrfg/ngx_feature_paths.hpp"
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_params.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
void test_paths(){
    using xrfg::NgxFeaturePaths;
    NgxFeaturePaths private_init(nullptr,L"layer",L"game");
    auto* info=private_init.get();
    require(info->PathListInfo.Length==2&&!wcscmp(info->PathListInfo.Path[0],L"game")&&
        !wcscmp(info->PathListInfo.Path[1],L"layer"),"private first init omitted game or reversed priority");
    const wchar_t* original[]={L"custom",L"LAYER"};
    NVSDK_NGX_FeatureCommonInfo caller{};caller.PathListInfo={original,2};
    caller.LoggingInfo.MinimumLoggingLevel=NVSDK_NGX_LOGGING_LEVEL_VERBOSE;
    NgxFeaturePaths augmented(&caller,L"layer",L"game");info=augmented.get();
    require(info->PathListInfo.Length==3&&info->PathListInfo.Path[0]==original[0]&&
        info->PathListInfo.Path[1]==original[1]&&!wcscmp(info->PathListInfo.Path[2],L"game"),
        "existing layer path suppressed game insertion or caller priority changed");
    require(caller.PathListInfo.Length==2&&caller.PathListInfo.Path==original&&
        info->LoggingInfo.MinimumLoggingLevel==caller.LoggingInfo.MinimumLoggingLevel,
        "caller contract mutated");
    const wchar_t* complete[]={L"GAME",L"layer"};caller.PathListInfo={complete,2};
    NgxFeaturePaths unchanged(&caller,L"LAYER",L"game");
    require(!unchanged.changed()&&unchanged.get()->PathListInfo.Length==2,"duplicate paths");
    require(!xrfg::ngx_application_directory().empty(),"actual executable directory");
    std::cout<<"NGX private/game-first search, caller precedence, duplicate and policy preservation PASS\n";
}

int hardware(const wchar_t* model,const wchar_t* layer,bool legacy){
    wchar_t core_path[32768]{};DWORD bytes=sizeof(core_path);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore",
        L"FullPath",RRF_RT_REG_SZ,nullptr,core_path,&bytes))return 77;
    wcscat_s(core_path,L"\\_nvngx.dll");
    const auto core=LoadLibraryExW(core_path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!core)return 77;
    using Init=NVSDK_NGX_Result(*)(unsigned long long,const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_FeatureCommonInfo*);
    using ProjectInit=NVSDK_NGX_Result(*)(const char*,NVSDK_NGX_EngineType,const char*,
        const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_FeatureCommonInfo*);
    using Capability=NVSDK_NGX_Result(*)(NVSDK_NGX_Parameter**);
    const auto init=reinterpret_cast<Init>(GetProcAddress(core,"NVSDK_NGX_D3D12_Init_Ext"));
    const auto private_init=reinterpret_cast<ProjectInit>(
        GetProcAddress(core,"NVSDK_NGX_D3D12_Init_ProjectID"));
    const auto capability=reinterpret_cast<Capability>(GetProcAddress(core,"NVSDK_NGX_D3D12_GetCapabilityParameters"));
    require(init&&private_init&&capability,"driver NGX ABI");
    ID3D12Device* device{};
    if(FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))return 77;
    xrfg::NgxFeaturePaths corrected(nullptr,layer,model);
    const wchar_t* old_paths[]={layer};NVSDK_NGX_FeatureCommonInfo old{};old.PathListInfo={old_paths,1};
    const auto logs=std::filesystem::temp_directory_path()/L"ofxr-ngx-discovery-test";
    std::filesystem::create_directories(logs);
    const auto result=private_init("2152e8d8-a3d7-4d4c-88a5-37f740f81036",
        NVSDK_NGX_ENGINE_TYPE_CUSTOM,"OFXR-Bridge",logs.c_str(),device,
        NVSDK_NGX_Version_API,legacy?&old:corrected.get());
    require(NVSDK_NGX_SUCCEED(result),"actual driver NGX initialization");
    auto query=[&]{NVSDK_NGX_Parameter* p{};int available=-1;
        require(NVSDK_NGX_SUCCEED(capability(&p))&&p,"driver capability block");
        require(NVSDK_NGX_SUCCEED(p->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&available)),"driver SR availability query");return available;};
    const auto first=query();require(first==(legacy?0:1),"first NGX initialization reported wrong native DLSS availability");
    require(NVSDK_NGX_SUCCEED(init(102941618,logs.c_str(),device,NVSDK_NGX_Version_API,corrected.get())),"later game initialization");
    const auto later=query();require(later==first,"driver no longer retains first initialization search paths");
    std::cout<<"actual NVIDIA NGX "<<(legacy?"legacy layer-only":"corrected game + layer")
        <<": SR first="<<first<<" after game init="<<later<<" PASS\n";
    device->Release();return 0;
}
int wmain(int argc,wchar_t** argv){try{
    if(argc==4)return hardware(argv[2],argv[3],!wcscmp(argv[1],L"legacy"));
    test_paths();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
