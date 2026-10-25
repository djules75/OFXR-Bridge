// Real GPU ownership test: a deliberately parked OFXR lane must not park the
// next capture/AFW ingress copy. Completed outputs alone may reach D11/XR.
#include "xrfg/d3d11_d3d12_interop.hpp"
#include "xrfg/d3d12_history.hpp"
#include "xrfg/d3d12_frame_synthesizer.hpp"
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void ok(HRESULT value, const char* text) { require(SUCCEEDED(value), text); }
bool completed(ID3D12Fence* fence, UINT64 value, DWORD timeout=2000) {
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    require(event!=nullptr,"completion event");
    const auto hr=fence->SetEventOnCompletion(value,event);
    const bool ready=SUCCEEDED(hr) && WaitForSingleObject(event,timeout)==WAIT_OBJECT_0;
    CloseHandle(event);return ready;
}
// Declared AFTER GPU owners so an assertion cannot strand their destructors.
struct ReleaseGate { ComPtr<ID3D12Fence> fence; ~ReleaseGate(){if(fence)fence->Signal(UINT64_MAX-1);} };
}
int main() {
    try {
        ComPtr<ID3D11Device> d11;ComPtr<ID3D11DeviceContext> context;
        ok(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&d11,nullptr,&context),"hardware D11");
        ComPtr<ID3D12Device> d12;ComPtr<ID3D12CommandQueue> producer, synthesis;
        ok(xrfg::create_d3d12_device_for_d3d11(d11.Get(),&d12,&producer),"D12 owner");
        const auto queue_desc=producer->GetDesc();
        ok(d12->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&synthesis)),"separate OFXR queue");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=128;desc.Height=64;desc.ArraySize=2;desc.MipLevels=1;
        desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> source, current0, scratch, synthetic;
        for(auto address:{source.GetAddressOf(),current0.GetAddressOf(),scratch.GetAddressOf(),synthetic.GetAddressOf()})
            ok(d11->CreateTexture2D(&desc,nullptr,address),"eye texture");
        ID3D11Texture2D* sources[]{source.Get()};
        ID3D11Texture2D* currents[]{current0.Get(),scratch.Get()};
        ID3D11Texture2D* synthetics[]{synthetic.Get()};
        xrfg::D3D11D3D12SwapchainInterop interop;
        ok(interop.initialize(d11.Get(),context.Get(),d12.Get(),producer.Get(),sources,currents,synthetics),"interop");
        require(FAILED(interop.configure_synthesis_queue(producer.Get())),"shared queue must not masquerade as isolated");
        ok(interop.configure_synthesis_queue(synthesis.Get()),"isolated timeline");
        ok(interop.configure_synthesis_queue(synthesis.Get()),"idempotent lane setup");
        auto history=std::make_shared<xrfg::D3D12SwapchainHistory>();
        ok(history->initialize(d12.Get(),producer.Get(),interop.source_images(),D3D12_RESOURCE_STATE_COMMON),"capture history");
        xrfg::D3D12FrameSynthesizer ofxr, afw_ingress;
        const auto initialize=[&](auto& engine,ID3D12CommandQueue* queue) {
            ok(engine.initialize(d12.Get(),queue,history,interop.current_destination_images(),
                interop.synthetic_destination_images(),desc.Format,D3D12_RESOURCE_STATE_COMMON,
                xrfg::D3D12OpticalFlowBackend::fidelity_fx,{},false,
                xrfg::D3D12FrameGenerationMode::ofxr,true,true),"copy-only engine");
        };
        initialize(ofxr,synthesis.Get());initialize(afw_ingress,producer.Get());
        ComPtr<ID3D12Fence> gate, progress;
        ok(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"owned gate");
        ok(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&progress)),"producer progress");
        ReleaseGate release{gate};
        std::array<xrfg::D3D12ReprojectionView,2> views{};
        for(unsigned eye=0;eye<2;++eye){views[eye].array_slice=eye;
            views[eye].image_rect={0,0,desc.Width,desc.Height};
            views[eye].pose.orientation.w=1;
            views[eye].fov={-0.7f,0.7f,0.7f,-0.7f};}
        const std::array<std::uint32_t,2> first{0xff153759,0xff26486a};
        const std::array<std::uint32_t,2> second{0xff917355,0xff826446};
        const auto capture=[&](const auto& colours) {
            std::vector<std::uint32_t> pixels(desc.Width*desc.Height);
            for(unsigned eye=0;eye<2;++eye){std::fill(pixels.begin(),pixels.end(),colours[eye]);
                context->UpdateSubresource(source.Get(),eye,nullptr,pixels.data(),desc.Width*4,0);}
            ok(interop.prepare_capture(0),"D11 ingress");
            xrfg::D3D12HistoryCaptureTicket ticket;
            ok(history->capture(0,&ticket),"history copy");ok(interop.finish_capture(),"capture timeline");
            ok(history->commit(ticket),"capture commit");return ticket;
        };
        auto staging_desc=desc;staging_desc.Usage=D3D11_USAGE_STAGING;
        staging_desc.BindFlags=0;staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;
        ok(d11->CreateTexture2D(&staging_desc,nullptr,&readback),"pixel witness");
        const auto pixels_equal=[&](ID3D11Texture2D* image,const auto& expected) {
            context->CopyResource(readback.Get(),image);
            for(unsigned eye=0;eye<2;++eye){D3D11_MAPPED_SUBRESOURCE mapped{};
                ok(context->Map(readback.Get(),eye,D3D11_MAP_READ,0,&mapped),"read terminal eye");
                bool match=true;
                for(unsigned y=0;y<desc.Height;++y){auto row=reinterpret_cast<const std::uint32_t*>(
                    static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch);
                    match &= std::all_of(row,row+desc.Width,[&](auto v){return v==expected[eye];});}
                context->Unmap(readback.Get(),eye);require(match,"terminal pixels/eye identity changed");}
        };
        const auto a=capture(first);
        ok(synthesis->Wait(gate.Get(),1),"park only OFXR");
        ok(interop.prepare_synthesis(true),"OFXR destination ownership");
        xrfg::D3D12FrameSynthesisTicket pending;
        ok(ofxr.submit_copy(a,views,0,&pending),"enqueue parked OFXR");
        const auto pending_fence=ofxr.submission_fence(pending);
        const auto b=capture(second);
        ok(interop.prepare_synthesis(false),"AFW ingress destination ownership");
        xrfg::D3D12FrameSynthesisTicket ingress;
        ok(afw_ingress.submit_copy(b,views,1,&ingress),"AFW ingress after parked OFXR");
        ok(producer->Signal(progress.Get(),1),"capture/AFW progress marker");
        require(completed(progress.Get(),1),"OFXR fence blocked capture/AFW queue");
        require(ofxr.ticket_available(pending)==S_FALSE,"parked OFXR unexpectedly completed");
        const auto before=interop.publication_snapshot();
        require(interop.publish_completed(0,{},pending_fence.Get(),pending.fence_value)==HRESULT_FROM_WIN32(ERROR_BUSY),
            "unfinished OFXR must not publish");
        require(interop.publication_snapshot().copy_complete_value==before.copy_complete_value,
            "rejected publication changed ownership");
        xrfg::D3D11PublicationReceipt receipt;
        ok(interop.publish(1,{},&receipt),"AFW publication independent of OFXR");
        ok(xrfg::wait_for_d3d11_publication(receipt),"AFW publication completion");
        pixels_equal(scratch.Get(),second);
        ok(gate->Signal(1),"release OFXR");ok(ofxr.wait_for_idle(),"OFXR completion");
        ok(interop.publish_completed(0,{},pending_fence.Get(),pending.fence_value,&receipt),"completed OFXR publish");
        ok(xrfg::wait_for_d3d11_publication(receipt),"OFXR publication completion");
        pixels_equal(current0.Get(),first);
        // Exercise inline Framework publication on the new lane and interleave
        // capture-lane signals: each D12 timeline must still have one writer.
        for(unsigned i=0;i<12;++i){
            const auto ticket=capture(i%2?second:first);
            ok(interop.prepare_synthesis(true),"inline OFXR access");
            xrfg::D3D12FrameSynthesisTicket output;
            ok(ofxr.submit_copy(ticket,views,0,&output),"inline copy");
            ok(interop.publish(0,{},&receipt,{},true),"inline synthesis timeline");
            ok(xrfg::wait_for_d3d11_publication(receipt),"inline completion");
            pixels_equal(current0.Get(),i%2?second:first);
        }
        // A newer disjoint AFW scratch readback must not become a dependency of an
        // already frozen post-AFW history. The same output slot's pending
        // publication, however, MUST delay its reuse.
        const auto frozen=capture(first);
        ok(history->wait_for_idle(),"frozen history ready");
        ComPtr<ID3D12Fence> input_gate;ComPtr<ID3D11Fence> input_gate11;
        ok(d12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&input_gate)),"D11 input gate");
        ReleaseGate release_input{input_gate};
        HANDLE shared{};ok(d12->CreateSharedHandle(input_gate.Get(),nullptr,GENERIC_ALL,nullptr,&shared),"shared input gate");
        ComPtr<ID3D11Device5> device5;ComPtr<ID3D11DeviceContext4> context4;
        ok(d11.As(&device5),"D11 fence owner");ok(context.As(&context4),"D11 fence context");
        const auto opened=device5->OpenSharedFence(shared,IID_PPV_ARGS(&input_gate11));CloseHandle(shared);ok(opened,"open input gate");
        ok(context4->Wait(input_gate11.Get(),1),"park next AFW scratch publication");
        xrfg::D3D11PublicationReceipt scratch_receipt;
        const auto ingress_fence=afw_ingress.submission_fence(ingress);
        ok(interop.publish_completed(1,{},ingress_fence.Get(),ingress.fence_value,&scratch_receipt),"pending disjoint AFW scratch");
        ok(interop.capture_published_current(scratch_receipt),"pending scratch terminal readback");
        ok(interop.prepare_synthesis_targets(0,{}),"exact worker output dependencies");
        xrfg::D3D12FrameSynthesisTicket independent;
        ok(ofxr.submit_copy(frozen,views,0,&independent),"frozen input bypasses disjoint scratch readback");
        const auto independent_fence=ofxr.submission_fence(independent);
        require(completed(independent_fence.Get(),independent.fence_value,300),"scratch cursor parked immutable OFXR job");
        ok(interop.prepare_synthesis(true),"legacy conservative dependency control");
        ok(synthesis->Signal(progress.Get(),3),"legacy wait witness");
        require(!completed(progress.Get(),3,30),"legacy cursor control did not block");
        ok(input_gate->Signal(1),"release scratch readback");require(completed(progress.Get(),3),"legacy cursor drain");
        ok(context4->Wait(input_gate11.Get(),2),"park actual output publication");context->Flush();
        ok(interop.publish_completed(0,{},independent_fence.Get(),independent.fence_value,&receipt),"pending same-slot D11 read");
        ok(interop.prepare_synthesis_targets(0,{}),"wait for reused output slot");
        ok(synthesis->Signal(progress.Get(),4),"reuse witness");
        require(!completed(progress.Get(),4,30),"output slot overwritten before its D11 read retired");
        ok(input_gate->Signal(2),"release output read");require(completed(progress.Get(),4),"slot ownership drain");
        pixels_equal(current0.Get(),first);
        require(FAILED(interop.prepare_synthesis_targets(99,{})),"invalid output accepted");
        // Control: gating the shared lane really does block producer progress.
        ok(producer->Wait(gate.Get(),2),"shared queue control gate");
        ok(producer->Signal(progress.Get(),5),"shared queue control signal");
        require(!completed(progress.Get(),5,30),"shared queue control did not block");
        ok(gate->Signal(2),"release shared control");require(completed(progress.Get(),5),"control drain");
        ok(afw_ingress.wait_for_idle(),"ingress drain");ok(ofxr.wait_for_idle(),"synthesis drain");
        ok(interop.wait_for_idle(),"both publication timelines drained");ok(history->invalidate(),"history retirement");
        std::cout<<"PASS: blocked OFXR leaves capture/AFW progressing; both eyes exact; inline/completed publication and drain\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
