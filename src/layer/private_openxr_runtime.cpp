#include "xrfg/private_openxr_runtime.hpp"
#include "xrfg/private_output_distributor.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>
#include <wrl/client.h>
#include <d3d11.h>

namespace xrfg {
namespace {
using Microsoft::WRL::ComPtr;
std::int64_t clock_ns() noexcept {
    LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
    return q.QuadPart/f.QuadPart*1'000'000'000LL+
        q.QuadPart%f.QuadPart*1'000'000'000LL/f.QuadPart;
}
XrResult xr(HRESULT result) {return SUCCEEDED(result)?XR_SUCCESS:XR_ERROR_RUNTIME_FAILURE;}
template<class T> bool valid(const T* value,XrStructureType type) {
    return !value || (value->type==type && !value->next);
}
using Layer=std::variant<XrCompositionLayerProjection,XrCompositionLayerQuad,
    XrCompositionLayerCubeKHR,XrCompositionLayerCylinderKHR,
    XrCompositionLayerEquirectKHR,XrCompositionLayerEquirect2KHR>;
struct LayerCopy {
    Layer value;
    std::vector<XrCompositionLayerProjectionView> views;
    std::vector<XrCompositionLayerDepthInfoKHR> depths;
    std::optional<XrCompositionLayerColorScaleBiasKHR> color_scale;
    const XrCompositionLayerBaseHeader* header() {
        return std::visit([](auto& x){return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&x);},value);
    }
};
} // namespace

struct PrivateOpenXrRuntime::Impl {
    struct Image {
        ComPtr<ID3D12Resource> d12;
        ComPtr<ID3D11Texture2D> d11;
        unsigned readers{};
        bool acquired{},waited{};
    };
    struct Chain {
        XrSwapchain handle{};
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        std::vector<Image> images;
        std::vector<ComPtr<ID3D12Resource>> real12;
        std::vector<ComPtr<ID3D11Texture2D>> real11;
        std::deque<unsigned> acquired;
        std::optional<unsigned> released;
        unsigned cursor{};
        bool destroying{};
        unsigned prepared_users{};
        std::optional<unsigned> prepared_source;
        D3D12_RESOURCE_STATES image_state{D3D12_RESOURCE_STATE_RENDER_TARGET};
    };
    struct Lease {std::shared_ptr<Chain> chain;unsigned image{};};
    struct Packet {
        XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};
        std::vector<LayerCopy> copies;
        std::vector<const XrCompositionLayerBaseHeader*> layers;
        std::vector<Lease> leases;
        std::vector<std::shared_ptr<Chain>> prepared_chains;
        std::vector<Lease> copies_needed;
        std::uint64_t sequence{},ready{};
        std::int64_t accepted_at{},source_ready_at{},prepared_at{};
    };
    Options options;
    std::mutex mutex,accept_mutex,copy_mutex,stop_mutex;
    std::condition_variable changed;
    std::unordered_map<XrSwapchain,std::shared_ptr<Chain>> chains;
    std::unique_ptr<PrivateOutputFifo<std::shared_ptr<Packet>>> fifo;
    std::thread consumer,preparer;
    std::deque<std::shared_ptr<Packet>> prepared;
    std::atomic_bool stopping{true};
    XrResult failure{XR_SUCCESS};
    XrFrameState anchor{XR_TYPE_FRAME_STATE};
    std::int64_t anchor_at{};
    XrTime virtual_time{};
    unsigned waits{},begun{};
    bool seeded{},running{};
    std::uint64_t sequence{},ready_serial{},copy_serial{};
    std::size_t pending{};
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue12;
    ComPtr<ID3D12CommandQueue> producer_queue12;
    ComPtr<ID3D12Fence> ready12,copy12;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D11Device5> device11;
    ComPtr<ID3D11DeviceContext4> context11;
    ComPtr<ID3D11Fence> ready11,copy11;
    HANDLE fence_event{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    HANDLE timer{CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS)};
    explicit Impl(Options input):options(std::move(input)) {}
    ~Impl(){if(fence_event)CloseHandle(fence_event);if(timer)CloseHandle(timer);}
    bool d12() const noexcept {return device12!=nullptr;}
    std::int64_t period(XrDuration fallback) {
        auto value=options.physical_period?options.physical_period():0;
        return value>=2'000'000 && value<=50'000'000?value:
            std::clamp<std::int64_t>(fallback,2'000'000,50'000'000);
    }
    void trace(int stage,std::uint64_t seq,std::int64_t hold=0) {
        if(tracing())options.trace(stage,seq,clock_ns(),hold);
    }
    bool tracing() const {return options.trace && (!options.trace_enabled || options.trace_enabled());}
    template<class Fence> HRESULT await(Fence* fence,std::uint64_t value,bool interruptible=true) {
        if(!value)return S_OK;
        auto completed=fence->GetCompletedValue();
        if(completed==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
        if(completed>=value)return S_OK;
        HRESULT result=fence->SetEventOnCompletion(value,fence_event);
        if(FAILED(result))return result;
        while(fence->GetCompletedValue()<value) {
            if(interruptible && stopping)return HRESULT_FROM_WIN32(ERROR_CANCELLED);
            if(fence->GetCompletedValue()==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
            const auto waited=WaitForSingleObject(fence_event,5);
            if(waited!=WAIT_OBJECT_0 && waited!=WAIT_TIMEOUT)return E_FAIL;
        }
        return fence->GetCompletedValue()==UINT64_MAX?DXGI_ERROR_DEVICE_REMOVED:S_OK;
    }
    bool sleep_until(std::int64_t target,std::int64_t fine_window) {
        for(;;) {
            if(stopping)return false;
            auto left=target-clock_ns();if(left<=0)return true;
            // Let the timer do the long wait, waking before the deadline.
            // Its positive wakeup jitter must not be added to EVERY period.
            // The final bounded window uses QPC/pause on this consumer only;
            // no producer, GPU or runtime-entry lock is held here.
            if(left<=fine_window){YieldProcessor();continue;}
            left=std::min<std::int64_t>(left-fine_window,5'000'000);
            LARGE_INTEGER due{};due.QuadPart=-std::max<std::int64_t>(1,(left+99)/100);
            if(timer && SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(timer,10);
            else {std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::nanoseconds(left));}
        }
    }
    void fail(XrResult result) {
        {std::scoped_lock lock(mutex);if(XR_SUCCEEDED(failure))failure=result;}
        trace(-1,static_cast<std::uint64_t>(-result));
        stopping=true;fifo->stop();changed.notify_all();
    }
    void retire(const std::shared_ptr<Packet>& packet) {
        {std::scoped_lock lock(mutex);
         for(auto& lease:packet->leases)if(lease.chain->images[lease.image].readers)
             --lease.chain->images[lease.image].readers;
         for(auto& chain:packet->prepared_chains)if(chain->prepared_users && !--chain->prepared_users)
             chain->prepared_source.reset();
         if(pending)--pending;}
        changed.notify_all();
    }
    XrResult lease(Packet& packet,XrSwapchain handle) {
        if(std::any_of(packet.leases.begin(),packet.leases.end(),[&](auto& x){return x.chain->handle==handle;}))return XR_SUCCESS;
        auto it=chains.find(handle);
        if(it==chains.end() || it->second->destroying)return XR_ERROR_HANDLE_INVALID;
        auto& chain=*it->second;
        if(!chain.released || chain.images[*chain.released].acquired)return XR_ERROR_CALL_ORDER_INVALID;
        packet.leases.push_back({it->second,*chain.released});
        ++chain.images[*chain.released].readers;return XR_SUCCESS;
    }
    // Called under mutex. Every pointer submitted after EndFrame returns is
    // owned here. Unknown extension chains are refused, never borrowed.
    XrResult own(Packet& packet,const XrFrameEndInfo& source) {
        if(source.next || (source.layerCount && !source.layers))return XR_ERROR_VALIDATION_FAILURE;
        packet.info=source;packet.copies.reserve(source.layerCount);packet.layers.reserve(source.layerCount);
        auto take=[&](XrSwapchain h){return lease(packet,h);};
        for(unsigned i=0;i<source.layerCount;++i) {
            auto* header=source.layers[i];if(!header)return XR_ERROR_LAYER_INVALID;
            packet.copies.emplace_back();auto& copy=packet.copies.back();
            if(header->next){auto* next=static_cast<const XrCompositionLayerColorScaleBiasKHR*>(header->next);
                if(next->type!=XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR || next->next)return XR_ERROR_LAYER_INVALID;
                copy.color_scale=*next;}
            XrResult result=XR_SUCCESS;
            switch(header->type) {
            case XR_TYPE_COMPOSITION_LAYER_PROJECTION: {
                auto p=*reinterpret_cast<const XrCompositionLayerProjection*>(header);
                if(!p.views || !p.viewCount)return XR_ERROR_LAYER_INVALID;
                copy.views.assign(p.views,p.views+p.viewCount);copy.depths.resize(p.viewCount);
                for(unsigned v=0;v<p.viewCount;++v) {
                    auto& view=copy.views[v];result=take(view.subImage.swapchain);if(XR_FAILED(result))return result;
                    if(view.next) {
                        auto* ext=static_cast<const XrCompositionLayerDepthInfoKHR*>(view.next);
                        if(ext->type!=XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR || ext->next)return XR_ERROR_LAYER_INVALID;
                        copy.depths[v]=*ext;view.next=&copy.depths[v];
                        result=take(ext->subImage.swapchain);if(XR_FAILED(result))return result;
                    }
                }
                p.views=copy.views.data();copy.value=p;break;
            }
#define COPY_LAYER(tag,type,handle) case tag: {auto value=*reinterpret_cast<const type*>(header);result=take(value.handle);copy.value=value;break;}
            COPY_LAYER(XR_TYPE_COMPOSITION_LAYER_QUAD,XrCompositionLayerQuad,subImage.swapchain)
            COPY_LAYER(XR_TYPE_COMPOSITION_LAYER_CUBE_KHR,XrCompositionLayerCubeKHR,swapchain)
            COPY_LAYER(XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR,XrCompositionLayerCylinderKHR,subImage.swapchain)
            COPY_LAYER(XR_TYPE_COMPOSITION_LAYER_EQUIRECT_KHR,XrCompositionLayerEquirectKHR,subImage.swapchain)
            COPY_LAYER(XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR,XrCompositionLayerEquirect2KHR,subImage.swapchain)
#undef COPY_LAYER
            default:return XR_ERROR_LAYER_INVALID;
            }
            if(XR_FAILED(result))return result;
            if(copy.color_scale)std::visit([&](auto& layer){layer.next=&*copy.color_scale;},copy.value);
            packet.layers.push_back(copy.header());
        }
        packet.info.layers=packet.layers.data();return XR_SUCCESS;
    }
    // Only the preparer touches real image acquisition/copies. A real chain
    // cannot be overwritten until every queued presentation using its current
    // contents has completed EndFrame. Repeated immutable private images can
    // share those contents (e.g. an unchanged overlay) without another copy.
    bool reserve_preparation(Packet& packet) {
        std::unique_lock lock(mutex);
        for(auto& lease:packet.leases) {
            auto chain=lease.chain;
            changed.wait(lock,[&]{return stopping || !chain->prepared_users || chain->prepared_source==lease.image;});
            if(stopping)return false;
            if(!chain->prepared_users) {
                chain->prepared_source=lease.image;
                packet.copies_needed.push_back(lease);
            }
            ++chain->prepared_users;packet.prepared_chains.push_back(std::move(chain));
        }
        return true;
    }
    XrResult publish(Packet& packet) {
        const bool diagnostic=tracing();
        struct Target {Lease* lease;unsigned index;bool waited{},released{};};
        std::vector<Target> targets;targets.reserve(packet.copies_needed.size());
        XrResult result=XR_SUCCESS;
        for(auto& lease:packet.copies_needed) {
            unsigned index{};XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            result=options.real.acquire(lease.chain->handle,&acquire,&index);if(XR_FAILED(result))break;
            targets.push_back({&lease,index});
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
            result=options.real.wait_image(lease.chain->handle,&wait);if(XR_FAILED(result))break;
            targets.back().waited=true;
        }
        if(diagnostic)trace(12,packet.sequence);
        if(XR_SUCCEEDED(result) && !targets.empty()) {
            std::scoped_lock gpu(copy_mutex);
            HRESULT copied=S_OK;
            if(d12()) {
                copied=allocator->Reset();if(SUCCEEDED(copied))copied=list->Reset(allocator.Get(),nullptr);
                for(auto& target:targets) {
                    if(FAILED(copied))break;
                    auto& chain=*target.lease->chain;auto& image=chain.images[target.lease->image];
                    if(target.index>=chain.real12.size()){copied=E_INVALIDARG;break;}
                    const auto state=chain.image_state;
                    D3D12_RESOURCE_BARRIER barriers[2]{};
                    for(auto& b:barriers){b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
                    barriers[0].Transition.pResource=image.d12.Get();barriers[0].Transition.StateBefore=state;barriers[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
                    barriers[1].Transition.pResource=chain.real12[target.index].Get();barriers[1].Transition.StateBefore=state;barriers[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
                    list->ResourceBarrier(2,barriers);list->CopyResource(chain.real12[target.index].Get(),image.d12.Get());
                    for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(2,barriers);
                }
                if(SUCCEEDED(copied))copied=list->Close();
                if(SUCCEEDED(copied)){ID3D12CommandList* lists[]={list.Get()};queue12->ExecuteCommandLists(1,lists);copied=queue12->Signal(copy12.Get(),copy_serial+1);if(SUCCEEDED(copied))++copy_serial;}
                if(diagnostic)trace(13,packet.sequence);
                if(SUCCEEDED(copied))copied=await(copy12.Get(),copy_serial,false);
            } else {
                for(auto& target:targets) {
                    auto& chain=*target.lease->chain;
                    if(target.index>=chain.real11.size()){copied=E_INVALIDARG;break;}
                    context11->CopyResource(chain.real11[target.index].Get(),chain.images[target.lease->image].d11.Get());
                }
                if(SUCCEEDED(copied)){copied=context11->Signal(copy11.Get(),copy_serial+1);if(SUCCEEDED(copied))++copy_serial;}
                context11->Flush();if(diagnostic)trace(13,packet.sequence);
                if(SUCCEEDED(copied))copied=await(copy11.Get(),copy_serial,false);
            }
            if(diagnostic)trace(14,packet.sequence);
            result=xr(copied);
        }
        for(auto& target:targets)if(target.waited) {
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            auto released=options.real.release(target.lease->chain->handle,&release);
            if(XR_FAILED(released) && XR_SUCCEEDED(result))result=released;
        }
        return result;
    }
    void prepare() noexcept {
        try {
            while(!stopping) {
                auto entry=fifo->pop();if(!entry)break;auto packet=std::move(*entry);
                const bool source_pending=d12()?ready12->GetCompletedValue()<packet->ready:
                    ready11->GetCompletedValue()<packet->ready;
                const auto ready=d12()?await(ready12.Get(),packet->ready):await(ready11.Get(),packet->ready);
                // A source already complete when dequeued may have spent time
                // behind a real-chain lease. Do not mistake that backlog for
                // input jitter and add refill delay on every prepared image.
                packet->source_ready_at=source_pending?clock_ns():packet->accepted_at;
                if(tracing())options.trace(10,packet->sequence,packet->source_ready_at,source_pending?1:0);
                if(FAILED(ready) || !reserve_preparation(*packet)) {
                    if(!stopping)fail(XR_ERROR_RUNTIME_FAILURE);
                    retire(packet);fifo->retire();break;
                }
                trace(4,packet->sequence);
                const auto result=publish(*packet);
                if(XR_FAILED(result)) {fail(result);retire(packet);fifo->retire();break;}
                trace(5,packet->sequence);
                packet->prepared_at=clock_ns();
                {std::scoped_lock lock(mutex);prepared.push_back(std::move(packet));}
                changed.notify_all();
            }
        } catch(...) {fail(XR_ERROR_RUNTIME_FAILURE);}
    }
    XrResult real_wait(XrFrameState& prediction) {
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        auto result=options.real.wait(options.real.session,&wait,&prediction);
        if(XR_SUCCEEDED(result)) {
            std::scoped_lock lock(mutex);anchor=prediction;anchor.next=nullptr;anchor_at=clock_ns();seeded=true;changed.notify_all();
        }
        return result;
    }
    void run() noexcept {
        bool waited=false,begun=false;
        XrFrameState prediction{XR_TYPE_FRAME_STATE};
        try {
            PrivateOutputSchedule schedule;
            // Request the runtime cycle near its actual handoff, not at the
            // start of an entire refresh/preparation interval. Reserve only
            // the observed Wait/Begin cost plus the metadata lead. Preparation
            // remains independent and overlaps a blocking runtime Wait.
            std::int64_t runtime_budget=PrivateOutputSchedule::preparation_lead;
            auto result=real_wait(prediction);if(XR_FAILED(result)){fail(result);return;}
            waited=true;
            // Seed the producer's virtual clock, then close this empty cycle
            // immediately. Holding the seed Wait until a level finishes loading
            // would make the first content prediction arbitrarily old.
            XrFrameBeginInfo seed_begin{XR_TYPE_FRAME_BEGIN_INFO};
            result=options.real.begin(options.real.session,&seed_begin);
            if(XR_SUCCEEDED(result)) {
                waited=false;begun=true;
                XrFrameEndInfo seed_end{XR_TYPE_FRAME_END_INFO};
                seed_end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
                seed_end.displayTime=prediction.predictedDisplayTime;
                result=options.real.end(options.real.session,&seed_end);begun=false;
            }
            if(XR_FAILED(result))fail(result);
            while(!stopping) {
                // Start the runtime clock as soon as owned input exists. Image
                // acquisition/copy/GPU waits run on the independent preparer,
                // concurrently with WaitFrame and the physical cadence clock.
                {std::unique_lock lock(mutex);changed.wait(lock,[&]{return stopping || pending;});if(stopping)break;}
                const auto physical=period(prediction.predictedDisplayPeriod);
                schedule.period(physical);
                if(schedule.primed() && !sleep_until(schedule.deadline()-runtime_budget,
                    std::min<std::int64_t>(1'250'000,physical/8)))break;
                const bool diagnostic=tracing();
                const auto wait_enter=clock_ns();
                result=real_wait(prediction);
                const auto wait_leave=diagnostic?clock_ns():0;
                if(XR_SUCCEEDED(result)) {
                    waited=true;
                    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
                    result=options.real.begin(options.real.session,&begin);
                    if(XR_SUCCEEDED(result)){waited=false;begun=true;}
                }
                if(XR_FAILED(result)){fail(result);break;}
                const auto observed=std::clamp<std::int64_t>(clock_ns()-wait_enter+
                    PrivateOutputSchedule::preparation_lead,PrivateOutputSchedule::preparation_lead,physical);
                // A longer native call immediately gets enough overlap on the
                // next cycle. Release excess budget gradually after short calls
                // rather than alternately missing every longer runtime Wait.
                runtime_budget=observed>=runtime_budget?observed:
                    std::max(observed,runtime_budget-(runtime_budget-observed+15)/16);
                std::shared_ptr<Packet> packet;
                {std::unique_lock lock(mutex);changed.wait(lock,[&]{return stopping || !prepared.empty();});
                 if(stopping)break;packet=std::move(prepared.front());prepared.pop_front();}
                if(diagnostic) {options.trace(7,packet->sequence,wait_enter,0);options.trace(6,packet->sequence,wait_leave,0);}
                // Only fully prepared packets reach this thread. Arrival time,
                // not a rounded refresh slot, supplies the exact late hold.
                const auto refill=schedule.input_ready(packet->source_ready_at,options.capacity);
                if(diagnostic && refill)options.trace(11,packet->sequence,packet->source_ready_at,refill);
                schedule.ready(packet->prepared_at);
                // Only the residual time remains. A late completed copy moves
                // the phase by its exact overrun; it never causes a catch-up burst.
                // Prepare the final call 100us ahead. The physical deadline
                // itself is unchanged: never shorten a period to gain lead.
                if(sleep_until(schedule.preparation_deadline(),std::min<std::int64_t>(1'250'000,physical/8))) {
                    auto end=packet->info;
                    // Let the runtime select the display time. CPU cadence is
                    // not a reason to invent a different XR prediction.
                    end.displayTime=prediction.predictedDisplayTime;
                    if(!prediction.shouldRender){end.layerCount=0;end.layers=nullptr;}
                    // Optional logger work belongs in the early window, not
                    // after every deadline where it would accumulate drift.
                    if(options.trace)options.trace(3,packet->sequence,end.displayTime,prediction.predictedDisplayTime);
                    // Preserve V579's 2us quantization/function-call guard.
                    // A late preparation still delivers once, then holds its
                    // exact overrun; there is no catch-up or extra frame wait.
                    // Include the edge guard in this final spin window. A
                    // second tiny timer wait for that2us tail would reintroduce
                    // scheduler jitter instead of preserving the early wakeup.
                    if(!sleep_until(schedule.deadline()+2'000,PrivateOutputSchedule::preparation_lead+2'000)) {
                        retire(packet);fifo->retire();break;
                    }
                    const auto at_end=clock_ns();
                    result=options.real.end(options.real.session,&end);begun=false;
                    // This consumer exclusively owns the schedule. Commit its
                    // saved edge after the call too, leaving no bookkeeping
                    // between the timestamp and native entry. The completed
                    // wait already guarantees eligibility on monotonic QPC.
                    if(!schedule.delivered(at_end))result=XR_ERROR_RUNTIME_FAILURE;
                    if(options.trace)options.trace(8,packet->sequence,at_end,0);
                    if(XR_SUCCEEDED(result))trace(2,packet->sequence,schedule.delay());
                }
                retire(packet);fifo->retire();
                if(XR_FAILED(result)){fail(result);break;}
            }
        } catch(...) {fail(XR_ERROR_RUNTIME_FAILURE);}
        // Provisional sessions can stop after only seeding the virtual clock.
        // Close the real cycle before xrEndSession or a later start.
        if(waited) {XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};begun=XR_SUCCEEDED(options.real.begin(options.real.session,&begin));}
        if(begun) {
            XrFrameEndInfo empty{XR_TYPE_FRAME_END_INFO};empty.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            empty.displayTime=prediction.predictedDisplayTime;
            options.real.end(options.real.session,&empty);
        }
    }
};

PrivateOpenXrRuntime::PrivateOpenXrRuntime(Options options):impl_(std::make_unique<Impl>(std::move(options))){}
PrivateOpenXrRuntime::~PrivateOpenXrRuntime(){stop();}
HRESULT PrivateOpenXrRuntime::initialize() {
    auto& s=*impl_;auto& real=s.options.real;
    if(!real.wait || !real.begin || !real.end || !real.create || !real.destroy || !real.enumerate || !real.acquire || !real.wait_image || !real.release || !s.fence_event || !s.options.capacity)return E_INVALIDARG;
    HRESULT result=S_OK;
    if(s.options.device12 && s.options.queue12) {
        s.device12=s.options.device12;s.queue12=s.options.queue12;
        s.producer_queue12=s.options.producer_queue12?s.options.producer_queue12:s.options.queue12;
        result=s.device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s.ready12));
        if(SUCCEEDED(result))result=s.device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s.copy12));
        if(SUCCEEDED(result))result=s.device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&s.allocator));
        if(SUCCEEDED(result))result=s.device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator.Get(),nullptr,IID_PPV_ARGS(&s.list));
        if(SUCCEEDED(result))result=s.list->Close();
    } else if(s.options.device11 && s.options.context11) {
        result=s.options.device11->QueryInterface(IID_PPV_ARGS(&s.device11));
        if(SUCCEEDED(result))result=s.options.context11->QueryInterface(IID_PPV_ARGS(&s.context11));
        ComPtr<ID3D11Multithread> protection;
        if(SUCCEEDED(result))result=s.context11.As(&protection);
        if(SUCCEEDED(result))protection->SetMultithreadProtected(TRUE);
        if(SUCCEEDED(result))result=s.device11->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&s.ready11));
        if(SUCCEEDED(result))result=s.device11->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&s.copy11));
    } else result=E_INVALIDARG;
    return result;
}
XrResult PrivateOpenXrRuntime::start() {
    auto& s=*impl_;std::scoped_lock lock(s.mutex);
    if(s.running)return XR_ERROR_SESSION_RUNNING;
    s.fifo=std::make_unique<PrivateOutputFifo<std::shared_ptr<Impl::Packet>>>(s.options.capacity);
    s.prepared.clear();
    s.failure=XR_SUCCESS;s.stopping=false;s.seeded=false;s.running=true;s.waits=s.begun=0;s.virtual_time=0;
    s.preparer=std::thread([&s]{s.prepare();});
    s.consumer=std::thread([&s]{s.run();});return XR_SUCCESS;
}
void PrivateOpenXrRuntime::stop() {
    auto& s=*impl_;
    std::scoped_lock serial_stop(s.stop_mutex);
    {std::scoped_lock lock(s.mutex);s.stopping=true;if(s.fifo)s.fifo->stop();}
    s.changed.notify_all();if(s.consumer.joinable())s.consumer.join();if(s.preparer.joinable())s.preparer.join();
    std::scoped_lock accept(s.accept_mutex);
    // An interrupted GPU wait never authorizes reuse/free. Drain submitted
    // reads before dropping the pending packet leases and command allocator.
    if(s.copy_serial) {if(s.d12())s.await(s.copy12.Get(),s.copy_serial,false);else s.await(s.copy11.Get(),s.copy_serial,false);}
    if(s.ready_serial) {if(s.d12())s.await(s.ready12.Get(),s.ready_serial,false);else s.await(s.ready11.Get(),s.ready_serial,false);}
    {std::scoped_lock lock(s.mutex);s.fifo.reset();s.prepared.clear();s.pending=0;s.running=false;
     for(auto& [h,c]:s.chains) {c->prepared_users=0;c->prepared_source.reset();for(auto& image:c->images)image.readers=0;}}
    s.changed.notify_all();
}
XrResult PrivateOpenXrRuntime::drain() {
    auto& s=*impl_;std::unique_lock lock(s.mutex);
    s.changed.wait(lock,[&]{return s.stopping || !s.pending;});return s.failure;
}
XrResult PrivateOpenXrRuntime::wait_frame(const XrFrameWaitInfo* info,XrFrameState* output) {
    if(!valid(info,XR_TYPE_FRAME_WAIT_INFO) || !output || output->type!=XR_TYPE_FRAME_STATE || output->next)return XR_ERROR_VALIDATION_FAILURE;
    auto result=predict_frame(output);if(XR_FAILED(result))return result;
    std::scoped_lock lock(impl_->mutex);++impl_->waits;return result;
}
XrResult PrivateOpenXrRuntime::predict_frame(XrFrameState* output) {
    if(!output || output->type!=XR_TYPE_FRAME_STATE || output->next)return XR_ERROR_VALIDATION_FAILURE;
    auto& s=*impl_;std::unique_lock lock(s.mutex);
    if(!s.running)return XR_ERROR_SESSION_NOT_RUNNING;
    s.changed.wait(lock,[&]{return s.stopping || s.seeded;});
    if(XR_FAILED(s.failure))return s.failure;if(s.stopping)return XR_ERROR_SESSION_NOT_RUNNING;
    const auto interval=s.period(s.anchor.predictedDisplayPeriod);
    auto target=s.anchor.predictedDisplayTime+std::max<std::int64_t>(0,clock_ns()-s.anchor_at)+static_cast<XrDuration>(s.pending)*interval;
    target=std::max(target,s.virtual_time+1);s.virtual_time=target;
    output->predictedDisplayTime=target;output->predictedDisplayPeriod=interval;output->shouldRender=s.anchor.shouldRender;return XR_SUCCESS;
}
XrResult PrivateOpenXrRuntime::begin_frame(const XrFrameBeginInfo* info) {
    if(!valid(info,XR_TYPE_FRAME_BEGIN_INFO))return XR_ERROR_VALIDATION_FAILURE;
    auto& s=*impl_;std::scoped_lock lock(s.mutex);
    if(XR_FAILED(s.failure))return s.failure;if(!s.running || s.stopping)return XR_ERROR_SESSION_NOT_RUNNING;
    if(!s.waits)return XR_ERROR_CALL_ORDER_INVALID;--s.waits;
    const auto result=s.begun?XR_FRAME_DISCARDED:XR_SUCCESS;s.begun=1;return result;
}
XrResult PrivateOpenXrRuntime::end_frame(const XrFrameEndInfo* info) {
    if(!info || info->type!=XR_TYPE_FRAME_END_INFO)return XR_ERROR_VALIDATION_FAILURE;
    auto& s=*impl_;std::scoped_lock accept(s.accept_mutex);
    {std::scoped_lock lock(s.mutex);if(XR_FAILED(s.failure))return s.failure;if(!s.running || s.stopping)return XR_ERROR_SESSION_NOT_RUNNING;if(!s.begun)return XR_ERROR_CALL_ORDER_INVALID;}
    if(!s.fifo->reserve()){std::scoped_lock lock(s.mutex);return XR_FAILED(s.failure)?s.failure:XR_ERROR_SESSION_NOT_RUNNING;}
    std::shared_ptr<Impl::Packet> packet;
    struct Reservation {
        Impl& s;std::shared_ptr<Impl::Packet>& packet;bool accepted{},counted{};
        ~Reservation(){if(accepted)return;{std::scoped_lock lock(s.mutex);
            if(packet)for(auto& lease:packet->leases)--lease.chain->images[lease.image].readers;
            if(counted)--s.pending;}s.fifo->cancel();s.changed.notify_all();}
    } reservation{s,packet};
    packet=std::make_shared<Impl::Packet>();XrResult result;
    {std::scoped_lock lock(s.mutex);result=s.own(*packet,*info);
     if(XR_FAILED(result))return result;
     packet->sequence=++s.sequence;packet->ready=s.ready_serial+1;}
    const auto ready=s.d12()?s.producer_queue12->Signal(s.ready12.Get(),packet->ready):s.context11->Signal(s.ready11.Get(),packet->ready);
    if(!s.d12())s.context11->Flush();
    if(FAILED(ready)) {s.fail(XR_ERROR_RUNTIME_FAILURE);return XR_ERROR_RUNTIME_FAILURE;}
    s.ready_serial=packet->ready;
    packet->accepted_at=clock_ns();
    {std::scoped_lock lock(s.mutex);++s.pending;reservation.counted=true;}
    if(!s.fifo->accept(packet))return XR_ERROR_SESSION_NOT_RUNNING;
    reservation.accepted=true;{std::scoped_lock lock(s.mutex);s.begun=0;}s.changed.notify_all();
    s.trace(1,packet->sequence);return XR_SUCCESS;
}
XrResult PrivateOpenXrRuntime::create_swapchain(const XrSwapchainCreateInfo* info,XrSwapchain* output,D3D12_RESOURCE_STATES image_state,std::uint32_t image_count) {
    if(!info || !output || info->type!=XR_TYPE_SWAPCHAIN_CREATE_INFO || image_count<1 || image_count>3)return XR_ERROR_VALIDATION_FAILURE;
    auto& s=*impl_;auto chain=std::make_shared<Impl::Chain>();chain->info=*info;chain->info.next=nullptr;
    chain->image_state=(info->usageFlags&XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)?D3D12_RESOURCE_STATE_DEPTH_WRITE:image_state;
    auto result=s.options.real.create(s.options.real.session,info,&chain->handle);if(XR_FAILED(result))return result;
    unsigned count{};result=s.options.real.enumerate(chain->handle,0,&count,nullptr);
    auto abandon=[&](XrResult r){s.options.real.destroy(chain->handle);return r;};
    if(XR_FAILED(result) || !count)return abandon(XR_FAILED(result)?result:XR_ERROR_RUNTIME_FAILURE);
    const unsigned private_count=(info->createFlags&XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT)?1:image_count;
    chain->images.resize(private_count);HRESULT made=S_OK;
    if(s.d12()) {
        std::vector<XrSwapchainImageD3D12KHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        result=s.options.real.enumerate(chain->handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
        if(XR_FAILED(result))return abandon(result);for(auto& image:images)chain->real12.emplace_back(image.texture);
        auto desc=images.front().texture->GetDesc();D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        const auto state=chain->image_state;
        for(auto& image:chain->images) {
            made=s.device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&image.d12));if(FAILED(made))break;
            image.d12->SetName(L"OFXR private distributor image");
        }
    } else {
        std::vector<XrSwapchainImageD3D11KHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        result=s.options.real.enumerate(chain->handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
        if(XR_FAILED(result))return abandon(result);for(auto& image:images)chain->real11.emplace_back(image.texture);
        D3D11_TEXTURE2D_DESC desc{};images.front().texture->GetDesc(&desc);
        for(auto& image:chain->images){made=s.device11->CreateTexture2D(&desc,nullptr,&image.d11);if(FAILED(made))break;}
    }
    if(FAILED(made))return abandon(XR_ERROR_RUNTIME_FAILURE);
    {std::scoped_lock lock(s.mutex);s.chains.emplace(chain->handle,chain);}
    *output=chain->handle;return XR_SUCCESS;
}
XrResult PrivateOpenXrRuntime::destroy_swapchain(XrSwapchain handle) {
    auto& s=*impl_;std::unique_lock lock(s.mutex);auto it=s.chains.find(handle);if(it==s.chains.end())return XR_ERROR_HANDLE_INVALID;
    auto chain=it->second;chain->destroying=true;
    if(s.stopping){lock.unlock();stop();lock.lock();}
    s.changed.wait(lock,[&]{return std::none_of(chain->images.begin(),chain->images.end(),[](auto& i){return i.readers!=0;});});
    s.chains.erase(handle);lock.unlock();return s.options.real.destroy(handle);
}
XrResult PrivateOpenXrRuntime::enumerate_images(XrSwapchain handle,std::uint32_t capacity,std::uint32_t* count,XrSwapchainImageBaseHeader* output) {
    auto& s=*impl_;std::scoped_lock lock(s.mutex);auto it=s.chains.find(handle);if(it==s.chains.end())return XR_ERROR_HANDLE_INVALID;
    if(!count)return XR_ERROR_VALIDATION_FAILURE;*count=static_cast<unsigned>(it->second->images.size());if(!capacity)return XR_SUCCESS;
    if(capacity<*count)return XR_ERROR_SIZE_INSUFFICIENT;if(!output)return XR_ERROR_VALIDATION_FAILURE;
    for(unsigned i=0;i<*count;++i)if(s.d12()) {
        auto* image=reinterpret_cast<XrSwapchainImageD3D12KHR*>(output)+i;if(image->type!=XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR)return XR_ERROR_VALIDATION_FAILURE;image->texture=it->second->images[i].d12.Get();
    } else {
        auto* image=reinterpret_cast<XrSwapchainImageD3D11KHR*>(output)+i;if(image->type!=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR)return XR_ERROR_VALIDATION_FAILURE;image->texture=it->second->images[i].d11.Get();
    }
    return XR_SUCCESS;
}
XrResult PrivateOpenXrRuntime::acquire_image(XrSwapchain handle,const XrSwapchainImageAcquireInfo* info,std::uint32_t* output) {
    if(!valid(info,XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO) || !output)return XR_ERROR_VALIDATION_FAILURE;
    auto& s=*impl_;std::unique_lock lock(s.mutex);auto it=s.chains.find(handle);if(it==s.chains.end())return XR_ERROR_HANDLE_INVALID;auto chain=it->second;
    auto available=[&]() -> std::optional<unsigned> {
        for(unsigned n=0;n<chain->images.size();++n){auto index=(chain->cursor+n)%chain->images.size();auto& image=chain->images[index];if(!image.acquired && !image.readers)return static_cast<unsigned>(index);}return {};
    };
    if(std::all_of(chain->images.begin(),chain->images.end(),[](auto& image){return image.acquired;}))return XR_ERROR_CALL_ORDER_INVALID;
    s.changed.wait(lock,[&]{return (s.running && s.stopping) || chain->destroying || available().has_value();});
    if(s.running && s.stopping)return XR_FAILED(s.failure)?s.failure:XR_ERROR_SESSION_NOT_RUNNING;if(chain->destroying)return XR_ERROR_HANDLE_INVALID;
    auto index=*available();chain->images[index].acquired=true;chain->images[index].waited=false;chain->acquired.push_back(index);chain->cursor=(index+1)%chain->images.size();*output=index;return XR_SUCCESS;
}
XrResult PrivateOpenXrRuntime::wait_image(XrSwapchain handle,const XrSwapchainImageWaitInfo* info) {
    if(!info || info->type!=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO || info->next)return XR_ERROR_VALIDATION_FAILURE;
    auto& s=*impl_;std::scoped_lock lock(s.mutex);auto it=s.chains.find(handle);if(it==s.chains.end())return XR_ERROR_HANDLE_INVALID;
    for(auto index:it->second->acquired)if(!it->second->images[index].waited){it->second->images[index].waited=true;return XR_SUCCESS;}return XR_ERROR_CALL_ORDER_INVALID;
}
XrResult PrivateOpenXrRuntime::release_image(XrSwapchain handle,const XrSwapchainImageReleaseInfo* info) {
    if(!valid(info,XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO))return XR_ERROR_VALIDATION_FAILURE;
    auto& s=*impl_;std::scoped_lock lock(s.mutex);auto it=s.chains.find(handle);if(it==s.chains.end())return XR_ERROR_HANDLE_INVALID;auto& chain=*it->second;
    if(chain.acquired.empty() || !chain.images[chain.acquired.front()].waited)return XR_ERROR_CALL_ORDER_INVALID;
    auto index=chain.acquired.front();chain.acquired.pop_front();chain.images[index].acquired=false;chain.images[index].waited=false;chain.released=index;s.changed.notify_all();return XR_SUCCESS;
}
bool PrivateOpenXrRuntime::owns(XrSwapchain handle) const {std::scoped_lock lock(impl_->mutex);return impl_->chains.contains(handle);}
} // namespace xrfg
