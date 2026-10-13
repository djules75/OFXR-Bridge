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
struct MetadataParameters final:NVSDK_NGX_Parameter {
    bool present{true},flags_present{true};int flags{NVSDK_NGX_DLSS_Feature_Flags_MVLowRes};
    unsigned width{1672},height{1368},out_width{3344},out_height{2736};
    void Set(const char*,unsigned long long)override{}
    void Set(const char*,float)override{} void Set(const char*,double)override{}
    void Set(const char*,unsigned)override{} void Set(const char*,int)override{}
    void Set(const char*,ID3D11Resource*)override{} void Set(const char*,ID3D12Resource*)override{}
    void Set(const char*,void*)override{}
    NVSDK_NGX_Result Get(const char*,unsigned long long*)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
    NVSDK_NGX_Result Get(const char*,float*)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
    NVSDK_NGX_Result Get(const char*,double*)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
    NVSDK_NGX_Result Get(const char*,ID3D11Resource**)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
    NVSDK_NGX_Result Get(const char*,ID3D12Resource**)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
    NVSDK_NGX_Result Get(const char*,void**)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
    NVSDK_NGX_Result Get(const char* key,int* value)const override {
        if(present&&flags_present&&!std::strcmp(key,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags)){
            *value=flags;return NVSDK_NGX_Result_Success;}
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    NVSDK_NGX_Result Get(const char* key,unsigned* value)const override {
        if(!present)return NVSDK_NGX_Result_FAIL_InvalidParameter;
        if(!std::strcmp(key,NVSDK_NGX_Parameter_Width))*value=width;
        else if(!std::strcmp(key,NVSDK_NGX_Parameter_Height))*value=height;
        else if(!std::strcmp(key,NVSDK_NGX_Parameter_OutWidth))*value=out_width;
        else if(!std::strcmp(key,NVSDK_NGX_Parameter_OutHeight))*value=out_height;
        else return NVSDK_NGX_Result_FAIL_InvalidParameter;
        return NVSDK_NGX_Result_Success;
    }
    void Reset()override{present=false;}
};
const NVSDK_NGX_Handle* metadata_creation_handle{},*metadata_alias{};
VkCommandBuffer metadata_other_command{},metadata_callback_command{},metadata_callback_recording{};
Feature metadata_callback_feature{};unsigned metadata_native_slot{};
std::uint64_t metadata_outer_count{},metadata_native_count{};
void* metadata_callback_argument{};NVSDK_NGX_Result metadata_result{NVSDK_NGX_Result_Success};
NVSDK_NGX_Result metadata_create_original(VkCommandBuffer,NVSDK_NGX_Feature,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** out) {
    if(!out||!p)return NVSDK_NGX_Result_FAIL_InvalidParameter;
    *out=const_cast<NVSDK_NGX_Handle*>(metadata_creation_handle);if(p)p->Reset();return NVSDK_NGX_Result_Success;
}
NVSDK_NGX_Result metadata_create1_original(VkDevice,VkCommandBuffer cmd,NVSDK_NGX_Feature type,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** out) {
    return metadata_create_original(cmd,type,p,out);
}
NVSDK_NGX_Result metadata_native_original(VkCommandBuffer cmd,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter*,void* callback) {
    metadata_callback_command=cmd;metadata_callback_argument=callback;
    {std::scoped_lock lock(hooks().mutex);metadata_callback_feature=hooks().features.at(h);}
    if(const auto scope=EvaluationDeviceScope::exact(cmd,metadata_callback_feature.type))
        metadata_callback_recording=scope->recording();else metadata_callback_recording={};
    metadata_native_count=native_ingress::upscaler_observation().evaluations;
    return metadata_result;
}
NVSDK_NGX_Result metadata_generic_original(VkCommandBuffer cmd,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter* p,void* callback) {
    metadata_outer_count=native_ingress::upscaler_observation().evaluations;
    auto& changed=*static_cast<MetadataParameters*>(const_cast<NVSDK_NGX_Parameter*>(p));
    changed.present=true;changed.flags_present=true;
    std::swap(changed.width,changed.out_width);std::swap(changed.height,changed.out_height);
    const auto input=metadata_other_command?metadata_other_command:cmd;
    return metadata_native_slot==0?evaluate<0>(input,metadata_alias,p,callback):evaluate<1>(input,metadata_alias,p,callback);
}
void verify_metadata_dispatch(VkDevice device,VkCommandBuffer command,VkCommandBuffer other) {
    const auto cached_create=&create<2>;const auto cached_evaluate=&evaluate<2>;
    struct RestoreModules {
        struct Original{Create create;Create1 create1;Evaluate evaluate;bool typed;};
        std::array<Original,3> saved{};
        std::vector<const NVSDK_NGX_Handle*> handles;
        RestoreModules(){for(unsigned i=0;i<saved.size();++i){auto& m=hooks().modules[i];saved[i]={m.create,m.create1,m.evaluate,m.typed_implementation};}}
        ~RestoreModules(){std::scoped_lock lock(hooks().mutex);
            for(unsigned i=0;i<saved.size();++i){auto& m=hooks().modules[i];m.create=saved[i].create;m.create1=saved[i].create1;m.evaluate=saved[i].evaluate;m.typed_implementation=saved[i].typed;}
            for(auto* h:handles)hooks().features.erase(h);
            metadata_creation_handle=metadata_alias=nullptr;metadata_other_command={};}
    } restore;
    auto& generic=hooks().modules[2];generic.create=&metadata_create_original;
    generic.evaluate=&metadata_generic_original;generic.typed_implementation=false;
    for(unsigned index=0;index<2;++index){auto& m=hooks().modules[index];m.create=&metadata_create_original;
        m.create1=&metadata_create1_original;m.evaluate=&metadata_native_original;m.typed_implementation=true;}
    for(unsigned slot=0;slot<2;++slot) {
        metadata_native_slot=slot;const auto type=slot==0?NVSDK_NGX_Feature_SuperSampling:NVSDK_NGX_Feature_RayReconstruction;
        alignas(16) std::array<unsigned char,16> public_storage{},alias_storage{},late_storage{},direct_storage{};
        auto* public_handle=reinterpret_cast<const NVSDK_NGX_Handle*>(public_storage.data());
        auto* alias=reinterpret_cast<const NVSDK_NGX_Handle*>(alias_storage.data());
        auto* late=reinterpret_cast<const NVSDK_NGX_Handle*>(late_storage.data());
        auto* direct=reinterpret_cast<const NVSDK_NGX_Handle*>(direct_storage.data());
        restore.handles.insert(restore.handles.end(),{public_handle,alias,late,direct});
        MetadataParameters p;metadata_creation_handle=public_handle;NVSDK_NGX_Handle* made{};
        require_transport(cached_create(command,type,nullptr,&made)==NVSDK_NGX_Result_FAIL_InvalidParameter&&
            !made&&!hooks().features.contains(public_handle),"Vulkan null parameters were not forwarded unchanged");
        require_transport(NVSDK_NGX_SUCCEED(cached_create(command,type,&p,&made))&&!p.present&&made==public_handle,
            "native Vulkan original creator/result/mutation changed");
        const auto original=hooks().features.at(made);
        require_transport(original.creation_observed&&original.flags_known&&original.width==1672&&original.out_width==3344&&
            original.out_height==2736,"native Vulkan creation dimensions read after original consumed them");
        metadata_alias=alias;auto* callback=reinterpret_cast<void*>(std::uintptr_t{0x83});
        const auto before=native_ingress::upscaler_observation().evaluations;
        require_transport(NVSDK_NGX_SUCCEED(cached_evaluate(command,made,&p,callback)),"cached Vulkan alias result");
        require_transport(metadata_outer_count==before&&metadata_native_count==before+1&&metadata_callback_argument==callback,
            "Vulkan generic/native SR/RR boundary duplicated or original callback changed");
        require_transport(metadata_callback_feature.creation_observed&&metadata_callback_feature.out_width==3344&&
            metadata_callback_feature.out_height==2736&&metadata_callback_feature.width==1672&&
            metadata_callback_feature.creation_source_identity==original.identity&&
            metadata_callback_feature.device==device&&p.out_width==1672,
            "native Vulkan alias trusted inverted evaluation dimensions");
        require_transport(metadata_callback_recording==command&&metadata_callback_command==native(command)&&can_split(command),
            "typed native suffix lost its exact original recording/owner");
        metadata_result=NVSDK_NGX_Result_FAIL_InvalidParameter;
        require_transport(cached_evaluate(command,made,&p,callback)==metadata_result,"native Vulkan failure result changed");
        metadata_result=NVSDK_NGX_Result_Success;
        const auto alias_generation=hooks().features.at(alias).identity;
        metadata_creation_handle=public_handle;p.present=true;p.flags_present=false;
        p.width=900;p.height=600;p.out_width=1800;p.out_height=1200;
        require_transport(NVSDK_NGX_SUCCEED(cached_create(command,type,&p,&made))&&
            !hooks().features.at(made).flags_known,"Vulkan public recreation must preserve unknown actual creation flags");
        require_transport(NVSDK_NGX_SUCCEED(cached_evaluate(command,made,&p,callback))&&
            metadata_callback_feature.flags_known&&metadata_callback_feature.out_width==1800&&
            metadata_callback_feature.out_height==1200&&metadata_callback_feature.width==900&&
            metadata_callback_feature.creation_source_identity==hooks().features.at(made).identity&&
            metadata_callback_feature.identity!=alias_generation,
            "Vulkan recreated receipt failed to refresh alias generation or merge exact typed flags");
        metadata_alias=late;metadata_other_command=other;p.present=true;
        require_transport(NVSDK_NGX_SUCCEED(cached_evaluate(command,made,&p,callback))&&
            !metadata_callback_feature.creation_observed&&!metadata_callback_feature.out_width&&
            metadata_callback_feature.flags_known&&metadata_callback_feature.device==device,
            "Vulkan foreign command inherited an enclosing creation receipt");
        metadata_other_command={};metadata_creation_handle=direct;
        p.present=true;p.width=2048;p.height=1080;p.out_width=4096;p.out_height=2160;
        require_transport(NVSDK_NGX_SUCCEED(slot==0?create1<0>(device,command,type,&p,&made):
            create1<1>(device,command,type,&p,&made)),"explicit-device Vulkan creation");
        const auto direct_created=hooks().features.at(made);metadata_alias=direct;
        p.present=true;p.width=1672;p.height=1368;p.out_width=3344;p.out_height=2736;
        require_transport(NVSDK_NGX_SUCCEED(cached_evaluate(command,public_handle,&p,callback))&&
            metadata_callback_feature.creation_source_identity==direct_created.identity&&
            metadata_callback_feature.out_width==4096,"Vulkan public ancestor overwrote direct native creation");
        for(auto* h:{public_handle,alias,late,direct}){hooks().features.erase(h);std::erase(restore.handles,h);}
    }
    std::cout<<"Native Vulkan immutable creation/cached SR-RR aliases: exact native suffix/original recording, resource fallback, callback/result isolation PASS\n";
}
struct TransportProbe {
    static constexpr unsigned width=16,height=8,pixel_size=4,byte_count=width*height*pixel_size;
    HMODULE loader{};VkInstance instance{};VkPhysicalDevice physical{};VkDevice native_device{};
    VkQueue native_queue{};uint32_t family{};PFN_vkGetInstanceProcAddr gipa{};PFN_vkGetDeviceProcAddr gdpa{};
    std::shared_ptr<Device> device;std::shared_ptr<Slot> captured;
    VkCommandPool pool{};VkCommandBuffer command{};VkImage image{};VkDeviceMemory image_memory{};VkFence finished{};
    HostBuffer upload,readback;ComPtr<ID3D12Resource> d12_readback;ComPtr<ID3D12Fence> progress;
    ComPtr<ID3D12InfoQueue> debug;std::array<unsigned char,byte_count> expected{};
    bool bootstrap{};
    HMODULE luke_fixture{};
    std::unordered_map<std::string,PFN_vkVoidFunction> cached_instance_commands;
    std::unordered_map<std::string,PFN_vkVoidFunction> cached_export_commands;
    template<class T>T instance_api(const char* name)const {
        return reinterpret_cast<T>(gipa(instance,name));
    }
    template<class T>T raw(const char* name)const {
        return reinterpret_cast<T>(unhooked_command(gdpa(native_device,name)));
    }
    template<class T>T observed(const char* name)const {
        if(bootstrap) {
            if(const auto saved=cached_export_commands.find(name);saved!=cached_export_commands.end())
                return reinterpret_cast<T>(saved->second);
            const auto found=cached_instance_commands.find(name);
            return reinterpret_cast<T>(found!=cached_instance_commands.end()?found->second:gipa(instance,name));
        }
        return reinterpret_cast<T>(resolve_command(native_device,name,reinterpret_cast<PFN_vkVoidFunction>(raw<T>(name))));
    }
    uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags properties)const {
        for(uint32_t i=0;i<device->memory.memoryTypeCount;++i)
            if((bits&(1u<<i))&&(device->memory.memoryTypes[i].propertyFlags&properties)==properties)return i;
        throw std::runtime_error("native transport memory type unavailable");
    }
    void setup() {
        if(bootstrap){
            require_transport(luke_vulkan_process(),"bootstrap executable must be one of the two selected games");
            luke_fixture=LoadLibraryW(L"luke-runtime-fixture.dll");
            require_transport(luke_fixture&&native_ingress::luke_detected(),"load actual Luke marker fixture");
        }
        loader=LoadLibraryW(L"vulkan-1.dll");if(!loader)throw TransportSkip("Vulkan loader unavailable");
        if(bootstrap)for(const auto* name:{"vkCreateCommandPool","vkAllocateCommandBuffers","vkBeginCommandBuffer"}) {
            // REAL VR/engines can cache actual export pointers before OFXR is
            // loaded. GIPA/GDPA replacement cannot observe these invocations.
            cached_export_commands.emplace(name,reinterpret_cast<PFN_vkVoidFunction>(GetProcAddress(loader,name)));
            require_transport(cached_export_commands.at(name)!=nullptr,"cache loader export before hooks");
        }
        if(bootstrap)poll_capture();
        gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
        require_transport(gipa!=nullptr,"Vulkan instance dispatcher unavailable");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_3;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
        require_transport(reinterpret_cast<PFN_vkCreateInstance>(gipa({},"vkCreateInstance"))(&info,nullptr,&instance),"create native probe instance");
        gdpa=instance_api<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
        if(bootstrap)for(const auto* name:{"vkCreateCommandPool","vkAllocateCommandBuffers","vkBeginCommandBuffer",
            "vkEndCommandBuffer","vkResetCommandBuffer","vkDestroyCommandPool","vkQueueSubmit"}) {
            // Actual engines cache device command addresses through GIPA before
            // creating VkDevice. No direct call to resolve_command may hide gaps.
            cached_instance_commands.emplace(name,gipa(instance,name));
            require_transport(cached_instance_commands.at(name)!=nullptr,"cache GIPA device command");
        }
        if(bootstrap)gdpa=hooks().gdpa; // raw driver dispatcher, not our command wrapper
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
        if(bootstrap){
            const auto native_owner=configure_luke_owner(device->owner.Get(),device->queue.Get());
            require_transport(native_owner==native_device,"early observed native device must bind independently of XR API");
            {std::scoped_lock lock(hooks().mutex);device=hooks().devices.at(native_owner);}
            ComPtr<ID3D12Device> queried;ComPtr<ID3D12CommandQueue> producer;
            require_transport(!query_luke_context(&queried,&producer),"creation must not publish an unsubmitted Luke producer");
        }
        (void)device->owner.As(&debug);if(debug)debug->ClearStoredMessages();
        require_transport(initialize_commands(instance,physical,native_device,gipa,gdpa),"initialize observed native command stream");
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool_info.queueFamilyIndex=family;pool_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        require_transport(observed<PFN_vkCreateCommandPool>("vkCreateCommandPool")(native_device,&pool_info,nullptr,&pool),"create observed native pool");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
        require_transport(observed<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(native_device,&allocate,&command),"allocate observed native command");
        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image_info.imageType=VK_IMAGE_TYPE_2D;image_info.format=VK_FORMAT_R16G16_SFLOAT;
        image_info.extent={width,height,1};image_info.mipLevels=image_info.arrayLayers=1;image_info.samples=VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling=VK_IMAGE_TILING_OPTIMAL;image_info.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        const auto create_image_fn=bootstrap?&create_image:raw<PFN_vkCreateImage>("vkCreateImage");
        require_transport(create_image_fn(native_device,&image_info,nullptr,&image),"create native source image");
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
    void image_barrier(VkImageLayout from,VkImageLayout to,VkAccessFlags source,VkAccessFlags destination,VkCommandBuffer received={}) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=from;barrier.newLayout=to;barrier.srcAccessMask=source;barrier.dstAccessMask=destination;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        observed<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(received?received:command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    }
    void wait_private(uint64_t value) {
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require_transport(event!=nullptr,"create private progress event");
        const auto result=progress->SetEventOnCompletion(value,event);const auto waited=SUCCEEDED(result)?WaitForSingleObject(event,2000):WAIT_FAILED;CloseHandle(event);
        require_transport(result,"arm private progress event");require_transport(waited==WAIT_OBJECT_0,"unsubmitted native prefix blocked private queue");
    }
    void verify() {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        require_transport(observed<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command,&begin),"begin observed native recording");
        require_transport(can_split(command),"GIPA-cached original recording was not observed before SR");
        VkCommandBuffer other{};VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool=pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
        require_transport(observed<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(native_device,&allocation,&other),
            "allocate independent metadata command");
        // Exercise a current suffix whose raw handle is not the registered
        // original. No packet is submitted; reset proves cancellation before
        // the existing hardware round-trip starts its own exact fenced work.
        CommandHandoff metadata_packet{captured->timeline,1,2,3};
        require_transport(split(command,metadata_packet).result,"split exact original before native metadata dispatch");
        require_transport(native(command)!=command,"metadata suffix counterfactual missing");
        verify_metadata_dispatch(native_device,command,other);
        require_transport(observed<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0),"cancel metadata-only recording");
        require_transport(observed<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command,&begin),"begin real transport after metadata cancellation");
        image_barrier(VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={width,height,1};
        raw<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(native(command),upload.buffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
        image_barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        require_transport(queue_family_for(command)==family,"transport must use observed native command-pool family");
        require_transport(retain_transport_recording(device,captured,command),"pin exact recording before native copy commands");
        auto& transport=captured->surfaces[motion];require_transport(copy_native(*device,transport,native(command),false,motion,family),"record production native freeze");
        // The first actual handoff supplies the raw suffix received by a
        // generic wrapper. A second inner handoff closes that received handle.
        // Its post-evaluate vkCmd calls must follow this exact live chain.
        require_transport(begin_d12(*captured,0),"begin first actual Vulkan transport split");
        copy_d12(captured->lists[0].Get(),transport,false);copy_d12(captured->lists[0].Get(),transport,true);
        Copyback copy{device.get(),&transport,motion,family};
        const auto first_receipt=handoff(device,captured,0,0,command,copy,false);
        require_transport(first_receipt.result,"first exact native handoff before wrapper receives suffix");
        const auto wrapper_received=native(command);
        require_transport(copy_native(*device,transport,native(wrapper_received),false,motion,family),"second exact native freeze");
        const unsigned phase=1;
        require_transport(begin_d12(*captured,phase),"begin exact D3D12 transport work");auto* list=captured->lists[phase].Get();copy_d12(list,transport,false);
        D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=transport.texture.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=d12_readback.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=transport.footprint;
        d12_barrier(list,transport.texture.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
        d12_barrier(list,transport.texture.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);copy_d12(list,transport,true);
        const auto receipt=handoff(device,captured,phase,0,wrapper_received,copy,bootstrap);
        require_transport(receipt.result,"split with exact deferred production work");
        require_transport(native(wrapper_received)==native(command)&&native(wrapper_received)!=wrapper_received&&
            device_for(wrapper_received)==native_device&&queue_family_for(wrapper_received)==family,
            "nested typed split lost its outer wrapper continuation alias");
        require_transport(captured->pending_callbacks==2&&captured->d12_value==0&&!captured->submitted_phases,"recording prematurely submitted D3D12 work");
        if(bootstrap){ComPtr<ID3D12Device> owner;ComPtr<ID3D12CommandQueue> queue;
            require_transport(!query_luke_context(&owner,&queue),"recording must not publish an unsubmitted producer");}
        require_transport(device->queue->Signal(progress.Get(),1),"signal private queue before Vulkan submission");wait_private(1);
        image_barrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT,wrapper_received);
        observed<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(wrapper_received,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&region);
        image_barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT,wrapper_received);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.buffer=readback.buffer;host.size=VK_WHOLE_SIZE;
        host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        observed<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(wrapper_received,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
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
        if(bootstrap){ComPtr<ID3D12Device> owner;ComPtr<ID3D12CommandQueue> queue;
            require_transport(query_luke_context(&owner,&queue)&&owner==device->owner&&queue==device->queue,
                "exact accepted native prefix and D12 transport must publish their actual owner");
            device->production_submitted=false;}
        require_transport(observed<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command,&begin),"begin native recording to abandon");
        captured->submitted_phases=0;for(unsigned role=0;role<role_count;++role)captured->native_before[role]=captured->surfaces[role].native_used;
        require_transport(retain_transport_recording(device,captured,command),"pin exact abandoned recording before native copy commands");
        require_transport(copy_native(*device,transport,native(command),false,motion,family),"record native freeze to abandon");
        require_transport(begin_d12(*captured,phase),"record D3D12 work to abandon");copy_d12(captured->lists[phase].Get(),transport,false);copy_d12(captured->lists[phase].Get(),transport,true);
        const auto d12_before=captured->d12_value;const auto abandoned=handoff(device,captured,phase,0,command,copy,bootstrap);require_transport(abandoned.result,"split exact abandoned native recording");
        require_transport(observed<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0),"abandon exact native recording");
        require_transport(abandoned.lifetime->disposition==RecordingDisposition::abandoned&&!captured->pending_callbacks&&captured->d12_value==d12_before&&captured->complete(),"abandonment must retire without GPU work");
        if(bootstrap){ComPtr<ID3D12Device> owner;ComPtr<ID3D12CommandQueue> queue;
            require_transport(!query_luke_context(&owner,&queue),"abandoned recording must not publish a producer");}
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
        if(bootstrap){stop_owner(native_device);
            ComPtr<ID3D12Device> owner;ComPtr<ID3D12CommandQueue> queue;
            require_transport(!query_luke_context(&owner,&queue),"retired Vulkan owner must not survive XR recreation");}
        instance_api<PFN_vkDestroyInstance>("vkDestroyInstance")(instance,nullptr);device.reset();FreeLibrary(loader);
        if(luke_fixture)FreeLibrary(luke_fixture);
    }
};
}
int run_buffer_transport_test(bool bootstrap) {
    // A failed GPU proof retains resources until process exit instead of
    // destroying possibly submitted external resources or waiting forever.
    auto* probe=new TransportProbe;
    probe->bootstrap=bootstrap;
    try {probe->setup();probe->verify();probe->cleanup();delete probe;
        std::cout<<"Native Vulkan production shared-buffer transport PASS: 128 RG16 pixels bit-identical in D3D12 and Vulkan, exact suffix/D3D12 fences, unsubmitted recording cancellation, zero D3D12 validation errors\n";return 0;
    }catch(const TransportSkip& why){std::cout<<"Native Vulkan transport SKIP: "<<why.what()<<'\n';return 77;}
    catch(const std::exception& why){std::cerr<<"Native Vulkan transport FAIL: "<<why.what()<<'\n';return 1;}
}
}
