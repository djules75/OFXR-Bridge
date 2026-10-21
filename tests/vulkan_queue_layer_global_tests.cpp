// The queue layer's answer for the global commands asked with no instance
// (V556): the loader's own exports, as a layer above it needs when it asks
// its next link before any instance exists (KHARVOX, DOOM 2016). Other
// commands asked with no instance stay unanswered.

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main(int argc, char** argv) {
    require(argc >= 2, "usage: vulkan_queue_layer_global_tests <OFXR_vulkan_queue_layer.dll>");
    // The loader's own answers must not depend on what the tray has armed.
    SetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", L"~implicit~");
    const HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (loader == nullptr) {
        std::cout << "SKIP: no Vulkan loader on this machine\n";
        return 0;
    }
    const HMODULE layer = LoadLibraryA(argv[1]);
    require(layer != nullptr, "loading the queue layer");
    const auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(layer, "OFXR_vkGetInstanceProcAddr"));
    require(gipa != nullptr, "the layer's vkGetInstanceProcAddr export");

    for (const char* name : {"vkEnumerateInstanceExtensionProperties",
                             "vkEnumerateInstanceLayerProperties",
                             "vkEnumerateInstanceVersion"}) {
        const auto expected = reinterpret_cast<PFN_vkVoidFunction>(GetProcAddress(loader, name));
        if (gipa(VK_NULL_HANDLE, name) != expected) {
            std::cerr << "FAILED: " << name << " is not the loader's export\n";
            return 1;
        }
    }
    require(gipa(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties") != nullptr,
        "vkEnumerateInstanceExtensionProperties unanswered");

    // What KHARVOX does with it: count, then fill.
    const auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        gipa(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
    std::uint32_t count = 0;
    require(enumerate(nullptr, &count, nullptr) == VK_SUCCESS && count > 0,
        "vkEnumerateInstanceExtensionProperties through the layer");

    // Unchanged: an instance command asked with no instance has nothing to
    // chain to, and the layer's own entry points are still its own.
    require(gipa(VK_NULL_HANDLE, "vkEnumeratePhysicalDevices") == nullptr,
        "an instance command answered with no instance");
    require(gipa(VK_NULL_HANDLE, "vkCreateInstance") != nullptr &&
            gipa(VK_NULL_HANDLE, "vkCreateInstance") !=
                reinterpret_cast<PFN_vkVoidFunction>(GetProcAddress(loader, "vkCreateInstance")),
        "vkCreateInstance is not the layer's own");

    std::cout << "vulkan_queue_layer_global_tests: OK (" << count << " instance extensions)\n";
    return 0;
}
