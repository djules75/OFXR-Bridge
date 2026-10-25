#include <windows.h>
#include <d3d12.h>
#include <nvsdk_ngx_params.h>
#ifndef XRFG_ENGINE_DISPATCH_FIXTURE
extern "C" __declspec(dllexport) unsigned RealVRVersion(){return 1;}
#endif
namespace { void* last_c_callback{}; }
extern "C" __declspec(dllexport) void* FixtureLastCCallback(){return last_c_callback;}
namespace { alignas(16) unsigned char handle[16]{}; }
extern "C" __declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_NGX_D3D12_CreateFeature(
 ID3D12GraphicsCommandList*,NVSDK_NGX_Feature id,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** out) {
 if(!out)return NVSDK_NGX_Result_FAIL_InvalidParameter;
 *out=reinterpret_cast<NVSDK_NGX_Handle*>(handle);
 if(p)p->Reset(); // The native implementation consumes creation-only parameters.
 return NVSDK_NGX_Result_Success;
}
extern "C" __declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_NGX_D3D12_EvaluateFeature(
 ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,void*) {
 // Models an injector's cached original call: no observable NVIDIA export.
 volatile unsigned result=static_cast<unsigned>(NVSDK_NGX_Result_Success);
 return static_cast<NVSDK_NGX_Result>(result);
}
extern "C" __declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle*) {
 volatile unsigned result=static_cast<unsigned>(NVSDK_NGX_Result_Success);
 return static_cast<NVSDK_NGX_Result>(result);
}

extern "C" __declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_NGX_D3D12_EvaluateFeature_C(
 ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,void* callback) {
 last_c_callback=callback;
 static volatile LONG c_calls{};InterlockedIncrement(&c_calls);
 volatile unsigned result=static_cast<unsigned>(NVSDK_NGX_Result_Success);
 return static_cast<NVSDK_NGX_Result>(result);
}
