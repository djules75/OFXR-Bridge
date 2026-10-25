#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <cstddef>
#include <cstring>
#include <algorithm>

namespace xrfg::native_vulkan {
namespace {
size_t device_chain_size(VkStructureType type) {
#include "vulkan_device_chain_sizes.inc"
    return 0;
}
// Own only the prefix that needs a feature edit. Unknown nodes are never
// guessed/copied, and all unedited pointer payloads remain borrowed for the
// synchronous vkCreateDevice call. Caller-owned pNext and features stay intact.
struct InteropDeviceRequest {
    VkDeviceCreateInfo info{};
    std::vector<const char*> extensions;
    std::vector<std::vector<std::max_align_t>> prefix;
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    bool prepare(const VkDeviceCreateInfo& original,const std::vector<VkExtensionProperties>& supported,bool timeline_supported) {
        info=original;
        if(!timeline_supported)return false;
        const auto available=[&](const char* name){return std::any_of(supported.begin(),supported.end(),[&](const auto& p){return !std::strcmp(p.extensionName,name);});};
        if(!available(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME)||!available(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME))return false;
        std::vector<const VkBaseInStructure*> nodes;
        const VkBaseInStructure* target{};bool enabled{};
        for(auto* node=static_cast<const VkBaseInStructure*>(original.pNext);node;node=node->pNext) {
            if(nodes.size()>=256||std::find(nodes.begin(),nodes.end(),node)!=nodes.end())return false;
            nodes.push_back(node);
            if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
                if(target)return false;target=node;enabled=reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(node)->timelineSemaphore!=0;
            }else if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES) {
                if(target)return false;target=node;enabled=reinterpret_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(node)->timelineSemaphore!=0;
            }
        }
        if(!enabled&&target) {
            const auto count=static_cast<size_t>(std::find(nodes.begin(),nodes.end(),target)-nodes.begin())+1;
            prefix.reserve(count);
            for(size_t i=0;i<count;++i) {
                const auto size=device_chain_size(nodes[i]->sType);if(!size)return false;
                prefix.emplace_back((size+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));std::memcpy(prefix.back().data(),nodes[i],size);
            }
            for(size_t i=0;i+1<count;++i)reinterpret_cast<VkBaseOutStructure*>(prefix[i].data())->pNext=reinterpret_cast<VkBaseOutStructure*>(prefix[i+1].data());
            auto* edited=reinterpret_cast<VkBaseOutStructure*>(prefix.back().data());
            if(target->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(edited)->timelineSemaphore=VK_TRUE;
            else reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(edited)->timelineSemaphore=VK_TRUE;
            info.pNext=prefix.front().data();
        }else if(!enabled) {timeline.timelineSemaphore=VK_TRUE;timeline.pNext=const_cast<void*>(original.pNext);info.pNext=&timeline;}
        if(original.enabledExtensionCount)extensions.assign(original.ppEnabledExtensionNames,original.ppEnabledExtensionNames+original.enabledExtensionCount);
        // The promoted base extensions are also requested when advertised, so
        // the Win32 dependencies work with native Vulkan 1.0/1.1 applications.
        const char* required[]{VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
            VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME};
        for(const auto* name:required)if(available(name)&&std::none_of(extensions.begin(),extensions.end(),[&](const auto* p){return !std::strcmp(p,name);}))extensions.push_back(name);
        info.enabledExtensionCount=static_cast<uint32_t>(extensions.size());info.ppEnabledExtensionNames=extensions.data();return true;
    }
};
}
}
