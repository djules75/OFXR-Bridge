// OFXR Bridge's Vulkan implicit layer: one mutex per device around every
// queue operation that Vulkan requires the application to synchronise
// itself.
//
// Why it exists. In a Vulkan session the OpenXR runtime submits on the queue
// the application handed it, inside xrEndFrame and the swapchain calls. The
// bridge's presenter thread makes those calls, so the runtime submits from
// the presenter thread while the game's own threads submit to the same
// queue - and Vulkan forbids that. Measured: No Man's Sky through
// OpenComposite lost the device under its own vkQueueSubmit on SteamVR and
// got XR_ERROR_RUNTIME_FAILURE from xrEndFrame on VDXR, both a few seconds
// into generation, both while the game was submitting heavily. The game's,
// the runtime's and the bridge's submissions all reach the driver through
// the loader, so a layer in the chain serialises all three.
//
// Guide submissions (V513). Hybrid on a bridged Vulkan session copies the
// game's DLSS vectors, at its NGX evaluation, inside the game's own command
// buffer - which runs whenever the game submits it, something only the queue
// sees. The OpenXR layer registers such a command buffer here with a
// timeline semaphore (a D3D12 fence imported into the game's device) and a
// value; when the game submits it, the layer signals that value on the same
// queue right after, under the same lock, so the D3D12 side waits for the
// copy and never for anything earlier, and marks the registration submitted.
// Nothing happens while nothing is registered beyond one atomic load.
//
// Global commands (V556). Asked for the instance-less global commands, it
// hands back the loader's exports rather than nullptr; see loader_global.
//
// What it does not do. It intercepts nothing else, holds the lock only
// across the driver's own call, and behaves the same in every process it is
// loaded into, since an implicit layer loads into every Vulkan process while
// it is registered. The tray registers it only while the bridge is armed and
// the Vulkan option is on, and OFXR_DISABLE_VULKAN_QUEUE_LAYER=1 turns it
// off for a process.
//
// Games run as administrator (V572). The Vulkan loader reads no per-user
// layer in an elevated process, so the tray's registration never reaches
// one. The machine-wide copy for those games (implicit_layer::
// kElevatedVulkanManifestName) is registered for good, not only while armed,
// so its manifest names OFXR_vkNegotiateLoaderLayerInterfaceVersionElevated,
// which decides once per process whether the copy acts: only where the
// process is elevated and the tray's per-user arm holds this very DLL.
// Anywhere else the layer is inert - it stays in the chain, because the
// Vulkan loader does not drop a layer that refuses negotiation but falls back
// to its manifest's vkGetInstanceProcAddr and inserts it anyway (measured on
// loader 1.4.357), and it hands out the next link's queue functions, so it
// locks nothing and sees no submission.

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include "xrfg/implicit_layer.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <vector>
#include <string_view>
#include <unordered_map>

namespace {

// The loader stores its dispatch pointer at the start of every dispatchable
// object; objects made from a device carry the device's, which is how a
// queue finds its device's table.
[[nodiscard]] void* dispatch_key(const void* handle) noexcept {
    return *static_cast<void* const*>(handle);
}

struct InstanceTable {
    PFN_vkGetInstanceProcAddr get_instance_proc_addr{};
    PFN_vkDestroyInstance destroy_instance{};
};

struct DeviceTable {
    PFN_vkGetDeviceProcAddr get_device_proc_addr{};
    PFN_vkDestroyDevice destroy_device{};
    PFN_vkQueueSubmit queue_submit{};
    PFN_vkQueueSubmit2 queue_submit2{};
    PFN_vkQueueSubmit2 queue_submit2_khr{};
    PFN_vkQueueWaitIdle queue_wait_idle{};
    PFN_vkQueueBindSparse queue_bind_sparse{};
    PFN_vkQueuePresentKHR queue_present{};
    // One lock for every queue of the device: the race is on a queue, and a
    // device rarely has more than one that matters, so the simpler scope
    // costs nothing measurable.
    std::mutex queue_mutex;
};

struct GuideRegistration {
    VkSemaphore semaphore{};
    std::uint64_t value{};
    // Where the value is written once signalled: the highest submitted.
    volatile LONG64* submitted{};
};

std::mutex g_guide_mutex;
// Several streams may record into one command buffer (one DLSS feature per
// eye, both in the frame's buffer): one registration per semaphore.
std::unordered_map<VkCommandBuffer, std::vector<GuideRegistration>> g_guides;
std::atomic<std::uint32_t> g_guide_count{0};

std::mutex g_tables_mutex;
std::unordered_map<void*, InstanceTable> g_instances;
std::unordered_map<void*, std::shared_ptr<DeviceTable>> g_devices;

[[nodiscard]] InstanceTable instance_table(void* key) {
    std::scoped_lock lock(g_tables_mutex);
    const auto found = g_instances.find(key);
    return found == g_instances.end() ? InstanceTable{} : found->second;
}

[[nodiscard]] std::shared_ptr<DeviceTable> device_table(void* key) {
    std::scoped_lock lock(g_tables_mutex);
    const auto found = g_devices.find(key);
    return found == g_devices.end() ? nullptr : found->second;
}

template <typename Function>
[[nodiscard]] Function load(
    PFN_vkGetDeviceProcAddr get, VkDevice device, const char* name) noexcept {
    return reinterpret_cast<Function>(get(device, name));
}

VKAPI_ATTR VkResult VKAPI_CALL layer_create_instance(
    const VkInstanceCreateInfo* create_info,
    const VkAllocationCallbacks* allocator,
    VkInstance* instance) {
    // Find this layer's link in the loader's chain and step past it.
    auto* chain = const_cast<VkLayerInstanceCreateInfo*>(
        static_cast<const VkLayerInstanceCreateInfo*>(create_info->pNext));
    while (chain != nullptr &&
           !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
             chain->function == VK_LAYER_LINK_INFO)) {
        chain = const_cast<VkLayerInstanceCreateInfo*>(
            static_cast<const VkLayerInstanceCreateInfo*>(chain->pNext));
    }
    if (chain == nullptr || chain->u.pLayerInfo == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const PFN_vkGetInstanceProcAddr next_get =
        chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    const auto next_create = reinterpret_cast<PFN_vkCreateInstance>(
        next_get(VK_NULL_HANDLE, "vkCreateInstance"));
    if (next_create == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkResult result = next_create(create_info, allocator, instance);
    if (result != VK_SUCCESS) {
        return result;
    }
    InstanceTable table;
    table.get_instance_proc_addr = next_get;
    table.destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(
        next_get(*instance, "vkDestroyInstance"));
    std::scoped_lock lock(g_tables_mutex);
    g_instances[dispatch_key(*instance)] = table;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layer_destroy_instance(
    VkInstance instance, const VkAllocationCallbacks* allocator) {
    if (instance == VK_NULL_HANDLE) {
        return;
    }
    InstanceTable table;
    {
        std::scoped_lock lock(g_tables_mutex);
        const auto found = g_instances.find(dispatch_key(instance));
        if (found != g_instances.end()) {
            table = found->second;
            g_instances.erase(found);
        }
    }
    if (table.destroy_instance != nullptr) {
        table.destroy_instance(instance, allocator);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL layer_create_device(
    VkPhysicalDevice physical_device,
    const VkDeviceCreateInfo* create_info,
    const VkAllocationCallbacks* allocator,
    VkDevice* device) {
    auto* chain = const_cast<VkLayerDeviceCreateInfo*>(
        static_cast<const VkLayerDeviceCreateInfo*>(create_info->pNext));
    while (chain != nullptr &&
           !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
             chain->function == VK_LAYER_LINK_INFO)) {
        chain = const_cast<VkLayerDeviceCreateInfo*>(
            static_cast<const VkLayerDeviceCreateInfo*>(chain->pNext));
    }
    if (chain == nullptr || chain->u.pLayerInfo == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const PFN_vkGetInstanceProcAddr next_instance_get =
        chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const PFN_vkGetDeviceProcAddr next_device_get =
        chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    const auto next_create = reinterpret_cast<PFN_vkCreateDevice>(
        next_instance_get(VK_NULL_HANDLE, "vkCreateDevice"));
    if (next_create == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkResult result =
        next_create(physical_device, create_info, allocator, device);
    if (result != VK_SUCCESS) {
        return result;
    }
    auto table = std::make_shared<DeviceTable>();
    table->get_device_proc_addr = next_device_get;
    table->destroy_device =
        load<PFN_vkDestroyDevice>(next_device_get, *device, "vkDestroyDevice");
    table->queue_submit =
        load<PFN_vkQueueSubmit>(next_device_get, *device, "vkQueueSubmit");
    table->queue_submit2 =
        load<PFN_vkQueueSubmit2>(next_device_get, *device, "vkQueueSubmit2");
    table->queue_submit2_khr =
        load<PFN_vkQueueSubmit2>(next_device_get, *device, "vkQueueSubmit2KHR");
    table->queue_wait_idle =
        load<PFN_vkQueueWaitIdle>(next_device_get, *device, "vkQueueWaitIdle");
    table->queue_bind_sparse =
        load<PFN_vkQueueBindSparse>(next_device_get, *device, "vkQueueBindSparse");
    table->queue_present =
        load<PFN_vkQueuePresentKHR>(next_device_get, *device, "vkQueuePresentKHR");
    std::scoped_lock lock(g_tables_mutex);
    g_devices[dispatch_key(*device)] = std::move(table);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layer_destroy_device(
    VkDevice device, const VkAllocationCallbacks* allocator) {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    std::shared_ptr<DeviceTable> table;
    {
        std::scoped_lock lock(g_tables_mutex);
        const auto found = g_devices.find(dispatch_key(device));
        if (found != g_devices.end()) {
            table = found->second;
            g_devices.erase(found);
        }
    }
    if (table && table->destroy_device != nullptr) {
        table->destroy_device(device, allocator);
    }
}

// The registrations a successful submission carried, taken out of the map.
template <typename Each>
[[nodiscard]] std::vector<GuideRegistration> take_guides(Each&& each_buffer) {
    std::vector<GuideRegistration> taken;
    if (g_guide_count.load(std::memory_order_acquire) == 0) return taken;
    try {
        std::scoped_lock lock(g_guide_mutex);
        each_buffer([&](VkCommandBuffer buffer) {
            const auto found = g_guides.find(buffer);
            if (found == g_guides.end()) return;
            taken.insert(taken.end(), found->second.begin(), found->second.end());
            g_guide_count.fetch_sub(
                static_cast<std::uint32_t>(found->second.size()), std::memory_order_acq_rel);
            g_guides.erase(found);
        });
    } catch (...) {
    }
    return taken;
}

// Signals each on the queue just submitted to, still under its lock.
void signal_guides(
    DeviceTable& table, VkQueue queue, const std::vector<GuideRegistration>& guides) {
    for (const GuideRegistration& guide : guides) {
        VkTimelineSemaphoreSubmitInfo timeline{};
        timeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timeline.signalSemaphoreValueCount = 1;
        timeline.pSignalSemaphoreValues = &guide.value;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.pNext = &timeline;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &guide.semaphore;
        if (table.queue_submit != nullptr &&
            table.queue_submit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
            guide.submitted != nullptr) {
            LONG64 seen = *guide.submitted;
            while (seen < static_cast<LONG64>(guide.value)) {
                const LONG64 previous = InterlockedCompareExchange64(
                    guide.submitted, static_cast<LONG64>(guide.value), seen);
                if (previous == seen) break;
                seen = previous;
            }
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL layer_queue_submit(
    VkQueue queue, uint32_t submit_count, const VkSubmitInfo* submits, VkFence fence) {
    const auto table = device_table(dispatch_key(queue));
    if (!table || table->queue_submit == nullptr) {
        return VK_ERROR_DEVICE_LOST;
    }
    std::scoped_lock lock(table->queue_mutex);
    const VkResult result = table->queue_submit(queue, submit_count, submits, fence);
    if (result == VK_SUCCESS && submits != nullptr) {
        const auto guides = take_guides([&](auto&& visit) {
            for (uint32_t i = 0; i < submit_count; ++i)
                for (uint32_t j = 0; j < submits[i].commandBufferCount; ++j)
                    visit(submits[i].pCommandBuffers[j]);
        });
        if (!guides.empty()) signal_guides(*table, queue, guides);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL layer_queue_submit2(
    VkQueue queue, uint32_t submit_count, const VkSubmitInfo2* submits, VkFence fence) {
    const auto table = device_table(dispatch_key(queue));
    if (!table || table->queue_submit2 == nullptr) {
        return VK_ERROR_DEVICE_LOST;
    }
    std::scoped_lock lock(table->queue_mutex);
    const VkResult result = table->queue_submit2(queue, submit_count, submits, fence);
    if (result == VK_SUCCESS && submits != nullptr) {
        const auto guides = take_guides([&](auto&& visit) {
            for (uint32_t i = 0; i < submit_count; ++i)
                for (uint32_t j = 0; j < submits[i].commandBufferInfoCount; ++j)
                    visit(submits[i].pCommandBufferInfos[j].commandBuffer);
        });
        if (!guides.empty()) signal_guides(*table, queue, guides);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL layer_queue_submit2_khr(
    VkQueue queue, uint32_t submit_count, const VkSubmitInfo2* submits, VkFence fence) {
    const auto table = device_table(dispatch_key(queue));
    if (!table || table->queue_submit2_khr == nullptr) {
        return VK_ERROR_DEVICE_LOST;
    }
    std::scoped_lock lock(table->queue_mutex);
    const VkResult result = table->queue_submit2_khr(queue, submit_count, submits, fence);
    if (result == VK_SUCCESS && submits != nullptr) {
        const auto guides = take_guides([&](auto&& visit) {
            for (uint32_t i = 0; i < submit_count; ++i)
                for (uint32_t j = 0; j < submits[i].commandBufferInfoCount; ++j)
                    visit(submits[i].pCommandBufferInfos[j].commandBuffer);
        });
        if (!guides.empty()) signal_guides(*table, queue, guides);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL layer_queue_wait_idle(VkQueue queue) {
    const auto table = device_table(dispatch_key(queue));
    if (!table || table->queue_wait_idle == nullptr) {
        return VK_ERROR_DEVICE_LOST;
    }
    std::scoped_lock lock(table->queue_mutex);
    return table->queue_wait_idle(queue);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_queue_bind_sparse(
    VkQueue queue, uint32_t bind_count, const VkBindSparseInfo* binds, VkFence fence) {
    const auto table = device_table(dispatch_key(queue));
    if (!table || table->queue_bind_sparse == nullptr) {
        return VK_ERROR_DEVICE_LOST;
    }
    std::scoped_lock lock(table->queue_mutex);
    return table->queue_bind_sparse(queue, bind_count, binds, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_queue_present(
    VkQueue queue, const VkPresentInfoKHR* present_info) {
    const auto table = device_table(dispatch_key(queue));
    if (!table || table->queue_present == nullptr) {
        return VK_ERROR_DEVICE_LOST;
    }
    std::scoped_lock lock(table->queue_mutex);
    return table->queue_present(queue, present_info);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_get_device_proc_addr(
    VkDevice device, const char* name);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_get_instance_proc_addr(
    VkInstance instance, const char* name);

// V572: the machine-wide copy outside an elevated, armed game.
std::atomic<bool> g_inert{false};

[[nodiscard]] bool queue_function(std::string_view name) noexcept {
    return name.rfind("vkQueue", 0) == 0;
}

// The functions this layer answers itself. A device-level query and an
// instance-level one both have to find them: the loader asks either way.
// Inert, only those that keep the chain linked.
[[nodiscard]] PFN_vkVoidFunction intercepted(std::string_view name) noexcept {
    if (g_inert.load(std::memory_order_acquire) && queue_function(name)) {
        return nullptr;
    }
    struct Entry {
        std::string_view name;
        PFN_vkVoidFunction function;
    };
    static const Entry entries[] = {
        {"vkGetInstanceProcAddr", reinterpret_cast<PFN_vkVoidFunction>(layer_get_instance_proc_addr)},
        {"vkGetDeviceProcAddr", reinterpret_cast<PFN_vkVoidFunction>(layer_get_device_proc_addr)},
        {"vkCreateInstance", reinterpret_cast<PFN_vkVoidFunction>(layer_create_instance)},
        {"vkDestroyInstance", reinterpret_cast<PFN_vkVoidFunction>(layer_destroy_instance)},
        {"vkCreateDevice", reinterpret_cast<PFN_vkVoidFunction>(layer_create_device)},
        {"vkDestroyDevice", reinterpret_cast<PFN_vkVoidFunction>(layer_destroy_device)},
        {"vkQueueSubmit", reinterpret_cast<PFN_vkVoidFunction>(layer_queue_submit)},
        {"vkQueueSubmit2", reinterpret_cast<PFN_vkVoidFunction>(layer_queue_submit2)},
        {"vkQueueSubmit2KHR", reinterpret_cast<PFN_vkVoidFunction>(layer_queue_submit2_khr)},
        {"vkQueueWaitIdle", reinterpret_cast<PFN_vkVoidFunction>(layer_queue_wait_idle)},
        {"vkQueueBindSparse", reinterpret_cast<PFN_vkVoidFunction>(layer_queue_bind_sparse)},
        {"vkQueuePresentKHR", reinterpret_cast<PFN_vkVoidFunction>(layer_queue_present)},
    };
    for (const Entry& entry : entries) {
        if (entry.name == name) {
            return entry.function;
        }
    }
    return nullptr;
}

// The global commands asked of this layer with no instance (V556). A layer
// above may ask its next link for them: KHARVOX (DOOM 2016) asks for
// vkEnumerateInstanceExtensionProperties from inside its vkCreateInstance and,
// answered with nullptr, fails the instance, which DOOM takes to its fatal
// error (0xC0000409). Before an instance exists there is no next link to
// chain to, so they go to the loader's own exports, which answer for the
// whole chain. A process whose loader has another name keeps nullptr.
[[nodiscard]] PFN_vkVoidFunction loader_global(const char* name) noexcept {
    static constexpr std::string_view kGlobals[] = {
        "vkEnumerateInstanceExtensionProperties",
        "vkEnumerateInstanceLayerProperties",
        "vkEnumerateInstanceVersion",
    };
    if (std::find(std::begin(kGlobals), std::end(kGlobals), std::string_view(name)) ==
        std::end(kGlobals)) {
        return nullptr;
    }
    const HMODULE loader = GetModuleHandleW(L"vulkan-1.dll");
    return loader != nullptr
        ? reinterpret_cast<PFN_vkVoidFunction>(GetProcAddress(loader, name))
        : nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_get_device_proc_addr(
    VkDevice device, const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    if (const auto function = intercepted(name); function != nullptr) {
        // A device that the layer did not see created has nothing to chain
        // to for these; leave them to the next layer.
        const auto table = device != VK_NULL_HANDLE ? device_table(dispatch_key(device)) : nullptr;
        if (table || std::strcmp(name, "vkGetDeviceProcAddr") == 0) {
            return function;
        }
    }
    if (device == VK_NULL_HANDLE) {
        return nullptr;
    }
    const auto table = device_table(dispatch_key(device));
    return table && table->get_device_proc_addr
        ? table->get_device_proc_addr(device, name)
        : nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_get_instance_proc_addr(
    VkInstance instance, const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    if (const auto function = intercepted(name); function != nullptr) {
        return function;
    }
    if (instance == VK_NULL_HANDLE) {
        return loader_global(name);
    }
    const InstanceTable table = instance_table(dispatch_key(instance));
    return table.get_instance_proc_addr != nullptr
        ? table.get_instance_proc_addr(instance, name)
        : nullptr;
}

}  // namespace

extern "C" __declspec(dllexport) VKAPI_ATTR VkResult VKAPI_CALL
OFXR_vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version) {
    if (version == nullptr ||
        version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT ||
        version->loaderLayerInterfaceVersion < 1) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (version->loaderLayerInterfaceVersion > CURRENT_LOADER_LAYER_INTERFACE_VERSION) {
        version->loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION;
    }
    if (version->loaderLayerInterfaceVersion >= 2) {
        version->pfnGetInstanceProcAddr = layer_get_instance_proc_addr;
        version->pfnGetDeviceProcAddr = layer_get_device_proc_addr;
        version->pfnGetPhysicalDeviceProcAddr = nullptr;
    }
    return VK_SUCCESS;
}

// V572: the machine-wide copy's negotiation, for games run as administrator.
extern "C" __declspec(dllexport) VKAPI_ATTR VkResult VKAPI_CALL
OFXR_vkNegotiateLoaderLayerInterfaceVersionElevated(VkNegotiateLayerInterface* version) {
    static std::once_flag decided;
    std::call_once(decided, [] {
        bool active = false;
        try {
            HMODULE module = nullptr;
            std::wstring path(32768, L'\0');
            if (xrfg::implicit_layer::process_is_elevated() &&
                GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&OFXR_vkNegotiateLoaderLayerInterfaceVersionElevated),
                    &module)) {
                const DWORD length = GetModuleFileNameW(
                    module, path.data(), static_cast<DWORD>(path.size()));
                if (length != 0 && length < path.size()) {
                    path.resize(length);
                    active = !xrfg::implicit_layer::armed_directory_matching(
                        path,
                        xrfg::implicit_layer::kVulkanRegistrySubkey,
                        xrfg::implicit_layer::kVulkanManifestPrefix).empty();
                }
            }
        } catch (...) {
            active = false;
        }
        g_inert.store(!active, std::memory_order_release);
    });
    return OFXR_vkNegotiateLoaderLayerInterfaceVersion(version);
}

// V572, for the tests: whether this copy decided to stay inert.
extern "C" __declspec(dllexport) int OFXR_vkLayerInert() {
    return g_inert.load(std::memory_order_acquire) ? 1 : 0;
}

// The loader's fallback for a manifest without a negotiation function; kept
// so a loader older than interface version 2 still finds the layer.
extern "C" __declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
OFXR_vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    return layer_get_instance_proc_addr(instance, name);
}

extern "C" __declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
OFXR_vkGetDeviceProcAddr(VkDevice device, const char* name) {
    return layer_get_device_proc_addr(device, name);
}

// Guide submissions (V513): see the note at the top. Registering a command
// buffer again for the same semaphore raises the value to signal - a later
// copy in the same buffer, whose signal covers the earlier one. Forgetting
// drops a semaphore's registrations that were never submitted.
extern "C" __declspec(dllexport) void
OFXR_vkRegisterGuideSubmission(
    VkCommandBuffer buffer, VkSemaphore semaphore, std::uint64_t value,
    volatile LONG64* submitted) {
    if (buffer == VK_NULL_HANDLE || semaphore == VK_NULL_HANDLE) return;
    try {
        std::scoped_lock lock(g_guide_mutex);
        auto& registrations = g_guides[buffer];
        for (GuideRegistration& registration : registrations) {
            if (registration.semaphore == semaphore) {
                registration.value = std::max(registration.value, value);
                registration.submitted = submitted;
                return;
            }
        }
        registrations.push_back({semaphore, value, submitted});
        g_guide_count.fetch_add(1, std::memory_order_acq_rel);
    } catch (...) {
    }
}

extern "C" __declspec(dllexport) void
OFXR_vkForgetGuideSubmissions(VkSemaphore semaphore) {
    try {
        std::scoped_lock lock(g_guide_mutex);
        for (auto entry = g_guides.begin(); entry != g_guides.end();) {
            auto& registrations = entry->second;
            const auto before = registrations.size();
            std::erase_if(registrations, [&](const GuideRegistration& registration) {
                return registration.semaphore == semaphore;
            });
            g_guide_count.fetch_sub(
                static_cast<std::uint32_t>(before - registrations.size()),
                std::memory_order_acq_rel);
            entry = registrations.empty() ? g_guides.erase(entry) : std::next(entry);
        }
    } catch (...) {
    }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Nothing happens per thread.
        return TRUE;
    }
    return TRUE;
}
