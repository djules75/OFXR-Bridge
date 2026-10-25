#pragma once
#include <d3d12.h>
#include <atomic>
#include <vector>
#include <mutex>

namespace xrfg {
// The pinned provider's D3D12 factories return owning COM references.
// Its private module has no shutdown export. Observe resource destruction
// without retaining the resource, then retire only surviving factory references
// after the isolated provider is unloaded and its GPU work has completed.
class AfwProviderAllocations {
    static constexpr GUID lifetime_id{0xa68797e4,0x2a29,0x4f92,{0xa9,0xeb,0x7e,0x8f,0x46,0xbe,0xcb,0x24}};
    struct Lifetime final : IUnknown {
        std::atomic<ULONG> references{1};
        std::atomic_bool attached{};
        ID3D12Object* resource{}; // weak; attached is cleared by resource destruction
        explicit Lifetime(ID3D12Object* value) noexcept:resource(value) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** output) override {
            if(!output)return E_POINTER;*output=nullptr;
            if(id!=__uuidof(IUnknown))return E_NOINTERFACE;
            *output=static_cast<IUnknown*>(this);AddRef();return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
        ULONG STDMETHODCALLTYPE Release() override {
            const auto left=--references;
            if(left==1)attached.store(false,std::memory_order_release);
            if(!left)delete this;
            return left;
        }
    };
    std::mutex mutex_;
    std::vector<Lifetime*> allocations_;
public:
    AfwProviderAllocations()=default;
    AfwProviderAllocations(const AfwProviderAllocations&)=delete;
    ~AfwProviderAllocations(){for(auto* item:allocations_)item->Release();}
    HRESULT observe(ID3D12Object* resource) noexcept {
        if(!resource)return E_POINTER;
        try {
            std::scoped_lock lock(mutex_);
            for(auto* item:allocations_)if(item->resource==resource && item->attached.load(std::memory_order_acquire))return S_OK;
            auto* item=new Lifetime(resource);
            try {allocations_.push_back(item);}catch(...){item->Release();throw;}
            const auto result=resource->SetPrivateDataInterface(lifetime_id,item);
            if(FAILED(result)){allocations_.pop_back();item->Release();return result;}
            item->attached.store(true,std::memory_order_release);
            return S_OK;
        }catch(...){return E_OUTOFMEMORY;}
    }
    unsigned retire() noexcept {
        std::scoped_lock lock(mutex_);
        unsigned retired{};
        for(auto i=allocations_.rbegin();i!=allocations_.rend();++i)if(auto* item=*i;item->attached.load(std::memory_order_acquire)) {
            item->resource->Release();++retired;
            // Release at most the factory reference; never drain foreign refs.
            item->attached.store(false,std::memory_order_release);
        }
        return retired;
    }
};
} // namespace xrfg
