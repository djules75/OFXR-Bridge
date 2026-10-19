// The NGX guide capture against a stand-in nvngx_dlss.dll (fake_nvngx_dlss)
// on WARP: the module is found and detoured, the originals still run, a
// feature per eye becomes a stream each, every evaluation publishes a
// snapshot of the motion vectors recorded into the game's list, and the
// resolver pairs the two streams into a stereo guide set whose fields are
// the parameters' - rectangles, scale, jitter, the low-resolution vector
// size from the creation flags.
#include "xrfg/d3d11_d3d12_interop.hpp"
#include "xrfg/dlss_motion_vectors.hpp"
#include "xrfg/ngx_guide_capture.hpp"
#include "xrfg/vulkan_d3d12_interop.hpp"

#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_vk.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_hresult(HRESULT result, const char* what) {
    if (FAILED(result)) {
        char text[256];
        std::snprintf(text, sizeof(text), "%s failed: 0x%08lX", what, static_cast<unsigned long>(result));
        throw std::runtime_error(text);
    }
}

// The SDK's parameter interface, as a game fills it: every typed Set and Get.
class Parameters final : public NVSDK_NGX_Parameter {
public:
    void Set(const char* name, unsigned long long value) override { u64_[name] = value; }
    void Set(const char* name, float value) override { f32_[name] = value; }
    void Set(const char* name, double value) override { f64_[name] = value; }
    void Set(const char* name, unsigned int value) override { u32_[name] = value; }
    void Set(const char* name, int value) override { i32_[name] = value; }
    void Set(const char* name, ID3D11Resource* value) override { d3d11_[name] = value; }
    void Set(const char* name, ID3D12Resource* value) override { d3d12_[name] = value; }
    void Set(const char* name, void* value) override { pointers_[name] = value; }
    NVSDK_NGX_Result Get(const char* name, unsigned long long* value) const override { return get(u64_, name, value); }
    NVSDK_NGX_Result Get(const char* name, float* value) const override { return get(f32_, name, value); }
    NVSDK_NGX_Result Get(const char* name, double* value) const override { return get(f64_, name, value); }
    NVSDK_NGX_Result Get(const char* name, unsigned int* value) const override { return get(u32_, name, value); }
    NVSDK_NGX_Result Get(const char* name, int* value) const override { return get(i32_, name, value); }
    NVSDK_NGX_Result Get(const char* name, ID3D11Resource** value) const override { return get(d3d11_, name, value); }
    NVSDK_NGX_Result Get(const char* name, ID3D12Resource** value) const override { return get(d3d12_, name, value); }
    NVSDK_NGX_Result Get(const char* name, void** value) const override { return get(pointers_, name, value); }
    void Reset() override {
        u64_.clear(); f32_.clear(); f64_.clear(); u32_.clear(); i32_.clear();
        d3d11_.clear(); d3d12_.clear(); pointers_.clear();
    }

private:
    template <class T>
    static NVSDK_NGX_Result get(const std::map<std::string, T>& values, const char* name, T* out) {
        const auto found = values.find(name);
        if (found == values.end()) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        *out = found->second;
        return NVSDK_NGX_Result_Success;
    }
    std::map<std::string, unsigned long long> u64_;
    std::map<std::string, float> f32_;
    std::map<std::string, double> f64_;
    std::map<std::string, unsigned int> u32_;
    std::map<std::string, int> i32_;
    std::map<std::string, ID3D11Resource*> d3d11_;
    std::map<std::string, ID3D12Resource*> d3d12_;
    std::map<std::string, void*> pointers_;
};

ComPtr<ID3D12Resource> create_texture(
    ID3D12Device* device, UINT width, UINT height, UINT16 slices, DXGI_FORMAT format,
    D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = slices;
    description.MipLevels = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ComPtr<ID3D12Resource> texture;
    require_hresult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
        state, nullptr, IID_PPV_ARGS(texture.GetAddressOf())), "CreateCommittedResource");
    return texture;
}

using CreateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback);
using EvaluateFeatureC = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback_C);
using ReleaseFeature = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
using Counter = unsigned (*)();

using CreateFeature11 = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D11DeviceContext*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFeature11 = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D11DeviceContext*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback);

// V512: a bridged D3D11 session. The game's DLSS runs on its D3D11 device;
// the capture copies each evaluation's vectors and depth into textures shared
// with the layer's D3D12 device and publishes them on its queue. Checked: the
// D3D11 exports are detoured, every evaluation publishes with its depth, the
// resolver pairs the two eye streams for a side-by-side image twice an
// output's width (UEVR's Native Stereo, Ready or Not), and the snapshot on
// the D3D12 side holds the very vectors the game wrote in D3D11.
using CreateFeatureVk = NVSDK_NGX_Result(NVSDK_CONV*)(
    VkCommandBuffer, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFeatureVk = NVSDK_NGX_Result(NVSDK_CONV*)(
    VkCommandBuffer, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback);

// V513: a bridged Vulkan session. The capture records the copies into the
// game's command buffer; the Vulkan queue layer, in the chain as the tray
// registers it, signals the shared timeline after the game submits it; the
// snapshot copies run on the D3D12 queue at the flush. Checked: the Vulkan
// exports are detoured; both eyes publish with depth (a packed D32S8 depth,
// through a buffer); the D3D12 snapshots hold the cleared vectors and depth;
// and a copy whose command buffer is never submitted is dropped at the end of
// the frame rather than offered.
const wchar_t* g_queue_layer_path = nullptr;

// queue_layer_path null (V514): no queue layer in the chain, as in Star
// Citizen; the copies are published at the flush after the game's
// submission, which the layer makes at the bridged release.
int run_vulkan(HMODULE module, const wchar_t* queue_layer_path) {
    const bool tracked = queue_layer_path != nullptr;
    const auto create = reinterpret_cast<CreateFeatureVk>(
        GetProcAddress(module, "NVSDK_NGX_VULKAN_CreateFeature"));
    const auto evaluate = reinterpret_cast<EvaluateFeatureVk>(
        GetProcAddress(module, "NVSDK_NGX_VULKAN_EvaluateFeature"));
    const auto release = reinterpret_cast<ReleaseFeature>(
        GetProcAddress(module, "NVSDK_NGX_VULKAN_ReleaseFeature"));
    require(create && evaluate && release, "the stand-in lacks a Vulkan export");

    // The queue layer in this process's chain, as layer_call_chain_depth.cmake
    // puts it there.
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring directory = std::wstring(temp) + L"xrfg-ngx-vk-" +
        std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(directory.c_str(), nullptr);
    std::wstring library(tracked ? queue_layer_path : L"");
    std::string escaped;
    for (wchar_t c : library) {
        if (c == L'\\') escaped += "\\\\";
        else escaped += static_cast<char>(c);
    }
    {
        FILE* manifest = nullptr;
        _wfopen_s(&manifest, (directory + L"\\VK_LAYER_OFXR_queue_serialize.json").c_str(), L"wb");
        require(manifest != nullptr, "writing the queue layer manifest");
        std::fprintf(manifest,
            "{\"file_format_version\":\"1.2.0\",\"layer\":{\"name\":\"VK_LAYER_OFXR_queue_serialize\","
            "\"type\":\"GLOBAL\",\"library_path\":\"%s\",\"api_version\":\"1.3.296\","
            "\"implementation_version\":\"1\",\"description\":\"OFXR queue layer under test\","
            "\"functions\":{\"vkNegotiateLoaderLayerInterfaceVersion\":\"OFXR_vkNegotiateLoaderLayerInterfaceVersion\","
            "\"vkGetInstanceProcAddr\":\"OFXR_vkGetInstanceProcAddr\",\"vkGetDeviceProcAddr\":\"OFXR_vkGetDeviceProcAddr\"}}}",
            escaped.c_str());
        std::fclose(manifest);
    }
    if (tracked) {
        SetEnvironmentVariableW(L"VK_LAYER_PATH", directory.c_str());
        SetEnvironmentVariableW(L"VK_INSTANCE_LAYERS", L"VK_LAYER_OFXR_queue_serialize");
    }
    // The tray's own registration of the layer, while it is armed, is an
    // implicit layer of the same name from the build it armed: kept out, so
    // the layer under test is the one in the chain.
    SetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", L"~implicit~");
    HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (loader == nullptr) {
        std::cout << "SKIP: no Vulkan implementation on this machine\n";
        return 0;
    }
    const auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
    const auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &application;
    VkInstance instance{};
    require(create_instance(&instance_info, nullptr, &instance) == VK_SUCCESS, "vkCreateInstance");
    const auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
    const auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance, "vkGetPhysicalDeviceProperties"));
    const auto families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    const auto create_device = reinterpret_cast<PFN_vkCreateDevice>(gipa(instance, "vkCreateDevice"));
    const auto gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance, "vkGetDeviceProcAddr"));
    std::uint32_t count = 0;
    enumerate(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> physicals(count);
    enumerate(instance, &count, physicals.data());
    VkPhysicalDevice physical{};
    for (VkPhysicalDevice candidate : physicals) {
        VkPhysicalDeviceProperties p{};
        properties(candidate, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { physical = candidate; break; }
    }
    if (physical == VK_NULL_HANDLE && !physicals.empty()) physical = physicals.front();
    require(physical != VK_NULL_HANDLE, "no Vulkan physical device");
    std::uint32_t family_count = 0;
    families(physical, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> family_properties(family_count);
    families(physical, &family_count, family_properties.data());
    std::uint32_t family = UINT32_MAX;
    for (std::uint32_t i = 0; i < family_count; ++i) {
        if ((family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) { family = i; break; }
    }
    require(family != UINT32_MAX, "no graphics queue family");
    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.timelineSemaphore = VK_TRUE;
    const char* extensions[]{VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext = &features12;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 2;
    device_info.ppEnabledExtensionNames = extensions;
    VkDevice device{};
    require(create_device(physical, &device_info, nullptr, &device) == VK_SUCCESS, "vkCreateDevice");
    require((GetModuleHandleW(L"OFXR_vulkan_queue_layer.dll") != nullptr) == tracked,
        tracked ? "the Vulkan queue layer is not in the chain"
                : "the Vulkan queue layer is in the chain of the untracked run");
    const auto load = [&](const char* name) { return gdpa(device, name); };
    const auto get_queue = reinterpret_cast<PFN_vkGetDeviceQueue>(load("vkGetDeviceQueue"));
    const auto create_image = reinterpret_cast<PFN_vkCreateImage>(load("vkCreateImage"));
    const auto image_requirements = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(load("vkGetImageMemoryRequirements"));
    const auto allocate_memory = reinterpret_cast<PFN_vkAllocateMemory>(load("vkAllocateMemory"));
    const auto bind_image = reinterpret_cast<PFN_vkBindImageMemory>(load("vkBindImageMemory"));
    const auto create_pool = reinterpret_cast<PFN_vkCreateCommandPool>(load("vkCreateCommandPool"));
    const auto allocate_buffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(load("vkAllocateCommandBuffers"));
    const auto begin = reinterpret_cast<PFN_vkBeginCommandBuffer>(load("vkBeginCommandBuffer"));
    const auto end = reinterpret_cast<PFN_vkEndCommandBuffer>(load("vkEndCommandBuffer"));
    const auto barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(load("vkCmdPipelineBarrier"));
    const auto clear_color = reinterpret_cast<PFN_vkCmdClearColorImage>(load("vkCmdClearColorImage"));
    const auto clear_depth = reinterpret_cast<PFN_vkCmdClearDepthStencilImage>(load("vkCmdClearDepthStencilImage"));
    const auto submit = reinterpret_cast<PFN_vkQueueSubmit>(load("vkQueueSubmit"));
    const auto wait_idle = reinterpret_cast<PFN_vkQueueWaitIdle>(load("vkQueueWaitIdle"));
    const auto memory_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        gipa(instance, "vkGetPhysicalDeviceMemoryProperties"));
    VkPhysicalDeviceMemoryProperties memory{};
    memory_properties(physical, &memory);
    VkQueue queue{};
    get_queue(device, family, 0, &queue);

    const xrfg::VulkanSessionBinding binding{instance, physical, device, family, 0};
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue12;
    require_hresult(xrfg::create_d3d12_device_for_vulkan(binding, device12.GetAddressOf(),
        queue12.GetAddressOf()), "create_d3d12_device_for_vulkan");
    xrfg::configure_ngx_guide_capture_vulkan(binding, device12.Get(), queue12.Get(), true);
    xrfg::poll_ngx_guide_capture();
    auto statistics = xrfg::ngx_guide_capture_statistics();
    if (!statistics.vulkan_active || statistics.vulkan_modules_hooked != 1) {
        std::cerr << "vulkan_active=" << statistics.vulkan_active
                  << " vulkan_modules_hooked=" << statistics.vulkan_modules_hooked
                  << " failures=" << statistics.vulkan_transport_failures
                  << " stage=" << statistics.vulkan_start_stage << std::endl;
    }
    require(statistics.vulkan_untracked == !tracked,
        "the Vulkan capture did not report whether it tracks submissions");
    require(statistics.vulkan_active && statistics.vulkan_modules_hooked == 1,
        "the stand-in's Vulkan exports were not detoured, or the capture did not start");

    const auto image = [&](VkFormat format, std::uint32_t width, std::uint32_t height, VkImageUsageFlags usage) {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        VkImage result{};
        require(create_image(device, &info, nullptr, &result) == VK_SUCCESS, "vkCreateImage");
        VkMemoryRequirements requirements{};
        image_requirements(device, result, &requirements);
        std::uint32_t type = 0;
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            if ((requirements.memoryTypeBits & (1U << i)) != 0 &&
                (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
                type = i;
                break;
            }
        }
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = type;
        VkDeviceMemory backing{};
        require(allocate_memory(device, &allocate, nullptr, &backing) == VK_SUCCESS, "vkAllocateMemory");
        require(bind_image(device, result, backing, 0) == VK_SUCCESS, "vkBindImageMemory");
        return result;
    };
    constexpr std::uint32_t kRenderWidth = 48, kRenderHeight = 32;
    constexpr std::uint32_t kOutputWidth = 96, kOutputHeight = 64;
    constexpr VkImageUsageFlags kInput = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const VkImage motion_left = image(VK_FORMAT_R16G16_SFLOAT, kRenderWidth, kRenderHeight, kInput);
    const VkImage motion_right = image(VK_FORMAT_R16G16_SFLOAT, kRenderWidth, kRenderHeight, kInput);
    const VkImage depth = image(VK_FORMAT_D32_SFLOAT_S8_UINT, kRenderWidth, kRenderHeight,
        kInput | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    const VkImage output = image(VK_FORMAT_R8G8B8A8_UNORM, kOutputWidth, kOutputHeight,
        kInput | VK_IMAGE_USAGE_STORAGE_BIT);

    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool{};
    require(create_pool(device, &pool_info, nullptr, &pool) == VK_SUCCESS, "vkCreateCommandPool");
    VkCommandBufferAllocateInfo buffer_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    buffer_info.commandPool = pool;
    buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    buffer_info.commandBufferCount = 2;
    std::array<VkCommandBuffer, 2> buffers{};
    require(allocate_buffers(device, &buffer_info, buffers.data()) == VK_SUCCESS, "vkAllocateCommandBuffers");

    Parameters creation;
    creation.Set(NVSDK_NGX_Parameter_Width, kRenderWidth);
    creation.Set(NVSDK_NGX_Parameter_Height, kRenderHeight);
    creation.Set(NVSDK_NGX_Parameter_OutWidth, kOutputWidth);
    creation.Set(NVSDK_NGX_Parameter_OutHeight, kOutputHeight);
    creation.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
        static_cast<int>(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted));
    NVSDK_NGX_Handle* left = nullptr;
    NVSDK_NGX_Handle* right = nullptr;
    VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    require(begin(buffers[0], &begin_info) == VK_SUCCESS, "vkBeginCommandBuffer");
    require(NVSDK_NGX_SUCCEED(create(buffers[0], NVSDK_NGX_Feature_SuperSampling, &creation, &left)) && left &&
            NVSDK_NGX_SUCCEED(create(buffers[0], NVSDK_NGX_Feature_SuperSampling, &creation, &right)) && right,
        "Vulkan feature creation failed through the hook");

    // The game's frame: clear the inputs, put them where DLSS reads them.
    const auto transition = [&](VkCommandBuffer buffer, VkImage target, VkImageAspectFlags aspect,
                                 VkImageLayout from, VkImageLayout to) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = target;
        b.subresourceRange = {aspect, 0, 1, 0, 1};
        barrier(buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
            0, nullptr, 0, nullptr, 1, &b);
    };
    const VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkImageSubresourceRange depth_range{VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
    for (VkImage target : {motion_left, motion_right}) {
        transition(buffers[0], target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }
    transition(buffers[0], depth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue left_value{};
    left_value.float32[0] = 1.5F;
    left_value.float32[1] = -2.0F;
    VkClearColorValue right_value{};
    right_value.float32[0] = 3.0F;
    right_value.float32[1] = 0.5F;
    clear_color(buffers[0], motion_left, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &left_value, 1, &color_range);
    clear_color(buffers[0], motion_right, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &right_value, 1, &color_range);
    const VkClearDepthStencilValue depth_value{0.5F, 0};
    clear_depth(buffers[0], depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &depth_value, 1, &depth_range);
    for (VkImage target : {motion_left, motion_right}) {
        transition(buffers[0], target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    transition(buffers[0], depth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const auto resource = [](VkImage target, VkFormat format, std::uint32_t width, std::uint32_t height,
                              VkImageAspectFlags aspect) {
        NVSDK_NGX_Resource_VK value{};
        value.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
        value.Resource.ImageViewInfo.Image = target;
        value.Resource.ImageViewInfo.Format = format;
        value.Resource.ImageViewInfo.Width = width;
        value.Resource.ImageViewInfo.Height = height;
        value.Resource.ImageViewInfo.SubresourceRange = {aspect, 0, 1, 0, 1};
        return value;
    };
    NVSDK_NGX_Resource_VK output_resource = resource(output, VK_FORMAT_R8G8B8A8_UNORM, kOutputWidth,
        kOutputHeight, VK_IMAGE_ASPECT_COLOR_BIT);
    NVSDK_NGX_Resource_VK left_resource = resource(motion_left, VK_FORMAT_R16G16_SFLOAT, kRenderWidth,
        kRenderHeight, VK_IMAGE_ASPECT_COLOR_BIT);
    NVSDK_NGX_Resource_VK right_resource = resource(motion_right, VK_FORMAT_R16G16_SFLOAT, kRenderWidth,
        kRenderHeight, VK_IMAGE_ASPECT_COLOR_BIT);
    NVSDK_NGX_Resource_VK depth_resource = resource(depth, VK_FORMAT_D32_SFLOAT_S8_UINT, kRenderWidth,
        kRenderHeight, VK_IMAGE_ASPECT_DEPTH_BIT);
    const auto evaluate_eye = [&](VkCommandBuffer buffer, NVSDK_NGX_Handle* handle,
                                  NVSDK_NGX_Resource_VK* vectors) {
        Parameters frame;
        frame.Set(NVSDK_NGX_Parameter_Output, static_cast<void*>(&output_resource));
        frame.Set(NVSDK_NGX_Parameter_MotionVectors, static_cast<void*>(vectors));
        frame.Set(NVSDK_NGX_Parameter_Depth, static_cast<void*>(&depth_resource));
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, kRenderWidth);
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, kRenderHeight);
        frame.Set(NVSDK_NGX_Parameter_Reset, 0);
        require(NVSDK_NGX_SUCCEED(evaluate(buffer, handle, &frame, nullptr)),
            "Vulkan evaluation failed through the hook");
    };
    evaluate_eye(buffers[0], left, &left_resource);
    evaluate_eye(buffers[0], right, &right_resource);
    require(end(buffers[0]) == VK_SUCCESS, "vkEndCommandBuffer");
    // Recorded, not submitted: nothing is published yet, at any flush - a
    // game recording ahead submits after the frame before ends. (Untracked,
    // the layer flushes only at the bridged release, after the submission.)
    if (tracked) {
        xrfg::flush_ngx_guide_capture_vulkan(false);
        xrfg::flush_ngx_guide_capture_vulkan(true);
    }
    statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.vulkan_publications == 0 && statistics.vulkan_transport_failures == 0 &&
            statistics.vulkan_dropped == 0,
        "a Vulkan copy was published, or dropped, before its command buffer was submitted");
    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &buffers[0];
    require(submit(queue, 1, &submit_info, VK_NULL_HANDLE) == VK_SUCCESS, "vkQueueSubmit");
    // Untracked, the bridge's wait for the game's queue orders the D3D12
    // queue behind the copy; the test waits for it instead.
    if (!tracked) require(wait_idle(queue) == VK_SUCCESS, "vkQueueWaitIdle (untracked)");
    xrfg::flush_ngx_guide_capture_vulkan(false);
    require(wait_idle(queue) == VK_SUCCESS, "vkQueueWaitIdle");
    statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.vulkan_publications == 2 && statistics.vulkan_dropped == 0,
        "the submitted Vulkan copies were not both published at the flush");

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC eye_description{};
    eye_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    eye_description.Width = kOutputWidth * 2U;
    eye_description.Height = kOutputHeight;
    eye_description.DepthOrArraySize = 1;
    eye_description.MipLevels = 1;
    eye_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    eye_description.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> eye_image;
    require_hresult(device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &eye_description,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(eye_image.GetAddressOf())), "eye image");
    const auto set = xrfg::resolve_dlss_motion_vectors(eye_image.Get(), queue12.Get());
    require(set != nullptr && set->eye_count == 2, "no side-by-side guide set resolved from Vulkan");

    ComPtr<ID3D12CommandAllocator> allocator;
    require_hresult(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(allocator.GetAddressOf())), "allocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    require_hresult(device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
        nullptr, IID_PPV_ARGS(list.GetAddressOf())), "list");
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer_description{};
    buffer_description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_description.Width = 3U * 256U * kRenderHeight;
    buffer_description.Height = 1;
    buffer_description.DepthOrArraySize = 1;
    buffer_description.MipLevels = 1;
    buffer_description.SampleDesc.Count = 1;
    buffer_description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    require_hresult(device12->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE,
        &buffer_description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(readback.GetAddressOf())), "readback");
    const auto copy_out = [&](ID3D12Resource* source_resource, D3D12_RESOURCE_STATES state,
                              DXGI_FORMAT format, unsigned slot) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = source_resource;
        b.Transition.StateBefore = state;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = source_resource;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint.Offset = slot * 256ULL * kRenderHeight;
        destination.PlacedFootprint.Footprint = {format, kRenderWidth, kRenderHeight, 1, 256};
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        list->ResourceBarrier(1, &b);
    };
    for (unsigned eye = 0; eye < 2; ++eye) {
        const auto& frame = set->eyes[eye];
        require(frame && frame->motion_vectors && frame->depth, "a Vulkan eye lacks its vectors or depth");
        require(frame->producer_queue.Get() == queue12.Get() && frame->depth_inverted,
            "the Vulkan frame's queue or flags were not carried");
        copy_out(frame->motion_vectors.Get(), frame->resource_state, DXGI_FORMAT_R16G16_FLOAT, eye);
    }
    copy_out(set->eyes[0]->depth.Get(), set->eyes[0]->depth_resource_state, DXGI_FORMAT_R32_FLOAT, 2);
    require_hresult(list->Close(), "Close");
    ID3D12CommandList* lists[]{list.Get()};
    queue12->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    require_hresult(device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())), "fence");
    require_hresult(queue12->Signal(fence.Get(), 1), "Signal");
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    require_hresult(fence->SetEventOnCompletion(1, done), "SetEventOnCompletion");
    require(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, "the D3D12 queue did not finish");
    CloseHandle(done);
    std::uint32_t* mapped = nullptr;
    require_hresult(readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map");
    const std::uint32_t eye0 = mapped[0];
    const std::uint32_t eye1 = mapped[256U * kRenderHeight / 4U];
    const std::uint32_t depth_bits = mapped[2U * 256U * kRenderHeight / 4U];
    readback->Unmap(0, nullptr);
    constexpr std::uint32_t kLeftVector = 0x3E00U | (0xC000U << 16U);
    constexpr std::uint32_t kRightVector = 0x4200U | (0x3800U << 16U);
    require((eye0 == kLeftVector && eye1 == kRightVector) || (eye0 == kRightVector && eye1 == kLeftVector),
        "the D3D12 snapshots do not hold the vectors the game cleared in Vulkan");
    require(depth_bits == 0x3F000000U, "the D3D12 depth snapshot does not hold the game's Vulkan depth");

    // Copies whose command buffer the game never submits: never published,
    // and dropped when their slot comes round again (four slots a feature).
    // Tracked only: untracked, nothing can tell.
    if (tracked) {
    require(begin(buffers[1], &begin_info) == VK_SUCCESS, "vkBeginCommandBuffer (unsubmitted)");
    for (int round = 0; round < 5; ++round) evaluate_eye(buffers[1], left, &left_resource);
    require(end(buffers[1]) == VK_SUCCESS, "vkEndCommandBuffer (unsubmitted)");
    xrfg::flush_ngx_guide_capture_vulkan(true);
    statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.vulkan_publications == 2 && statistics.vulkan_dropped == 1,
        "unsubmitted Vulkan copies were published, or not dropped when their slot came round");
    }

    require(NVSDK_NGX_SUCCEED(release(left)) && NVSDK_NGX_SUCCEED(release(right)),
        "Vulkan release failed through the hook");
    xrfg::stop_ngx_guide_capture_vulkan(queue12.Get());
    std::cout << "ngx guide capture Vulkan" << (tracked ? "" : " untracked")
              << ": publications=" << statistics.vulkan_publications
              << " eye0=0x" << std::hex << eye0 << " eye1=0x" << eye1 << " depth=0x" << depth_bits
              << std::dec << "\n";
    return 0;
}

int run_d3d11(HMODULE module) {
    const auto create = reinterpret_cast<CreateFeature11>(
        GetProcAddress(module, "NVSDK_NGX_D3D11_CreateFeature"));
    const auto evaluate = reinterpret_cast<EvaluateFeature11>(
        GetProcAddress(module, "NVSDK_NGX_D3D11_EvaluateFeature"));
    const auto release = reinterpret_cast<ReleaseFeature>(
        GetProcAddress(module, "NVSDK_NGX_D3D11_ReleaseFeature"));
    const auto d3d11_evaluations = reinterpret_cast<Counter>(
        GetProcAddress(module, "fake_ngx_d3d11_evaluations"));
    require(create && evaluate && release && d3d11_evaluations, "the stand-in lacks a D3D11 export");

    // The game's device, and the layer's D3D12 device on its adapter, as the
    // bridge makes them.
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context11;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1};
    require_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, device11.GetAddressOf(), nullptr, context11.GetAddressOf()),
        "D3D11CreateDevice");
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue12;
    require_hresult(xrfg::create_d3d12_device_for_d3d11(device11.Get(),
        device12.GetAddressOf(), queue12.GetAddressOf()), "create_d3d12_device_for_d3d11");

    xrfg::configure_ngx_guide_capture_d3d11(device11.Get(), device12.Get(), queue12.Get(), true);
    xrfg::poll_ngx_guide_capture();
    auto statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.d3d11_active && statistics.d3d11_modules_hooked == 1,
        "the stand-in's D3D11 exports were not detoured");

    constexpr UINT kRenderWidth = 48, kRenderHeight = 32;
    constexpr UINT kOutputWidth = 96, kOutputHeight = 64;
    Parameters creation;
    creation.Set(NVSDK_NGX_Parameter_Width, kRenderWidth);
    creation.Set(NVSDK_NGX_Parameter_Height, kRenderHeight);
    creation.Set(NVSDK_NGX_Parameter_OutWidth, kOutputWidth);
    creation.Set(NVSDK_NGX_Parameter_OutHeight, kOutputHeight);
    creation.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
        static_cast<int>(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
            NVSDK_NGX_DLSS_Feature_Flags_MVJittered | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted));
    const auto created_before = statistics.features_created;
    NVSDK_NGX_Handle* left = nullptr;
    NVSDK_NGX_Handle* right = nullptr;
    require(NVSDK_NGX_SUCCEED(create(context11.Get(), NVSDK_NGX_Feature_SuperSampling, &creation, &left)) &&
            left && NVSDK_NGX_SUCCEED(create(context11.Get(), NVSDK_NGX_Feature_SuperSampling, &creation, &right)) &&
            right,
        "D3D11 feature creation failed through the hook");
    require(xrfg::ngx_guide_capture_statistics().features_created == created_before + 2,
        "the two D3D11 features were not recorded");

    const auto texture = [&](UINT width, UINT height, DXGI_FORMAT format, UINT bind, std::uint32_t fill) {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = format;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = bind;
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height, fill);
        D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4U, 0};
        ComPtr<ID3D11Texture2D> result;
        require_hresult(device11->CreateTexture2D(&description, &data, result.GetAddressOf()),
            "CreateTexture2D");
        return result;
    };
    // R16G16_FLOAT: (1.5, -2.0) for the left eye, (3.0, 0.5) for the right.
    constexpr std::uint32_t kLeftVector = 0x3E00U | (0xC000U << 16U);
    constexpr std::uint32_t kRightVector = 0x4200U | (0x3800U << 16U);
    const auto output = texture(kOutputWidth, kOutputHeight, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0);
    const auto motion_left = texture(kRenderWidth, kRenderHeight, DXGI_FORMAT_R16G16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE, kLeftVector);
    const auto motion_right = texture(kRenderWidth, kRenderHeight, DXGI_FORMAT_R16G16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE, kRightVector);
    const auto depth = texture(kRenderWidth, kRenderHeight, DXGI_FORMAT_R32_FLOAT,
        D3D11_BIND_SHADER_RESOURCE, 0x3F000000U);

    const auto vectors_before = xrfg::dlss_motion_vector_statistics();
    const auto evaluate_frame = [&](NVSDK_NGX_Handle* handle, ID3D11Texture2D* vectors, float jitter) {
        Parameters frame;
        frame.Set(NVSDK_NGX_Parameter_Output, static_cast<ID3D11Resource*>(output.Get()));
        frame.Set(NVSDK_NGX_Parameter_MotionVectors, static_cast<ID3D11Resource*>(vectors));
        frame.Set(NVSDK_NGX_Parameter_Depth, static_cast<ID3D11Resource*>(depth.Get()));
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, kRenderWidth);
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, kRenderHeight);
        frame.Set(NVSDK_NGX_Parameter_Jitter_Offset_X, jitter);
        frame.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, -jitter);
        frame.Set(NVSDK_NGX_Parameter_Reset, 0);
        require(NVSDK_NGX_SUCCEED(evaluate(context11.Get(), handle, &frame, nullptr)),
            "D3D11 evaluation failed through the hook");
    };
    // Each frame's snapshot copies go to the queue at the bridged session's
    // image release, as the layer flushes them.
    for (int frame = 0; frame < 3; ++frame) {
        evaluate_frame(left, motion_left.Get(), frame % 2 ? 0.25F : -0.25F);
        evaluate_frame(right, motion_right.Get(), frame % 2 ? 0.25F : -0.25F);
        context11->Flush();
        xrfg::flush_ngx_guide_capture_d3d11();
    }
    require(d3d11_evaluations() == 6, "the original D3D11 EvaluateFeature did not run six times");
    statistics = xrfg::ngx_guide_capture_statistics();
    const auto vectors_after = xrfg::dlss_motion_vector_statistics();
    require(statistics.d3d11_publications == 6 && statistics.d3d11_transport_failures == 0,
        "the D3D11 evaluations were not all published");
    require(vectors_after.published - vectors_before.published == 6 &&
            vectors_after.depth_publications - vectors_before.depth_publications == 6,
        "the registry did not take every D3D11 publication with its depth");

    // The side-by-side image UEVR submits: both eyes' outputs in one.
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC image_description{};
    image_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    image_description.Width = kOutputWidth * 2U;
    image_description.Height = kOutputHeight;
    image_description.DepthOrArraySize = 1;
    image_description.MipLevels = 1;
    image_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    image_description.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> image;
    require_hresult(device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &image_description,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(image.GetAddressOf())), "eye image");
    const auto set = xrfg::resolve_dlss_motion_vectors(image.Get(), queue12.Get());
    require(set != nullptr && set->eye_count == 2, "no side-by-side guide set resolved from D3D11");

    // Read each eye's snapshot back on the D3D12 queue.
    ComPtr<ID3D12CommandAllocator> allocator;
    require_hresult(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(allocator.GetAddressOf())), "allocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    require_hresult(device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
        nullptr, IID_PPV_ARGS(list.GetAddressOf())), "list");
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer_description{};
    buffer_description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_description.Width = 2U * 256U * kRenderHeight;
    buffer_description.Height = 1;
    buffer_description.DepthOrArraySize = 1;
    buffer_description.MipLevels = 1;
    buffer_description.SampleDesc.Count = 1;
    buffer_description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    require_hresult(device12->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE,
        &buffer_description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(readback.GetAddressOf())), "readback");
    for (unsigned eye = 0; eye < 2; ++eye) {
        const auto& frame = set->eyes[eye];
        require(frame && frame->motion_vectors && frame->depth, "an eye lacks its vectors or depth");
        require(frame->producer_queue.Get() == queue12.Get(), "the D3D11 frame is not on the layer's queue");
        require(frame->motion_width == kRenderWidth && frame->depth_width == kRenderWidth &&
                frame->depth_inverted,
            "the D3D11 frame's rectangles or flags were not carried");
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = frame->motion_vectors.Get();
        barrier.Transition.StateBefore = frame->resource_state;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = frame->motion_vectors.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint.Offset = eye * 256ULL * kRenderHeight;
        destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16_FLOAT, kRenderWidth, kRenderHeight, 1, 256};
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
    }
    require_hresult(list->Close(), "Close");
    ID3D12CommandList* lists[]{list.Get()};
    queue12->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    require_hresult(device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())), "fence");
    require_hresult(queue12->Signal(fence.Get(), 1), "Signal");
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    require_hresult(fence->SetEventOnCompletion(1, done), "SetEventOnCompletion");
    require(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, "the D3D12 queue did not finish");
    CloseHandle(done);
    std::uint32_t* mapped = nullptr;
    require_hresult(readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map");
    const std::uint32_t eye0 = mapped[0];
    const std::uint32_t eye1 = mapped[256U * kRenderHeight / 4U];
    readback->Unmap(0, nullptr);
    require((eye0 == kLeftVector && eye1 == kRightVector) || (eye0 == kRightVector && eye1 == kLeftVector),
        "the D3D12 snapshots do not hold the vectors the game wrote in D3D11");

    const auto released_before = xrfg::ngx_guide_capture_statistics().features_released;
    require(NVSDK_NGX_SUCCEED(release(left)) && NVSDK_NGX_SUCCEED(release(right)),
        "D3D11 release failed through the hook");
    require(xrfg::ngx_guide_capture_statistics().features_released == released_before + 2,
        "the two D3D11 features were not released");
    xrfg::stop_ngx_guide_capture_d3d11(queue12.Get());
    std::cout << "ngx guide capture D3D11: publications=" << statistics.d3d11_publications
              << " eye0=0x" << std::hex << eye0 << " eye1=0x" << eye1 << std::dec << "\n";
    // V514: "untracked" in place of the queue layer's path runs the Vulkan
    // phase without it in the chain.
    if (g_queue_layer_path == nullptr) return 0;
    return run_vulkan(module,
        std::wstring_view(g_queue_layer_path) == L"untracked" ? nullptr : g_queue_layer_path);
}

// V513: No Man's Sky's one evaluation for both eyes, split per eye.
void test_split_both_eyes_guide() {
    xrfg::DlssMotionVectorFrame frame{};
    frame.output_width = 96;
    frame.output_height = 64;
    frame.motion_x = 2;
    frame.motion_width = 48;
    frame.motion_height = 32;
    frame.depth_width = 48;
    frame.depth_height = 32;
    frame.publication = 7;
    const auto left = xrfg::split_both_eyes_guide(frame, 48, 64, 0);
    const auto right = xrfg::split_both_eyes_guide(frame, 48, 64, 1);
    require(left && right, "a both-eyes evaluation was not split");
    require(left->output_x == 0 && left->output_width == 48 && left->motion_x == 2 &&
            left->motion_width == 24 && right->output_x == 0 && right->output_width == 48 &&
            right->motion_x == 26 && right->motion_width == 24 && right->publication == 7,
        "the halves are not the evaluation's left and right");
    require(!xrfg::split_both_eyes_guide(frame, 40, 64, 0) &&
            !xrfg::split_both_eyes_guide(frame, 48, 60, 1),
        "an evaluation that does not span two views was split");
    std::cout << "both-eyes guide split: right motion_x=" << right->motion_x << "\n";
}

// V515: Streamline's per-viewport cameras, as MSFS 2024's foveated rendering
// gives them - four views a frame, per eye a periphery over a field a little
// wider than the eye's and an inset, every DLSS output a third of the eye
// image's size. The stand-in sl.interposer.dll's slEvaluateFeature makes the
// NGX evaluation inside itself, as Streamline's DLSS plugin does. Checked:
// both exports are detoured; every evaluation made inside carries its
// viewport's camera, read from a row-vector (D3D, left-handed) projection
// and from a column-vector right-handed one; the resolver gives each eye its
// periphery - by fit for asymmetric eyes, by the cameras' positions when
// both fit alike - placed scaled over the eye image; the insets alone cover
// no eye.
namespace sl_test {

struct TypeId {
    std::uint32_t data1;
    std::uint16_t data2;
    std::uint16_t data3;
    std::array<std::uint8_t, 8> data4;
};

struct Header {
    const Header* next;
    TypeId type;
    std::size_t version;
};

struct Constants {
    Header header;
    float view_to_clip[4][4];
    float other_matrices[4][4][4];
    float jitter_offset[2];
    float motion_vector_scale[2];
    float pinhole_offset[2];
    float position[3];
    float up[3];
    float right[3];
    float forward[3];
    float near_far_fov_aspect_invalid[5];
    char flags[7];
};
static_assert(offsetof(Constants, view_to_clip) == 32 && offsetof(Constants, position) == 376 &&
    offsetof(Constants, right) == 400);

struct Viewport {
    Header header;
    std::uint32_t value;
};

constexpr TypeId kConstants{
    0xdcd35ad7, 0x4e4a, 0x4bad, {0xa9, 0x0c, 0xe0, 0xc4, 0x9e, 0xb2, 0x3a, 0xfe}};
constexpr TypeId kViewport{
    0x171b6435, 0x9b3c, 0x4fc8, {0x99, 0x94, 0xfb, 0xe5, 0x25, 0x69, 0xaa, 0xa4}};

struct Field {
    float left, right, up, down;
};

// A projection for the field: row vectors with w = +z (left-handed D3D), or
// column vectors with w = -z (right-handed).
Constants constants_for(const Field& f, float camera_x, bool column_right_handed) {
    Constants c{};
    c.header.type = kConstants;
    c.header.version = 2;
    auto& m = c.view_to_clip;
    m[0][0] = 2.0F / (f.right - f.left);
    m[1][1] = 2.0F / (f.up - f.down);
    const float off_x = (f.right + f.left) / (f.right - f.left);
    const float off_y = (f.up + f.down) / (f.up - f.down);
    if (column_right_handed) {
        // NDC = (t m00 - m02) / 1 for z = -depth.
        m[0][2] = off_x;
        m[1][2] = off_y;
        m[3][2] = -1.0F;
        m[2][3] = 0.05F;
    } else {
        m[2][0] = -off_x;
        m[2][1] = -off_y;
        m[2][3] = 1.0F;
        m[3][2] = 0.05F;
    }
    c.position[0] = camera_x;
    c.right[0] = 1.0F;
    c.up[1] = 1.0F;
    c.forward[2] = 1.0F;
    return c;
}

struct Evaluation {
    EvaluateFeature evaluate{};
    ID3D12GraphicsCommandList* list{};
    NVSDK_NGX_Handle* handle{};
    ID3D12Resource* output{};
    ID3D12Resource* motion{};
    UINT render_width{}, render_height{};
    bool ok{};
};

void evaluate_inside(void* context) {
    auto* e = static_cast<Evaluation*>(context);
    Parameters frame;
    frame.Set(NVSDK_NGX_Parameter_Output, e->output);
    frame.Set(NVSDK_NGX_Parameter_MotionVectors, e->motion);
    frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, e->render_width);
    frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, e->render_height);
    frame.Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0F);
    frame.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0F);
    frame.Set(NVSDK_NGX_Parameter_Reset, 0);
    e->ok = NVSDK_NGX_SUCCEED(e->evaluate(e->list, e->handle, &frame, nullptr));
}

}  // namespace sl_test

template <class Execute>
void run_streamline_views(const wchar_t* ngx_path, CreateFeature create, EvaluateFeature evaluate,
    ReleaseFeature release, ID3D12Device* device, ID3D12CommandQueue* queue,
    ID3D12GraphicsCommandList* list, Execute&& execute) {
    using namespace sl_test;
    std::wstring sl_path(ngx_path);
    sl_path = sl_path.substr(0, sl_path.find_last_of(L"\\/") + 1) + L"sl.interposer.dll";
    const HMODULE sl = LoadLibraryW(sl_path.c_str());
    require(sl != nullptr && GetModuleHandleW(L"sl.interposer.dll") == sl,
        "the stand-in sl.interposer.dll did not load");
    using SetConstants = int (*)(const void*, const void*, const void*);
    using EvaluateSl = int (*)(std::uint32_t, const void*, const void* const*, std::uint32_t, void*);
    using SetCallback = void (*)(void (*)(void*), void*);
    const auto set_constants = reinterpret_cast<SetConstants>(GetProcAddress(sl, "slSetConstants"));
    const auto evaluate_sl = reinterpret_cast<EvaluateSl>(GetProcAddress(sl, "slEvaluateFeature"));
    const auto set_callback =
        reinterpret_cast<SetCallback>(GetProcAddress(sl, "fake_sl_set_evaluate_callback"));
    const auto sl_evaluations = reinterpret_cast<Counter>(GetProcAddress(sl, "fake_sl_evaluations"));
    require(set_constants && evaluate_sl && set_callback && sl_evaluations,
        "the stand-in sl.interposer.dll lacks an export");
    xrfg::poll_ngx_guide_capture();
    require(xrfg::ngx_guide_capture_statistics().streamline_hooked,
        "Streamline's exports were not detoured");

    constexpr UINT kRender = 16, kRenderHeight = 12, kOutput = 32, kOutputHeight = 24;
    constexpr UINT kEyeWidth = 96, kEyeHeight = 72;
    Parameters creation;
    creation.Set(NVSDK_NGX_Parameter_Width, kRender);
    creation.Set(NVSDK_NGX_Parameter_Height, kRenderHeight);
    creation.Set(NVSDK_NGX_Parameter_OutWidth, kOutput);
    creation.Set(NVSDK_NGX_Parameter_OutHeight, kOutputHeight);
    creation.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
        static_cast<int>(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes));
    // Viewports 0 and 1 the left and right peripheries, 2 and 3 the insets.
    const std::array<Field, 4> fields{{{-1.25F, 1.05F, 1.15F, -1.15F},
        {-1.05F, 1.25F, 1.15F, -1.15F}, {-0.35F, 0.35F, 0.35F, -0.35F},
        {-0.35F, 0.35F, 0.35F, -0.35F}}};
    const std::array<float, 4> camera_x{-0.032F, 0.032F, -0.032F, 0.032F};
    std::array<NVSDK_NGX_Handle*, 4> handles{};
    std::array<ComPtr<ID3D12Resource>, 4> outputs, motions;
    std::array<Constants, 4> constants{};
    std::array<Viewport, 4> viewports{};
    for (std::uint32_t v = 0; v < 4; ++v) {
        require(NVSDK_NGX_SUCCEED(create(list, NVSDK_NGX_Feature_SuperSampling, &creation, &handles[v])) &&
                handles[v], "a view's feature creation failed through the hook");
        outputs[v] = create_texture(device, kOutput, kOutputHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT,
            D3D12_RESOURCE_STATE_COMMON);
        motions[v] = create_texture(device, kRender, kRenderHeight, 1, DXGI_FORMAT_R16G16_FLOAT,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        constants[v] = constants_for(fields[v], camera_x[v], v == 1);
        viewports[v].header.type = kViewport;
        viewports[v].header.version = 1;
        viewports[v].value = 40U + v;
    }
    const auto publications_before = xrfg::ngx_guide_capture_statistics().publications_with_camera;
    int frame_token = 0;
    for (UINT64 frame = 1; frame <= 2; ++frame) {
        for (std::uint32_t v = 0; v < 4; ++v) {
            require(set_constants(&constants[v], &frame_token, &viewports[v]) == 0,
                "slSetConstants failed through the hook");
        }
        for (std::uint32_t v = 0; v < 4; ++v) {
            Evaluation evaluation{evaluate, list, handles[v], outputs[v].Get(), motions[v].Get(),
                kRender, kRenderHeight};
            set_callback(&evaluate_inside, &evaluation);
            const void* inputs[]{&viewports[v]};
            require(evaluate_sl(0, &frame_token, inputs, 1, nullptr) == 0 && evaluation.ok,
                "slEvaluateFeature or the NGX evaluation inside it failed");
        }
        set_callback(nullptr, nullptr);
        execute(3 + frame);
    }
    require(sl_evaluations() == 8, "the original slEvaluateFeature did not run eight times");
    require(xrfg::ngx_guide_capture_statistics().publications_with_camera == publications_before + 8,
        "the evaluations inside Streamline's did not carry their cameras");

    const std::uint64_t watermark = xrfg::dlss_motion_vector_publications();
    const auto resolve = [&](const xrfg::DlssEyeView& left_eye, const xrfg::DlssEyeView& right_eye) {
        return xrfg::resolve_view_placed_dlss_guides(queue, {left_eye, right_eye}, {watermark, watermark});
    };
    // Asymmetric eyes: each periphery fits its own eye best.
    const xrfg::DlssEyeView left_eye{-1.2F, 1.0F, 1.1F, -1.1F, 0, 0, kEyeWidth, kEyeHeight};
    const xrfg::DlssEyeView right_eye{-1.0F, 1.2F, 1.1F, -1.1F, 0, 0, kEyeWidth, kEyeHeight};
    auto placed = resolve(left_eye, right_eye);
    require(placed[0] && placed[1], "no view-placed guides for the two eyes");
    for (std::uint32_t eye = 0; eye < 2; ++eye) {
        const auto& f = *placed[eye]->eyes[0];
        require(f.view.known && f.view.viewport == 40U + eye,
            "an eye did not take its own periphery");
        require(f.placed && f.output_x == 0 && f.output_y == 0 && f.output_width == kEyeWidth &&
                f.output_height == kEyeHeight && f.serial == 2 && f.previous_serial == 1,
            "the periphery was not placed over the eye image");
        // The field reaches 0.05 beyond the eye's 2.2 each side: 2.18
        // pixels of 96 left of the image, 16 texels over 100.36 pixels.
        const float per_x = 16.0F / (96.0F * 2.3F / 2.2F);
        const float per_y = 12.0F / (72.0F * 2.3F / 2.2F);
        require(std::fabs(f.placed_motion_rect[0] - 96.0F * 0.05F / 2.2F * per_x) < 1e-3F &&
                std::fabs(f.placed_motion_rect[2] - 96.0F * per_x) < 1e-3F &&
                std::fabs(f.placed_motion_rect[1] - 72.0F * 0.05F / 2.2F * per_y) < 1e-3F &&
                std::fabs(f.placed_motion_rect[3] - 72.0F * per_y) < 1e-3F,
            "the placed vector rectangle is not the camera's");
    }
    // Symmetric eyes: both peripheries fit alike, and the camera further
    // left is the left eye.
    const xrfg::DlssEyeView symmetric{-1.0F, 1.0F, 1.1F, -1.1F, 0, 0, kEyeWidth, kEyeHeight};
    placed = resolve(symmetric, symmetric);
    require(placed[0] && placed[1] && placed[0]->eyes[0]->view.viewport == 40U &&
            placed[1]->eyes[0]->view.viewport == 41U,
        "the cameras' positions did not decide between two alike peripheries");
    // A field wider than every camera's: no guide.
    const auto unmatched_before = xrfg::dlss_motion_vector_statistics().view_unmatched;
    const xrfg::DlssEyeView wide{-1.5F, 1.5F, 1.1F, -1.1F, 0, 0, kEyeWidth, kEyeHeight};
    placed = resolve(wide, wide);
    require(!placed[0] && !placed[1] &&
            xrfg::dlss_motion_vector_statistics().view_unmatched == unmatched_before + 1,
        "a guide was placed over a field no camera covers");
    for (auto* handle : handles) {
        require(NVSDK_NGX_SUCCEED(release(handle)), "a view's feature release failed");
    }
    std::cout << "streamline views: placed x=" << 96.0F * 0.05F / 2.2F << " px outside, "
              << "publications with a camera=" << 8 << "\n";
}

int run(const wchar_t* module_path) {
    test_split_both_eyes_guide();
    // WARP: the capture only records copies, which WARP executes.
    ComPtr<IDXGIFactory4> factory;
    require_hresult(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf())), "CreateDXGIFactory2");
    ComPtr<IDXGIAdapter> warp;
    require_hresult(factory->EnumWarpAdapter(IID_PPV_ARGS(warp.GetAddressOf())), "EnumWarpAdapter");
    ComPtr<ID3D12Device> device;
    require_hresult(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(device.GetAddressOf())), "D3D12CreateDevice(WARP)");
    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    require_hresult(device->CreateCommandQueue(&queue_description, IID_PPV_ARGS(queue.GetAddressOf())),
        "CreateCommandQueue");
    ComPtr<ID3D12CommandAllocator> allocator;
    require_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(allocator.GetAddressOf())), "CreateCommandAllocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    require_hresult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
        nullptr, IID_PPV_ARGS(list.GetAddressOf())), "CreateCommandList");
    ComPtr<ID3D12Fence> fence;
    require_hresult(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())),
        "CreateFence");

    // Before the module is in the process: nothing to hook, no failure.
    xrfg::poll_ngx_guide_capture();
    require(xrfg::ngx_guide_capture_statistics().modules_hooked == 0,
        "hooked something before the module was loaded");

    const HMODULE module = LoadLibraryW(module_path);
    require(module != nullptr, "the stand-in nvngx_dlss.dll did not load");
    require(GetModuleHandleW(L"nvngx_dlss.dll") == module,
        "the stand-in is not the process's nvngx_dlss.dll");
    const auto create = reinterpret_cast<CreateFeature>(GetProcAddress(module, "NVSDK_NGX_D3D12_CreateFeature"));
    const auto evaluate = reinterpret_cast<EvaluateFeature>(GetProcAddress(module, "NVSDK_NGX_D3D12_EvaluateFeature"));
    const auto evaluate_c = reinterpret_cast<EvaluateFeatureC>(GetProcAddress(module, "NVSDK_NGX_D3D12_EvaluateFeature_C"));
    const auto release = reinterpret_cast<ReleaseFeature>(GetProcAddress(module, "NVSDK_NGX_D3D12_ReleaseFeature"));
    const auto creations = reinterpret_cast<Counter>(GetProcAddress(module, "fake_ngx_creations"));
    const auto evaluations = reinterpret_cast<Counter>(GetProcAddress(module, "fake_ngx_evaluations"));
    const auto c_evaluations = reinterpret_cast<Counter>(GetProcAddress(module, "fake_ngx_c_evaluations"));
    const auto releases = reinterpret_cast<Counter>(GetProcAddress(module, "fake_ngx_releases"));
    require(create && evaluate && evaluate_c && release && creations && evaluations && c_evaluations && releases,
        "the stand-in lacks an export");

    // Enabling starts capture and the monitor; the poll finds the module now.
    xrfg::configure_ngx_guide_capture(queue.Get(), true);
    xrfg::poll_ngx_guide_capture();
    auto statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.active, "capture did not become active");
    // Its D3D12 exports, since V512 its D3D11 ones and since V513 its Vulkan ones.
    require(statistics.modules_hooked == 3 && statistics.d3d11_modules_hooked == 1 &&
            statistics.vulkan_modules_hooked == 1 &&
            statistics.modules_failed == 0,
        "the stand-in's exports were not detoured");

    // A game's stereo DLSS: one feature per eye, low-resolution jittered
    // vectors, rendering 48x32 into a 96x64 output (an array of two eyes).
    constexpr UINT kRenderWidth = 48, kRenderHeight = 32;
    constexpr UINT kOutputWidth = 96, kOutputHeight = 64;
    Parameters creation;
    creation.Set(NVSDK_NGX_Parameter_Width, kRenderWidth);
    creation.Set(NVSDK_NGX_Parameter_Height, kRenderHeight);
    creation.Set(NVSDK_NGX_Parameter_OutWidth, kOutputWidth);
    creation.Set(NVSDK_NGX_Parameter_OutHeight, kOutputHeight);
    creation.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
        static_cast<int>(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_MVJittered));
    NVSDK_NGX_Handle* left = nullptr;
    NVSDK_NGX_Handle* right = nullptr;
    require(NVSDK_NGX_SUCCEED(create(list.Get(), NVSDK_NGX_Feature_SuperSampling, &creation, &left)) && left,
        "left feature creation failed through the hook");
    require(NVSDK_NGX_SUCCEED(create(list.Get(), NVSDK_NGX_Feature_SuperSampling, &creation, &right)) && right,
        "right feature creation failed through the hook");
    // A feature of another kind is not a guide.
    NVSDK_NGX_Handle* other = nullptr;
    require(NVSDK_NGX_SUCCEED(create(list.Get(), NVSDK_NGX_Feature_FrameGeneration, &creation, &other)) && other,
        "frame-generation feature creation failed through the hook");
    require(creations() == 3, "the original CreateFeature did not run three times");
    statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.features_created == 2, "two guide features were not recorded");

    const auto output = create_texture(device.Get(), kOutputWidth, kOutputHeight, 2,
        DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    const auto motion = create_texture(device.Get(), kRenderWidth, kRenderHeight, 1,
        DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const auto motion_right = create_texture(device.Get(), kRenderWidth, kRenderHeight, 1,
        DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    const auto evaluate_frame = [&](NVSDK_NGX_Handle* handle, ID3D12Resource* vectors, float jitter, bool c_entry) {
        Parameters frame;
        frame.Set(NVSDK_NGX_Parameter_Output, output.Get());
        frame.Set(NVSDK_NGX_Parameter_MotionVectors, vectors);
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, kRenderWidth);
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, kRenderHeight);
        frame.Set(NVSDK_NGX_Parameter_MV_Scale_X, -1.0F);
        frame.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0F);
        frame.Set(NVSDK_NGX_Parameter_Jitter_Offset_X, jitter);
        frame.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, -jitter);
        frame.Set(NVSDK_NGX_Parameter_Reset, 0);
        const NVSDK_NGX_Result result = c_entry
            ? evaluate_c(list.Get(), handle, &frame, nullptr)
            : evaluate(list.Get(), handle, &frame, nullptr);
        require(NVSDK_NGX_SUCCEED(result), "evaluation failed through the hook");
    };
    const auto execute = [&](UINT64 value) {
        require_hresult(list->Close(), "Close");
        ID3D12CommandList* lists[]{list.Get()};
        queue->ExecuteCommandLists(1, lists);
        require_hresult(queue->Signal(fence.Get(), value), "Signal");
        HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        require(done != nullptr, "CreateEvent");
        require_hresult(fence->SetEventOnCompletion(value, done), "SetEventOnCompletion");
        require(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, "the queue did not finish");
        CloseHandle(done);
        require_hresult(allocator->Reset(), "allocator Reset");
        require_hresult(list->Reset(allocator.Get(), nullptr), "list Reset");
    };

    // Frame 1: both eyes, the plain entry point.
    evaluate_frame(left, motion.Get(), 0.25F, false);
    evaluate_frame(right, motion_right.Get(), 0.25F, false);
    execute(1);
    require(evaluations() == 2, "the original EvaluateFeature did not run twice");
    statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.evaluations == 2 && statistics.publications == 2,
        "the two evaluations were not both published");
    const auto vectors = xrfg::dlss_motion_vector_statistics();
    require(vectors.published == 2 && vectors.snapshot_copies == 2 && vectors.snapshot_failures == 0,
        "the registry did not snapshot both publications");

    // Frame 2: the C entry point, which the detour also covers.
    evaluate_frame(left, motion.Get(), -0.25F, true);
    evaluate_frame(right, motion_right.Get(), -0.25F, true);
    execute(2);
    require(c_evaluations() == 2, "the original EvaluateFeature_C did not run twice");
    statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.publications == 4, "the C evaluations were not published");

    // The resolver pairs the two streams into the eyes, with the serials of
    // frame 2 following frame 1, and the parameters' values.
    const auto set = xrfg::resolve_dlss_motion_vectors(output.Get(), queue.Get());
    require(set != nullptr && set->eye_count == 2, "no stereo guide set resolved");
    for (unsigned eye = 0; eye < 2; ++eye) {
        const auto& frame = set->eyes[eye];
        require(frame != nullptr, "an eye has no frame");
        require(frame->serial == 2 && frame->previous_serial == 1, "the serials do not pair frame 2 with frame 1");
        require(frame->motion_width == kRenderWidth && frame->motion_height == kRenderHeight,
            "low-resolution vectors did not take the render size");
        require(frame->output_width == kOutputWidth && frame->output_height == kOutputHeight,
            "the output rectangle is not the creation's display size");
        require(frame->scale_x == -1.0F && frame->scale_y == 1.0F, "the vector scale was not carried");
        require(frame->jittered && std::fabs(frame->jitter_x - -0.25F) < 1e-6F &&
                std::fabs(frame->previous_jitter_x - 0.25F) < 1e-6F,
            "the jitter and its previous value were not carried");
        require(frame->motion_vectors != nullptr && frame->motion_vectors.Get() != motion.Get() &&
                frame->motion_vectors.Get() != motion_right.Get(),
            "the frame holds the game's vectors rather than a snapshot");
        require(frame->producer_queue.Get() == queue.Get(), "the producer queue is not the session's");
    }
    require(set->eyes[0]->stream != set->eyes[1]->stream, "the eyes share a stream");

    run_streamline_views(module_path, create, evaluate, release, device.Get(), queue.Get(),
        list.Get(), execute);

    // Paused: evaluations run, nothing is published.
    const auto evaluations_before_pause = evaluations();
    const auto publications_before_pause = xrfg::ngx_guide_capture_statistics().publications;
    xrfg::stop_ngx_guide_capture(queue.Get());
    evaluate_frame(left, motion.Get(), 0.0F, false);
    execute(10);
    require(evaluations() == evaluations_before_pause + 1, "the original did not run while paused");
    require(xrfg::ngx_guide_capture_statistics().publications == publications_before_pause,
        "a paused capture published");

    // Release retires the features; the original runs.
    const auto releases_before = releases();
    const auto released_before = xrfg::ngx_guide_capture_statistics().features_released;
    require(NVSDK_NGX_SUCCEED(release(left)) && NVSDK_NGX_SUCCEED(release(right)) &&
            NVSDK_NGX_SUCCEED(release(other)),
        "release failed through the hook");
    require(releases() == releases_before + 3, "the original ReleaseFeature did not run three times");
    require(xrfg::ngx_guide_capture_statistics().features_released == released_before + 2,
        "the two guide features were not released");
    std::cout << "ngx guide capture: hooked=" << statistics.modules_hooked
              << " publications=" << statistics.publications << "\n";
    return run_d3d11(module);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::cerr << "usage: xrfg_ngx_guide_capture_tests <path to stand-in nvngx_dlss.dll>\n";
        return 2;
    }
    try {
        // V513: with the Vulkan queue layer's path, the Vulkan phase runs too.
        if (argc > 2) g_queue_layer_path = argv[2];
        return run(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "ngx guide capture test failed: " << error.what() << "\n";
        return 1;
    }
}
