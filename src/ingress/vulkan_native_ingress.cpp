#include "xrfg/vulkan_native_ingress.hpp"
#include "xrfg/upscaler_observation.hpp"
#include "xrfg/vulkan_command_stream.hpp"
#include "xrfg/native_ingress.hpp"
#include "xrfg/neural_rendering.hpp"
#include "xrfg/embedded_control.hpp"
#include "xrfg/luke_color_origin.hpp"
#include "xrfg/luke_vulkan_scope.hpp"
#include "xrfg/bridge_flight_logger.hpp"
#include "xrfg/ngx_feature_metadata.hpp"
#include "xrfg/ngx_evaluation_boundary.hpp"
#include "xrfg/native_export_address.hpp"
#include "xrfg/native_ingress/streamline_camera.hpp"
#include "vulkan_device_request.hpp"
#include <nvsdk_ngx_vk.h>
#include <safetyhook.hpp>
#include <wrl/client.h>
#include <windows.h>
#include <array>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <bit>

namespace xrfg::native_vulkan {
namespace {
using Microsoft::WRL::ComPtr;
struct Device;
enum Role{color,motion,depth,output,role_count};
struct Surface {
    ComPtr<ID3D12Resource> buffer,texture;
    VkBuffer imported{};VkDeviceMemory memory{};
    NVSDK_NGX_ImageViewInfo_VK source{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    bool native_used{};
};
struct Slot {
    std::array<Surface,role_count> surfaces;
    ComPtr<ID3D12Fence> fence,d12_done;VkSemaphore timeline{};
    std::uint64_t value{},final_value{},d12_value{};
    unsigned pending_callbacks{},submitted_phases{},recording_leases{};
    std::array<bool,role_count> native_before{};
    std::array<ComPtr<ID3D12CommandAllocator>,2> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>,2> lists;
    bool complete()const {
        if(pending_callbacks||recording_leases)return false;if(!final_value)return true;
        const auto native_completed=fence->GetCompletedValue(),d12_completed=d12_done->GetCompletedValue();
        return native_completed!=UINT64_MAX&&native_completed>=final_value&&
            d12_completed!=UINT64_MAX&&d12_completed>=d12_value;
    }
};
struct Device {
    VulkanSessionBinding binding{};
    PFN_vkGetInstanceProcAddr gipa{};PFN_vkGetDeviceProcAddr gdpa{};
    ComPtr<ID3D12Device> owner;ComPtr<ID3D12CommandQueue> queue;
    VkPhysicalDeviceMemoryProperties memory{};
    std::recursive_mutex mutex;
    std::vector<std::shared_ptr<Slot>> slots;
    bool native_alive{true};
    bool owner_active{true};
    bool production_submitted{};
};
struct RecordingLease {
    std::shared_ptr<Device> device;std::shared_ptr<Slot> slot;
    ~RecordingLease(){std::scoped_lock lock(device->mutex);if(slot->recording_leases)--slot->recording_leases;}
};
bool retain_transport_recording(const std::shared_ptr<Device>& d,const std::shared_ptr<Slot>& s,VkCommandBuffer original) {
    auto lease=std::make_shared<RecordingLease>();lease->device=d;lease->slot=s;++s->recording_leases;
    return retain_recording(original,std::move(lease));
}
struct Feature:NgxFeatureMetadata {VkDevice device{};};
class EvaluationDeviceScope {
    inline static thread_local EvaluationDeviceScope* current_{};
    EvaluationDeviceScope* parent_{};
    Feature feature_{};VkCommandBuffer recording_{};bool public_dispatch_{};
public:
    EvaluationDeviceScope(const Feature& feature,VkCommandBuffer recording,bool public_dispatch) noexcept
        :parent_(current_),feature_(feature),recording_(recording),public_dispatch_(public_dispatch) {
        if(public_dispatch_&&(!feature_.creation_observed||feature_.creation_source_identity!=feature_.identity)) {
            Feature inherited;
            if(inherit(native(recording),feature_.type,inherited))feature_=inherited;
        }
        current_=this;
    }
    ~EvaluationDeviceScope(){current_=parent_;}
    static const EvaluationDeviceScope* exact(VkCommandBuffer command,NVSDK_NGX_Feature type) noexcept {
        return current_&&command&&current_->recording_&&current_->feature_.type==type&&
            native(current_->recording_)==command&&device_for(current_->recording_)==current_->feature_.device?
            current_:nullptr;
    }
    static bool inherit(VkCommandBuffer command,NVSDK_NGX_Feature type,Feature& feature) noexcept {
        const auto scope=exact(command,type);
        if(!scope||!scope->public_dispatch_||!scope->feature_.creation_observed||
            (type!=NVSDK_NGX_Feature_SuperSampling&&type!=NVSDK_NGX_Feature_RayReconstruction))return false;
        feature=scope->feature_;feature.identity=0;return true;
    }
    VkDevice device()const noexcept{return feature_.device;}
    VkCommandBuffer recording()const noexcept{return recording_;}
};
using Create=NVSDK_NGX_Result(*)(VkCommandBuffer,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
using Create1=NVSDK_NGX_Result(*)(VkDevice,VkCommandBuffer,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
using Evaluate=NVSDK_NGX_Result(*)(VkCommandBuffer,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,void*);
using Release=NVSDK_NGX_Result(*)(NVSDK_NGX_Handle*);
struct Module {SafetyHookInline c,c1,e,r;Create create{};Create1 create1{};Evaluate evaluate{};Release release{};HMODULE retained{};bool typed_implementation{};};
struct Hooks {
    std::mutex mutex;
    std::array<Module,3> modules;
    std::unordered_map<const NVSDK_NGX_Handle*,Feature> features;
    std::unordered_map<VkDevice,std::shared_ptr<Device>> devices;
    std::unordered_map<VkDevice,bool> interop_devices;
    std::unordered_map<void*,VkInstance> instances;
    std::unordered_map<VkDevice,VulkanSessionBinding> observed_devices;
    std::vector<std::shared_ptr<Device>> retired;
    // Observed native allocations carry actual transfer usage. Unknown images
    // reject capture; metadata cannot invent Vulkan allocation capabilities.
    struct ImageAllocation{VkDevice device{};VkImageUsageFlags usage{};VkImageCreateFlags flags{};};
    std::unordered_map<VkImage,ImageAllocation> image_usage;
    SafetyHookInline gdpa_hook,gipa_hook,create_image_hook,destroy_image_hook;
    SafetyHookInline create_device_hook,destroy_device_hook;
    PFN_vkGetDeviceProcAddr gdpa{};PFN_vkGetInstanceProcAddr gipa{};
    PFN_vkCreateImage create_image{};PFN_vkDestroyImage destroy_image{};
    PFN_vkCreateDevice create_device{};PFN_vkDestroyDevice destroy_device{};
};
Hooks& hooks(){static auto* h=new Hooks;return *h;}
bool ingress_reason(unsigned reason,std::uint64_t detail=0) noexcept {
    static std::array<std::atomic<ULONGLONG>,16> last{};
    if(reason>=last.size())return false;
    const auto now=GetTickCount64()+1;auto before=last[reason].load();
    if(before && now-before<1000)return false;
    if(!last[reason].compare_exchange_strong(before,now))return false;
    bridge_flight_logger().event(BridgeFlightOperation::luke_afw_stage,reason,954,detail,0);return true;
}
template<class T>T get(const NVSDK_NGX_Parameter* p,const char* name,T fallback={}) {
    T value=fallback;return p&&NVSDK_NGX_SUCCEED(p->Get(name,&value))?value:fallback;
}
template<class T>T api(const Device& d,const char* name){return reinterpret_cast<T>(unhooked_command(d.gdpa(d.binding.device,name)));}
void destroy_surface(Device& d,Surface& s) {
    if(s.imported)if(auto fn=api<PFN_vkDestroyBuffer>(d,"vkDestroyBuffer"))fn(d.binding.device,s.imported,nullptr);
    if(s.memory)if(auto fn=api<PFN_vkFreeMemory>(d,"vkFreeMemory"))fn(d.binding.device,s.memory,nullptr);
    s={};
}
void collect_retired() {
    std::vector<std::shared_ptr<Device>> retired;
    {std::scoped_lock lock(hooks().mutex);retired=hooks().retired;}
    for(const auto& d:retired) {
        std::unique_lock lock(d->mutex,std::try_to_lock);if(!lock.owns_lock())continue;
        if(!std::all_of(d->slots.begin(),d->slots.end(),[](const auto& s){return s->complete();}))continue;
        for(auto& s:d->slots) {
            if(d->native_alive) {
                for(auto& surface:s->surfaces)destroy_surface(*d,surface);
                if(s->timeline)if(auto fn=api<PFN_vkDestroySemaphore>(*d,"vkDestroySemaphore"))fn(d->binding.device,s->timeline,nullptr);
            }
            s->timeline={};
        }
        d->slots.clear();native_ingress::stop_queue(d->queue.Get());
        {std::scoped_lock registry_lock(hooks().mutex);std::erase(hooks().retired,d);}
    }
}
bool shape(const NVSDK_NGX_Resource_VK* r) {
    return r&&r->Type==NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW&&r->Resource.ImageViewInfo.Image&&
        r->Resource.ImageViewInfo.SubresourceRange.layerCount==1&&r->Resource.ImageViewInfo.SubresourceRange.levelCount==1&&
        r->Resource.ImageViewInfo.Width&&r->Resource.ImageViewInfo.Height;
}
DXGI_FORMAT format(VkFormat f,Role role) {
    if(role==motion){if(f==VK_FORMAT_R16G16_SFLOAT)return DXGI_FORMAT_R16G16_FLOAT;
        if(f==VK_FORMAT_R32G32_SFLOAT)return DXGI_FORMAT_R32G32_FLOAT;return DXGI_FORMAT_UNKNOWN;}
    if(role==depth){if(f==VK_FORMAT_D32_SFLOAT||f==VK_FORMAT_D32_SFLOAT_S8_UINT||f==VK_FORMAT_R32_SFLOAT)return DXGI_FORMAT_R32_FLOAT;
        if(f==VK_FORMAT_D16_UNORM||f==VK_FORMAT_R16_UNORM)return DXGI_FORMAT_R16_UNORM;return DXGI_FORMAT_UNKNOWN;}
    return dxgi_format_for_vulkan(f);
}
unsigned pixel_bytes(DXGI_FORMAT f) {
    switch(f){case DXGI_FORMAT_R16_UNORM:return 2;case DXGI_FORMAT_R16G16_FLOAT:case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:case DXGI_FORMAT_R10G10B10A2_UNORM:return 4;
    case DXGI_FORMAT_R32G32_FLOAT:case DXGI_FORMAT_R16G16B16A16_FLOAT:return 8;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:return 16;default:return 0;}
}
void d12_barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
    if(from==to)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list->ResourceBarrier(1,&b);
}
HRESULT surface(Device& d,Surface& target,const NVSDK_NGX_ImageViewInfo_VK& source,Role role) {
    Surface s;
    struct Cleanup{Device& device;Surface& surface;bool kept{};~Cleanup(){if(!kept)destroy_surface(device,surface);}} cleanup{d,s};
    const auto f=format(source.Format,role);const auto bpp=pixel_bytes(f);if(!bpp)return DXGI_ERROR_UNSUPPORTED;
    auto create_buffer=api<PFN_vkCreateBuffer>(d,"vkCreateBuffer");auto requirements=api<PFN_vkGetBufferMemoryRequirements>(d,"vkGetBufferMemoryRequirements");
    auto import_properties=api<PFN_vkGetMemoryWin32HandlePropertiesKHR>(d,"vkGetMemoryWin32HandlePropertiesKHR");
    auto allocate=api<PFN_vkAllocateMemory>(d,"vkAllocateMemory");auto bind=api<PFN_vkBindBufferMemory>(d,"vkBindBufferMemory");
    if(!create_buffer||!requirements||!import_properties||!allocate||!bind)return E_NOINTERFACE;
    D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=source.Width;
    texture.Height=source.Height;texture.DepthOrArraySize=texture.MipLevels=1;texture.Format=f;texture.SampleDesc.Count=1;
    if(role==color||role==output)texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    HRESULT result=d.owner->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&s.texture));
    if(FAILED(result))return result;
    UINT64 bytes{};d.owner->GetCopyableFootprints(&texture,0,1,0,&s.footprint,nullptr,nullptr,&bytes);
    VkExternalMemoryBufferCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
    external.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.pNext=&external;buffer.size=bytes;
    buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if(create_buffer(d.binding.device,&buffer,nullptr,&s.imported)!=VK_SUCCESS)return DXGI_ERROR_UNSUPPORTED;
    VkMemoryRequirements req{};requirements(d.binding.device,s.imported,&req);
    D3D12_RESOURCE_DESC staging{};staging.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    // Vulkan specifies that memory-requirement size is meaningless for a
    // D3D12_RESOURCE import. The committed resource owns its allocation size.
    staging.Width=(bytes+65535)&~UINT64(65535);
    staging.Height=staging.DepthOrArraySize=staging.MipLevels=1;staging.SampleDesc.Count=1;staging.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    result=d.owner->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&staging,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&s.buffer));
    HANDLE handle{};if(SUCCEEDED(result))result=d.owner->CreateSharedHandle(s.buffer.Get(),nullptr,GENERIC_ALL,nullptr,&handle);
    if(FAILED(result))return result;
    VkMemoryWin32HandlePropertiesKHR properties{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
    if(import_properties(d.binding.device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,handle,&properties)!=VK_SUCCESS){CloseHandle(handle);return E_INVALIDARG;}
    const auto types=req.memoryTypeBits&properties.memoryTypeBits;unsigned index{};
    for(;index<d.memory.memoryTypeCount;++index)if(types&(1u<<index))break;
    if(index==d.memory.memoryTypeCount){CloseHandle(handle);return E_INVALIDARG;}
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.buffer=s.imported;
    VkImportMemoryWin32HandleInfoKHR imported{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
    imported.pNext=&dedicated;imported.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;imported.handle=handle;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.pNext=&imported;
    allocation.allocationSize=staging.Width;allocation.memoryTypeIndex=index;
    const auto allocated=allocate(d.binding.device,&allocation,nullptr,&s.memory);CloseHandle(handle);
    if(allocated!=VK_SUCCESS||bind(d.binding.device,s.imported,s.memory,0)!=VK_SUCCESS)return DXGI_ERROR_UNSUPPORTED;
    s.source=source;destroy_surface(d,target);target=std::move(s);cleanup.kept=true;return S_OK;
}
HRESULT initialize_slot(Device& d,Slot& slot) {
    struct Cleanup{Device& device;Slot& slot;bool kept{};~Cleanup(){
        if(!kept&&slot.timeline){if(auto fn=api<PFN_vkDestroySemaphore>(device,"vkDestroySemaphore"))fn(device.binding.device,slot.timeline,nullptr);slot.timeline={};}
    }} cleanup{d,slot};
    HRESULT result=d.owner->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&slot.fence));
    if(SUCCEEDED(result))result=d.owner->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&slot.d12_done));
    HANDLE handle{};if(SUCCEEDED(result))result=d.owner->CreateSharedHandle(slot.fence.Get(),nullptr,GENERIC_ALL,nullptr,&handle);
    if(FAILED(result))return result;
    auto create=api<PFN_vkCreateSemaphore>(d,"vkCreateSemaphore");auto import=api<PFN_vkImportSemaphoreWin32HandleKHR>(d,"vkImportSemaphoreWin32HandleKHR");
    VkSemaphoreTypeCreateInfo timeline{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};timeline.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};info.pNext=&timeline;
    if(!create||!import||create(d.binding.device,&info,nullptr,&slot.timeline)!=VK_SUCCESS){CloseHandle(handle);return E_NOINTERFACE;}
    VkImportSemaphoreWin32HandleInfoKHR imported{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};
    imported.semaphore=slot.timeline;imported.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;imported.handle=handle;
    const auto imported_result=import(d.binding.device,&imported);CloseHandle(handle);
    if(imported_result!=VK_SUCCESS)return DXGI_ERROR_UNSUPPORTED;
    for(unsigned n=0;n<2;++n) {
        result=d.owner->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocators[n]));
        if(SUCCEEDED(result))result=d.owner->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocators[n].Get(),nullptr,IID_PPV_ARGS(&slot.lists[n]));
        if(SUCCEEDED(result))result=slot.lists[n]->Close();if(FAILED(result))return result;
    }cleanup.kept=true;return S_OK;
}
std::shared_ptr<Slot> slot(Device& d) {
    // Native copyback, not only the D3D12 work, must retire before reuse.
    // Completed slots with matching resources may be reset; in-flight slots
    // remain process-pinned even when an abandoned app buffer never submits.
    for(const auto& s:d.slots)if(s->complete())return s;
    if(d.slots.size()>=14)return {};
    auto s=std::make_shared<Slot>();if(FAILED(initialize_slot(d,*s)))return {};
    d.slots.push_back(s);return s;
}
bool copy_native(Device& d,Surface& s,VkCommandBuffer cmd,bool to_native,Role role,uint32_t native_family) {
    if(!s.imported)return false;
    auto barrier=api<PFN_vkCmdPipelineBarrier>(d,"vkCmdPipelineBarrier");
    auto from_image=api<PFN_vkCmdCopyImageToBuffer>(d,"vkCmdCopyImageToBuffer");
    auto to_image=api<PFN_vkCmdCopyBufferToImage>(d,"vkCmdCopyBufferToImage");if(!barrier||!from_image||!to_image)return false;
    VkBufferMemoryBarrier buffer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};buffer.buffer=s.imported;buffer.size=VK_WHOLE_SIZE;
    buffer.srcQueueFamilyIndex=s.native_used?VK_QUEUE_FAMILY_EXTERNAL:VK_QUEUE_FAMILY_IGNORED;
    buffer.dstQueueFamilyIndex=s.native_used?native_family:VK_QUEUE_FAMILY_IGNORED;
    buffer.dstAccessMask=to_native?VK_ACCESS_TRANSFER_READ_BIT:VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&buffer,0,nullptr);
    const auto layout=role==output?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageMemoryBarrier image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};image.image=s.source.Image;
    image.subresourceRange=s.source.SubresourceRange;
    image.srcQueueFamilyIndex=image.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    image.oldLayout=layout;image.newLayout=to_native?VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image.srcAccessMask=role==output?VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT;
    image.dstAccessMask=to_native?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_TRANSFER_READ_BIT;
    barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&image);
    VkBufferImageCopy region{};region.bufferOffset=s.footprint.Offset;
    region.bufferRowLength=s.footprint.Footprint.RowPitch/pixel_bytes(s.footprint.Footprint.Format);
    region.imageSubresource={s.source.SubresourceRange.aspectMask,s.source.SubresourceRange.baseMipLevel,s.source.SubresourceRange.baseArrayLayer,1};
    // Packed depth/stencil transports only the actual depth plane.
    if(role==depth&&(region.imageSubresource.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT))region.imageSubresource.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    region.imageExtent={s.source.Width,s.source.Height,1};
    if(to_native)to_image(cmd,s.imported,s.source.Image,image.newLayout,1,&region);
    else from_image(cmd,s.source.Image,image.newLayout,s.imported,1,&region);
    image.srcAccessMask=image.dstAccessMask;image.dstAccessMask=role==output?VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT;
    std::swap(image.oldLayout,image.newLayout);
    buffer.srcQueueFamilyIndex=native_family;buffer.dstQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL;
    buffer.srcAccessMask=to_native?VK_ACCESS_TRANSFER_READ_BIT:VK_ACCESS_TRANSFER_WRITE_BIT;buffer.dstAccessMask=0;
    barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,1,&buffer,1,&image);
    s.native_used=true;return true;
}
void copy_d12(ID3D12GraphicsCommandList* list,Surface& s,bool to_buffer) {
    D3D12_TEXTURE_COPY_LOCATION buffer{};buffer.pResource=s.buffer.Get();buffer.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;buffer.PlacedFootprint=s.footprint;
    D3D12_TEXTURE_COPY_LOCATION texture{};texture.pResource=s.texture.Get();texture.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    d12_barrier(list,s.buffer.Get(),D3D12_RESOURCE_STATE_COMMON,to_buffer?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_COPY_SOURCE);
    d12_barrier(list,s.texture.Get(),D3D12_RESOURCE_STATE_COMMON,to_buffer?D3D12_RESOURCE_STATE_COPY_SOURCE:D3D12_RESOURCE_STATE_COPY_DEST);
    if(to_buffer)list->CopyTextureRegion(&buffer,0,0,0,&texture,nullptr);else list->CopyTextureRegion(&texture,0,0,0,&buffer,nullptr);
    d12_barrier(list,s.texture.Get(),to_buffer?D3D12_RESOURCE_STATE_COPY_SOURCE:D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
    d12_barrier(list,s.buffer.Get(),to_buffer?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
}
struct Parameters:NVSDK_NGX_Parameter {
    const NVSDK_NGX_Parameter* base{};Slot* slot{};
    Parameters(const NVSDK_NGX_Parameter* p,Slot* s):base(p),slot(s){}
#define SCALAR(T) void Set(const char*,T)override{} NVSDK_NGX_Result Get(const char* n,T* v)const override{return base->Get(n,v);}
    SCALAR(unsigned long long) SCALAR(float) SCALAR(double) SCALAR(unsigned int) SCALAR(int)
#undef SCALAR
    void Set(const char*,ID3D11Resource*)override{}void Set(const char*,ID3D12Resource*)override{}void Set(const char*,void*)override{}
    NVSDK_NGX_Result Get(const char* n,ID3D11Resource** v)const override{return base->Get(n,v);}
    ID3D12Resource* resource(const char* n)const {
        Role role;if(!strcmp(n,NVSDK_NGX_Parameter_Color))role=color;else if(!strcmp(n,NVSDK_NGX_Parameter_Output))role=output;
        else if(!strcmp(n,NVSDK_NGX_Parameter_MotionVectors))role=motion;else if(!strcmp(n,NVSDK_NGX_Parameter_Depth))role=depth;else return nullptr;
        return slot->surfaces[role].texture.Get();
    }
    NVSDK_NGX_Result Get(const char* n,ID3D12Resource** v)const override{if(auto* r=resource(n)){*v=r;return NVSDK_NGX_Result_Success;}return NVSDK_NGX_Result_FAIL_InvalidParameter;}
    NVSDK_NGX_Result Get(const char* n,void** v)const override{if(auto* r=resource(n)){*v=r;return NVSDK_NGX_Result_Success;}return base->Get(n,v);}
    void Reset()override{}
};
struct Copyback {Device* device{};Surface* surface{};Role role{};uint32_t family{UINT32_MAX};};
VkResult record_copyback(VkCommandBuffer cmd,void* context) {
    auto& c=*static_cast<Copyback*>(context);return copy_native(*c.device,*c.surface,cmd,true,c.role,c.family)?VK_SUCCESS:VK_ERROR_INITIALIZATION_FAILED;
}
HRESULT begin_d12(Slot& slot,unsigned phase) {
    auto result=slot.allocators[phase]->Reset();if(SUCCEEDED(result))result=slot.lists[phase]->Reset(slot.allocators[phase].Get(),nullptr);return result;
}
struct Submission {HRESULT result{};bool executed{};};
Submission submit_d12(Device& d,Slot& s,unsigned phase,std::uint64_t native_ready,std::uint64_t d12_ready) {
    auto result=d.queue->Wait(s.fence.Get(),native_ready);if(FAILED(result))return {result,false};
    ID3D12CommandList* lists[]{s.lists[phase].Get()};d.queue->ExecuteCommandLists(1,lists);
    const auto ready=d.queue->Signal(s.fence.Get(),d12_ready);
    const auto done=d.queue->Signal(s.d12_done.Get(),++s.d12_value);
    return {FAILED(ready)?ready:done,true};
}
struct Work {
    std::shared_ptr<Device> device;std::shared_ptr<Slot> slot;
    std::vector<neural_rendering::SubmissionReceipt> nr_receipts;
    std::uint64_t stream{},prefix{},ready{};unsigned phase{};
    bool delivered{}, exact_publication{};
};
void work_submitted(VkQueue,void* opaque) noexcept {
    try {
        auto& work=*static_cast<Work*>(opaque);auto& d=*work.device;auto& s=*work.slot;
        std::scoped_lock lock(d.mutex);if(work.delivered)return;work.delivered=true;
        if(s.pending_callbacks)--s.pending_callbacks;
        const auto submission=submit_d12(d,s,work.phase,work.prefix,work.ready);
        if(submission.executed){++s.submitted_phases;neural_rendering::complete_submission(d.queue.Get(),work.nr_receipts);
            if(d.owner_active&&work.phase==1&&work.exact_publication&&SUCCEEDED(submission.result)){
                d.production_submitted=true;ingress_reason(0,reinterpret_cast<std::uint64_t>(d.binding.device));}}
        else {neural_rendering::abandon_submission(work.nr_receipts);retire_dlss_motion_vector_stream(work.stream);}
    }catch(...) {}
}
void work_abandoned(void* opaque) noexcept {
    try {
        auto& work=*static_cast<Work*>(opaque);auto& d=*work.device;auto& s=*work.slot;
        std::scoped_lock lock(d.mutex);if(work.delivered)return;work.delivered=true;
        if(s.pending_callbacks)--s.pending_callbacks;
        // No Wait/Execute was issued for this exact recording. Abandonment is
        // explicit; neither elapsed frames nor a guessed fence cancels work.
        neural_rendering::abandon_submission(work.nr_receipts);
        retire_dlss_motion_vector_stream(work.stream);
        if(!s.pending_callbacks&&!s.submitted_phases) {
            s.final_value=0;
            for(unsigned role=0;role<role_count;++role)s.surfaces[role].native_used=s.native_before[role];
        }
    }catch(...) {}
}
CommandSplit handoff(const std::shared_ptr<Device>& d,const std::shared_ptr<Slot>& s,
    unsigned phase,std::uint64_t stream,VkCommandBuffer original,Copyback& copy,bool exact_publication=false) {
    ID3D12CommandList* lists[]{s->lists[phase].Get()};
    const auto closed=s->lists[phase]->Close();if(FAILED(closed)) {
        neural_rendering::abandon_submission(neural_rendering::take_submission_receipts(1,lists));
        retire_dlss_motion_vector_stream(stream);return {};
    }
    auto work=std::make_shared<Work>();work->device=d;work->slot=s;work->phase=phase;work->stream=stream;
    work->exact_publication=exact_publication;
    work->prefix=++s->value;work->ready=++s->value;const auto retired=++s->value;s->final_value=retired;
    work->nr_receipts=neural_rendering::take_submission_receipts(1,lists);
    ++s->pending_callbacks;
    CommandHandoff packet{s->timeline,work->prefix,work->ready,retired};
    packet.submission_context=work;packet.submitted=&work_submitted;packet.abandoned=&work_abandoned;
    const auto result=split(original,packet,record_copyback,&copy);
    if(result.result!=VK_SUCCESS)work_abandoned(work.get());
    return result;
}
template<unsigned Index>NVSDK_NGX_Result evaluate(VkCommandBuffer original,const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* p,void* callback) {
    VkCommandBuffer recording=original;
    auto raw=[&]{return hooks().modules[Index].evaluate(native(recording),handle,p,callback);};
    if(!p||!handle)return raw();
    const auto command_identity=native(original);
    const auto observed_device=device_for(original);
    Feature f{};std::shared_ptr<Device> d;bool native_available{},replaced{};
    {std::scoped_lock lock(hooks().mutex);
     if constexpr(Index<2)if(hooks().modules[Index].typed_implementation) {
        const auto type=Index==0?NVSDK_NGX_Feature_SuperSampling:NVSDK_NGX_Feature_RayReconstruction;
        const auto existing=hooks().features.find(handle);
        const bool direct_creation=existing!=hooks().features.end()&&existing->second.creation_observed&&
            existing->second.creation_source_identity&&existing->second.creation_source_identity==existing->second.identity;
        const auto scoped=EvaluationDeviceScope::exact(command_identity,type);
        const auto scoped_device=scoped?scoped->device():VkDevice{};
        if(scoped&&(!observed_device||observed_device==scoped_device))recording=scoped->recording();
        Feature recovered{};recovered.device=observed_device?observed_device:scoped_device;
        const bool owner_conflict=observed_device&&scoped_device&&observed_device!=scoped_device;
        const bool inherited=!direct_creation&&EvaluationDeviceScope::inherit(command_identity,type,recovered);
        bool recover=inherited;
        if(inherited&&!recovered.flags_known) {
            NgxFeatureMetadata flags;
            if(recover_typed_ngx_metadata(true,type,p,flags)&&flags.flags_known) {
                recovered.flags=flags.flags;recovered.flags_known=true;
            }
        }
        if(!inherited&&(existing==hooks().features.end()||!existing->second.flags_known)) {
            if(existing!=hooks().features.end()&&existing->second.creation_observed) {
                recovered=existing->second;NgxFeatureMetadata flags;
                recover=recover_typed_ngx_metadata(true,type,p,flags)&&flags.flags_known;
                if(recover){recovered.flags=flags.flags;recovered.flags_known=true;}
            } else recover=recover_typed_ngx_metadata(true,type,p,recovered);
        }
        if(recover&&recovered.device&&!owner_conflict) {
            replaced=existing!=hooks().features.end()&&inherited&&
                existing->second.creation_source_identity!=recovered.creation_source_identity;
            recovered.identity=existing==hooks().features.end()||replaced?
                native_ingress::allocate_handle_key():existing->second.identity;
            hooks().features[handle]=recovered;
        }
     }
     const auto it=hooks().features.find(handle);if(it!=hooks().features.end()) {
        f=it->second;const auto device=hooks().devices.find(f.device);if(device!=hooks().devices.end())d=device->second;
        if(f.type==NVSDK_NGX_Feature_SuperSampling||f.type==NVSDK_NGX_Feature_RayReconstruction)
            native_available=hooks().modules[f.type==NVSDK_NGX_Feature_SuperSampling?0:1].typed_implementation;
     }}
    if(replaced)invalidate_dlss_motion_vector_stream_history(reinterpret_cast<std::uintptr_t>(handle));
    if(!f.identity||(f.type!=NVSDK_NGX_Feature_SuperSampling&&f.type!=NVSDK_NGX_Feature_RayReconstruction)||
        (observed_device&&f.device!=observed_device)) {
        if(f.creation_observed){EvaluationDeviceScope block(f,{},false);return raw();}
        return raw();
    }
    if(const auto scoped=EvaluationDeviceScope::exact(command_identity,f.type)) {
        if(scoped->device()==f.device)recording=scoped->recording();
    }
    EvaluationDeviceScope owner(f,recording,!hooks().modules[Index].typed_implementation);
    NgxEvaluationBoundary boundary(Index,hooks().modules[Index].typed_implementation,native_available);
    if(!boundary.observes())return raw();
    native_ingress::observe_upscaler_inputs(get<void*>(p,NVSDK_NGX_Parameter_MotionVectors) != nullptr,
        get<void*>(p,NVSDK_NGX_Parameter_Depth) != nullptr);
    if(const auto refusal=split_refusal(recording)){ingress_reason(1,refusal);return raw();}
    const auto native_family=queue_family_for(recording);if(native_family==UINT32_MAX)return raw();
    if(native_ingress::luke_detected()&&!f.flags_known)return raw();
    if(!d||!d->owner||!d->queue){ingress_reason(2);return raw();}std::scoped_lock lock(d->mutex);
    std::array<const char*,role_count> names{NVSDK_NGX_Parameter_Color,NVSDK_NGX_Parameter_MotionVectors,NVSDK_NGX_Parameter_Depth,NVSDK_NGX_Parameter_Output};
    std::array<NVSDK_NGX_Resource_VK*,role_count> resources{};
    for(unsigned r=0;r<role_count;++r) {
        resources[r]=static_cast<NVSDK_NGX_Resource_VK*>(get<void*>(p,names[r]));
        if(!shape(resources[r])){ingress_reason(4,r);return raw();}
    }
    const auto& output_info=resources[output]->Resource.ImageViewInfo;
    const auto& motion_info=resources[motion]->Resource.ImageViewInfo;
    const auto& depth_info=resources[depth]->Resource.ImageViewInfo;
    DlssMotionVectorPublication g{};g.stream=reinterpret_cast<std::uintptr_t>(handle);g.producer_queue=d->queue.Get();g.verified_producer_device=d->owner.Get();
    const auto render_width=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,
        f.creation_observed&&f.width?f.width:depth_info.Width);
    const auto render_height=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,
        f.creation_observed&&f.height?f.height:depth_info.Height);
    g.output_width=ngx_display_dimension(f.out_width,p,NVSDK_NGX_Parameter_OutWidth,output_info.Width,f.creation_observed);
    g.output_height=ngx_display_dimension(f.out_height,p,NVSDK_NGX_Parameter_OutHeight,output_info.Height,f.creation_observed);
    g.depth_width=render_width;g.depth_height=render_height;
    const bool low=(f.flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)!=0;
    g.motion_width=low?render_width:g.output_width;g.motion_height=low?render_height:g.output_height;
    g.scale_x=get<float>(p,NVSDK_NGX_Parameter_MV_Scale_X,1);g.scale_y=get<float>(p,NVSDK_NGX_Parameter_MV_Scale_Y,1);
    g.jitter_x=get<float>(p,NVSDK_NGX_Parameter_Jitter_Offset_X);g.jitter_y=get<float>(p,NVSDK_NGX_Parameter_Jitter_Offset_Y);
    g.reset=get<int>(p,NVSDK_NGX_Parameter_Reset)!=0;g.jittered=(f.flags&NVSDK_NGX_DLSS_Feature_Flags_MVJittered)!=0;
    g.depth_inverted=(f.flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)!=0;g.frame_time_delta_ms=get<float>(p,NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,16.6667F);
    if(!native_ingress::apply_camera(g,nullptr)&&native_ingress::luke_detected()) {
        const auto camera=native_ingress::streamline_camera::current();
        const auto mask=camera.rejection|(!native_ingress::streamline_camera::hooked()?0x1000U:0U);
        if(ingress_reason(3,(std::uint64_t(camera.frame)<<32)|mask)) {
            const auto bits=[](float value){return std::bit_cast<std::uint32_t>(value);};
            bridge_flight_logger().event(BridgeFlightOperation::luke_afw_stage,3,955,
                (std::uint64_t(bits(camera.near_z))<<32)|bits(camera.far_z),
                (std::uint64_t(bits(camera.fov))<<32)|camera.viewport);
        }
        return raw();
    }
    for(unsigned r=0;r<role_count;++r) {
        const auto required=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|((r==color||r==output)?VK_IMAGE_USAGE_TRANSFER_DST_BIT:0);
        bool supported{};{std::scoped_lock registry_lock(hooks().mutex);const auto usage=hooks().image_usage.find(resources[r]->Resource.ImageViewInfo.Image);
            supported=usage!=hooks().image_usage.end()&&usage->second.device==f.device&&
                !(usage->second.flags&VK_IMAGE_CREATE_PROTECTED_BIT)&&(usage->second.usage&required)==required;
        }
        if(!supported){ingress_reason(5,r);return raw();}
    }
    auto s=slot(*d);if(!s){ingress_reason(6);return raw();}
    s->submitted_phases=0;
    for(unsigned role=0;role<role_count;++role)s->native_before[role]=s->surfaces[role].native_used;
    for(unsigned r=0;r<role_count;++r) {
        auto& target=s->surfaces[r];const auto& image=resources[r]->Resource.ImageViewInfo;
        if(target.texture&&(target.source.Width!=image.Width||target.source.Height!=image.Height||target.source.Format!=image.Format)) {
            // Reallocation only after both APIs retired. Keep abandoned/in-flight
            // resources process-pinned; their fences are the recovery boundary.
            if(d->slots.size()>=14)return raw();
            auto replacement=std::make_shared<Slot>();if(FAILED(initialize_slot(*d,*replacement)))return raw();
            d->slots.push_back(replacement);s=replacement;break;
        }
    }
    for(unsigned r=0;r<role_count;++r){auto& target=s->surfaces[r];const auto& image=resources[r]->Resource.ImageViewInfo;
        if(!target.buffer&&FAILED(surface(*d,target,image,static_cast<Role>(r)))){ingress_reason(7,r);return raw();}target.source=image;}
    g.output=s->surfaces[output].texture.Get();g.motion_vectors=s->surfaces[motion].texture.Get();g.depth=s->surfaces[depth].texture.Get();
    g.resource_state=g.depth_resource_state=g.output_resource_state=D3D12_RESOURCE_STATE_COMMON;
    g.output_x=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X);g.output_y=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y);
    g.motion_x=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X);g.motion_y=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y);
    g.depth_x=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X);g.depth_y=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y);
    const auto available=[](UINT size,UINT base){return size-std::min(size,base);};
    g.output_width=std::min(g.output_width,available(output_info.Width,g.output_x));
    g.output_height=std::min(g.output_height,available(output_info.Height,g.output_y));
    g.motion_width=std::min(g.motion_width,available(motion_info.Width,g.motion_x));
    g.motion_height=std::min(g.motion_height,available(motion_info.Height,g.motion_y));
    g.depth_width=std::min(g.depth_width,available(depth_info.Width,g.depth_x));
    g.depth_height=std::min(g.depth_height,available(depth_info.Height,g.depth_y));
    const auto nr=neural_rendering::status();const bool pre_nr=nr.enabled&&nr.before_sr;
    if(FAILED(begin_d12(*s,pre_nr?0:1))||!retain_transport_recording(d,s,recording))return raw();
    if(!copy_native(*d,s->surfaces[motion],native(recording),false,motion,native_family)||!copy_native(*d,s->surfaces[depth],native(recording),false,depth,native_family))return raw();
    Parameters shadow(p,s.get());std::shared_ptr<const DlssMotionVectorFrame> frame;
    if(pre_nr) {
        if(!copy_native(*d,s->surfaces[color],native(recording),false,color,native_family))return raw();
        auto* list=s->lists[0].Get();for(auto role:{motion,depth,color})copy_d12(list,s->surfaces[role],false);
        g.producer_command_list=list;frame=publish_dlss_motion_vectors(g);
        for(auto role:{color,motion,depth})d12_barrier(list,s->surfaces[role].texture.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        neural_rendering::before_sr(f.identity,list,&shadow,d->queue.Get());
        for(auto role:{color,motion,depth})d12_barrier(list,s->surfaces[role].texture.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        copy_d12(list,s->surfaces[color],true);
        Copyback copy{d.get(),&s->surfaces[color],color,native_family};
        const auto split_result=handoff(d,s,0,g.stream,recording,copy);
        if(split_result.result!=VK_SUCCESS)return raw();
    }
    const auto result=raw();if(NVSDK_NGX_FAILED(result))return result;
    if((pre_nr&&FAILED(begin_d12(*s,1)))||!copy_native(*d,s->surfaces[output],native(recording),false,output,native_family))return result;
    auto* list=s->lists[1].Get();copy_d12(list,s->surfaces[output],false);
    if(!pre_nr){for(auto role:{motion,depth})copy_d12(list,s->surfaces[role],false);g.producer_command_list=list;frame=publish_dlss_motion_vectors(g);}
    for(auto role:{motion,depth})d12_barrier(list,s->surfaces[role].texture.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    d12_barrier(list,s->surfaces[output].texture.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    neural_rendering::after_sr(f.identity,list,&shadow,d->queue.Get());
    if(frame&&g.protect_snapshot_lifetime&&embedded::afw_snapshot().desired.mode!=0&&boundary.can_complete())
        mark_luke_color_origin(list,g.output,frame->color_origin_token,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,d->owner.Get(),
            {g.output_x,g.output_y,g.output_width,g.output_height});
    d12_barrier(list,s->surfaces[output].texture.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
    for(auto role:{motion,depth})d12_barrier(list,s->surfaces[role].texture.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
    copy_d12(list,s->surfaces[output],true);
    Copyback copy{d.get(),&s->surfaces[output],output,native_family};
    const auto transferred=handoff(d,s,1,g.stream,recording,copy,frame!=nullptr);
    if(transferred.result!=VK_SUCCESS)ingress_reason(8,static_cast<std::uint64_t>(transferred.result));
    return result;
}
Feature creation_feature(VkDevice device,NVSDK_NGX_Feature type,NVSDK_NGX_Parameter* p) {
    Feature f{};f.device=device;f.type=type;f.creation_observed=true;
    f.flags=get<int>(p,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags);f.width=get<UINT>(p,NVSDK_NGX_Parameter_Width);f.height=get<UINT>(p,NVSDK_NGX_Parameter_Height);
    f.flags_known=p&&NVSDK_NGX_SUCCEED(p->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&f.flags));
    f.out_width=get<UINT>(p,NVSDK_NGX_Parameter_OutWidth);f.out_height=get<UINT>(p,NVSDK_NGX_Parameter_OutHeight);
    return f;
}
void register_feature(Feature f,NVSDK_NGX_Handle** out,NVSDK_NGX_Result result) {
    if(NVSDK_NGX_FAILED(result)||!out||!*out||!f.device)return;
    f.identity=native_ingress::allocate_handle_key();f.creation_source_identity=f.identity;bool replaced{};
    {std::scoped_lock lock(hooks().mutex);replaced=hooks().features.contains(*out);hooks().features[*out]=f;}
    if(replaced)invalidate_dlss_motion_vector_stream_history(reinterpret_cast<std::uintptr_t>(*out));
}
template<unsigned Index>NVSDK_NGX_Result create1(VkDevice device,VkCommandBuffer cmd,NVSDK_NGX_Feature type,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** out) {
    const auto f=creation_feature(device,type,p);
    const auto result=hooks().modules[Index].create1(device,native(cmd),type,p,out);if(p)register_feature(f,out,result);return result;
}
template<unsigned Index>NVSDK_NGX_Result create(VkCommandBuffer cmd,NVSDK_NGX_Feature type,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** out) {
    const auto device=device_for(cmd);
    const auto f=creation_feature(device,type,p);
    const auto result=hooks().modules[Index].create(native(cmd),type,p,out);if(p)register_feature(f,out,result);return result;
}
template<unsigned Index>NVSDK_NGX_Result release(NVSDK_NGX_Handle* handle) {
    Feature f{};{std::scoped_lock lock(hooks().mutex);auto it=hooks().features.find(handle);if(it!=hooks().features.end())f=it->second;}
    const auto result=hooks().modules[Index].release(handle);
    if(NVSDK_NGX_SUCCEED(result)){std::scoped_lock lock(hooks().mutex);hooks().features.erase(handle);
        retire_dlss_motion_vector_stream(reinterpret_cast<std::uintptr_t>(handle));if(f.identity)neural_rendering::retire_handle(f.identity);}
    return result;
}
template<unsigned Index>void install(HMODULE module) {
    auto& m=hooks().modules[Index];if(!module||m.retained)return;
    auto c=GetProcAddress(module,"NVSDK_NGX_VULKAN_CreateFeature"),c1=GetProcAddress(module,"NVSDK_NGX_VULKAN_CreateFeature1"),
        e=GetProcAddress(module,"NVSDK_NGX_VULKAN_EvaluateFeature"),r=GetProcAddress(module,"NVSDK_NGX_VULKAN_ReleaseFeature");if(!e||!r||(!c&&!c1))return;
    if(Index<2&&native_ingress::luke_detected()) {
        const auto original_e=native_export_address(module,"NVSDK_NGX_VULKAN_EvaluateFeature");
        const auto original_r=native_export_address(module,"NVSDK_NGX_VULKAN_ReleaseFeature");
        const auto original_c=native_export_address(module,"NVSDK_NGX_VULKAN_CreateFeature");
        const auto original_c1=native_export_address(module,"NVSDK_NGX_VULKAN_CreateFeature1");
        if(original_e&&original_r&&(original_c||original_c1)){e=original_e;r=original_r;c=original_c;c1=original_c1;}
    }
    HMODULE implementation{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(e),&implementation);
    m.typed_implementation=Index<2&&implementation==module;
    if(c)m.c=safetyhook::create_inline(c,reinterpret_cast<void*>(&create<Index>),SafetyHookInline::StartDisabled);
    if(c1)m.c1=safetyhook::create_inline(c1,reinterpret_cast<void*>(&create1<Index>),SafetyHookInline::StartDisabled);
    m.e=safetyhook::create_inline(e,reinterpret_cast<void*>(&evaluate<Index>),SafetyHookInline::StartDisabled);
    m.r=safetyhook::create_inline(r,reinterpret_cast<void*>(&release<Index>),SafetyHookInline::StartDisabled);
    if((c&&!m.c)||(c1&&!m.c1)||!m.e||!m.r){m.c={};m.c1={};m.e={};m.r={};return;}
    if(m.c)m.create=m.c.original<Create>();if(m.c1)m.create1=m.c1.original<Create1>();
    m.evaluate=m.e.original<Evaluate>();m.release=m.r.original<Release>();
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(e),&m.retained);
    if(m.c)(void)m.c.enable();if(m.c1)(void)m.c1.enable();(void)m.e.enable();(void)m.r.enable();
}
VkResult VKAPI_CALL create_image(VkDevice device,const VkImageCreateInfo* info,const VkAllocationCallbacks* allocator,VkImage* image) {
    // Transfer capability is requested at native allocation creation, never
    // inferred afterward from resolution or successful NGX evaluation.
    VkImageCreateInfo local=*info;
    if(!(local.flags&VK_IMAGE_CREATE_PROTECTED_BIT)&&local.imageType==VK_IMAGE_TYPE_2D&&local.samples==VK_SAMPLE_COUNT_1_BIT&&local.mipLevels==1&&local.arrayLayers==1)
        local.usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto result=hooks().create_image(device,&local,allocator,image);
    if(result!=VK_SUCCESS&&local.usage!=info->usage){result=hooks().create_image(device,info,allocator,image);local=*info;}
    if(result==VK_SUCCESS){std::scoped_lock lock(hooks().mutex);hooks().image_usage[*image]={device,local.usage,local.flags};}
    return result;
}
void VKAPI_CALL destroy_image(VkDevice device,VkImage image,const VkAllocationCallbacks* allocator) {
    {std::scoped_lock lock(hooks().mutex);const auto it=hooks().image_usage.find(image);
        if(it!=hooks().image_usage.end()&&it->second.device==device)hooks().image_usage.erase(it);}
    hooks().destroy_image(device,image,allocator);
}
VkResult VKAPI_CALL create_device(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* allocator,VkDevice* device) {
    InteropDeviceRequest request;bool interop{};
    const auto module=GetModuleHandleW(L"vulkan-1.dll");
    auto enumerate=reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(GetProcAddress(module,"vkEnumerateDeviceExtensionProperties"));
    auto features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(GetProcAddress(module,"vkGetPhysicalDeviceFeatures2"));
    if(enumerate&&features&&info) {
        uint32_t count{};if(enumerate(physical,nullptr,&count,nullptr)==VK_SUCCESS) {
            std::vector<VkExtensionProperties> supported(count);
            if(enumerate(physical,nullptr,&count,supported.data())==VK_SUCCESS) {
                VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
                VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};query.pNext=&timeline;features(physical,&query);
                interop=request.prepare(*info,supported,timeline.timelineSemaphore!=0);
            }
        }
    }
    const auto result=hooks().create_device(physical,interop?&request.info:info,allocator,device);
    if(result==VK_SUCCESS) {
        {std::scoped_lock lock(hooks().mutex);hooks().interop_devices[*device]=interop;
            const auto instance=hooks().instances.find(*reinterpret_cast<void* const*>(physical));
            if(instance!=hooks().instances.end())hooks().observed_devices[*device]={instance->second,physical,*device,0,0};}
        // Observe command pools, allocation and recording before the application
        // retrieves its device dispatcher. No existing state is guessed later.
        initialize_commands({},physical,*device,hooks().gipa,hooks().gdpa);
    }
    return result;
}
void VKAPI_CALL destroy_device(VkDevice device,const VkAllocationCallbacks* allocator) {
    stop_owner(device);
    {std::scoped_lock lock(hooks().mutex);hooks().interop_devices.erase(device);
        hooks().observed_devices.erase(device);
        std::erase_if(hooks().image_usage,[&](const auto& image){return image.second.device==device;});}
    std::vector<std::shared_ptr<Device>> retired;{std::scoped_lock lock(hooks().mutex);retired=hooks().retired;}
    for(const auto& d:retired)if(d->binding.device==device){std::scoped_lock device_lock(d->mutex);d->native_alive=false;}
    destroy_commands(device);hooks().destroy_device(device,allocator);
}
PFN_vkVoidFunction VKAPI_CALL get_device(VkDevice device,const char* name) {
    auto fn=hooks().gdpa(device,name);
    if(!strcmp(name,"vkCreateImage"))return fn?reinterpret_cast<PFN_vkVoidFunction>(&create_image):nullptr;
    if(!strcmp(name,"vkDestroyImage"))return fn?reinterpret_cast<PFN_vkVoidFunction>(&destroy_image):nullptr;
    if(!strcmp(name,"vkDestroyDevice"))return fn?reinterpret_cast<PFN_vkVoidFunction>(&destroy_device):nullptr;
    return resolve_command(device,name,fn);
}
PFN_vkVoidFunction VKAPI_CALL get_instance(VkInstance instance,const char* name) {
    if(instance){std::scoped_lock lock(hooks().mutex);hooks().instances[*reinterpret_cast<void* const*>(instance)]=instance;}
    auto fn=hooks().gipa(instance,name);
    if(!strcmp(name,"vkGetDeviceProcAddr"))return fn?reinterpret_cast<PFN_vkVoidFunction>(&get_device):nullptr;
    if(!strcmp(name,"vkCreateDevice"))return fn?reinterpret_cast<PFN_vkVoidFunction>(&create_device):nullptr;
    if(!strcmp(name,"vkCreateImage"))return fn?reinterpret_cast<PFN_vkVoidFunction>(&create_image):nullptr;
    if(!strcmp(name,"vkDestroyImage"))return fn?reinterpret_cast<PFN_vkVoidFunction>(&destroy_image):nullptr;
    if(!strcmp(name,"vkDestroyDevice"))return fn?reinterpret_cast<PFN_vkVoidFunction>(&destroy_device):nullptr;
    return resolve_command(VK_NULL_HANDLE,name,fn);
}
}
void configure_owner(const VulkanSessionBinding& binding,ID3D12Device* owner,ID3D12CommandQueue* xr_queue) noexcept {
    try {
        poll_capture();if(!binding.device||!owner||!xr_queue||!hooks().gdpa||!hooks().gipa)return;
        collect_retired();
        {std::scoped_lock lock(hooks().mutex);if(hooks().retired.size()>=8)return;
            if(hooks().devices.contains(binding.device))return;
            const auto capabilities=hooks().interop_devices.find(binding.device);
            if(capabilities==hooks().interop_devices.end()||!capabilities->second)return;}
        auto d=std::make_shared<Device>();d->binding=binding;d->owner=owner;d->gdpa=hooks().gdpa;d->gipa=hooks().gipa;
        ComPtr<ID3D12Device> queue_owner;ComPtr<IUnknown> a,b;owner->QueryInterface(IID_PPV_ARGS(&a));
        if(FAILED(xr_queue->GetDevice(IID_PPV_ARGS(&queue_owner))))return;queue_owner.As(&b);if(a.Get()!=b.Get())return;
        auto description=xr_queue->GetDesc();description.Priority=D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        description.Flags=D3D12_COMMAND_QUEUE_FLAG_NONE;if(FAILED(owner->CreateCommandQueue(&description,IID_PPV_ARGS(&d->queue))))return;
        const auto memory=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(d->gipa(binding.instance,"vkGetPhysicalDeviceMemoryProperties"));
        if(!memory)return;memory(binding.physical_device,&d->memory);
        if(!initialize_commands(binding.instance,binding.physical_device,binding.device,d->gipa,d->gdpa))return;
        native_ingress::configure_queue(d->queue.Get());
        {std::scoped_lock lock(hooks().mutex);hooks().devices[binding.device]=std::move(d);}
    }catch(...) {}
}
void stop_owner(VkDevice device) noexcept {
    // An OpenXR session end cannot cancel the application's command recording.
    // Submission/abandonment callbacks and both exact fences own quiescence.
    std::shared_ptr<Device> retired;
    {std::scoped_lock lock(hooks().mutex);auto it=hooks().devices.find(device);if(it!=hooks().devices.end()) {
        retired=it->second;hooks().retired.push_back(retired);hooks().devices.erase(it);
    }}
    if(retired){std::scoped_lock lock(retired->mutex);retired->owner_active=false;retired->production_submitted=false;}
    collect_retired();
}
void poll_capture() noexcept {try {
#if !defined(XRFG_NATIVE_INGRESS_TESTS)
    if(!luke_vulkan_process()||!native_ingress::luke_detected())return;
#endif
    static std::mutex polling_mutex;std::scoped_lock polling_lock(polling_mutex);
    collect_retired();
    install<0>(GetModuleHandleW(L"nvngx_dlss.dll"));install<1>(GetModuleHandleW(L"nvngx_dlssd.dll"));install<2>(GetModuleHandleW(L"_nvngx.dll"));
    auto& h=hooks();if(h.gdpa_hook){ingress_reason(12);return;}const auto module=GetModuleHandleW(L"vulkan-1.dll");if(!module)return;
    auto gdpa=GetProcAddress(module,"vkGetDeviceProcAddr"),gipa=GetProcAddress(module,"vkGetInstanceProcAddr"),
        ci=GetProcAddress(module,"vkCreateImage"),di=GetProcAddress(module,"vkDestroyImage"),
        cd=GetProcAddress(module,"vkCreateDevice"),dd=GetProcAddress(module,"vkDestroyDevice");if(!gdpa||!gipa||!ci||!di||!cd||!dd)return;
    h.gdpa_hook=safetyhook::create_inline(gdpa,reinterpret_cast<void*>(&get_device),SafetyHookInline::StartDisabled);
    h.gipa_hook=safetyhook::create_inline(gipa,reinterpret_cast<void*>(&get_instance),SafetyHookInline::StartDisabled);
    h.create_image_hook=safetyhook::create_inline(ci,reinterpret_cast<void*>(&create_image),SafetyHookInline::StartDisabled);
    h.destroy_image_hook=safetyhook::create_inline(di,reinterpret_cast<void*>(&destroy_image),SafetyHookInline::StartDisabled);
    h.create_device_hook=safetyhook::create_inline(cd,reinterpret_cast<void*>(&create_device),SafetyHookInline::StartDisabled);
    h.destroy_device_hook=safetyhook::create_inline(dd,reinterpret_cast<void*>(&destroy_device),SafetyHookInline::StartDisabled);
    if(!h.gdpa_hook||!h.gipa_hook||!h.create_image_hook||!h.destroy_image_hook||!h.create_device_hook||!h.destroy_device_hook){
        h.gdpa_hook={};h.gipa_hook={};h.create_image_hook={};h.destroy_image_hook={};h.create_device_hook={};h.destroy_device_hook={};return;}
    h.gdpa=h.gdpa_hook.original<PFN_vkGetDeviceProcAddr>();h.gipa=h.gipa_hook.original<PFN_vkGetInstanceProcAddr>();
    h.create_image=h.create_image_hook.original<PFN_vkCreateImage>();h.destroy_image=h.destroy_image_hook.original<PFN_vkDestroyImage>();
    h.create_device=h.create_device_hook.original<PFN_vkCreateDevice>();h.destroy_device=h.destroy_device_hook.original<PFN_vkDestroyDevice>();
    (void)h.create_image_hook.enable();(void)h.destroy_image_hook.enable();(void)h.create_device_hook.enable();(void)h.destroy_device_hook.enable();
    (void)h.gdpa_hook.enable();(void)h.gipa_hook.enable();
    ingress_reason(13,install_command_entry_hooks(module));
}catch(...) {}}

VkDevice configure_luke_owner(ID3D12Device* owner,ID3D12CommandQueue* queue) noexcept {
    if(!luke_vulkan_process()||!native_ingress::luke_detected()||!owner||!queue)return {};
    try {
        poll_capture();std::vector<VulkanSessionBinding> observed;
        {std::scoped_lock lock(hooks().mutex);for(const auto& [device,binding]:hooks().observed_devices)
            if(hooks().interop_devices[device])observed.push_back(binding);}
        VulkanSessionBinding selected{};const auto luid=owner->GetAdapterLuid();
        for(const auto& binding:observed) {
            auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(hooks().gipa(binding.instance,"vkGetPhysicalDeviceProperties2"));
            if(!properties)continue;
            VkPhysicalDeviceIDProperties identity{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            VkPhysicalDeviceProperties2 info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};info.pNext=&identity;
            properties(binding.physical_device,&info);
            if(!identity.deviceLUIDValid||std::memcmp(identity.deviceLUID,&luid,sizeof(luid)))continue;
            if(selected.device)return {}; // multiple native owners are ambiguous
            selected=binding;
        }
        if(!selected.device){ingress_reason(9,observed.size());return {};}
        configure_owner(selected,owner,queue);
        std::scoped_lock lock(hooks().mutex);
        if(!hooks().devices.contains(selected.device)){ingress_reason(10);return {};}
        ingress_reason(11,reinterpret_cast<std::uint64_t>(selected.device));return selected.device;
    }catch(...){return {};}
}
bool query_luke_context(ID3D12Device** owner,ID3D12CommandQueue** queue) noexcept {
    if(owner)*owner=nullptr;if(queue)*queue=nullptr;
    if(!owner||!queue||!luke_vulkan_process()||!native_ingress::luke_detected())return false;
    try {
        std::vector<std::shared_ptr<Device>> devices;
        {std::scoped_lock lock(hooks().mutex);for(const auto& [key,d]:hooks().devices)devices.push_back(d);}
        std::shared_ptr<Device> selected;
        for(const auto& d:devices){std::scoped_lock lock(d->mutex);
            if(d->native_alive&&d->owner_active&&d->production_submitted){if(selected)return false;selected=d;}}
        if(!selected)return false;
        std::scoped_lock lock(selected->mutex);
        if(!selected->native_alive||!selected->owner_active||!selected->production_submitted||FAILED(selected->owner.CopyTo(owner)))return false;
        if(FAILED(selected->queue.CopyTo(queue))){(*owner)->Release();*owner=nullptr;return false;}
        return true;
    }catch(...){return false;}
}
}

extern "C" __declspec(dllexport) void OFXR_StartLukeVulkanIngressV1() noexcept {
    // Called by the already armed Vulkan layer before the game creates devices.
    // Never from DllMain, and the exact executable gate keeps all other Vulkan
    // applications on the suspended path.
    if(xrfg::native_vulkan::luke_vulkan_process())xrfg::native_vulkan::poll_capture();
}

#if defined(XRFG_NATIVE_INGRESS_TESTS)
#include "native_vulkan_transport_test.inc"
#endif
