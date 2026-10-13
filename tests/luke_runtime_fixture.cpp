extern "C" __declspec(dllexport) unsigned RealVRVersion(){return 1;}
#include <vulkan/vulkan.h>
#include <cstring>
namespace {
PFN_vkGetDeviceProcAddr lower{};unsigned calls{},creates{},destroys{};
// Opaque NGX arguments keep this public-dispatch fixture independent of the
// vendor SDK. Their pointer/enum ABI matches the declared Vulkan NGX exports.
using NgxCreate=unsigned(*)(VkCommandBuffer,int,void*,void**);
using NgxCreate1=unsigned(*)(VkDevice,VkCommandBuffer,int,void*,void**);
using NgxEvaluate=unsigned(*)(VkCommandBuffer,const void*,const void*,void*);
using NgxRelease=unsigned(*)(void*);
NgxCreate ngx_create{};NgxCreate1 ngx_create1{};NgxEvaluate ngx_evaluate{};NgxRelease ngx_release{};
VkResult VKAPI_CALL create(VkDevice device,const VkImageCreateInfo* info,const VkAllocationCallbacks* allocator,VkImage* output) {
    ++creates;return reinterpret_cast<PFN_vkCreateImage>(lower(device,"vkCreateImage"))(device,info,allocator,output);
}
void VKAPI_CALL destroy(VkDevice device,VkImage image,const VkAllocationCallbacks* allocator) {
    ++destroys;reinterpret_cast<PFN_vkDestroyImage>(lower(device,"vkDestroyImage"))(device,image,allocator);
}
}
extern "C" __declspec(dllexport) void TestLukeVulkanNgxCallbacks(
    NgxCreate create,NgxCreate1 create1,NgxEvaluate evaluate,NgxRelease release) {
    ngx_create=create;ngx_create1=create1;ngx_evaluate=evaluate;ngx_release=release;
}
extern "C" __declspec(dllexport) unsigned NVSDK_NGX_VULKAN_CreateFeature(
    VkCommandBuffer command,int type,void* parameters,void** handle) {
    return ngx_create?ngx_create(command,type,parameters,handle):0xbad00005U;
}
extern "C" __declspec(dllexport) unsigned NVSDK_NGX_VULKAN_CreateFeature1(
    VkDevice device,VkCommandBuffer command,int type,void* parameters,void** handle) {
    return ngx_create1?ngx_create1(device,command,type,parameters,handle):0xbad00005U;
}
extern "C" __declspec(dllexport) unsigned NVSDK_NGX_VULKAN_EvaluateFeature(
    VkCommandBuffer command,const void* handle,const void* parameters,void* callback) {
    return ngx_evaluate?ngx_evaluate(command,handle,parameters,callback):0xbad00005U;
}
extern "C" __declspec(dllexport) unsigned NVSDK_NGX_VULKAN_ReleaseFeature(void* handle) {
    return ngx_release?ngx_release(handle):0xbad00005U;
}
extern "C" __declspec(dllexport) void TestLukeVulkanDispatcher(PFN_vkGetDeviceProcAddr dispatcher) {lower=dispatcher;}
extern "C" __declspec(dllexport) void TestLukeVulkanCounts(unsigned* get,unsigned* made,unsigned* retired) {
    *get=calls;*made=creates;*retired=destroys;
}
extern "C" __declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,const char* name) {
    ++calls;if(!lower)return nullptr;const auto original=lower(device,name);
    if(!original||!name)return original;
    if(!std::strcmp(name,"vkCreateImage"))return reinterpret_cast<PFN_vkVoidFunction>(&create);
    if(!std::strcmp(name,"vkDestroyImage"))return reinterpret_cast<PFN_vkVoidFunction>(&destroy);
    return original;
}
