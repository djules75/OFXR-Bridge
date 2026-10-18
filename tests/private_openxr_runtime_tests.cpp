#include "xrfg/private_openxr_runtime.hpp"
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
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context;
    std::unordered_map<XrSwapchain,Chain> chains;
    std::uintptr_t next_handle{100};
    std::mutex gpu,mutex;
    std::condition_variable changed;
    std::atomic_bool block_end{true},end_entered{},fail_end{};
    std::atomic_uint real_acquires{},real_waits{},real_begins{},real_ends{};
    bool cycle_waited{},cycle_begun{};
    bool throttled{true},real_common{};
    std::atomic_int slow_image_wait_ms{};
    std::atomic_uint empty_ends{};
    unsigned expected_views{1};
    std::function<XrResult(XrSwapchain,unsigned)> release_callback;
    std::function<void()> after_head_ready;
    std::function<D3D12_RESOURCE_STATES()> create_state;
    std::vector<unsigned> pixels;
    std::vector<float> positions;
    std::vector<std::int64_t> handoffs;
    std::vector<XrTime> targets;
    XrTime raw_prediction{};
    std::shared_ptr<xrfg::PrivateOpenXrRuntime> transport;
    explicit Fixture(bool d12,bool split=true):native12(d12) {
        current=this;
        if(d12) {
            ok(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device12)),"actual D3D12 device");
            D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
            ok(device12->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)),"queue");
            if(split)ok(device12->CreateCommandQueue(&desc,IID_PPV_ARGS(&delivery_queue)),"independent delivery queue");
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
        options.trace=[this](int stage,std::uint64_t,std::int64_t,std::int64_t){if(stage==4 && after_head_ready)after_head_ready();};
        transport=std::make_shared<xrfg::PrivateOpenXrRuntime>(std::move(options));
        ok(transport->initialize(),"initialize actual private endpoint");xr_ok(transport->start(),"start endpoint");
    }
    ~Fixture(){block_end=false;changed.notify_all();if(gate)gate->Signal(1);transport->stop();transport.reset();CloseHandle(event);if(preparation_timer)CloseHandle(preparation_timer);current=nullptr;}
    void gpu_wait(ID3D12CommandQueue* target=nullptr){if(!target)target=queue.Get();ok(target->Signal(fence.Get(),++serial),"test signal");ok(fence->SetEventOnCompletion(serial,event),"test event");check(WaitForSingleObject(event,5000)==WAIT_OBJECT_0,"test GPU complete");}
    template<class Record> void commands(Record record,ID3D12CommandQueue* target=nullptr) {
        if(!target)target=queue.Get();
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
        ok(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"test allocator");
        ok(device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"test list");
        record(list.Get());ok(list->Close(),"test close");ID3D12CommandList* lists[]={list.Get()};target->ExecuteCommandLists(1,lists);gpu_wait(target);
    }
    static XrResult XRAPI_CALL wait(XrSession,const XrFrameWaitInfo*,XrFrameState* output) {
        auto& f=*current;const auto n=++f.real_waits;
        check(!f.cycle_waited && !f.cycle_begun,"one real Wait per cycle");f.cycle_waited=true;
        // Vary the runtime horizon while retaining the monotonic prediction
        // required by OpenXR. XR time deliberately has another epoch than QPC.
        f.raw_prediction=std::max(f.raw_prediction+1,epoch+ns()+((!f.throttled || n%2)?period:period*3));
        output->predictedDisplayTime=f.raw_prediction;output->predictedDisplayPeriod=(!f.throttled || n%2)?period:period*2;output->shouldRender=XR_TRUE;return XR_SUCCESS;
    }
    static XrResult XRAPI_CALL begin(XrSession,const XrFrameBeginInfo*){auto& f=*current;check(f.cycle_waited && !f.cycle_begun,"real Begin follows real Wait");f.cycle_waited=false;f.cycle_begun=true;++f.real_begins;return XR_SUCCESS;}
    static XrResult XRAPI_CALL end(XrSession,const XrFrameEndInfo* info) {
        auto& f=*current;
        check(f.cycle_begun && !f.cycle_waited,"real End follows real Begin");f.cycle_begun=false;
        const auto start=ns();
        if(!info->layerCount){++f.empty_ends;return XR_SUCCESS;}
        if(info->layerCount){std::unique_lock lock(f.mutex);f.end_entered=true;f.changed.notify_all();f.changed.wait(lock,[&]{return !f.block_end.load();});}
        check(info->displayTime==f.raw_prediction,"End must retain this cycle's exact runtime prediction");
        unsigned color{};float position{};
        if(info->layerCount) {
            auto* p=reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
            check(p->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION && p->viewCount==f.expected_views,"owned projection metadata");
            auto& chain=f.chains.at(p->views[0].subImage.swapchain);position=p->views[0].pose.position.x;
            std::scoped_lock lock(f.gpu);
            if(f.native12) {
                auto source=chain.d12[chain.released];D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;
                ok(f.device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"readback");
                f.commands([&](ID3D12GraphicsCommandList* list){
                    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={source.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,chain.state,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
                    D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=source.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,1,1,1,256};D3D12_BOX box{0,0,0,1,1,1};list->CopyTextureRegion(&to,0,0,0,&from,&box);
                    std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
                },f.delivery_queue.Get());
                void* data{};ok(readback->Map(0,nullptr,&data),"map readback");color=*static_cast<unsigned char*>(data);readback->Unmap(0,nullptr);
            } else {
                auto source=chain.d11[chain.released];D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.BindFlags=desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> readback;
                ok(f.device11->CreateTexture2D(&desc,nullptr,&readback),"D11 readback");f.context->CopyResource(readback.Get(),source.Get());D3D11_MAPPED_SUBRESOURCE mapped{};ok(f.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"D11 map");color=*static_cast<unsigned char*>(mapped.pData);f.context->Unmap(readback.Get(),0);
            }
        }
        {std::scoped_lock lock(f.mutex);f.pixels.push_back(color);f.positions.push_back(position);f.handoffs.push_back(start);f.targets.push_back(info->displayTime);++f.real_ends;}
        f.changed.notify_all();return f.fail_end?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
    }
    static XrResult XRAPI_CALL create(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* output) {
        auto& f=*current;*output=reinterpret_cast<XrSwapchain>(f.next_handle++);auto& chain=f.chains[*output];
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
    static XrResult XRAPI_CALL acquire(XrSwapchain h,const XrSwapchainImageAcquireInfo*,std::uint32_t* index){auto& f=*current;check(f.cycle_begun,"real image preparation belongs after Begin");auto& c=f.chains.at(h);check(!c.owned,"physical image ownership");c.acquired=(c.released+1)%2;c.owned=true;c.waited=false;*index=c.acquired;++f.real_acquires;return XR_SUCCESS;}
    static XrResult XRAPI_CALL wait_image(XrSwapchain h,const XrSwapchainImageWaitInfo*){auto& f=*current;auto& c=f.chains.at(h);check(c.owned,"physical wait after acquire");if(const auto delay=f.slow_image_wait_ms.load()){LARGE_INTEGER due{};due.QuadPart=-delay*10'000LL;check(f.preparation_timer && SetWaitableTimer(f.preparation_timer,&due,0,nullptr,nullptr,FALSE),"precise preparation delay");check(WaitForSingleObject(f.preparation_timer,1000)==WAIT_OBJECT_0,"preparation delay completed");}c.waited=true;return XR_SUCCESS;}
    static XrResult XRAPI_CALL release(XrSwapchain h,const XrSwapchainImageReleaseInfo*){auto& c=current->chains.at(h);check(c.owned && c.waited,"physical release after wait");c.released=c.acquired;c.owned=false;return XR_SUCCESS;}
    static XrResult XRAPI_CALL to_qpc(XrInstance,XrTime t,LARGE_INTEGER* q){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);const auto n=t-epoch;q->QuadPart=n/1'000'000'000LL*f.QuadPart+n%1'000'000'000LL*f.QuadPart/1'000'000'000LL;return XR_SUCCESS;}
    XrSwapchain make_chain() {
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.format=DXGI_FORMAT_R8G8B8A8_UNORM;info.width=info.height=info.arraySize=info.faceCount=info.mipCount=info.sampleCount=1;info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
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
    XrResult submit(XrSwapchain handle,float identity) {
        XrCompositionLayerProjectionView view{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};view.pose.orientation.w=1;view.pose.position.x=identity;view.subImage.swapchain=handle;view.subImage.imageRect.extent={1,1};
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=reinterpret_cast<XrSpace>(9);projection.viewCount=1;projection.views=&view;
        auto* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};info.displayTime=epoch+ns();info.layerCount=1;info.layers=&layer;info.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
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
    check(f.real_acquires==4,"no real acquisition on producer");xr_ok(f.transport->destroy_swapchain(chain),"destroy drained image");
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
    check(f.real_begins==f.empty_ends && f.real_acquires==0,"no content cycle before FIFO head GPU completion (startup empty clock cycle is allowed)");
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
    for(unsigned i=1;i<f.handoffs.size();++i)check(f.handoffs[i]-f.handoffs[i-1]>=period,"fresh prediction keeps physical refresh minimum");
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
}
int main(int argc,char** argv){if(argc>1){preparation_overlap(true);preparation_overlap(false);return 0;}ownership(true);ownership(false);stop_wakeup();failure_wakeup();pending_gpu();preparation_overlap(true);preparation_overlap(false);future_work_dependency(false);future_work_dependency(true);return 0;}
