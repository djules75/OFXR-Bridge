#include "xrfg/private_openxr_runtime.hpp"
#include "xrfg/private_delivery_queue.hpp"
#include <wrl/client.h>
#include <dxgi1_4.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;
namespace {
void check(bool condition,const char* message){if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::fflush(stderr);std::_Exit(1);}}
void ok(HRESULT result,const char* message){check(SUCCEEDED(result),message);}
void xr_ok(XrResult result,const char* message){if(XR_FAILED(result))std::fprintf(stderr,"XR result=%d: %s\n",result,message);check(XR_SUCCEEDED(result),message);}
std::int64_t ns(){LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);return q.QuadPart/f.QuadPart*1'000'000'000LL+q.QuadPart%f.QuadPart*1'000'000'000LL/f.QuadPart;}
constexpr std::int64_t period=11'111'111,epoch=1'000'000'000'000LL;
struct Chain {
    std::vector<ComPtr<ID3D12Resource>> d12;
    std::vector<ComPtr<ID3D11Texture2D>> d11;
    unsigned acquired{},released{};bool owned{},waited{};
    bool static_image{};unsigned acquisitions{};
    D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_RENDER_TARGET};
};
struct Fixture;
Fixture* current;
struct Fixture {
    bool native12;
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandQueue> delivery_queue;
    ComPtr<ID3D12Fence> fence,gate;
    std::uint64_t serial{};
    HANDLE event{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    HANDLE preparation_timer{CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS)}; // high resolution
    HANDLE frame_wait_timer{CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS)};
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context;
    std::unordered_map<XrSwapchain,Chain> chains;
    std::uintptr_t next_handle{100};
    std::mutex gpu,mutex,protocol;
    std::condition_variable changed;
    std::atomic_bool block_end{true},end_entered{},fail_end{};
    std::atomic_uint real_acquires{},real_waits{},real_begins{},real_ends{};
    bool cycle_waited{},cycle_begun{};
    unsigned successful_waits{};
    std::atomic_bool throttled{true};
    bool real_common{},reuse_current_real_image{};
    std::atomic_int slow_image_wait_ms{};
    std::atomic_int slow_frame_wait_ms{};
    std::atomic_uint fail_wait_call{};
    std::function<int(unsigned)> frame_wait_us;
    std::atomic<DWORD> preparation_thread{},delivery_thread{},wait_thread{};
    std::atomic_uint empty_ends{};
    unsigned expected_views{1};
    std::function<XrResult(XrSwapchain,unsigned)> release_callback;
    std::function<void()> after_head_ready;
    std::function<void(std::uint64_t)> preparation_callback;
    std::function<void(std::uint64_t)> prepared_callback;
    std::function<void(XrSwapchain,unsigned)> image_wait_callback;
    std::function<D3D12_RESOURCE_STATES()> create_state;
    std::vector<unsigned> pixels,overlay_pixels;
    std::vector<float> positions;
    std::vector<std::int64_t> handoffs;
    std::vector<XrTime> targets;
    XrTime raw_prediction{},begun_prediction{};
    unsigned advanced_predictions{};
    std::int64_t wait_return{},begun_wait_return{},prediction_age_limit{};
    std::vector<std::int64_t> prediction_ages;
    std::vector<std::int64_t> refill_margins;
    std::vector<std::int64_t> native_margins;
    std::int64_t physical_origin{};
    std::atomic_bool physical_clock_enabled{true};
    std::shared_ptr<xrfg::PrivateOpenXrRuntime> transport;
    explicit Fixture(bool d12,bool split=true,std::function<int(unsigned)> variable_wait={},bool physical_clock=false):native12(d12),frame_wait_us(std::move(variable_wait)) {
        current=this;
        if(d12) {
            ok(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device12)),"actual D3D12 device");
            D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
            ok(device12->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)),"queue");
            if(split) {
                D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY priority{desc.Type,D3D12_COMMAND_QUEUE_PRIORITY_HIGH,FALSE};
                const bool high=SUCCEEDED(device12->CheckFeatureSupport(D3D12_FEATURE_COMMAND_QUEUE_PRIORITY,&priority,sizeof(priority))) && priority.PriorityForTypeIsSupported;
                ok(xrfg::create_private_delivery_queue(desc,high,[&](const D3D12_COMMAND_QUEUE_DESC& requested){return device12->CreateCommandQueue(&requested,IID_PPV_ARGS(delivery_queue.ReleaseAndGetAddressOf()));}),"independent priority delivery queue");
            }
            ok(device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"test fence");
            ok(device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"GPU gate");
        } else {
            D3D_FEATURE_LEVEL level{};
            ok(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device11,&level,&context),"actual D3D11 device");
        }
        xrfg::PrivateOpenXrRuntime::Options options;
        options.real={reinterpret_cast<XrInstance>(1),reinterpret_cast<XrSession>(2),wait,begin,end,create,destroy,enumerate,acquire,wait_image,release,to_qpc};
        options.device12=device12.Get();options.queue12=delivery_queue?delivery_queue.Get():queue.Get();options.producer_queue12=queue.Get();options.device11=device11.Get();options.context11=context.Get();
        options.physical_period=[]{return period;};options.capacity=3;
        if(physical_clock) {
            physical_origin=ns();
            options.physical_clock=[this]() -> std::optional<xrfg::PrivatePhysicalClock> {
                if(!physical_clock_enabled)return {};
                const auto n=std::max(std::int64_t{},(ns()-physical_origin)/period);
                return xrfg::PrivatePhysicalClock{physical_origin+n*period,period,1000+static_cast<std::uint64_t>(n)};
            };
        }
        options.trace=[this](int stage,std::uint64_t seq,std::int64_t,std::int64_t margin){if(stage==11)refill_margins.push_back(margin);if(stage==17)native_margins.push_back(margin);if(stage==4) {preparation_thread=GetCurrentThreadId();if(after_head_ready)after_head_ready();if(preparation_callback)preparation_callback(seq);}if(stage==5 && prepared_callback)prepared_callback(seq);};
        transport=std::make_shared<xrfg::PrivateOpenXrRuntime>(std::move(options));
        ok(transport->initialize(),"initialize actual private endpoint");xr_ok(transport->start(),"start endpoint");
    }
    ~Fixture(){block_end=false;changed.notify_all();if(gate)gate->Signal(1);transport->stop();transport.reset();CloseHandle(event);if(preparation_timer)CloseHandle(preparation_timer);if(frame_wait_timer)CloseHandle(frame_wait_timer);current=nullptr;}
    void gpu_wait(ID3D12CommandQueue* target=nullptr){if(!target)target=queue.Get();ok(target->Signal(fence.Get(),++serial),"test signal");ok(fence->SetEventOnCompletion(serial,event),"test event");check(WaitForSingleObject(event,5000)==WAIT_OBJECT_0,"test GPU complete");}
    template<class Record> void commands(Record record,ID3D12CommandQueue* target=nullptr) {
        if(!target)target=queue.Get();
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
        ok(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"test allocator");
        ok(device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"test list");
        record(list.Get());ok(list->Close(),"test close");ID3D12CommandList* lists[]={list.Get()};target->ExecuteCommandLists(1,lists);gpu_wait(target);
    }
    unsigned read_pixel(Chain& chain) {
        unsigned color{};
        if(native12) {
            auto source=chain.d12[chain.released];D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;
            ok(device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"readback");
            commands([&](ID3D12GraphicsCommandList* list){
                D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={source.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,chain.state,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
                D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=source.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,1,1,1,256};D3D12_BOX box{0,0,0,1,1,1};list->CopyTextureRegion(&to,0,0,0,&from,&box);
                std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
            },delivery_queue.Get());
            void* data{};ok(readback->Map(0,nullptr,&data),"map readback");color=*static_cast<unsigned char*>(data);readback->Unmap(0,nullptr);
        } else {
            auto source=chain.d11[chain.released];D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.BindFlags=desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> readback;
            ok(device11->CreateTexture2D(&desc,nullptr,&readback),"D11 readback");context->CopyResource(readback.Get(),source.Get());D3D11_MAPPED_SUBRESOURCE mapped{};ok(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"D11 map");color=*static_cast<unsigned char*>(mapped.pData);context->Unmap(readback.Get(),0);
        }
        return color;
    }
    static XrResult XRAPI_CALL wait(XrSession,const XrFrameWaitInfo*,XrFrameState* output) {
        auto& f=*current;f.wait_thread=GetCurrentThreadId();const auto n=++f.real_waits;
        if(f.fail_wait_call==n)return XR_ERROR_RUNTIME_FAILURE;
        const auto delay=f.frame_wait_us?f.frame_wait_us(n):f.slow_frame_wait_ms.load()*1000;
        if(delay){LARGE_INTEGER due{};due.QuadPart=-delay*10LL;check(f.frame_wait_timer && SetWaitableTimer(f.frame_wait_timer,&due,0,nullptr,nullptr,FALSE),"precise runtime delay");check(WaitForSingleObject(f.frame_wait_timer,1000)==WAIT_OBJECT_0,"runtime delay completed");}
        std::scoped_lock protocol(f.protocol);
        check(!f.cycle_waited && f.real_begins==f.successful_waits,"one real Wait, after prior Begin; it may overlap prior End");++f.successful_waits;f.cycle_waited=true;
        // Vary the runtime horizon while retaining the monotonic prediction
        // required by OpenXR. XR time deliberately has another epoch than QPC.
        f.raw_prediction=std::max(f.raw_prediction+1,epoch+ns()+((!f.throttled || n%2)?period:period*3));
        output->predictedDisplayTime=f.raw_prediction;output->predictedDisplayPeriod=(!f.throttled || n%2)?period:period*2;output->shouldRender=XR_TRUE;f.wait_return=ns();return XR_SUCCESS;
    }
    static XrResult XRAPI_CALL begin(XrSession,const XrFrameBeginInfo*){auto& f=*current;std::scoped_lock protocol(f.protocol);check(f.cycle_waited && !f.cycle_begun,"real Begin follows real Wait and previous End");f.cycle_waited=false;f.cycle_begun=true;f.begun_prediction=f.raw_prediction;f.begun_wait_return=f.wait_return;++f.real_begins;return XR_SUCCESS;}
    static XrResult XRAPI_CALL end(XrSession,const XrFrameEndInfo* info) {
        // Timestamp native entry before fixture validation/bookkeeping, so its
        // work cannot masquerade as an early subsequent runtime handoff.
        const auto start=ns();
        auto& f=*current;
        XrTime prediction{};std::int64_t predicted_at{};
        {std::scoped_lock protocol(f.protocol);check(f.cycle_begun,"real End follows real Begin even if next Wait completed");f.cycle_begun=false;prediction=f.begun_prediction;predicted_at=f.begun_wait_return;}
        if(!info->layerCount){++f.empty_ends;return XR_SUCCESS;}
        if(f.prediction_age_limit && f.real_ends>=2) {
            f.prediction_ages.push_back(start-predicted_at);
            if(start-predicted_at>f.prediction_age_limit)std::fprintf(stderr,"freshness age%.3fms, limit%.3fms\n",(start-predicted_at)/1e6,f.prediction_age_limit/1e6);
            check(start-predicted_at<=f.prediction_age_limit,"ready output must use a fresh runtime cycle rather than age its prediction through the refresh wait");
        }
        f.delivery_thread=GetCurrentThreadId();
        if(info->layerCount){std::unique_lock lock(f.mutex);f.end_entered=true;f.changed.notify_all();f.changed.wait(lock,[&]{return !f.block_end.load();});}
        check(info->displayTime>=prediction && (info->displayTime-prediction)%period==0,"End uses its own runtime prediction, advancing whole physical periods only if expired");
        if(info->displayTime>prediction)++f.advanced_predictions;
        unsigned color{},overlay_color{};bool has_overlay{};float position{};
        if(info->layerCount) {
            auto* p=reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
            check(p->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION && p->viewCount==f.expected_views,"owned projection metadata");
            auto& chain=f.chains.at(p->views[0].subImage.swapchain);position=p->views[0].pose.position.x;
            std::scoped_lock lock(f.gpu);
            color=f.read_pixel(chain);
            if(info->layerCount>1) {
                auto* quad=reinterpret_cast<const XrCompositionLayerQuad*>(info->layers[1]);
                check(quad->type==XR_TYPE_COMPOSITION_LAYER_QUAD,"owned overlay metadata");
                overlay_color=f.read_pixel(f.chains.at(quad->subImage.swapchain));has_overlay=true;
            }
        }
        {std::scoped_lock lock(f.mutex);f.pixels.push_back(color);if(has_overlay)f.overlay_pixels.push_back(overlay_color);f.positions.push_back(position);f.handoffs.push_back(start);f.targets.push_back(info->displayTime);++f.real_ends;}
        f.changed.notify_all();return f.fail_end?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
    }
    static XrResult XRAPI_CALL create(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* output) {
        auto& f=*current;*output=reinterpret_cast<XrSwapchain>(f.next_handle++);auto& chain=f.chains[*output];
        chain.static_image=(info->createFlags&XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT)!=0;
        chain.state=f.create_state?f.create_state():f.real_common?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_RENDER_TARGET;
        for(unsigned i=0;i<2;++i)if(f.native12) {
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=info->width;desc.Height=info->height;desc.DepthOrArraySize=static_cast<UINT16>(info->arraySize);desc.MipLevels=1;desc.Format=static_cast<DXGI_FORMAT>(info->format);desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> texture;
            ok(f.device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,chain.state,nullptr,IID_PPV_ARGS(&texture)),"fake runtime real D12 image");chain.d12.push_back(std::move(texture));
        } else {
            D3D11_TEXTURE2D_DESC desc{};desc.Width=info->width;desc.Height=info->height;desc.ArraySize=info->arraySize;desc.MipLevels=1;desc.Format=static_cast<DXGI_FORMAT>(info->format);desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Texture2D> texture;ok(f.device11->CreateTexture2D(&desc,nullptr,&texture),"fake runtime real D11 image");chain.d11.push_back(std::move(texture));
        }
        return XR_SUCCESS;
    }
    static XrResult XRAPI_CALL destroy(XrSwapchain handle){current->chains.erase(handle);return XR_SUCCESS;}
    static XrResult XRAPI_CALL enumerate(XrSwapchain handle,std::uint32_t capacity,std::uint32_t* count,XrSwapchainImageBaseHeader* images) {
        auto& f=*current;auto& chain=f.chains.at(handle);*count=2;if(!capacity)return XR_SUCCESS;if(capacity<2)return XR_ERROR_SIZE_INSUFFICIENT;
        for(unsigned i=0;i<2;++i)if(f.native12)reinterpret_cast<XrSwapchainImageD3D12KHR*>(images)[i].texture=chain.d12[i].Get();else reinterpret_cast<XrSwapchainImageD3D11KHR*>(images)[i].texture=chain.d11[i].Get();return XR_SUCCESS;
    }
    static XrResult XRAPI_CALL acquire(XrSwapchain h,const XrSwapchainImageAcquireInfo*,std::uint32_t* index){auto& f=*current;auto& c=f.chains.at(h);check(!c.owned,"physical image ownership");check(!c.static_image || !c.acquisitions,"static physical image can be acquired only once");++c.acquisitions;c.acquired=f.reuse_current_real_image?c.released:(c.released+1)%2;c.owned=true;c.waited=false;*index=c.acquired;++f.real_acquires;return XR_SUCCESS;}
    static XrResult XRAPI_CALL wait_image(XrSwapchain h,const XrSwapchainImageWaitInfo*){auto& f=*current;auto& c=f.chains.at(h);check(c.owned,"physical wait after acquire");if(const auto delay=f.slow_image_wait_ms.load()){LARGE_INTEGER due{};due.QuadPart=-delay*10'000LL;check(f.preparation_timer && SetWaitableTimer(f.preparation_timer,&due,0,nullptr,nullptr,FALSE),"precise preparation delay");check(WaitForSingleObject(f.preparation_timer,1000)==WAIT_OBJECT_0,"preparation delay completed");}c.waited=true;if(f.image_wait_callback)f.image_wait_callback(h,c.acquired);return XR_SUCCESS;}
    static XrResult XRAPI_CALL release(XrSwapchain h,const XrSwapchainImageReleaseInfo*){auto& c=current->chains.at(h);check(c.owned && c.waited,"physical release after wait");c.released=c.acquired;c.owned=false;return XR_SUCCESS;}
    static XrResult XRAPI_CALL to_qpc(XrInstance,XrTime t,LARGE_INTEGER* q){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);const auto n=t-epoch;q->QuadPart=n/1'000'000'000LL*f.QuadPart+n%1'000'000'000LL*f.QuadPart/1'000'000'000LL;return XR_SUCCESS;}
    XrSwapchain make_chain(bool static_image=false) {
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.createFlags=static_image?XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT:0;info.format=DXGI_FORMAT_R8G8B8A8_UNORM;info.width=info.height=info.arraySize=info.faceCount=info.mipCount=info.sampleCount=1;info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        XrSwapchain handle{};xr_ok(transport->create_swapchain(&info,&handle),"private swapchain creation");return handle;
    }
    void write(XrSwapchain handle,unsigned value) {
        unsigned index{};XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait_info.timeout=XR_INFINITE_DURATION;
        xr_ok(transport->acquire_image(handle,&acquire_info,&index),"private acquire");xr_ok(transport->wait_image(handle,&wait_info),"private wait");
        {std::scoped_lock lock(gpu);float color[]={value/255.0F,0,0,1};unsigned count{};
        if(native12) {
            std::array<XrSwapchainImageD3D12KHR,3> images;for(auto& image:images)image={XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR};xr_ok(transport->enumerate_images(handle,3,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"enumerate private D12");
            check(images[index].texture!=chains[handle].d12[0].Get() && images[index].texture!=chains[handle].d12[1].Get(),"producer never writes real XR image");
            ComPtr<ID3D12DescriptorHeap> heap;D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;desc.NumDescriptors=1;ok(device12->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap)),"RTV heap");device12->CreateRenderTargetView(images[index].texture,nullptr,heap->GetCPUDescriptorHandleForHeapStart());
            commands([&](ID3D12GraphicsCommandList* list){list->ClearRenderTargetView(heap->GetCPUDescriptorHandleForHeapStart(),color,0,nullptr);});
        } else {
            std::array<XrSwapchainImageD3D11KHR,3> images;for(auto& image:images)image={XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};xr_ok(transport->enumerate_images(handle,3,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"enumerate private D11");check(images[index].texture!=chains[handle].d11[0].Get(),"D11 producer private");ComPtr<ID3D11RenderTargetView> rtv;ok(device11->CreateRenderTargetView(images[index].texture,nullptr,&rtv),"D11 RTV");context->ClearRenderTargetView(rtv.Get(),color);
        }
        }
        XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};xr_ok(release_callback?release_callback(handle,index):transport->release_image(handle,&release_info),"private release");
    }
    void begin_virtual(){XrFrameState state{XR_TYPE_FRAME_STATE};xr_ok(transport->wait_frame(nullptr,&state),"virtual wait");check(state.predictedDisplayPeriod==period,"physical period despite throttled runtime prediction");xr_ok(transport->begin_frame(nullptr),"virtual begin");}
    XrResult submit(XrSwapchain handle,float identity,XrSwapchain overlay=XR_NULL_HANDLE) {
        XrCompositionLayerProjectionView view{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};view.pose.orientation.w=1;view.pose.position.x=identity;view.subImage.swapchain=handle;view.subImage.imageRect.extent={1,1};
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=reinterpret_cast<XrSpace>(9);projection.viewCount=1;projection.views=&view;
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};quad.space=reinterpret_cast<XrSpace>(9);quad.pose.orientation.w=1;quad.size={1,1};quad.subImage.swapchain=overlay;quad.subImage.imageRect.extent={1,1};
        std::array<const XrCompositionLayerBaseHeader*,2> layers={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection),reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad)};
        XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};info.displayTime=epoch+ns();info.layerCount=overlay?2:1;info.layers=layers.data();info.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        auto result=transport->end_frame(&info);view.pose.position.x=-999;view.subImage.swapchain=XR_NULL_HANDLE;return result;
    }
    void unblock(){block_end=false;changed.notify_all();}
    void wait_entered(){std::unique_lock lock(mutex);check(changed.wait_for(lock,3s,[&]{return end_entered.load();}),"consumer reached blocked real EndFrame");}
    void wait_count(unsigned count){std::unique_lock lock(mutex);check(changed.wait_for(lock,4s,[&]{return real_ends>=count;}),"consumer delivered all accepted outputs");}
};
void ownership(bool d12) {
    Fixture f(d12);auto chain=f.make_chain();
    for(unsigned n=1;n<=3;++n){f.write(chain,n*40);f.begin_virtual();xr_ok(f.submit(chain,static_cast<float>(n)),"End returns success without real delivery");if(n==1)f.wait_entered();}
    check(f.real_ends==0,"three accepted while real EndFrame remains blocked");
    f.begin_virtual();auto blocked=std::async(std::launch::async,[&]{return f.submit(chain,4);});
    check(blocked.wait_for(70ms)==std::future_status::timeout,"full private FIFO backpressures EndFrame");
    f.unblock();check(blocked.wait_for(3s)==std::future_status::ready,"slot retirement wakes producer");xr_ok(blocked.get(),"fourth accepted");f.wait_count(4);xr_ok(f.transport->drain(),"drain");
    const std::vector<unsigned> expected{40,80,120,120};check(f.pixels==expected,"FIFO pixels survive producer advance without overwrite");check(f.positions==std::vector<float>({1,2,3,4}),"projection metadata copied before producer stack mutation");
    for(unsigned i=1;i<4;++i){check(f.handoffs[i]-f.handoffs[i-1]>=period,"strict one physical refresh between real handoffs including long End");check(f.targets[i]>f.targets[i-1],"monotonic runtime target across variable horizon");}
    check(f.advanced_predictions>0,"long held End must exercise expired overlapped prediction phase advancement");
    check(f.real_acquires>=3 && f.real_acquires<=4,"private immutable repeat may share the prepared real image; no producer acquisition");xr_ok(f.transport->destroy_swapchain(chain),"destroy drained image");
    std::printf("%s private End handoff, bounded FIFO/backpressure, immutable pixels/metadata, physical cadence and XR/QPC target PASS\n",d12?"D3D12":"D3D11");
}
void stop_wakeup() {
    Fixture f(true);auto chain=f.make_chain();f.write(chain,90);
    for(int n=0;n<3;++n){f.begin_virtual();xr_ok(f.submit(chain,n),"accept held image");if(!n)f.wait_entered();}
    f.begin_virtual();auto pending=std::async(std::launch::async,[&]{return f.submit(chain,4);});check(pending.wait_for(50ms)==std::future_status::timeout,"stop test full");
    auto stopped=std::async(std::launch::async,[&]{f.transport->stop();});
    check(pending.wait_for(2s)==std::future_status::ready,"stop wakes blocked End before runtime returns");check(pending.get()==XR_ERROR_SESSION_NOT_RUNNING,"stopped producer receives real state");f.unblock();check(stopped.wait_for(3s)==std::future_status::ready,"consumer stop joins");stopped.get();
    std::puts("Stop wakes full producer, joins consumer and retires GPU reads PASS");
}
void failure_wakeup() {
    Fixture f(true);auto chain=f.make_chain();f.write(chain,90);
    for(int n=0;n<3;++n){f.begin_virtual();xr_ok(f.submit(chain,n),"failure test accept");if(!n)f.wait_entered();}
    f.begin_virtual();auto pending=std::async(std::launch::async,[&]{return f.submit(chain,4);});check(pending.wait_for(50ms)==std::future_status::timeout,"failure test full");
    f.fail_end=true;f.unblock();check(pending.wait_for(3s)==std::future_status::ready,"runtime failure wakes blocked producer");check(pending.get()==XR_ERROR_RUNTIME_FAILURE,"runtime error propagated, no fabricated success after failure");
    std::puts("Runtime failure propagation and cancellation PASS");
}
void pending_gpu() {
    Fixture f(true);f.unblock();auto chain=f.make_chain();f.write(chain,77);
    ok(f.queue->Wait(f.gate.Get(),1),"independently hold producer GPU");f.begin_virtual();
    xr_ok(f.submit(chain,1),"accept while producer GPU is pending");std::this_thread::sleep_for(20ms);
    check(f.real_ends==0 && f.real_acquires==0,"no image write/content delivery before source GPU completion; runtime clock may overlap preparation");
    ok(f.gate->Signal(1),"release GPU independently of XR");f.wait_count(1);xr_ok(f.transport->drain(),"GPU pending drain");check(f.pixels==std::vector<unsigned>{77},"pending frame retains actual source");
    f.transport->stop();check(!f.cycle_waited && !f.cycle_begun,"stop closes the real frame cycle");
    xr_ok(f.transport->start(),"restart same session endpoint");f.begin_virtual();xr_ok(f.submit(chain,2),"same endpoint works after restart");f.wait_count(2);xr_ok(f.transport->drain(),"restart drain");
    std::puts("Private End returns before GPU readiness; real consumer waits exact fence; session restart PASS");
}
void preparation_overlap(bool d12) {
    Fixture f(d12);f.unblock();f.throttled=false;
    f.slow_image_wait_ms=4;
    auto chain=f.make_chain();
    // The runtime reports an available image only after4ms. That preparation
    // must fit INSIDE the refresh interval, not be added after a full interval.
    for(unsigned i=1;i<=8;++i){f.write(chain,i*25);f.begin_virtual();xr_ok(f.submit(chain,float(i)),"accept paced packet");}
    f.wait_count(8);xr_ok(f.transport->drain(),"paced FIFO drain");
    check(f.pixels==std::vector<unsigned>({25,50,75,100,125,150,175,200}),"paced delivery preserves every FIFO image");
    for(unsigned i=1;i<f.handoffs.size();++i) {
        const auto interval=f.handoffs[i]-f.handoffs[i-1];
        if(interval<period)std::fprintf(stderr,"%s overlap handoff%u interval%lldns, required%lldns\n",d12?"D3D12":"D3D11",i,static_cast<long long>(interval),static_cast<long long>(period));
        check(interval>=period,"fresh prediction keeps physical refresh minimum");
    }
    const auto average=(f.handoffs.back()-f.handoffs.front())/(f.handoffs.size()-1);
    std::fprintf(stderr,"%s overlap measured interval%.3fms\n",d12?"D3D12":"D3D11",average/1e6);
    check(average<period+3'000'000,"refresh pacing must include preparation instead of adding it to every interval");
    std::printf("%s4ms preparation inside refresh, mean interval%.3fms; raw runtime time/Begin-before-copy/FIFO PASS\n",d12?"D3D12":"D3D11",average/1e6);
}
void future_work_dependency(bool split) {
    Fixture f(true,split);f.unblock();auto chain=f.make_chain();f.write(chain,77);
    std::promise<void> queued;auto observed=queued.get_future();
    f.after_head_ready=[&]{ok(f.queue->Wait(f.gate.Get(),1),"later unrelated producer work");queued.set_value();};
    f.begin_virtual();xr_ok(f.submit(chain,1),"accept completed head before later producer dependency");
    check(observed.wait_for(3s)==std::future_status::ready,"consumer observed exact head completion");
    {std::unique_lock lock(f.mutex);f.changed.wait_for(lock,50ms,[&]{return f.real_ends.load()>0;});}
    const bool independent=f.real_ends.load()>0;
    ok(f.gate->Signal(1),"release later work for safe fixture cleanup");f.wait_count(1);xr_ok(f.transport->drain(),"future dependency drain");
    check(independent==split,"only the split queue must deliver the ready head while later producer work is blocked");
    check(f.pixels==std::vector<unsigned>{77},"independent delivery preserves the exact completed image");
    std::printf("Ready-head future dependency: split=%d independent=%d PASS\n",split,independent);
}
void cadence_precision(bool d12) {
    Fixture f(d12);f.unblock();f.throttled=false;f.slow_image_wait_ms=4;
    auto chain=f.make_chain();
    for(unsigned n=1;n<=48;++n){f.write(chain,n*4);f.begin_virtual();xr_ok(f.submit(chain,float(n)),"precision cadence packet");}
    f.wait_count(48);xr_ok(f.transport->drain(),"precision cadence drain");
    const auto average=(f.handoffs.back()-f.handoffs.front())/(f.handoffs.size()-1);
    for(unsigned n=1;n<f.handoffs.size();++n)check(f.handoffs[n]-f.handoffs[n-1]>=period,"precision must never send before a physical interval");
    std::fprintf(stderr,"%s precision mean interval%.6fms (%.3ffps)\n",d12?"D3D12":"D3D11",average/1e6,1e9/average);
    check(average<period+250'000,"timer overshoot must not impose a common sub-refresh FPS ceiling");
    check(f.pixels.size()==48 && f.pixels.front()==4 && f.pixels.back()==192,"precision wait preserves FIFO images");
}
void preparation_independence(bool d12) {
    Fixture f(d12);f.unblock();f.throttled=false;
    f.slow_frame_wait_ms=5;f.slow_image_wait_ms=8;
    auto chain=f.make_chain();
    for(unsigned n=1;n<=24;++n){f.write(chain,n*8);f.begin_virtual();xr_ok(f.submit(chain,float(n)),"parallel preparation input");}
    f.wait_count(24);xr_ok(f.transport->drain(),"parallel preparation drain");
    const auto average=(f.handoffs.back()-f.handoffs.front())/(f.handoffs.size()-1);
    std::fprintf(stderr,"%s independent preparation measured%.6fms\n",d12?"D3D12":"D3D11",average/1e6);
    check(f.preparation_thread && f.delivery_thread && f.preparation_thread!=f.delivery_thread,"GPU preparation must not execute on the delivery thread");
    for(unsigned n=1;n<f.handoffs.size();++n)check(f.handoffs[n]-f.handoffs[n-1]>=period,"independent preparer cannot cause a short output interval");
    check(average<period+1'000'000,"5ms runtime wait and8ms preparation must overlap, not add to13ms per frame");
    check(f.pixels.size()==24 && f.pixels.front()==8 && f.pixels.back()==192,"independent delivery retains every accepted image");
    std::printf("%s independent5ms runtime+8ms preparation, interval%.3fms PASS\n",d12?"D3D12":"D3D11",average/1e6);
}
void stalled_preparer() {
    Fixture f(true);f.unblock();auto a=f.make_chain(),b=f.make_chain(),c=f.make_chain();
    std::promise<void> entered,release;auto seen=entered.get_future();auto gate=release.get_future();
    f.preparation_callback=[&](std::uint64_t seq){if(seq==3){entered.set_value();gate.wait();}};
    for(auto h:{a,b,c}){f.write(h,unsigned(h==a?40:h==b?80:120));f.begin_virtual();xr_ok(f.submit(h,1),"independent-chain input");}
    check(seen.wait_for(3s)==std::future_status::ready,"third preparation held independently");
    f.wait_count(2);check(f.real_ends==2,"ready outputs must be delivered while later preparation is blocked");
    release.set_value();f.wait_count(3);xr_ok(f.transport->drain(),"stalled preparation drain");
    check(f.pixels==std::vector<unsigned>({40,80,120}),"late third frame must not overwrite/drop the earlier pair");
    std::puts("Delivery consumes ready pair while third preparation is blocked PASS");
}
void unchanged_overlay() {
    Fixture f(true);auto a=f.make_chain(),b=f.make_chain(),overlay=f.make_chain();
    f.write(overlay,33);f.write(a,40);f.begin_virtual();xr_ok(f.submit(a,1,overlay),"first overlay frame");f.wait_entered();
    std::promise<void> ready;auto seen=ready.get_future();f.preparation_callback=[&](std::uint64_t seq){if(seq==2)ready.set_value();};
    f.write(b,80);f.begin_virtual();xr_ok(f.submit(b,2,overlay),"second unchanged-overlay frame");
    check(seen.wait_for(3s)==std::future_status::ready,"unchanged overlay must not serialize another frame's preparation");
    check(f.real_acquires==3,"second frame reuses the same immutable overlay contents without an additional XR chain/copy");
    f.unblock();f.wait_count(2);xr_ok(f.transport->drain(),"unchanged overlay drain");
    check(f.pixels==std::vector<unsigned>({40,80}),"shared overlay must not replace either scene image");
    std::puts("Immutable overlay sharing preserves lookahead without extra swapchains PASS");
}
void same_chain_lookahead(bool d12) {
    Fixture f(d12);auto chain=f.make_chain();
    f.write(chain,40);f.begin_virtual();xr_ok(f.submit(chain,1),"first same-chain input");f.wait_entered();
    const auto previous=f.chains.at(chain).released;
    std::promise<void> ready;auto seen=ready.get_future();
    f.prepared_callback=[&](std::uint64_t seq){if(seq==2)ready.set_value();};
    f.write(chain,80);f.begin_virtual();xr_ok(f.submit(chain,2),"next same-chain input");
    check(seen.wait_for(3s)==std::future_status::ready,"next real image must finish GPU preparation while prior same-chain EndFrame is blocked");
    auto& real=f.chains.at(chain);
    check(f.real_acquires==2 && real.owned && real.waited,"exactly one next real target stays acquired/waited");
    check(real.released==previous && real.acquired!=previous,"copying ahead must not release/replace the previous EndFrame image");
    check(f.chains.size()==1,"lookahead allocates no extra real swapchain");
    f.unblock();f.wait_count(2);xr_ok(f.transport->drain(),"same-chain lookahead drain");
    check(f.pixels==std::vector<unsigned>({40,80}),"same-chain lookahead preserves the previous and next scene pixels");
    check(f.handoffs[1]-f.handoffs[0]>=period,"same-chain lookahead retains strict physical cadence");
    std::printf("%s same-chain GPU lookahead with unreleased next target and exact FIFO pixels PASS\n",d12?"D3D12":"D3D11");
}
void changing_overlay_lookahead() {
    Fixture f(true);auto a=f.make_chain(),b=f.make_chain(),overlay=f.make_chain();
    f.write(overlay,20);f.write(a,40);f.begin_virtual();xr_ok(f.submit(a,1,overlay),"first changing-overlay frame");f.wait_entered();
    const auto previous=f.chains.at(overlay).released;
    std::promise<void> ready;auto seen=ready.get_future();
    f.prepared_callback=[&](std::uint64_t seq){if(seq==2)ready.set_value();};
    f.write(overlay,60);f.write(b,80);f.begin_virtual();xr_ok(f.submit(b,2,overlay),"next changing-overlay frame");
    check(seen.wait_for(3s)==std::future_status::ready,"changing shared overlay must not prevent next scene and overlay GPU preparation");
    auto& real=f.chains.at(overlay);
    check(real.owned && real.waited && real.released==previous,"new overlay stays waited/unreleased while previous frame captures old overlay");
    check(f.real_acquires==4 && f.chains.size()==3,"changing-overlay lookahead reuses existing real chains only");
    f.unblock();f.wait_count(2);xr_ok(f.transport->drain(),"changing-overlay lookahead drain");
    check(f.pixels==std::vector<unsigned>({40,80}),"changing shared overlay preserves both scene images");
    check(f.overlay_pixels==std::vector<unsigned>({20,60}),"native End captures previous overlay then next overlay, never the future released image");
    check(f.chains.at(overlay).released!=previous && !f.chains.at(overlay).owned,"new overlay is released only for its own FIFO frame");
    std::puts("Changing shared overlay preserves next-scene GPU lookahead without early release PASS");
}
void stop_prepared_target() {
    Fixture f(true);auto chain=f.make_chain();
    f.write(chain,40);f.begin_virtual();xr_ok(f.submit(chain,1),"stop target first input");f.wait_entered();
    std::promise<void> ready;auto seen=ready.get_future();
    f.prepared_callback=[&](std::uint64_t seq){if(seq==2)ready.set_value();};
    f.write(chain,80);f.begin_virtual();xr_ok(f.submit(chain,2),"stop target next input");
    check(seen.wait_for(3s)==std::future_status::ready,"stop test owns a GPU-complete unreleased target");
    auto stopped=std::async(std::launch::async,[&]{f.transport->stop();});
    const auto limit=std::chrono::steady_clock::now()+3s;
    for(;;) {XrFrameState prediction{XR_TYPE_FRAME_STATE};
        if(f.transport->predict_frame(&prediction)==XR_ERROR_SESSION_NOT_RUNNING)break;
        check(std::chrono::steady_clock::now()<limit,"stop must mark the endpoint stopped before waiting for native End");std::this_thread::yield();}
    f.unblock();check(stopped.wait_for(3s)==std::future_status::ready,"stop drains an unreleased prepared image");stopped.get();
    check(f.real_ends==1 && !f.chains.at(chain).owned,"stop releases canceled prepared target without presenting it");
    f.prepared_callback={};xr_ok(f.transport->start(),"restart after pending target cleanup");
    f.begin_virtual();xr_ok(f.submit(chain,3),"restart retained private input");f.wait_count(2);xr_ok(f.transport->drain(),"restart target drain");
    check(f.pixels==std::vector<unsigned>({40,80}),"restart copies retained private source after canceled real target cleanup");
    std::puts("Stop releases an unpresented waited target and preserves restart ownership PASS");
}
void reused_real_index(bool cancel) {
    Fixture f(true);f.reuse_current_real_image=true;auto chain=f.make_chain();
    f.write(chain,40);f.begin_virtual();xr_ok(f.submit(chain,1),"reused-index first input");f.wait_entered();
    std::promise<void> acquired;auto seen=acquired.get_future();
    f.image_wait_callback=[&](XrSwapchain h,unsigned){if(h==chain && f.real_acquires==2)acquired.set_value();};
    f.write(chain,80);f.begin_virtual();xr_ok(f.submit(chain,2),"reused-index next input");
    check(seen.wait_for(3s)==std::future_status::ready,"runtime returns the still-current physical index for the next target");
    {std::scoped_lock lock(f.gpu);check(f.read_pixel(f.chains.at(chain))==40,"next copy must not overwrite an older retained generation sharing the same real index");}
    if(cancel) {
        auto stopped=std::async(std::launch::async,[&]{f.transport->stop();});
        const auto limit=std::chrono::steady_clock::now()+3s;
        for(;;){XrFrameState prediction{XR_TYPE_FRAME_STATE};if(f.transport->predict_frame(&prediction)==XR_ERROR_SESSION_NOT_RUNNING)break;
            check(std::chrono::steady_clock::now()<limit,"stop wakes real-index lease wait");std::this_thread::yield();}
        f.unblock();check(stopped.wait_for(3s)==std::future_status::ready,"stop drains a real-index reuse wait");stopped.get();
        check(f.pixels==std::vector<unsigned>({40}) && !f.chains.at(chain).owned,"canceling an aliased target preserves prior pixels and releases acquired ownership");
    } else {
        f.unblock();f.wait_count(2);xr_ok(f.transport->drain(),"reused-index drain");
        check(f.pixels==std::vector<unsigned>({40,80}),"runtime index reuse waits only for the old generation then preserves both FIFO images");
    }
    std::printf("Reused physical image index protects retained pixels%s PASS\n",cancel?" and stops cleanly":"");
}
void static_image_repeat() {
    Fixture f(true);f.unblock();auto chain=f.make_chain(true);f.write(chain,70);
    for(unsigned n=1;n<=4;++n){f.begin_virtual();xr_ok(f.submit(chain,float(n)),"repeat immutable static source");f.wait_count(n);xr_ok(f.transport->drain(),"static repeat drain");}
    check(f.real_acquires==1,"static repeated image reuses the sole runtime acquisition after users reach zero");
    unsigned index{};check(f.transport->acquire_image(chain,nullptr,&index)==XR_ERROR_CALL_ORDER_INVALID,"private static image cannot be acquired again after release");
    f.transport->stop();xr_ok(f.transport->start(),"static retained chain restart");
    f.begin_virtual();xr_ok(f.submit(chain,5),"repeat static source after session restart");f.wait_count(5);xr_ok(f.transport->drain(),"static restart drain");
    check(f.real_acquires==1 && f.pixels==std::vector<unsigned>(5,70),"static generation survives session restart without a second physical acquire/release");
    std::puts("Static image repeats retain one physical acquisition across drained frames and restart PASS");
}
void stop_first_static_target() {
    Fixture f(true);f.unblock();auto chain=f.make_chain(true);f.write(chain,70);
    std::promise<void> entered,proceed;auto seen=entered.get_future();auto gate=proceed.get_future();
    f.image_wait_callback=[&](XrSwapchain h,unsigned){if(h==chain){entered.set_value();gate.wait();}};
    f.begin_virtual();xr_ok(f.submit(chain,1),"first static target before cancellation");
    check(seen.wait_for(3s)==std::future_status::ready,"static first target is acquired/waited before GPUcopy");
    auto stopped=std::async(std::launch::async,[&]{f.transport->stop();});
    const auto limit=std::chrono::steady_clock::now()+3s;
    for(;;){XrFrameState prediction{XR_TYPE_FRAME_STATE};if(f.transport->predict_frame(&prediction)==XR_ERROR_SESSION_NOT_RUNNING)break;
        check(std::chrono::steady_clock::now()<limit,"stop marks static target cancellation before native Wait returns");std::this_thread::yield();}
    proceed.set_value();check(stopped.wait_for(3s)==std::future_status::ready,"stop finishes the first static GPUcopy and releases its one acquired target");stopped.get();
    check(f.real_acquires==1 && !f.chains.at(chain).owned && f.real_ends==0,"canceled static target is released once without presenting");
    f.image_wait_callback={};xr_ok(f.transport->start(),"restart after canceled first static target");
    f.begin_virtual();xr_ok(f.submit(chain,2),"reuse completed static target after cancellation");f.wait_count(1);xr_ok(f.transport->drain(),"canceled static restart drain");
    check(f.real_acquires==1 && f.pixels==std::vector<unsigned>({70}),"restart presents the exact static source without forbidden reacquisition");
    std::puts("Stop after first static Wait finishes its safe GPUcopy and restarts with one acquisition PASS");
}
void overlapped_native_wait(bool cancel) {
    std::promise<void> entered,release;auto seen=entered.get_future();auto gate=release.get_future();
    Fixture f(true,true,[&](unsigned n){if(n==2)return 14'000;if(n==3){entered.set_value();gate.wait();}return 0;});
    auto chain=f.make_chain();f.write(chain,70);
    for(unsigned n=0;n<2;++n){f.begin_virtual();xr_ok(f.submit(chain,float(n)),"overlapped native Wait input");}
    check(seen.wait_for(3s)==std::future_status::ready,"next native Wait must start before the current End completes");
    f.wait_entered();f.unblock();f.wait_count(1);
    check(f.wait_thread && f.wait_thread!=f.delivery_thread,"native Wait must run independently of Begin/End delivery");
    check(f.pixels==std::vector<unsigned>({70}),"current frame must deliver while the next native Wait is still blocked");
    if(cancel) {
        auto stopped=std::async(std::launch::async,[&]{f.transport->stop();});
        check(stopped.wait_for(30ms)==std::future_status::timeout,"stop cannot abandon an in-flight native Wait");
        release.set_value();check(stopped.wait_for(3s)==std::future_status::ready,"stop must join and balance overlapped Wait");stopped.get();
        check(!f.cycle_waited && !f.cycle_begun && f.real_ends==1,"canceled lookahead is closed once without a phantom content frame");
        xr_ok(f.transport->start(),"restart after overlapped native Wait stop");f.begin_virtual();xr_ok(f.submit(chain,3),"restart input after joined Wait");f.wait_count(2);xr_ok(f.transport->drain(),"overlapped restart drain");
    } else {release.set_value();f.wait_count(2);xr_ok(f.transport->drain(),"overlapped Wait drain");}
    check(f.pixels==std::vector<unsigned>({70,70}),"overlapped native Wait keeps exact FIFO images");
    for(unsigned n=1;n<f.handoffs.size();++n)check(f.handoffs[n]-f.handoffs[n-1]>=period,"overlapped native Wait cannot shorten the physical interval");
    std::puts(cancel?"Overlapped native Wait stop/join/cleanup/restart PASS":"Blocked next native Wait does not block current delivery PASS");
}
void overlapped_wait_failure() {
    Fixture f(true,true,[](unsigned n){return n==2?14'000:0;});f.fail_wait_call=3;
    auto chain=f.make_chain();f.write(chain,70);
    for(unsigned n=0;n<2;++n){f.begin_virtual();xr_ok(f.submit(chain,float(n)),"queued native failure input");}
    f.wait_entered();f.unblock();f.wait_count(1);
    check(f.transport->drain()==XR_ERROR_RUNTIME_FAILURE,"future native Wait error must propagate after the current handoff");
    check(f.pixels==std::vector<unsigned>({70}),"future Wait failure cannot erase the current valid content frame");
    f.transport->stop();check(!f.cycle_waited && !f.cycle_begun,"failed lookahead leaves no native frame open");
    std::puts("Future native Wait failure preserves current delivery and propagates PASS");
}
void physical_clock_pipeline(bool d12) {
    Fixture f(d12,true,{},true);f.unblock();f.throttled=false;
    auto chain=f.make_chain();std::vector<unsigned> expected;
    f.preparation_callback=[&](std::uint64_t seq){if(seq%5==0)std::this_thread::sleep_for(5ms);};
    for(unsigned n=0;n<24;++n){const auto color=40+n*3;f.write(chain,color);expected.push_back(color);f.begin_virtual();xr_ok(f.submit(chain,float(n)),"physical-clock irregular input");if(n%7==0)std::this_thread::sleep_for(9ms);}
    f.wait_count(24);xr_ok(f.transport->drain(),"physical-clock drain");
    check(f.pixels==expected,"physical clock retained every exact FIFO image");
    std::uint64_t previous{};
    std::int64_t largest{};
    for(auto at:f.handoffs){const auto elapsed=at-f.physical_origin;const auto slot=static_cast<std::uint64_t>(elapsed/period);const auto phase=elapsed%period;
        if(phase>=300'000)std::fprintf(stderr,"physical phase%lldns at slot%llu\n",static_cast<long long>(phase),slot);
        check(phase<300'000 && slot>previous,"handoff must stay on absolute headset slots after late preparation");previous=slot;largest=std::max(largest,phase);}
    for(unsigned n=1;n<f.handoffs.size();++n)check(f.handoffs[n]-f.handoffs[n-1]>=period-100'000,"physical-clock correction cannot burst outputs");
    std::printf("%s absolute physical slots,24exact FIFO pixels, late-preparation phase recovery, largest%.3fms PASS\n",d12?"D3D12":"D3D11",largest/1e6);
}
void late_physical_clock() {
    Fixture f(true,true,{},true);f.physical_clock_enabled=false;f.unblock();f.throttled=false;
    auto chain=f.make_chain();f.write(chain,70);
    for(unsigned n=0;n<3;++n){f.begin_virtual();xr_ok(f.submit(chain,float(n)),"unavailable physical clock input");f.wait_count(n+1);xr_ok(f.transport->drain(),"unavailable clock drain");}
    f.physical_clock_enabled=true;
    for(unsigned n=3;n<9;++n){f.begin_virtual();xr_ok(f.submit(chain,float(n)),"late physical clock input");}
    f.wait_count(9);xr_ok(f.transport->drain(),"late physical clock drain");
    check(f.pixels==std::vector<unsigned>(9,70),"late physical-clock activation must preserve every queued image");
    for(unsigned n=3;n<f.handoffs.size();++n){check((f.handoffs[n]-f.physical_origin)%period<300'000,"late physical clock must anchor to display phase");check(f.handoffs[n]-f.handoffs[n-1]>=period-100'000,"first anchored slot cannot burst after fallback");}
    std::puts("Late physical-clock availability transitions from fallback without a burst or lost image PASS");
}
void native_margin_scope(bool d12) {
    {
        Fixture f(d12);f.unblock();f.throttled=false;
        auto chain=f.make_chain();f.write(chain,70);f.slow_image_wait_ms=8;
        // The native cycle is already ready before each deliberately late
        // image wait/copy. This must not create a new native margin afterward.
        f.preparation_callback=[&](std::uint64_t seq){
            const auto limit=std::chrono::steady_clock::now()+3s;
            while(f.real_begins<seq+1){check(std::chrono::steady_clock::now()<limit,"native Begin overlaps late preparation");std::this_thread::yield();}
        };
        for(unsigned n=0;n<4;++n){f.begin_virtual();xr_ok(f.submit(chain,float(n)),"late preparation input");f.wait_count(n+1);xr_ok(f.transport->drain(),"late preparation drain");}
        check(f.native_margins.empty(),"late GPU preparation must not be charged another native1.1ms margin");
        check(f.pixels==std::vector<unsigned>(4,70),"late preparation retains all finite FIFO inputs");
        for(unsigned n=1;n<f.handoffs.size();++n)check(f.handoffs[n]-f.handoffs[n-1]>=period,"late preparation preserves physical minimum");
    }
    {
        Fixture f(d12,true,[](unsigned n){return n==2?8'000:0;});f.unblock();f.throttled=false;
        auto chain=f.make_chain();f.write(chain,80);
        f.begin_virtual();xr_ok(f.submit(chain,1),"late native input");f.wait_count(1);xr_ok(f.transport->drain(),"late native drain");
        check(!f.native_margins.empty(),"genuinely late native Begin still receives held presentation headroom");
        check(f.pixels==std::vector<unsigned>({80}),"native margin preserves the owned image");
    }
    std::printf("%s native headroom excludes late GPU preparation and retains late native margin PASS\n",d12?"D3D12":"D3D11");
}
void rising_native_wait() {
    Fixture f(true,true,[](unsigned n){return 200+std::min(n>8?n-8:0u,20u)*400;});f.unblock();f.throttled=false;
    // Reproduce the observed staircase: each call takes another0.4ms while
    // immutable pixels are already ready. The predictor must learn variation,
    // rather than missing every refresh by the same unreserved increment.
    auto chain=f.make_chain();f.write(chain,70);
    for(unsigned n=0;n<40;++n){f.begin_virtual();xr_ok(f.submit(chain,float(n)),"rising native Wait owned input");}
    f.wait_count(40);xr_ok(f.transport->drain(),"rising native Wait drain");
    unsigned late{};
    for(unsigned n=1;n<f.handoffs.size();++n){const auto delta=f.handoffs[n]-f.handoffs[n-1];check(delta>=period,"rising Wait retains strict physical minimum");if(n>=8 && n<=28 && delta>period+200'000)++late;}
    const auto average=(f.handoffs[28]-f.handoffs[8])/20;
    std::fprintf(stderr,"Rising native Wait: mean%.6fms, late%u/21\n",average/1e6,late);
    check(average<period+200'000 && late<=5,"rising runtime cost must not cause a persistent sub-refresh staircase with ready input");
    check(f.pixels==std::vector<unsigned>(40,70),"variable native entry retains every FIFO image");
    std::puts("Rising native Wait learns service-time variation without slowing production or shortening physical cadence PASS");
}
void fresh_runtime_window(bool d12,int wait_ms=0) {
    Fixture f(d12);f.throttled=false;auto chain=f.make_chain();f.write(chain,70);
    f.slow_frame_wait_ms=wait_ms;
    // Retaining a recent longer native Wait can leave some lead after a
    // shorter timer wake. V589 deliberately opens native entry another1ms
    // earlier, so allow exactly that increment in prediction age; the physical
    // minimum interval and all cadence/FIFO assertions remain unchanged.
    f.prediction_age_limit=wait_ms?4'000'000:2'000'000;
    for(unsigned n=0;n<12;++n) {
        f.begin_virtual();xr_ok(f.submit(chain,float(n)),"fresh-cycle immutable input");
        if(!n){f.wait_entered();f.unblock();}
    }
    f.wait_count(12);xr_ok(f.transport->drain(),"fresh runtime window drain");
    for(unsigned i=1;i<f.handoffs.size();++i){const auto delta=f.handoffs[i]-f.handoffs[i-1];if(delta<period)std::fprintf(stderr,"%s freshness interval%lldns required%lldns\n",d12?"D12":"D11",static_cast<long long>(delta),static_cast<long long>(period));check(delta>=period,"fresh runtime window preserves physical minimum");}
    check(f.pixels==std::vector<unsigned>(12,70),"fresh runtime window retains every accepted image");
    const auto oldest=*std::max_element(f.prediction_ages.begin(),f.prediction_ages.end());
    std::printf("%s fresh runtime prediction, native Wait%dms, oldest%.3fms under saturated ready FIFO PASS\n",d12?"D3D12":"D3D11",wait_ms,oldest/1e6);
}
void adaptive_source_refill(bool d12) {
    Fixture f(d12);f.throttled=false;auto chain=f.make_chain();f.write(chain,40);
    f.begin_virtual();xr_ok(f.submit(chain,0),"adaptive first input");f.wait_entered();f.unblock();f.wait_count(1);
    std::this_thread::sleep_for(18ms); // deliberate gap, no throughput assertion
    for(unsigned n=1;n<5;++n){f.begin_virtual();xr_ok(f.submit(chain,float(n)),"adaptive irregular input");}
    f.wait_count(5);xr_ok(f.transport->drain(),"adaptive refill drain");
    check(!f.refill_margins.empty(),"late source must build automatic presentation margin");
    for(auto margin:f.refill_margins)check(margin>0 && margin<=period*2,"refill must fit the existing three-slot pool");
    check(f.pixels==std::vector<unsigned>(5,40),"irregular input must deliver all five accepted images without awaiting imaginary90fps");
    for(unsigned i=1;i<f.handoffs.size();++i)check(f.handoffs[i]-f.handoffs[i-1]>=period,"adaptive source refill must not catch up with a burst");
    std::printf("%s automatic source refill retains all finite irregular input, bounded3slots, strict cadence PASS\n",d12?"D3D12":"D3D11");
}
void queue_priority_policy() {
    D3D12_COMMAND_QUEUE_DESC original{};original.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;original.NodeMask=1;
    std::vector<int> attempts;
    auto create=[&](bool reject_high,bool reject_all) {
        attempts.clear();return xrfg::create_private_delivery_queue(original,true,[&](const D3D12_COMMAND_QUEUE_DESC& desc)->HRESULT {
            check(desc.Type==original.Type && desc.Flags==original.Flags && desc.NodeMask==original.NodeMask,"priority choice preserves binding queue descriptor");
            attempts.push_back(desc.Priority);
            return reject_all || (reject_high && desc.Priority==D3D12_COMMAND_QUEUE_PRIORITY_HIGH)?E_FAIL:S_OK;
        });
    };
    ok(create(false,false),"supported priority queue");check(attempts==std::vector<int>{100},"deadline delivery requests HIGH once");
    ok(create(true,false),"rejected priority fallback");check(attempts==std::vector<int>({100,0}),"priority rejection retries original dedicated queue");
    check(FAILED(create(true,true)) && attempts==std::vector<int>({100,0}),"both queue failures propagate rather than alias producer queue");
    attempts.clear();ok(xrfg::create_private_delivery_queue(original,false,[&](const D3D12_COMMAND_QUEUE_DESC& desc)->HRESULT{attempts.push_back(desc.Priority);return S_OK;}),"unsupported priority uses original descriptor");
    check(attempts==std::vector<int>{0},"unsupported HIGH is not requested");
    original.Priority=D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME;
    ok(create(false,false),"retain already higher priority");check(attempts==std::vector<int>{10000},"never lower inherited priority or introduce realtime from normal");
    std::puts("Final delivery priority: supported HIGH, descriptor preservation, original dedicated fallback, failure propagation PASS");
}
}
int main(int argc,char** argv){if(argc>1){if(std::string_view(argv[1])=="phase"){physical_clock_pipeline(true);physical_clock_pipeline(false);late_physical_clock();}else if(std::string_view(argv[1])=="margin"){native_margin_scope(true);native_margin_scope(false);}else if(std::string_view(argv[1])=="pipeline"){overlapped_native_wait(false);overlapped_native_wait(true);overlapped_wait_failure();}else if(std::string_view(argv[1])=="fresh"){fresh_runtime_window(true);fresh_runtime_window(false);fresh_runtime_window(true,5);fresh_runtime_window(false,5);}else{cadence_precision(true);cadence_precision(false);}return 0;}queue_priority_policy();ownership(true);ownership(false);stop_wakeup();failure_wakeup();pending_gpu();preparation_overlap(true);preparation_overlap(false);future_work_dependency(false);future_work_dependency(true);cadence_precision(true);cadence_precision(false);preparation_independence(true);preparation_independence(false);stalled_preparer();unchanged_overlay();same_chain_lookahead(true);same_chain_lookahead(false);changing_overlay_lookahead();stop_prepared_target();reused_real_index(false);reused_real_index(true);static_image_repeat();stop_first_static_target();overlapped_native_wait(false);overlapped_native_wait(true);overlapped_wait_failure();physical_clock_pipeline(true);physical_clock_pipeline(false);late_physical_clock();native_margin_scope(true);native_margin_scope(false);rising_native_wait();fresh_runtime_window(true);fresh_runtime_window(false);fresh_runtime_window(true,5);fresh_runtime_window(false,5);adaptive_source_refill(true);adaptive_source_refill(false);return 0;}
