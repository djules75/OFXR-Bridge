#include "xrfg/native_vulkan_transport_test.hpp"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <iostream>
#include <stdexcept>
#include <sstream>

namespace xrfg::native_vulkan {
namespace {
struct TransportSkip:std::runtime_error {using std::runtime_error::runtime_error;};
void require_transport(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
void require_transport(VkResult result,const char* message) {
    if(result!=VK_SUCCESS){std::ostringstream text;text<<message<<": VkResult "<<result;throw std::runtime_error(text.str());}
}
void require_transport(HRESULT result,const char* message) {
    if(FAILED(result)){std::ostringstream text;text<<message<<": HRESULT 0x"<<std::hex<<result;throw std::runtime_error(text.str());}
}
struct HostBuffer {VkBuffer buffer{};VkDeviceMemory memory{};};
struct TransportProbe {
    static constexpr unsigned width=16,height=8,pixel_size=4,byte_count=width*height*pixel_size;
    HMODULE loader{};VkInstance instance{};VkPhysicalDevice physical{};VkDevice native_device{};
    VkQueue native_queue{};uint32_t family{};PFN_vkGetInstanceProcAddr gipa{};PFN_vkGetDeviceProcAddr gdpa{};
    std::shared_ptr<Device> device;std::shared_ptr<Slot> captured;
    VkCommandPool pool{};VkCommandBuffer command{};VkImage image{};VkDeviceMemory image_memory{};VkFence finished{};
    HostBuffer upload,readback;ComPtr<ID3D12Resource> d12_readback;ComPtr<ID3D12Fence> progress;
    ComPtr<ID3D12InfoQueue> debug;std::array<unsigned char,byte_count> expected{};
    template<class T>T instance_api(const char* name)const {
        return reinterpret_cast<T>(gipa(instance,name));
    }
    template<class T>T raw(const char* name)const {
        return reinterpret_cast<T>(gdpa(native_device,name));
    }
    template<class T>T observed(const char* name)const {
        return reinterpret_cast<T>(resolve_command(native_device,name,reinterpret_cast<PFN_vkVoidFunction>(raw<T>(name))));
    }
    uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags properties)const {
        for(uint32_t i=0;i<device->memory.memoryTypeCount;++i)
            if((bits&(1u<<i))&&(device->memory.memoryTypes[i].propertyFlags&properties)==properties)return i;
        throw std::runtime_error("native transport memory type unavailable");
    }
    void setup() {
        loader=LoadLibraryW(L"vulkan-1.dll");if(!loader)throw TransportSkip("Vulkan loader unavailable");
        gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
        require_transport(gipa!=nullptr,"Vulkan instance dispatcher unavailable");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_3;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
        require_transport(reinterpret_cast<PFN_vkCreateInstance>(gipa({},"vkCreateInstance"))(&info,nullptr,&instance),"create native probe instance");
        gdpa=instance_api<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
        uint32_t count{};auto enumerate=instance_api<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
        require_transport(enumerate(instance,&count,nullptr),"enumerate native devices");
        std::vector<VkPhysicalDevice> physical_devices(count);require_transport(enumerate(instance,&count,physical_devices.data()),"read native devices");
        ComPtr<ID3D12Debug> debug_controller;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))debug_controller->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;require_transport(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"create DXGI factory");
        device=std::make_shared<Device>();
        for(auto candidate:physical_devices) {
            VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&id;
            instance_api<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2")(candidate,&properties);
            if(!id.deviceLUIDValid)continue;LUID luid{};std::memcpy(&luid,id.deviceLUID,sizeof(luid));ComPtr<IDXGIAdapter1> adapter;
            if(SUCCEEDED(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)))&&
                SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device->owner)))){physical=candidate;break;}
        }
        if(!physical)throw TransportSkip("matched Vulkan/D3D12 physical device unavailable");
        auto queues=instance_api<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
        count=0;queues(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);queues(physical,&count,families.data());
        while(family<count&&!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;
        if(family==count)throw TransportSkip("native graphics queue unavailable");
        auto extensions=instance_api<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
        count=0;require_transport(extensions(physical,nullptr,&count,nullptr),"enumerate interop extensions");
        std::vector<VkExtensionProperties> names(count);require_transport(extensions(physical,nullptr,&count,names.data()),"read interop extensions");
        const char* required[]{VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};
        for(const auto* name:required)if(std::none_of(names.begin(),names.end(),[&](const auto& entry){return !std::strcmp(entry.extensionName,name);}))
            throw TransportSkip("native Win32 external memory/semaphore unavailable");
        VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&timeline;
        instance_api<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2")(physical,&features);
        if(!timeline.timelineSemaphore)throw TransportSkip("native timeline semaphore unavailable");
        timeline.pNext=nullptr;float priority=1;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
        VkPhysicalDeviceVulkan12Features requested12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceFeatures2 requested{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};requested.pNext=&requested12;
        VkDeviceCreateInfo create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};create.pNext=&requested;create.queueCreateInfoCount=1;create.pQueueCreateInfos=&queue;
        InteropDeviceRequest interop;require_transport(interop.prepare(create,names,true),"prepare actual native interop features");
        require_transport(create.pNext==&requested&&requested.pNext==&requested12&&!requested12.timelineSemaphore&&create.enabledExtensionCount==0&&create.ppEnabledExtensionNames==nullptr,"interop request edited caller-owned feature chain");
        require_transport(instance_api<PFN_vkCreateDevice>("vkCreateDevice")(physical,&interop.info,nullptr,&native_device),"create native probe device with immutable interop request");
        raw<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(native_device,family,0,&native_queue);
        device->binding={instance,physical,native_device,family,0};device->gipa=gipa;device->gdpa=gdpa;
        instance_api<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(physical,&device->memory);
        D3D12_COMMAND_QUEUE_DESC description{};description.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        require_transport(device->owner->CreateCommandQueue(&description,IID_PPV_ARGS(&device->queue)),"create exact private capture queue");
        (void)device->owner.As(&debug);if(debug)debug->ClearStoredMessages();
        require_transport(initialize_commands(instance,physical,native_device,gipa,gdpa),"initialize observed native command stream");
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool_info.queueFamilyIndex=family;pool_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        require_transport(observed<PFN_vkCreateCommandPool>("vkCreateCommandPool")(native_device,&pool_info,nullptr,&pool),"create observed native pool");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
        require_transport(observed<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(native_device,&allocate,&command),"allocate observed native command");
        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image_info.imageType=VK_IMAGE_TYPE_2D;image_info.format=VK_FORMAT_R16G16_SFLOAT;
        image_info.extent={width,height,1};image_info.mipLevels=image_info.arrayLayers=1;image_info.samples=VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling=VK_IMAGE_TILING_OPTIMAL;image_info.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        require_transport(raw<PFN_vkCreateImage>("vkCreateImage")(native_device,&image_info,nullptr,&image),"create native source image");
        VkMemoryRequirements requirements{};raw<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(native_device,image,&requirements);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=requirements.size;memory.memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        require_transport(raw<PFN_vkAllocateMemory>("vkAllocateMemory")(native_device,&memory,nullptr,&image_memory),"allocate native source memory");
        require_transport(raw<PFN_vkBindImageMemory>("vkBindImageMemory")(native_device,image,image_memory,0),"bind native source image");
        upload=host_buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);readback=host_buffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        for(unsigned i=0;i<byte_count;++i)expected[i]=static_cast<unsigned char>((i*13u+i/7u+3u)&255u);
        void* mapped{};require_transport(raw<PFN_vkMapMemory>("vkMapMemory")(native_device,upload.memory,0,byte_count,0,&mapped),"map native upload");
        std::memcpy(mapped,expected.data(),byte_count);raw<PFN_vkUnmapMemory>("vkUnmapMemory")(native_device,upload.memory);
        captured=std::make_shared<Slot>();require_transport(initialize_slot(*device,*captured),"create production transport slot");device->slots.push_back(captured);
        NVSDK_NGX_ImageViewInfo_VK source{};source.Image=image;source.Format=VK_FORMAT_R16G16_SFLOAT;source.Width=width;source.Height=height;
        source.SubresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};require_transport(surface(*device,captured->surfaces[motion],source,motion),"import production shared D3D12 buffer");
        const auto& footprint=captured->surfaces[motion].footprint;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=footprint.Footprint.RowPitch*height;
        buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        require_transport(device->owner->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&d12_readback)),"create D3D12 typed readback");
        require_transport(device->owner->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&progress)),"create private progress fence");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};require_transport(raw<PFN_vkCreateFence>("vkCreateFence")(native_device,&fence,nullptr,&finished),"create exact native suffix fence");
    }
    HostBuffer host_buffer(VkBufferUsageFlags usage) {
        HostBuffer result;VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};create.size=byte_count;create.usage=usage;
        require_transport(raw<PFN_vkCreateBuffer>("vkCreateBuffer")(native_device,&create,nullptr,&result.buffer),"create host transfer buffer");
        VkMemoryRequirements requirements{};raw<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(native_device,result.buffer,&requirements);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=requirements.size;
        memory.memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        require_transport(raw<PFN_vkAllocateMemory>("vkAllocateMemory")(native_device,&memory,nullptr,&result.memory),"allocate host transfer memory");
        require_transport(raw<PFN_vkBindBufferMemory>("vkBindBufferMemory")(native_device,result.buffer,result.memory,0),"bind host transfer buffer");return result;
    }
    void image_barrier(VkImageLayout from,VkImageLayout to,VkAccessFlags source,VkAccessFlags destination) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=from;barrier.newLayout=to;barrier.srcAccessMask=source;barrier.dstAccessMask=destination;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        raw<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(native(command),VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    }
    void wait_private(uint64_t value) {
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require_transport(event!=nullptr,"create private progress event");
        const auto result=progress->SetEventOnCompletion(value,event);const auto waited=SUCCEEDED(result)?WaitForSingleObject(event,2000):WAIT_FAILED;CloseHandle(event);
        require_transport(result,"arm private progress event");require_transport(waited==WAIT_OBJECT_0,"unsubmitted native prefix blocked private queue");
    }
    void verify() {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        require_transport(observed<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command,&begin),"begin observed native recording");
        image_barrier(VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={width,height,1};
        raw<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(native(command),upload.buffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
        image_barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        require_transport(queue_family_for(command)==family,"transport must use observed native command-pool family");
        require_transport(retain_transport_recording(device,captured,command),"pin exact recording before native copy commands");
        auto& transport=captured->surfaces[motion];require_transport(copy_native(*device,transport,native(command),false,motion,family),"record production native freeze");
        require_transport(begin_d12(*captured,0),"begin exact D3D12 transport work");auto* list=captured->lists[0].Get();copy_d12(list,transport,false);
        D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=transport.texture.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=d12_readback.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=transport.footprint;
        d12_barrier(list,transport.texture.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
        d12_barrier(list,transport.texture.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);copy_d12(list,transport,true);
        Copyback copy{device.get(),&transport,motion,family};const auto receipt=handoff(device,captured,0,0,command,copy);
        require_transport(receipt.result,"split with exact deferred production work");
        require_transport(captured->pending_callbacks==1&&captured->d12_value==0&&!captured->submitted_phases,"recording prematurely submitted D3D12 work");
        require_transport(device->queue->Signal(progress.Get(),1),"signal private queue before Vulkan submission");wait_private(1);
        image_barrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        raw<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(native(command),image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&region);
        image_barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.buffer=readback.buffer;host.size=VK_WHOLE_SIZE;
        host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        raw<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(native(command),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
        require_transport(observed<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command),"end observed native continuation");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        require_transport(observed<PFN_vkQueueSubmit>("vkQueueSubmit")(native_queue,1,&submit,finished),"submit exact native production packet");
        require_transport(raw<PFN_vkWaitForFences>("vkWaitForFences")(native_device,1,&finished,VK_TRUE,5000000000ULL),"wait exact native suffix completion");
        require_transport(device->queue->Signal(progress.Get(),2),"signal exact D3D12 work completion");wait_private(2);
        require_transport(captured->fence->GetCompletedValue()>=captured->final_value&&captured->d12_done->GetCompletedValue()>=captured->d12_value&&receipt.lifetime->disposition==RecordingDisposition::submitted,"both exact transport fences must retire");
        require_transport(!captured->complete()&&captured->recording_leases==1,"submitted source recording still references private native capture resources");
        void* mapped{};require_transport(raw<PFN_vkMapMemory>("vkMapMemory")(native_device,readback.memory,0,byte_count,0,&mapped),"map native round-trip");
        require_transport(!std::memcmp(mapped,expected.data(),byte_count),"native round-trip changed RG16 motion bits");raw<PFN_vkUnmapMemory>("vkUnmapMemory")(native_device,readback.memory);
        D3D12_RANGE range{0,transport.footprint.Footprint.RowPitch*height};require_transport(d12_readback->Map(0,&range,&mapped),"map exact D3D12 typed capture");
        for(unsigned row=0;row<height;++row)require_transport(!std::memcmp(static_cast<unsigned char*>(mapped)+row*transport.footprint.Footprint.RowPitch,expected.data()+row*width*pixel_size,width*pixel_size),"D3D12 texture never received native motion bits");
        D3D12_RANGE no_write{};d12_readback->Unmap(0,&no_write);
        require_transport(observed<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0),"reset completed native recording");
        require_transport(captured->complete()&&!captured->recording_leases,"source recording disposal must release exact transport lease");
        require_transport(observed<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command,&begin),"begin native recording to abandon");
        captured->submitted_phases=0;for(unsigned role=0;role<role_count;++role)captured->native_before[role]=captured->surfaces[role].native_used;
        require_transport(retain_transport_recording(device,captured,command),"pin exact abandoned recording before native copy commands");
        require_transport(copy_native(*device,transport,native(command),false,motion,family),"record native freeze to abandon");
        require_transport(begin_d12(*captured,0),"record D3D12 work to abandon");copy_d12(captured->lists[0].Get(),transport,false);copy_d12(captured->lists[0].Get(),transport,true);
        const auto d12_before=captured->d12_value;const auto abandoned=handoff(device,captured,0,0,command,copy);require_transport(abandoned.result,"split exact abandoned native recording");
        require_transport(observed<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0),"abandon exact native recording");
        require_transport(abandoned.lifetime->disposition==RecordingDisposition::abandoned&&!captured->pending_callbacks&&captured->d12_value==d12_before&&captured->complete(),"abandonment must retire without GPU work");
        require_transport(device->queue->Signal(progress.Get(),3),"signal private queue after abandonment");wait_private(3);
        if(debug)for(UINT64 index=0;index<debug->GetNumStoredMessages();++index) {
            SIZE_T bytes{};debug->GetMessage(index,nullptr,&bytes);std::vector<unsigned char> storage(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if(SUCCEEDED(debug->GetMessage(index,message,&bytes))&&(message->Severity==D3D12_MESSAGE_SEVERITY_ERROR||message->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION))
                throw std::runtime_error(message->pDescription);
        }
    }
    void cleanup() {
        // Called only after actual native suffix + private D3D12 fence proofs.
        observed<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(native_device,pool,nullptr);pool={};destroy_commands(native_device);
        for(auto& target:captured->surfaces)destroy_surface(*device,target);
        raw<PFN_vkDestroySemaphore>("vkDestroySemaphore")(native_device,captured->timeline,nullptr);captured->timeline={};device->slots.clear();captured.reset();
        for(auto* buffer:{&upload,&readback}){raw<PFN_vkDestroyBuffer>("vkDestroyBuffer")(native_device,buffer->buffer,nullptr);raw<PFN_vkFreeMemory>("vkFreeMemory")(native_device,buffer->memory,nullptr);}
        raw<PFN_vkDestroyImage>("vkDestroyImage")(native_device,image,nullptr);raw<PFN_vkFreeMemory>("vkFreeMemory")(native_device,image_memory,nullptr);
        raw<PFN_vkDestroyFence>("vkDestroyFence")(native_device,finished,nullptr);raw<PFN_vkDestroyDevice>("vkDestroyDevice")(native_device,nullptr);
        instance_api<PFN_vkDestroyInstance>("vkDestroyInstance")(instance,nullptr);device.reset();FreeLibrary(loader);
    }
};
}
int run_buffer_transport_test() {
    // A failed GPU proof retains resources until process exit instead of
    // destroying possibly submitted external resources or waiting forever.
    auto* probe=new TransportProbe;
    try {probe->setup();probe->verify();probe->cleanup();delete probe;
        std::cout<<"Native Vulkan production shared-buffer transport PASS: 128 RG16 pixels bit-identical in D3D12 and Vulkan, exact suffix/D3D12 fences, unsubmitted recording cancellation, zero D3D12 validation errors\n";return 0;
    }catch(const TransportSkip& why){std::cout<<"Native Vulkan transport SKIP: "<<why.what()<<'\n';return 77;}
    catch(const std::exception& why){std::cerr<<"Native Vulkan transport FAIL: "<<why.what()<<'\n';return 1;}
}
}
