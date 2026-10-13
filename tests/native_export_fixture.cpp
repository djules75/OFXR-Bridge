#include <windows.h>
extern "C" __declspec(dllexport) int NVSDK_NGX_D3D12_CreateFeature() {return 11;}
extern "C" __declspec(dllexport) int NVSDK_NGX_D3D12_EvaluateFeature() {return 13;}
extern "C" __declspec(dllexport) int NVSDK_NGX_D3D12_ReleaseFeature() {return 17;}
extern "C" __declspec(dllexport) int NativeExportFixtureValue=23;
