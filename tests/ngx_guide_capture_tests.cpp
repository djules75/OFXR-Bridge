// The NGX guide capture against a stand-in nvngx_dlss.dll (fake_nvngx_dlss)
// on WARP: the module is found and detoured, the originals still run, a
// feature per eye becomes a stream each, every evaluation publishes a
// snapshot of the motion vectors recorded into the game's list, and the
// resolver pairs the two streams into a stereo guide set whose fields are
// the parameters' - rectangles, scale, jitter, the low-resolution vector
// size from the creation flags.
#include "xrfg/bridge_flight_logger.hpp"
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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

// V524: a game that renders in D3D12 behind a bridged D3D11 session, as
// R.E.A.L. VR runs Cyberpunk 2077. Only the D3D11 session's capture is
// running; the game's D3D12 evaluations copy their vectors and depth into
// textures shared with the layer's D3D12 device, in the game's own list, and
// the snapshot copies go to the layer's queue at the release's flush. Checked:
// every evaluation publishes with its depth, the side-by-side image resolves
// both eyes on the layer's queue, and the snapshots hold the game's vectors.
void run_bridged_d3d12(HMODULE module, ID3D12Device* layer_device, ID3D12CommandQueue* layer_queue) {
    const auto create = reinterpret_cast<CreateFeature>(
        GetProcAddress(module, "NVSDK_NGX_D3D12_CreateFeature"));
    const auto evaluate = reinterpret_cast<EvaluateFeature>(
        GetProcAddress(module, "NVSDK_NGX_D3D12_EvaluateFeature"));
    const auto release = reinterpret_cast<ReleaseFeature>(
        GetProcAddress(module, "NVSDK_NGX_D3D12_ReleaseFeature"));
    require(create && evaluate && release, "the stand-in lacks a D3D12 export");
    require(!xrfg::ngx_guide_capture_statistics().active &&
            xrfg::ngx_guide_capture_statistics().d3d11_active,
        "the bridged D3D12 phase needs the D3D11 capture alone");

    // The game's device, on the layer's adapter.
    ComPtr<IDXGIFactory4> factory;
    require_hresult(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf())), "CreateDXGIFactory2");
    ComPtr<IDXGIAdapter1> adapter;
    require_hresult(factory->EnumAdapterByLuid(layer_device->GetAdapterLuid(),
        IID_PPV_ARGS(adapter.GetAddressOf())), "EnumAdapterByLuid");
    ComPtr<ID3D12Device> game;
    require_hresult(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(game.GetAddressOf())), "D3D12CreateDevice(game)");
    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> game_queue;
    require_hresult(game->CreateCommandQueue(&queue_description, IID_PPV_ARGS(game_queue.GetAddressOf())),
        "game queue");
    ComPtr<ID3D12CommandAllocator> game_allocator;
    require_hresult(game->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(game_allocator.GetAddressOf())), "game allocator");
    ComPtr<ID3D12GraphicsCommandList> game_list;
    require_hresult(game->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, game_allocator.Get(),
        nullptr, IID_PPV_ARGS(game_list.GetAddressOf())), "game list");
    ComPtr<ID3D12Fence> game_fence;
    require_hresult(game->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(game_fence.GetAddressOf())),
        "game fence");
    UINT64 game_value = 0;
    const auto run_game = [&] {
        require_hresult(game_list->Close(), "game Close");
        ID3D12CommandList* lists[]{game_list.Get()};
        game_queue->ExecuteCommandLists(1, lists);
        require_hresult(game_queue->Signal(game_fence.Get(), ++game_value), "game Signal");
        HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        require_hresult(game_fence->SetEventOnCompletion(game_value, done), "game SetEventOnCompletion");
        require(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, "the game's queue did not finish");
        CloseHandle(done);
        require_hresult(game_allocator->Reset(), "game allocator Reset");
        require_hresult(game_list->Reset(game_allocator.Get(), nullptr), "game list Reset");
    };

    constexpr UINT kRenderWidth = 48, kRenderHeight = 32;
    constexpr UINT kOutputWidth = 96, kOutputHeight = 64;
    constexpr UINT kPitch = 256;
    // R16G16_FLOAT: (-1.0, 2.0) for the left eye, (0.5, -0.5) for the right;
    // the depth 0.75.
    constexpr std::uint32_t kLeftVector = 0xBC00U | (0x4000U << 16U);
    constexpr std::uint32_t kRightVector = 0x3800U | (0xB800U << 16U);
    constexpr std::uint32_t kDepth = 0x3F400000U;
    const auto output = create_texture(game.Get(), kOutputWidth, kOutputHeight, 1,
        DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    const auto motion_left = create_texture(game.Get(), kRenderWidth, kRenderHeight, 1,
        DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_STATE_COPY_DEST);
    const auto motion_right = create_texture(game.Get(), kRenderWidth, kRenderHeight, 1,
        DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_STATE_COPY_DEST);
    const auto depth = create_texture(game.Get(), kRenderWidth, kRenderHeight, 1,
        DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_STATE_COPY_DEST);
    {
        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = 3ULL * kPitch * kRenderHeight;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> upload;
        require_hresult(game->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload.GetAddressOf())), "upload");
        std::uint32_t* mapped = nullptr;
        require_hresult(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "upload Map");
        const std::array<std::uint32_t, 3> fills{kLeftVector, kRightVector, kDepth};
        for (std::size_t texture = 0; texture < fills.size(); ++texture) {
            for (UINT row = 0; row < kRenderHeight; ++row) {
                std::uint32_t* line = mapped + (texture * kRenderHeight + row) * (kPitch / 4U);
                for (UINT column = 0; column < kRenderWidth; ++column) line[column] = fills[texture];
            }
        }
        upload->Unmap(0, nullptr);
        const std::array<ID3D12Resource*, 3> targets{motion_left.Get(), motion_right.Get(), depth.Get()};
        const std::array<DXGI_FORMAT, 3> formats{DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT,
            DXGI_FORMAT_R32_FLOAT};
        for (std::size_t texture = 0; texture < targets.size(); ++texture) {
            D3D12_TEXTURE_COPY_LOCATION from{};
            from.pResource = upload.Get();
            from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint.Offset = texture * kPitch * kRenderHeight;
            from.PlacedFootprint.Footprint = {formats[texture], kRenderWidth, kRenderHeight, 1, kPitch};
            D3D12_TEXTURE_COPY_LOCATION to{};
            to.pResource = targets[texture];
            to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            game_list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = targets[texture];
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            game_list->ResourceBarrier(1, &barrier);
        }
        run_game();
    }

    Parameters creation;
    creation.Set(NVSDK_NGX_Parameter_Width, kRenderWidth);
    creation.Set(NVSDK_NGX_Parameter_Height, kRenderHeight);
    creation.Set(NVSDK_NGX_Parameter_OutWidth, kOutputWidth);
    creation.Set(NVSDK_NGX_Parameter_OutHeight, kOutputHeight);
    creation.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
        static_cast<int>(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_MVJittered));
    NVSDK_NGX_Handle* left = nullptr;
    NVSDK_NGX_Handle* right = nullptr;
    require(NVSDK_NGX_SUCCEED(create(game_list.Get(), NVSDK_NGX_Feature_SuperSampling, &creation, &left)) &&
            left && NVSDK_NGX_SUCCEED(create(game_list.Get(), NVSDK_NGX_Feature_SuperSampling, &creation, &right)) &&
            right,
        "bridged D3D12 feature creation failed through the hook");

    const auto before = xrfg::ngx_guide_capture_statistics();
    const auto vectors_before = xrfg::dlss_motion_vector_statistics();
    const auto evaluate_frame = [&](NVSDK_NGX_Handle* handle, ID3D12Resource* vectors, float jitter) {
        Parameters frame;
        frame.Set(NVSDK_NGX_Parameter_Output, output.Get());
        frame.Set(NVSDK_NGX_Parameter_MotionVectors, vectors);
        frame.Set(NVSDK_NGX_Parameter_Depth, depth.Get());
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, kRenderWidth);
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, kRenderHeight);
        frame.Set(NVSDK_NGX_Parameter_Jitter_Offset_X, jitter);
        frame.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, -jitter);
        frame.Set(NVSDK_NGX_Parameter_Reset, 0);
        require(NVSDK_NGX_SUCCEED(evaluate(game_list.Get(), handle, &frame, nullptr)),
            "bridged D3D12 evaluation failed through the hook");
    };
    // The game runs its frame's list, then the mod releases the eye image.
    for (int frame = 0; frame < 3; ++frame) {
        evaluate_frame(left, motion_left.Get(), frame % 2 ? 0.25F : -0.25F);
        evaluate_frame(right, motion_right.Get(), frame % 2 ? 0.25F : -0.25F);
        run_game();
        xrfg::flush_ngx_guide_capture_d3d11();
    }
    const auto after = xrfg::ngx_guide_capture_statistics();
    const auto vectors_after = xrfg::dlss_motion_vector_statistics();
    require(after.d3d12_bridged_publications - before.d3d12_bridged_publications == 6 &&
            after.d3d12_bridged_transport_failures == 0 &&
            after.skipped_device_mismatch == before.skipped_device_mismatch,
        "the bridged D3D12 evaluations were not all published");
    require(vectors_after.published - vectors_before.published == 6 &&
            vectors_after.depth_publications - vectors_before.depth_publications == 6,
        "the registry did not take every bridged D3D12 publication with its depth");

    // The side-by-side image, on the layer's device.
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
    require_hresult(layer_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &image_description,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(image.GetAddressOf())), "bridged eye image");
    const auto set = xrfg::resolve_dlss_motion_vectors(image.Get(), layer_queue);
    require(set != nullptr && set->eye_count == 2, "no side-by-side guide set from the bridged D3D12 game");

    ComPtr<ID3D12CommandAllocator> allocator;
    require_hresult(layer_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(allocator.GetAddressOf())), "allocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    require_hresult(layer_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
        nullptr, IID_PPV_ARGS(list.GetAddressOf())), "list");
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer_description{};
    buffer_description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_description.Width = 4ULL * kPitch * kRenderHeight;
    buffer_description.Height = 1;
    buffer_description.DepthOrArraySize = 1;
    buffer_description.MipLevels = 1;
    buffer_description.SampleDesc.Count = 1;
    buffer_description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    require_hresult(layer_device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE,
        &buffer_description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(readback.GetAddressOf())), "readback");
    const auto read = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES state, DXGI_FORMAT format,
                          unsigned slot) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = state;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = resource;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint.Offset = slot * static_cast<UINT64>(kPitch) * kRenderHeight;
        destination.PlacedFootprint.Footprint = {format, kRenderWidth, kRenderHeight, 1, kPitch};
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
    };
    for (unsigned eye = 0; eye < 2; ++eye) {
        const auto& frame = set->eyes[eye];
        require(frame && frame->motion_vectors && frame->depth, "a bridged eye lacks its vectors or depth");
        require(frame->producer_queue.Get() == layer_queue, "the bridged frame is not on the layer's queue");
        require(frame->motion_width == kRenderWidth && frame->output_width == kOutputWidth,
            "the bridged frame's rectangles were not carried");
        read(frame->motion_vectors.Get(), frame->resource_state, DXGI_FORMAT_R16G16_FLOAT, eye);
        read(frame->depth.Get(), frame->depth_resource_state, frame->depth->GetDesc().Format, 2U + eye);
    }
    require_hresult(list->Close(), "Close");
    ID3D12CommandList* lists[]{list.Get()};
    layer_queue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    require_hresult(layer_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())),
        "fence");
    require_hresult(layer_queue->Signal(fence.Get(), 1), "Signal");
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    require_hresult(fence->SetEventOnCompletion(1, done), "SetEventOnCompletion");
    require(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, "the layer's queue did not finish");
    CloseHandle(done);
    std::uint32_t* mapped = nullptr;
    require_hresult(readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map");
    const std::uint32_t slot_words = kPitch * kRenderHeight / 4U;
    const std::uint32_t eye0 = mapped[0];
    const std::uint32_t eye1 = mapped[slot_words];
    const std::uint32_t depth0 = mapped[2U * slot_words];
    const std::uint32_t depth1_last = mapped[3U * slot_words + (kRenderHeight - 1U) * (kPitch / 4U) +
        kRenderWidth - 1U];
    readback->Unmap(0, nullptr);
    require((eye0 == kLeftVector && eye1 == kRightVector) || (eye0 == kRightVector && eye1 == kLeftVector),
        "the layer's snapshots do not hold the vectors the game wrote in D3D12");
    require(depth0 == kDepth && depth1_last == kDepth,
        "the layer's depth snapshots do not hold the game's D3D12 depth");

    require(NVSDK_NGX_SUCCEED(release(left)) && NVSDK_NGX_SUCCEED(release(right)),
        "bridged D3D12 release failed through the hook");

    // V524: the alternating-eye anchor. One feature renders both eyes in
    // turn, the right eye first; its finished output carries each eye's
    // picture - a ramp across for the left eye, a ramp down for the right -
    // as do the eye images. The comparisons anchor the stream with the left
    // eye on the second evaluation's parity, and the resolver gives each eye
    // its own evaluation.
    {
        xrfg::configure_dlss_alternating_eyes(true);
        xrfg::configure_ngx_alternating_eye_anchor(true);
        // V540: wide enough for the eye measurement's patches to slide 16
        // pixels either way.
        constexpr UINT kEyeWidth = 256, kEyeHeight = 96;
        // Its features created at that size: the display size the guides take.
        Parameters alternating_creation = creation;
        alternating_creation.Set(NVSDK_NGX_Parameter_OutWidth, kEyeWidth);
        alternating_creation.Set(NVSDK_NGX_Parameter_OutHeight, kEyeHeight);
        constexpr UINT kOutputPitch = kEyeWidth * 8U;
        constexpr UINT kEyePitch = kEyeWidth * 4U;
        const auto to_half = [](float value) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            const std::uint32_t sign = (bits >> 16U) & 0x8000U;
            const int exponent = static_cast<int>((bits >> 23U) & 0xFFU) - 127 + 15;
            const std::uint32_t mantissa = bits & 0x7FFFFFU;
            if (value == 0.0F || exponent <= 0) return static_cast<std::uint16_t>(sign);
            return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10U) |
                (mantissa >> 13U));
        };
        const auto ramp = [](bool across, UINT x, UINT y) {
            return static_cast<float>((across ? x : y) % 32U) / 31.0F;
        };
        D3D12_HEAP_PROPERTIES default_heap{};
        default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC output_description{};
        output_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        output_description.Width = kEyeWidth;
        output_description.Height = kEyeHeight;
        output_description.DepthOrArraySize = 1;
        output_description.MipLevels = 1;
        output_description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        output_description.SampleDesc.Count = 1;
        output_description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> shared_output;
        require_hresult(game->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE,
            &output_description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(shared_output.GetAddressOf())), "alternating output");
        // Both eyes' pictures in one upload buffer each side: the output's
        // (half floats) on the game's device, the eye images' (8-bit) on the
        // layer's.
        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        const auto upload_buffer = [&](ID3D12Device* device, UINT64 size) {
            D3D12_RESOURCE_DESC buffer{};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = size;
            buffer.Height = 1;
            buffer.DepthOrArraySize = 1;
            buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1;
            buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> resource;
            require_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE,
                &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(resource.GetAddressOf())), "alternating upload");
            return resource;
        };
        const UINT64 output_bytes = static_cast<UINT64>(kOutputPitch) * kEyeHeight;
        const auto output_upload = upload_buffer(game.Get(), 2U * output_bytes);
        {
            std::uint8_t* mapped = nullptr;
            require_hresult(output_upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map");
            for (UINT picture = 0; picture < 2; ++picture) {
                for (UINT y = 0; y < kEyeHeight; ++y) {
                    auto* line = reinterpret_cast<std::uint16_t*>(
                        mapped + picture * output_bytes + static_cast<UINT64>(y) * kOutputPitch);
                    for (UINT x = 0; x < kEyeWidth; ++x) {
                        const std::uint16_t value = to_half(ramp(picture == 0, x, y));
                        line[4U * x] = value;
                        line[4U * x + 1U] = value;
                        line[4U * x + 2U] = value;
                        line[4U * x + 3U] = to_half(1.0F);
                    }
                }
            }
            output_upload->Unmap(0, nullptr);
        }
        D3D12_RESOURCE_DESC eye_description = output_description;
        // Typeless, as the runtimes' images and so the bridge's shared
        // textures are.
        eye_description.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        eye_description.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        std::array<ComPtr<ID3D12Resource>, 2> eye_images{};
        for (auto& eye_image : eye_images) {
            require_hresult(layer_device->CreateCommittedResource(&default_heap,
                D3D12_HEAP_FLAG_NONE, &eye_description, D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(eye_image.GetAddressOf())), "alternating eye image");
        }
        const UINT64 eye_bytes = static_cast<UINT64>(kEyePitch) * kEyeHeight;
        const auto eye_upload = upload_buffer(layer_device, 2U * eye_bytes);
        {
            std::uint8_t* mapped = nullptr;
            require_hresult(eye_upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map");
            for (UINT eye = 0; eye < 2; ++eye) {
                for (UINT y = 0; y < kEyeHeight; ++y) {
                    std::uint8_t* line = mapped + eye * eye_bytes + static_cast<UINT64>(y) * kEyePitch;
                    for (UINT x = 0; x < kEyeWidth; ++x) {
                        const auto value = static_cast<std::uint8_t>(ramp(eye == 0, x, y) * 255.0F);
                        line[4U * x] = value;
                        line[4U * x + 1U] = value;
                        line[4U * x + 2U] = value;
                        line[4U * x + 3U] = 255;
                    }
                }
            }
            eye_upload->Unmap(0, nullptr);
        }
        ComPtr<ID3D12CommandAllocator> layer_allocator;
        require_hresult(layer_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(layer_allocator.GetAddressOf())), "layer allocator");
        ComPtr<ID3D12GraphicsCommandList> layer_list;
        require_hresult(layer_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            layer_allocator.Get(), nullptr, IID_PPV_ARGS(layer_list.GetAddressOf())), "layer list");
        for (UINT eye = 0; eye < 2; ++eye) {
            D3D12_TEXTURE_COPY_LOCATION from{};
            from.pResource = eye_upload.Get();
            from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint.Offset = eye * eye_bytes;
            from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_TYPELESS, kEyeWidth,
                kEyeHeight, 1, kEyePitch};
            D3D12_TEXTURE_COPY_LOCATION to{};
            to.pResource = eye_images[eye].Get();
            to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            layer_list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        }
        require_hresult(layer_list->Close(), "layer Close");
        ComPtr<ID3D12Fence> layer_fence;
        require_hresult(layer_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(layer_fence.GetAddressOf())), "layer fence");
        UINT64 layer_value = 0;
        const auto layer_idle = [&] {
            require_hresult(layer_queue->Signal(layer_fence.Get(), ++layer_value), "layer Signal");
            HANDLE idle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            require_hresult(layer_fence->SetEventOnCompletion(layer_value, idle), "layer event");
            require(WaitForSingleObject(idle, 10000) == WAIT_OBJECT_0, "the layer's queue did not finish");
            CloseHandle(idle);
        };
        {
            ID3D12CommandList* upload_lists[]{layer_list.Get()};
            layer_queue->ExecuteCommandLists(1, upload_lists);
            layer_idle();
        }

        NVSDK_NGX_Handle* shared = nullptr;
        require(NVSDK_NGX_SUCCEED(create(game_list.Get(), NVSDK_NGX_Feature_SuperSampling, &alternating_creation,
                    &shared)) && shared,
            "the alternating feature was not created through the hook");
        const auto transition = [&](D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = shared_output.Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            game_list->ResourceBarrier(1, &barrier);
        };
        // An eye's evaluation: its picture into the output, as DLSS would
        // leave it, then the call.
        const auto evaluate_eye = [&](UINT picture, ID3D12Resource* vectors,
                                      NVSDK_NGX_Handle* through = nullptr, int reset = 0) {
            D3D12_TEXTURE_COPY_LOCATION from{};
            from.pResource = output_upload.Get();
            from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint.Offset = picture * output_bytes;
            from.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16B16A16_FLOAT, kEyeWidth,
                kEyeHeight, 1, kOutputPitch};
            D3D12_TEXTURE_COPY_LOCATION to{};
            to.pResource = shared_output.Get();
            to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            game_list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            transition(D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Parameters frame;
            frame.Set(NVSDK_NGX_Parameter_Output, shared_output.Get());
            frame.Set(NVSDK_NGX_Parameter_MotionVectors, vectors);
            frame.Set(NVSDK_NGX_Parameter_Depth, depth.Get());
            frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, kRenderWidth);
            frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, kRenderHeight);
            frame.Set(NVSDK_NGX_Parameter_Reset, reset);
            require(NVSDK_NGX_SUCCEED(evaluate(game_list.Get(), through != nullptr ? through : shared,
                        &frame, nullptr)),
                "an alternating evaluation failed through the hook");
            transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        };
        const std::array<xrfg::NgxEyeImage, 2> eyes{{
            {eye_images[0].Get(), 0, 0, kEyeWidth, kEyeHeight},
            {eye_images[1].Get(), 0, 0, kEyeWidth, kEyeHeight},
        }};
        // A feature kept from an earlier session, evaluated once: the anchor
        // takes the stream that is publishing (Cyberpunk 2077 keeps its first
        // session's feature beside the one it plays with).
        NVSDK_NGX_Handle* kept = nullptr;
        require(NVSDK_NGX_SUCCEED(create(game_list.Get(), NVSDK_NGX_Feature_SuperSampling, &alternating_creation,
                    &kept)) && kept,
            "the kept feature was not created through the hook");
        std::swap(shared, kept);
        evaluate_eye(0, motion_left.Get());
        std::swap(shared, kept);
        run_game();
        xrfg::flush_ngx_guide_capture_d3d11();
        for (int frame = 0; frame < 8; ++frame) {
            evaluate_eye(1, motion_right.Get());
            evaluate_eye(0, motion_left.Get());
            run_game();
            xrfg::flush_ngx_guide_capture_d3d11();
            xrfg::anchor_ngx_alternating_eyes(eyes, xrfg::dlss_motion_vector_publications());
            layer_idle();
        }
        // V540: the anchor is the eye measurement's, analysed on a thread of
        // its own - a few more releases until five votes are in.
        const auto guided = [&] {
            const auto watermark = xrfg::dlss_motion_vector_publications();
            return xrfg::resolve_dlss_motion_vectors(
                       eye_images[0].Get(), layer_queue, 0, watermark, watermark) != nullptr;
        };
        for (int wait = 0; wait < 300 && !guided(); ++wait) {
            xrfg::anchor_ngx_alternating_eyes(eyes, xrfg::dlss_motion_vector_publications());
            layer_idle();
            Sleep(10);
        }
        std::uint64_t stream = 0;
        {
            const auto watermark = xrfg::dlss_motion_vector_publications();
            const auto left_set = xrfg::resolve_dlss_motion_vectors(
                eye_images[0].Get(), layer_queue, 0, watermark, watermark);
            const auto right_set = xrfg::resolve_dlss_motion_vectors(
                eye_images[1].Get(), layer_queue, 1, watermark, watermark);
            require(left_set && right_set && left_set->eyes[0] && right_set->eyes[0],
                "the anchored alternating stream did not guide both eyes");
            stream = left_set->eyes[0]->stream;
            require(xrfg::dlss_motion_vector_stream_position(stream).anchored,
                "the pictures did not anchor the alternating stream");
            // Under R.E.A.L. VR every handle publishes into one stream (V526),
            // the kept feature's evaluation first (serial 1): the left eye has
            // the newest (17); the right, a frame late as R.E.A.L. VR's AERv2
            // hands it over, the one before its newest (14).
            require(left_set->eyes[0]->serial == 17 && right_set->eyes[0]->serial == 14,
                "the anchor did not give each eye its evaluation");
            // The vectors as the game wrote them: the stretch is measured and
            // recorded (848), not applied, in this build.
            require(left_set->eyes[0]->scale_x == 1.0F && right_set->eyes[0]->scale_x == 1.0F,
                "the alternating eyes' vectors were stretched");
            std::cout << "ngx guide capture alternating anchor: left serial "
                      << left_set->eyes[0]->serial << ", right " << right_set->eyes[0]->serial << "\n";
        }
        // V540: R.E.A.L. VR's fix evaluates each eye through a feature of its
        // own. The handles anchor once: after a reset - a new epoch, which a
        // serial parity's anchor does not survive - the eyes are known at
        // once, even where the reset swaps the order the eyes evaluate in.
        {
            NVSDK_NGX_Handle* left_feature = nullptr;
            NVSDK_NGX_Handle* right_feature = nullptr;
            require(NVSDK_NGX_SUCCEED(create(game_list.Get(), NVSDK_NGX_Feature_SuperSampling,
                        &alternating_creation, &left_feature)) && left_feature &&
                    NVSDK_NGX_SUCCEED(create(game_list.Get(), NVSDK_NGX_Feature_SuperSampling,
                        &alternating_creation, &right_feature)) && right_feature,
                "the per-eye features were not created through the hook");
            const auto frame_of_two = [&](bool left_first, int reset) {
                if (left_first) {
                    evaluate_eye(0, motion_left.Get(), left_feature, reset);
                    evaluate_eye(1, motion_right.Get(), right_feature);
                } else {
                    evaluate_eye(1, motion_right.Get(), right_feature, reset);
                    evaluate_eye(0, motion_left.Get(), left_feature);
                }
                run_game();
                xrfg::flush_ngx_guide_capture_d3d11();
            };
            frame_of_two(false, 1);
            for (int frame = 0; frame < 4; ++frame) {
                frame_of_two(false, 0);
                xrfg::anchor_ngx_alternating_eyes(eyes, xrfg::dlss_motion_vector_publications());
                layer_idle();
            }
            for (int wait = 0; wait < 300 &&
                 !xrfg::dlss_motion_vector_stream_position(stream).tag_anchored; ++wait) {
                xrfg::anchor_ngx_alternating_eyes(eyes, xrfg::dlss_motion_vector_publications());
                layer_idle();
                Sleep(10);
            }
            require(xrfg::dlss_motion_vector_stream_position(stream).tag_anchored,
                "the per-eye handles did not anchor the alternating eyes");
            const auto resolve_both = [&] {
                const auto watermark = xrfg::dlss_motion_vector_publications();
                return std::make_pair(
                    xrfg::resolve_dlss_motion_vectors(eye_images[0].Get(), layer_queue, 0,
                        watermark, watermark),
                    xrfg::resolve_dlss_motion_vectors(eye_images[1].Get(), layer_queue, 1,
                        watermark, watermark));
            };
            auto [left_set, right_set] = resolve_both();
            require(left_set && right_set && left_set->eyes[0] && right_set->eyes[0],
                "the handles' anchor did not guide both eyes");
            const std::uint64_t left_tag = left_set->eyes[0]->eye_tag;
            auto newest = xrfg::dlss_motion_vector_stream_position(stream).serial;
            require(left_tag != 0 && right_set->eyes[0]->eye_tag != 0 &&
                    left_tag != right_set->eyes[0]->eye_tag &&
                    left_set->eyes[0]->serial == newest && right_set->eyes[0]->serial == newest - 3,
                "the handles' anchor did not give each eye its evaluation");
            // A reset, the left eye now evaluated first: no comparison, no new
            // vote - the right eye has the newest, the left its one before.
            frame_of_two(true, 1);
            frame_of_two(true, 0);
            std::tie(left_set, right_set) = resolve_both();
            newest = xrfg::dlss_motion_vector_stream_position(stream).serial;
            require(left_set && right_set && left_set->eyes[0] && right_set->eyes[0] &&
                    left_set->eyes[0]->eye_tag == left_tag &&
                    right_set->eyes[0]->eye_tag != left_tag &&
                    right_set->eyes[0]->serial == newest && left_set->eyes[0]->serial == newest - 3,
                "the handles' anchor did not hold through a reset that swapped the eyes' order");
            std::cout << "ngx guide capture alternating handles: left tag " << left_tag
                      << ", after the reset left serial " << left_set->eyes[0]->serial
                      << ", right " << right_set->eyes[0]->serial << "\n";
            require(NVSDK_NGX_SUCCEED(release(left_feature)) &&
                    NVSDK_NGX_SUCCEED(release(right_feature)),
                "the per-eye features' release failed");
        }
        require(NVSDK_NGX_SUCCEED(release(shared)) && NVSDK_NGX_SUCCEED(release(kept)),
            "the alternating features' release failed");
        xrfg::configure_ngx_alternating_eye_anchor(false);
        xrfg::configure_dlss_alternating_eyes(false);
    }
    std::cout << "ngx guide capture bridged D3D12: publications="
              << after.d3d12_bridged_publications << " eye0=0x" << std::hex << eye0
              << " eye1=0x" << eye1 << std::dec << "\n";
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
    const auto evaluate_frame = [&](NVSDK_NGX_Handle* handle, ID3D11Texture2D* vectors, float jitter,
                                    ID3D11Texture2D* frame_depth = nullptr) {
        Parameters frame;
        frame.Set(NVSDK_NGX_Parameter_Output, static_cast<ID3D11Resource*>(output.Get()));
        frame.Set(NVSDK_NGX_Parameter_MotionVectors, static_cast<ID3D11Resource*>(vectors));
        frame.Set(NVSDK_NGX_Parameter_Depth,
            static_cast<ID3D11Resource*>(frame_depth != nullptr ? frame_depth : depth.Get()));
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

    // V521: a packed depth-stencil buffer (Skyrim's R24G8), which D3D11
    // cannot share, goes over unpacked to a float. Cleared to 0.25 and left
    // bound for writing, as a game may leave it at its DLSS call: the unpack
    // unbinds it for its read and binds it back.
    {
        D3D11_TEXTURE2D_DESC packed_description{};
        packed_description.Width = kRenderWidth;
        packed_description.Height = kRenderHeight;
        packed_description.MipLevels = 1;
        packed_description.ArraySize = 1;
        packed_description.Format = DXGI_FORMAT_R24G8_TYPELESS;
        packed_description.SampleDesc.Count = 1;
        packed_description.Usage = D3D11_USAGE_DEFAULT;
        packed_description.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> packed;
        require_hresult(device11->CreateTexture2D(&packed_description, nullptr, packed.GetAddressOf()),
            "packed depth");
        D3D11_DEPTH_STENCIL_VIEW_DESC write_description{};
        write_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        write_description.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11DepthStencilView> write_view;
        require_hresult(device11->CreateDepthStencilView(packed.Get(), &write_description,
            write_view.GetAddressOf()), "packed depth view");
        context11->ClearDepthStencilView(write_view.Get(),
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.25F, 7);
        context11->OMSetRenderTargets(0, nullptr, write_view.Get());
        evaluate_frame(left, motion_left.Get(), 0.25F, packed.Get());
        evaluate_frame(right, motion_right.Get(), 0.25F, packed.Get());
        ComPtr<ID3D11DepthStencilView> still_bound;
        context11->OMGetRenderTargets(0, nullptr, still_bound.GetAddressOf());
        require(still_bound.Get() == write_view.Get(), "the unpack did not bind the game's depth back");
        context11->OMSetRenderTargets(0, nullptr, nullptr);
        context11->Flush();
        xrfg::flush_ngx_guide_capture_d3d11();
        const auto packed_set = xrfg::resolve_dlss_motion_vectors(image.Get(), queue12.Get());
        require(packed_set != nullptr && packed_set->eye_count == 2, "no guide set with the packed depth");

        require_hresult(allocator->Reset(), "allocator reset");
        require_hresult(list->Reset(allocator.Get(), nullptr), "list reset");
        for (unsigned eye = 0; eye < 2; ++eye) {
            const auto& frame = packed_set->eyes[eye];
            require(frame && frame->depth &&
                    frame->depth->GetDesc().Format == DXGI_FORMAT_R32_FLOAT,
                "an eye's packed depth did not go over as a float");
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = frame->depth.Get();
            barrier.Transition.StateBefore = frame->depth_resource_state;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = frame->depth.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = readback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint.Offset = eye * 256ULL * kRenderHeight;
            destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_FLOAT, kRenderWidth, kRenderHeight, 1, 256};
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            list->ResourceBarrier(1, &barrier);
        }
        require_hresult(list->Close(), "Close packed");
        ID3D12CommandList* packed_lists[]{list.Get()};
        queue12->ExecuteCommandLists(1, packed_lists);
        require_hresult(queue12->Signal(fence.Get(), 2), "Signal packed");
        HANDLE packed_done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        require_hresult(fence->SetEventOnCompletion(2, packed_done), "SetEventOnCompletion packed");
        require(WaitForSingleObject(packed_done, 10000) == WAIT_OBJECT_0, "the packed depth readback did not finish");
        CloseHandle(packed_done);
        float* depths = nullptr;
        require_hresult(readback->Map(0, nullptr, reinterpret_cast<void**>(&depths)), "Map packed");
        const float depth0 = depths[0];
        const float depth1 = depths[256U * kRenderHeight / 4U];
        const float depth_last = depths[(kRenderHeight - 1U) * 64U + kRenderWidth - 1U];
        readback->Unmap(0, nullptr);
        require(std::abs(depth0 - 0.25F) < 1.0e-4F && std::abs(depth1 - 0.25F) < 1.0e-4F &&
                std::abs(depth_last - 0.25F) < 1.0e-4F,
            "the unpacked depth does not read what the game's depth buffer holds");
        std::cout << "ngx guide capture D3D11 packed depth: " << depth0 << " " << depth1 << "\n";
    }

    const auto released_before = xrfg::ngx_guide_capture_statistics().features_released;
    require(NVSDK_NGX_SUCCEED(release(left)) && NVSDK_NGX_SUCCEED(release(right)),
        "D3D11 release failed through the hook");
    require(xrfg::ngx_guide_capture_statistics().features_released == released_before + 2,
        "the two D3D11 features were not released");

    // V519: a feature the game created before the capture's hooks went in -
    // Skyrim's DLSS started with Hybrid off and Hybrid was switched on later.
    // It registers from its first evaluation and publishes from then on; the
    // D3D12 capture is stopped here, as it is in a bridged session.
    const auto create_unhooked = reinterpret_cast<NVSDK_NGX_Handle* (*)(int)>(
        GetProcAddress(module, "fake_ngx_d3d11_create_unhooked"));
    require(create_unhooked != nullptr, "the stand-in lacks fake_ngx_d3d11_create_unhooked");
    const auto late_before = xrfg::ngx_guide_capture_statistics();
    require(!late_before.active, "the D3D12 capture is still active in the D3D11 phase");
    //
    // Then its release (V519). The registry used to free a released
    // feature's snapshots at once, with their copies possibly still queued -
    // the GPU then wrote freed memory, and this test, releasing right after a
    // flush, took the PC down three times. Here the release comes before the
    // flush: the evaluations' copy lists are recorded but never submitted
    // (the transport drops them), so no GPU work ever touches these
    // snapshots whatever the registry does, and the test checks the
    // registry's side - the snapshots held past the release, freed once the
    // producer queue's fence and the grace have passed.
    NVSDK_NGX_Handle* late = create_unhooked(NVSDK_NGX_Feature_SuperSampling);
    require(late != nullptr &&
            xrfg::ngx_guide_capture_statistics().features_created == late_before.features_created,
        "the unhooked creation was seen");
    evaluate_frame(late, motion_left.Get(), 0.25F);
    evaluate_frame(late, motion_left.Get(), -0.25F);
    const auto late_after = xrfg::ngx_guide_capture_statistics();
    require(late_after.features_created == late_before.features_created + 1 &&
            late_after.skipped_unknown_feature == late_before.skipped_unknown_feature &&
            late_after.d3d11_publications == late_before.d3d11_publications + 2,
        "a D3D11 feature created before the hooks was not registered from its evaluation");
    // The D3D11 side's copies into the transport's shared textures done too.
    {
        D3D11_QUERY_DESC query_description{D3D11_QUERY_EVENT, 0};
        ComPtr<ID3D11Query> query;
        require_hresult(device11->CreateQuery(&query_description, query.GetAddressOf()), "event query");
        context11->End(query.Get());
        BOOL finished = FALSE;
        for (int spin = 0; spin < 10000 &&
                context11->GetData(query.Get(), &finished, sizeof(finished), 0) != S_OK; ++spin) {
            Sleep(1);
        }
        require(finished == TRUE, "the D3D11 copies did not finish");
    }
    const auto held_before = xrfg::dlss_motion_vector_statistics().retired_snapshots_held;
    require(NVSDK_NGX_SUCCEED(release(late)), "D3D11 release of the late feature failed");
    require(xrfg::dlss_motion_vector_statistics().retired_snapshots_held == held_before + 1,
        "the released feature's snapshots were freed at once, not held for their copies");
    // Past the registry's grace, its fence long passed: a resolve frees them.
    Sleep(2100);
    static_cast<void>(xrfg::resolve_dlss_motion_vectors(image.Get(), queue12.Get()));
    require(xrfg::dlss_motion_vector_statistics().retired_snapshots_held == 0,
        "retired snapshots were kept after their copies finished");
    run_bridged_d3d12(module, device12.Get(), queue12.Get());
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
#if defined(_WIN64)
static_assert(offsetof(Constants, view_to_clip) == 32 && offsetof(Constants, position) == 376 &&
    offsetof(Constants, right) == 400);
#endif

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
    // V549, the third frame: the cameras' positions the wrong way round, as
    // the Dream Air's flip (#33) put them.
    const std::array<float, 4> swapped_x{0.032F, -0.032F, 0.032F, -0.032F};
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
    // Frames 1 and 2 evaluate the peripheries then the insets; frame 3 (V549)
    // as MSFS 2024 does - each eye's periphery then its inset, the eye's
    // image released after them - with the cameras swapped.
    for (UINT64 frame = 1; frame <= 3; ++frame) {
        const bool msfs_order = frame == 3;
        for (std::uint32_t v = 0; v < 4; ++v) {
            if (msfs_order) constants[v] = constants_for(fields[v], swapped_x[v], v == 1);
            require(set_constants(&constants[v], &frame_token, &viewports[v]) == 0,
                "slSetConstants failed through the hook");
        }
        const std::array<std::uint32_t, 4> order = msfs_order
            ? std::array<std::uint32_t, 4>{0, 2, 1, 3} : std::array<std::uint32_t, 4>{0, 1, 2, 3};
        for (const std::uint32_t v : order) {
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
    require(sl_evaluations() == 12, "the original slEvaluateFeature did not run twelve times");
    require(xrfg::ngx_guide_capture_statistics().publications_with_camera == publications_before + 12,
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
                f.output_height == kEyeHeight && f.serial == 3 && f.previous_serial == 2,
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
        // V516: the eye's inset - the camera at the periphery's position -
        // rides on it, with its field in the eye now and a frame before.
        require(f.inset != nullptr && f.inset->view.viewport == 42U + eye,
            "an eye's inset was not joined to its periphery");
        const xrfg::DlssEyeView& eye_view = eye == 0 ? left_eye : right_eye;
        const float inset_x = (-0.35F - eye_view.tan_left) / 2.2F * 96.0F;
        require(std::fabs(f.inset->view_rect[0] - inset_x) < 1e-3F &&
                std::fabs(f.inset->view_rect[2] - 0.7F / 2.2F * 96.0F) < 1e-3F &&
                std::fabs(f.inset->view_rect[1] - 0.75F / 2.2F * 72.0F) < 1e-3F &&
                f.inset->previous_view_rect_known &&
                std::fabs(f.inset->previous_view_rect[0] - inset_x) < 1e-3F,
            "the inset's field is not placed in its eye");
    }
    require(xrfg::dlss_motion_vector_statistics().inset_matched != 0,
        "the inset join was not counted");
    // Symmetric eyes: both peripheries fit alike, and with the releases
    // saying nothing (both after every evaluation) the camera further left
    // is the left eye - the swapped one, viewport 41, in the third frame.
    const xrfg::DlssEyeView symmetric{-1.0F, 1.0F, 1.1F, -1.1F, 0, 0, kEyeWidth, kEyeHeight};
    placed = resolve(symmetric, symmetric);
    require(placed[0] && placed[1] && placed[0]->eyes[0]->view.viewport == 41U &&
            placed[1]->eyes[0]->view.viewport == 40U,
        "the cameras' positions did not decide between two alike peripheries");
    // V549: the releases' order outranks the cameras. The left eye's image
    // released after its periphery and inset (two publications before the
    // right's), the right's after all four: each eye takes the streams
    // evaluated before its own release, its inset among them.
    const auto resolve_released = [&](std::uint64_t left_release, std::uint64_t right_release) {
        return xrfg::resolve_view_placed_dlss_guides(
            queue, {symmetric, symmetric}, {left_release, right_release});
    };
    placed = resolve_released(watermark - 2, watermark);
    require(placed[0] && placed[1] && placed[0]->eyes[0]->view.viewport == 40U &&
            placed[1]->eyes[0]->view.viewport == 41U,
        "the releases' order did not pick the peripheries over the cameras");
    require(placed[0]->eyes[0]->inset && placed[0]->eyes[0]->inset->view.viewport == 42U &&
            placed[1]->eyes[0]->inset && placed[1]->eyes[0]->inset->view.viewport == 43U,
        "the insets did not follow their eye's release group");
    // The right eye released first: its group is the streams before it.
    placed = resolve_released(watermark, watermark - 2);
    require(placed[0] && placed[1] && placed[0]->eyes[0]->view.viewport == 41U &&
            placed[1]->eyes[0]->view.viewport == 40U,
        "the eye released first did not take the streams evaluated before it");
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

// V521: an upscaler's input offset inside its eye's render region, removed;
// regions elsewhere on the texture left alone.
// V551: a feature registered from its evaluation takes its sizes the right
// way round. No Man's Sky's evaluation has them swapped.
void test_late_feature_sizes() {
    const auto nms = xrfg::late_feature_sizes(6144, 3264, 3564, 1893);
    require(nms.swapped && nms.width == 3564 && nms.height == 1893 && nms.out_width == 6144 &&
            nms.out_height == 3264, "No Man's Sky's swapped sizes were not put back");
    // Its creation's: render 4840 x 1895 into 8344 x 3268, left as they are.
    const auto creation = xrfg::late_feature_sizes(4840, 1895, 8344, 3268);
    require(!creation.swapped && creation.width == 4840 && creation.out_width == 8344,
        "an upscale's sizes were swapped");
    // DLAA (equal), an output smaller on one axis only, and no output size.
    require(!xrfg::late_feature_sizes(2546, 1994, 2546, 1994).swapped, "DLAA swapped");
    require(!xrfg::late_feature_sizes(2000, 1000, 1800, 1200).swapped, "one axis swapped");
    const auto unknown = xrfg::late_feature_sizes(2000, 1000, 0, 0);
    require(!unknown.swapped && unknown.out_width == 0, "a missing output size swapped");
}

void test_input_offset_within_region() {
    // Skyrim, PureDark's upscaler with its foveated offsets at scale 1.0:
    // 2824 x 2212 per eye side by side, output 4870 x 3813 at (0, 0).
    require(xrfg::dlss_input_offset_within_region(56, 0, 2824, 4870) == 56, "left eye x");
    require(xrfg::dlss_input_offset_within_region(2768, 0, 2824, 4870) == -56, "right eye x");
    require(xrfg::dlss_input_offset_within_region(88, 0, 2212, 3813) == 88, "y");
    // On the grid: nothing.
    require(xrfg::dlss_input_offset_within_region(0, 0, 2824, 4870) == 0, "left at origin");
    require(xrfg::dlss_input_offset_within_region(2824, 0, 2824, 4870) == 0, "right at its region");
    // An eye at the half of a larger allocation (4870 of 9740, rendering
    // 2824): a third of a region off the grid, not an offset.
    require(xrfg::dlss_input_offset_within_region(4870, 0, 2824, 4870) == 0, "allocation half");
    // An output placed where its input is: consistent, nothing to remove.
    require(xrfg::dlss_input_offset_within_region(58, 100, 2824, 4870) == 0, "placed output");
    require(xrfg::dlss_input_offset_within_region(100, 100, 4870, 4870) == 0, "full resolution");
    require(xrfg::dlss_input_offset_within_region(10, 0, 0, 4870) == 0, "no extent");
}

int run(const wchar_t* module_path) {
    test_split_both_eyes_guide();
    test_input_offset_within_region();
    test_late_feature_sizes();
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
        // V540: a flight log of the run, as the layer writes one
        // (XRFG_TEST_GPU_TRACE, as the history tests').
        if (std::getenv("XRFG_TEST_GPU_TRACE") != nullptr) {
            const auto trace_dir = std::filesystem::temp_directory_path() /
                ("xrfg-ngx-" + std::to_string(GetCurrentProcessId()));
            std::filesystem::create_directories(trace_dir);
            std::ofstream(trace_dir / "ofxr_bridge.ini") <<
                "[diagnostics]\nlogging_enabled=1\nmax_file_mb=32\nflush_each_event=0\n";
            xrfg::bridge_flight_logger().initialize(trace_dir);
            std::cout << "flight log in " << trace_dir.string() << "\n";
        }
        // V513: with the Vulkan queue layer's path, the Vulkan phase runs too.
        if (argc > 2) g_queue_layer_path = argv[2];
        return run(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "ngx guide capture test failed: " << error.what() << "\n";
        return 1;
    }
}
