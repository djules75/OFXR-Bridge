#include "xrfg/vulkan_native_ingress.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>

int main() {
    HMODULE loader=LoadLibraryW(L"vulkan-1.dll");
    if(!loader){std::cerr<<"Vulkan loader unavailable\n";return 1;}
    // Copy actual loader entry bytes before polling repeatedly with valid API
    // addresses available. The shipping stubs must neither patch nor call them.
    const char* names[]{"vkGetDeviceProcAddr","vkGetInstanceProcAddr","vkCreateImage",
        "vkDestroyImage","vkCreateDevice","vkDestroyDevice","vkCmdDraw","vkQueueSubmit"};
    unsigned char before[8][16]{};
    FARPROC addresses[8]{};
    for(unsigned i=0;i<8;++i){addresses[i]=GetProcAddress(loader,names[i]);if(!addresses[i])return 2;memcpy(before[i],addresses[i],16);}
    for(unsigned i=0;i<100;++i)xrfg::native_vulkan::poll_capture();
    // Invalid opaque owner pointers deliberately detect any attempted transport
    // setup. A suspended ingress has no ownership and never dereferences them.
    xrfg::VulkanSessionBinding binding{};binding.device=reinterpret_cast<VkDevice>(1);
    xrfg::native_vulkan::configure_owner(binding,reinterpret_cast<ID3D12Device*>(2),reinterpret_cast<ID3D12CommandQueue*>(3));
    xrfg::native_vulkan::stop_owner(binding.device);
    for(unsigned i=0;i<8;++i)if(memcmp(before[i],addresses[i],16)){std::cerr<<"Loader hook installed: "<<names[i]<<'\n';return 3;}
    FreeLibrary(loader);
    std::cout<<"Shipping Vulkan capture stays suspended: loader entries unchanged, no owner dereference or private transport\n";
    return 0;
}
