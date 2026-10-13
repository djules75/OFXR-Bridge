// Exercise the shipping recorder with a private session, without sending
// keyboard input or adding a test-only control to the distributed layer.
#include "../src/common/two_frame_capture.cpp"
#include "xrfg/d3d11_d3d12_interop.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void success(HRESULT value, const char* message) { require(SUCCEEDED(value), message); }
}

int main() {
    using Microsoft::WRL::ComPtr;
    try {
        ComPtr<ID3D11Device> d11;
        ComPtr<ID3D11DeviceContext> context;
        success(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_SINGLETHREADED, nullptr, 0, D3D11_SDK_VERSION,
            &d11, nullptr, &context), "D11 device");
        ComPtr<ID3D12Device> d12;
        ComPtr<ID3D12CommandQueue> queue;
        success(xrfg::create_d3d12_device_for_d3d11(d11.Get(), &d12, &queue), "matching D12 device");
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 256; description.Height = 64;
        description.ArraySize = 2; description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> source, current, synthetic;
        success(d11->CreateTexture2D(&description, nullptr, &source), "source");
        success(d11->CreateTexture2D(&description, nullptr, &current), "current");
        success(d11->CreateTexture2D(&description, nullptr, &synthetic), "synthetic");
        ID3D11Texture2D* sources[]{source.Get()};
        ID3D11Texture2D* currents[]{current.Get()};
        ID3D11Texture2D* synthetics[]{synthetic.Get()};
        xrfg::D3D11D3D12SwapchainInterop bridge;
        success(bridge.initialize(d11.Get(), context.Get(), d12.Get(), queue.Get(),
            sources, currents, synthetics), "interop");
        const auto mirror = bridge.current_destination_images()[0];
        HANDLE shared{};
        success(d12->CreateSharedHandle(mirror, nullptr, GENERIC_ALL, nullptr, &shared), "mirror share");
        ComPtr<ID3D11Device1> device1;
        ComPtr<ID3D11Texture2D> alias;
        success(d11.As(&device1), "device1");
        const HRESULT opened = device1->OpenSharedResource1(shared, IID_PPV_ARGS(&alias));
        CloseHandle(shared);
        success(opened, "mirror alias");
        const std::array<std::uint32_t, 2> expected{0xff13579bu, 0xff2468acu};
        std::vector<std::uint32_t> pixels(description.Width * description.Height);
        for (unsigned eye = 0; eye < 2; ++eye) {
            std::fill(pixels.begin(), pixels.end(), expected[eye]);
            context->UpdateSubresource(alias.Get(), eye, nullptr, pixels.data(), description.Width * 4, 0);
        }
        // The ordinary source handshake orders preceding D11 alias writes.
        success(bridge.prepare_capture(0), "seed order");
        xrfg::D3D11PublicationReceipt receipt;
        success(bridge.publish(0, std::nullopt, &receipt), "publish exact XR image");
        success(xrfg::wait_for_d3d11_publication(receipt), "publication completion");

        xrfg::TwoFrameImageInfo info{};
        info.stage = "afw_xr_terminal_eye";
        info.capture_serial = 933;
        info.stream = 1234;
        info.swapchain = 5678;
        info.display_time = 9012;
        info.width = description.Width; info.height = description.Height;
        const auto before = bridge.publication_snapshot();
        require(bridge.diagnostic_capture_published_current(receipt, info) == S_FALSE,
            "inactive recorder must bypass");
        const auto after = bridge.publication_snapshot();
        require(before.ready_value == after.ready_value &&
            before.copy_complete_value == after.copy_complete_value,
            "inactive capture advanced publication");
        xrfg::diagnostic_ring_enabled = true;
        require(!xrfg::two_frame_capture_images_active() &&
            bridge.diagnostic_capture_published_current(receipt, info) == S_FALSE,
            "F9 ring must not enable full-resolution F8 copies");
        xrfg::diagnostic_ring_enabled = false;

        auto& manager = xrfg::manager();
        manager.session = std::make_shared<xrfg::Session>();
        manager.active_frame = 1;
        auto session = manager.session;
        // Poison only the D12 mirror after XR publication. A mirror-only
        // witness would now return the wrong bytes for both eyes.
        std::fill(pixels.begin(), pixels.end(), 0xffeeffffu);
        for (unsigned eye = 0; eye < 2; ++eye)
            context->UpdateSubresource(alias.Get(), eye, nullptr, pixels.data(), description.Width * 4, 0);
        for (unsigned eye = 0; eye < 2; ++eye) {
            info.eye = static_cast<int>(eye); info.slice = eye;
            success(bridge.diagnostic_capture_published_current(receipt, info), "terminal recorder");
        }
        success(bridge.wait_for_idle(), "diagnostic retirement");
        require(session->shots.size() == 2, "two exact eye shots");
        for (unsigned eye = 0; eye < 2; ++eye) {
            const auto& shot = *session->shots[eye];
            require(shot.owned_allocator && shot.producer_list && shot.fence &&
                xrfg::completed(shot), "retained submitted allocator/list/fence");
            require(shot.info.capture_serial == 933 && shot.info.eye == static_cast<int>(eye) &&
                shot.info.swapchain == 5678 && shot.info.display_time == 9012,
                "terminal identity changed");
            void* data{};
            success(shot.readback->Map(0, nullptr, &data), "readback map");
            bool exact = true;
            for (UINT y = 0; y < description.Height; ++y) {
                const auto* row = reinterpret_cast<const std::uint32_t*>(
                    static_cast<const unsigned char*>(data) + shot.footprint.Offset +
                    y * shot.footprint.Footprint.RowPitch);
                for (UINT x = 0; x < description.Width; ++x) exact &= row[x] == expected[eye];
            }
            shot.readback->Unmap(0, nullptr);
            require(exact, "witness read poisoned mirror instead of actual XR publication");
        }
        // Exhaustion must fail the diagnostic without changing XR pixels or
        // its immutable publication receipt; the final retirement still runs.
        session->allocated = xrfg::kMaximumBytes;
        require(bridge.diagnostic_capture_published_current(receipt, info) == E_OUTOFMEMORY,
            "bounded recorder budget");
        success(bridge.wait_for_idle(), "failed diagnostic retirement");
        require(receipt.copy_complete_value == before.copy_complete_value,
            "diagnostic rewrote publication receipt");
        manager.active_frame = 0;
        manager.session.reset();
        success(d12->GetDeviceRemovedReason(), "device retained");
        std::cout << "F8 terminal pixels: exact original D11 publication in both slices despite poisoned D12 mirror; inactive/ring bypass, allocator retention and budget retirement pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
