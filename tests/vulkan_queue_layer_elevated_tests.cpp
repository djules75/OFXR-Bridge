// V572: the Vulkan queue layer's machine-wide copy, for games run as
// administrator, is inert in any other process. The Vulkan loader does not
// drop a layer that refuses negotiation - it falls back to the manifest's
// vkGetInstanceProcAddr and inserts it anyway - so the copy stays in the chain
// and must hand out the next link's queue functions instead of its own.
// Checked through the real loader (vulkan-1.dll) with the layer added as an
// implicit layer by VK_ADD_IMPLICIT_LAYER_PATH, in this test's own, unelevated
// process: the copy's vkQueueSubmit must not be the layer's. The same DLL
// under the ordinary negotiation is the control, whose vkQueueSubmit is.

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kSkipped = 77;

[[nodiscard]] std::string json_path(const std::filesystem::path& path) {
    std::string output;
    for (const char character : path.string()) {
        if (character == '\\') output += "\\\\";
        else output += character;
    }
    return output;
}

void write_manifest(const std::filesystem::path& directory,
    const std::filesystem::path& layer, const char* name, const char* negotiation) {
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "layer.json", std::ios::binary)
        << "{\n  \"file_format_version\": \"1.2.0\",\n  \"layer\": {\n"
        << "    \"name\": \"" << name << "\",\n    \"type\": \"GLOBAL\",\n"
        << "    \"library_path\": \"" << json_path(layer) << "\",\n"
        << "    \"api_version\": \"1.3.296\",\n    \"implementation_version\": \"1\",\n"
        << "    \"description\": \"OFXR elevated-copy test\",\n"
        << "    \"functions\": {\n"
        << "      \"vkNegotiateLoaderLayerInterfaceVersion\": \"" << negotiation << "\",\n"
        << "      \"vkGetInstanceProcAddr\": \"OFXR_vkGetInstanceProcAddr\",\n"
        << "      \"vkGetDeviceProcAddr\": \"OFXR_vkGetDeviceProcAddr\"\n"
        << "    },\n"
        << "    \"disable_environment\": { \"OFXR_TEST_DISABLE\": \"1\" }\n"
        << "  }\n}\n";
}

enum class Outcome { failed, no_driver, layer_submit, other_submit };

[[nodiscard]] HMODULE module_of(const void* address) {
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        static_cast<LPCWSTR>(address), &module);
    return module;
}

// An instance and a device with the manifest in `directory` added; whose
// vkQueueSubmit the device hands out.
[[nodiscard]] Outcome probe(PFN_vkGetInstanceProcAddr gipa,
    const std::filesystem::path& directory, HMODULE* layer_module,
    const std::filesystem::path& layer, int* inert) {
    SetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH", directory.c_str());
    const auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(
        gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &application;
    VkInstance instance = VK_NULL_HANDLE;
    const VkResult created = create_instance(&instance_info, nullptr, &instance);
    SetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH", nullptr);
    if (created == VK_ERROR_INCOMPATIBLE_DRIVER) return Outcome::no_driver;
    if (created != VK_SUCCESS) return Outcome::failed;
    *layer_module = GetModuleHandleW(layer.c_str());
    // Asked while the instance holds the DLL; the loader unloads it after.
    if (*layer_module != nullptr) {
        if (const auto query = reinterpret_cast<int (*)()>(
                GetProcAddress(*layer_module, "OFXR_vkLayerInert"))) {
            *inert = query();
        }
    }

    const auto destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(
        gipa(instance, "vkDestroyInstance"));
    const auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        gipa(instance, "vkEnumeratePhysicalDevices"));
    const auto families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    const auto create_device = reinterpret_cast<PFN_vkCreateDevice>(
        gipa(instance, "vkCreateDevice"));
    const auto gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        gipa(instance, "vkGetDeviceProcAddr"));
    Outcome outcome = Outcome::failed;
    std::uint32_t count = 0;
    if (enumerate(instance, &count, nullptr) == VK_SUCCESS && count != 0) {
        std::vector<VkPhysicalDevice> devices(count);
        enumerate(instance, &count, devices.data());
        std::uint32_t family_count = 0;
        families(devices[0], &family_count, nullptr);
        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = 0;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue;
        VkDevice device = VK_NULL_HANDLE;
        if (family_count != 0 &&
            create_device(devices[0], &device_info, nullptr, &device) == VK_SUCCESS) {
            const auto submit = gdpa(device, "vkQueueSubmit");
            outcome = submit != nullptr && *layer_module != nullptr &&
                    module_of(reinterpret_cast<const void*>(submit)) == *layer_module
                ? Outcome::layer_submit
                : Outcome::other_submit;
            reinterpret_cast<PFN_vkDestroyDevice>(gdpa(device, "vkDestroyDevice"))(
                device, nullptr);
        } else {
            outcome = Outcome::no_driver;
        }
    }
    destroy_instance(instance, nullptr);
    return outcome;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <OFXR_vulkan_queue_layer.dll>\n", argv[0]);
        return 1;
    }
    // The tray's own per-user registration may be armed on this machine; it
    // is another copy of the same layer and stays out of this process.
    SetEnvironmentVariableW(L"OFXR_DISABLE_VULKAN_QUEUE_LAYER", L"1");
    const std::filesystem::path layer = std::filesystem::absolute(argv[1]);
    const HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (loader == nullptr) {
        std::puts("no Vulkan loader: skipped");
        return kSkipped;
    }
    const auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(loader, "vkGetInstanceProcAddr"));
    if (gipa == nullptr) {
        std::fputs("the loader exports no vkGetInstanceProcAddr\n", stderr);
        return 1;
    }
    const auto root = std::filesystem::temp_directory_path() /
        ("ofxr-vulkan-elevated-" + std::to_string(GetCurrentProcessId()));
    write_manifest(root / "elevated", layer, "VK_LAYER_OFXR_test_elevated",
        "OFXR_vkNegotiateLoaderLayerInterfaceVersionElevated");
    write_manifest(root / "ordinary", layer, "VK_LAYER_OFXR_test_ordinary",
        "OFXR_vkNegotiateLoaderLayerInterfaceVersion");

    // The machine-wide copy first: the inert decision is the DLL's, once.
    HMODULE elevated_module = nullptr;
    int inert = -1;
    const Outcome elevated = probe(gipa, root / "elevated", &elevated_module, layer, &inert);
    std::error_code ignored;
    std::filesystem::remove_all(root / "elevated", ignored);
    // The control loads the same DLL afresh after the copy has unloaded it.
    HMODULE ordinary_module = nullptr;
    int ordinary_inert = -1;
    const Outcome ordinary = GetModuleHandleW(layer.c_str()) == nullptr
        ? probe(gipa, root / "ordinary", &ordinary_module, layer, &ordinary_inert)
        : Outcome::failed;
    std::filesystem::remove_all(root, ignored);

    if (elevated == Outcome::no_driver && ordinary == Outcome::no_driver) {
        std::puts("no Vulkan device: skipped");
        return kSkipped;
    }
    std::printf("machine-wide copy: outcome %d, inert %d\ncontrol: outcome %d\n",
        static_cast<int>(elevated), inert, static_cast<int>(ordinary));
    if (elevated != Outcome::other_submit || inert != 1) {
        std::fputs("the machine-wide copy acted outside an elevated game\n", stderr);
        return 1;
    }
    if (ordinary != Outcome::layer_submit) {
        std::fputs("the control layer did not serialise: the probe proves nothing\n", stderr);
        return 1;
    }
    return 0;
}
