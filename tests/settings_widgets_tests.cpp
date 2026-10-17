#include "xrfg/settings_widgets.hpp"
#include "imgui.h"
#include "imgui_internal.h"

#include <windows.h>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace xrfg::autonomous;
int checks{};
void require(bool value, const char* detail) {
    ++checks;
    if (!value) throw std::runtime_error(detail);
}

struct Context {
    ImGuiContext* context{};
    SettingsWidgetState widgets{};
    ImVec2 size{};
    bool compact{};
    std::string rendered_text;
    explicit Context(ImVec2 dimensions, bool small) : size(dimensions), compact(small) {
        context = ImGui::CreateContext();
        ImGui::SetCurrentContext(context);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = size;
        io.DeltaTime = 1.F / 60.F;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::StyleColorsDark();
    }
    ~Context() {
        ImGui::SetCurrentContext(context);
        for (auto* texture : ImGui::GetPlatformIO().Textures) {
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
        ImGui::DestroyContext(context);
    }
    void draw(bool capture_text=false) {
        ImGui::SetCurrentContext(context);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoSavedSettings);
        if(capture_text)ImGui::LogToBuffer();
        render_settings_widgets(widgets, snapshot(), {.compact = compact});
        if(capture_text){rendered_text=GImGui->LogBuffer.c_str();ImGui::LogFinish();}
        ImGui::End();
        ImGui::Render();
        // This fixture exercises CPU widgets/navigation, not GPU rendering.
        // Honor the vendored 1.92 dynamic texture protocol with CPU identities.
        for (auto* texture : ImGui::GetPlatformIO().Textures) {
            if (texture->Status == ImTextureStatus_WantDestroy) {
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
            } else if (texture->Status == ImTextureStatus_WantCreate ||
                texture->Status == ImTextureStatus_WantUpdates) {
                texture->SetTexID(static_cast<ImTextureID>(texture->UniqueID + 1));
                texture->SetStatus(ImTextureStatus_OK);
            }
        }
        require(ImGui::GetCurrentContext() == context, "shared widgets preserve caller's context");
        require(ImGui::GetDrawData()->TotalVtxCount > 0, "settings surface renders real draw data");
        if (context->FrameCount >= 2) {
            unsigned tab_vertices{};
            for (int i = 0; i < context->TabBars.GetMapSize(); ++i) {
                auto* bar = context->TabBars.TryGetMapData(i);
                if (!bar) continue;
                for (const auto& vertex : bar->Window->DrawList->VtxBuffer)
                    if (bar->BarRect.Contains(vertex.pos)) ++tab_vertices;
            }
            require(tab_vertices > 24, "settings tab strip is visibly rendered after layout warmup");
        }
    }
    void activate(const char* child, const char* label) {
        ImGui::SetCurrentContext(context);
        for (auto* window : context->Windows) {
            if (std::strstr(window->Name, child)) {
                ImGui::ActivateItemByID(window->GetID(label));
                return;
            }
        }
        throw std::runtime_error("expected settings child was not rendered");
    }
    void choose(const char* label, int index, const char* item, const char* child = "/Generation_") {
        activate(child, label);
        draw();
        draw();
        ImGui::SetCurrentContext(context);
        ImGuiWindow* popup{};
        for (auto* window : context->Windows)
            if (window->Active && (window->Flags & ImGuiWindowFlags_Popup) &&
                std::strstr(window->Name, "##Combo")) popup = window;
        require(popup != nullptr, "generation combo opens its real popup");
        const auto seed = ImHashData(&index, sizeof(index), popup->IDStack.back());
        ImGui::ActivateItemByID(ImHashStr(item, 0, seed));
        draw();
        draw();
    }
    void select_tab(const char* label) {
        ImGui::SetCurrentContext(context);
        for (int i = 0; i < context->TabBars.GetMapSize(); ++i) {
            auto* bar = context->TabBars.TryGetMapData(i);
            if (!bar) continue;
            for (auto& tab : bar->Tabs) {
                if (std::strcmp(ImGui::TabBarGetTabName(bar, &tab), label) == 0) {
                    ImGui::ActivateItemByID(tab.ID);
                    draw();
                    draw();
                    require(bar->SelectedTabId == tab.ID, "tab responds to ImGui navigation activation");
                    return;
                }
            }
        }
        throw std::runtime_error("expected settings tab was not rendered");
    }
};

void rebase_and_publish() {
    SettingsWidgetState tray, headset;
    const auto initial = snapshot();
    synchronize_settings_widgets(tray, initial);
    synchronize_settings_widgets(headset, initial);
    require(tray.initialized && tray.draft == initial.desired,
        "first draw uses current persisted values even at initial revision");
    tray.draft.enabled = !initial.desired.enabled;
    tray.dirty = true;
    require(publish_settings_widgets(tray), "tray edit published through real control channel");
    headset.draft.nr_working_scale = .61F;
    headset.draft.nr_intensity = .42F;
    headset.dirty = true;
    headset.scale_editing = true;
    const auto concurrent = snapshot();
    synchronize_settings_widgets(headset, concurrent);
    require(headset.draft.enabled == tray.draft.enabled && headset.draft.nr_working_scale == .61F,
        "incoming tray change rebases without losing local headset slider edit");
    require(headset.base == concurrent.desired, "new baseline records current external values");
    require(!publish_settings_widgets(headset) && snapshot().revision == concurrent.revision,
        "resource-changing resolution waits until slider release");
    auto other = concurrent.desired;
    other.afw_mode = 2;
    require(request_changes(concurrent.desired, other), "publish interleaved external edit");
    headset.scale_editing = false;
    require(publish_settings_widgets(headset), "released headset edit publishes atomically");
    const auto final = snapshot();
    require(final.desired.enabled == tray.draft.enabled && final.desired.afw_mode == 2 &&
        final.desired.nr_working_scale == .61F && final.desired.nr_intensity == .42F,
        "publication preserves both earlier and raced external edits");
    require(headset.draft == final.desired && !headset.dirty && !headset.save_failed,
        "successful UI publication reloads shared current settings");
    const auto revision = final.revision;
    require(!publish_settings_widgets(headset) && snapshot().revision == revision,
        "idle UI does not republish settings");
    headset.draft.native_scale = 73;
    headset.dirty = headset.scale_editing = true;
    finish_settings_widget_edits(headset);
    require(snapshot().desired.native_scale == 73 && !headset.dirty && !headset.scale_editing,
        "closing a held resolution edit publishes without another ImGui frame");
}

void route_motion_widgets() {
    auto settings = snapshot().desired;
    settings.frame_generation = 0; settings.backend = 3;
    settings.motion_vectors = 0; settings.triple_frame_gen = false;
    require(request(settings), "initialize OFXR optical-flow selection");
    Context tray(ImVec2(674, 861), false), headset(ImVec2(1024, 768), true);
    tray.draw(); tray.draw(); headset.draw(); headset.draw();
    tray.choose("OFXR engine", 3, "Nvidia 0.3.0 (high latency, high performance, medium compatibility)");
    require(snapshot().desired.backend == 2, "new display order keeps 0.3.0 on persisted backend 2");
    headset.draw();
    headset.choose("OFXR engine", 2, "Nvidia 0.2.13 (medium latency, high compatibility)");
    require(snapshot().desired.backend == 3, "new display order keeps 0.2.13 on persisted backend 3");
    tray.draw();
    tray.choose("Frame generation", 1, "Hybrid (slow, needs DLSS)");
    require(snapshot().desired.frame_generation == 3, "actual tray selects Hybrid with stable mode ID");
    headset.draw();
    headset.choose("Frame generation", 0, "OFXR (fast)");
    require(snapshot().desired.motion_vectors == 0, "Hybrid preserves independent OFXR optical flow selection");
    tray.draw();
    tray.choose("Frame generation", 2, "FSR 3.1 (slow, needs DLSS)");
    require(snapshot().desired.frame_generation == 1, "actual tray selects FSR");
    headset.draw();
    headset.choose("Frame generation", 0, "OFXR (fast)");
    require(snapshot().desired.motion_vectors == 0,
        "returning from FSR restores OFXR optical flow");
    tray.draw();
    tray.choose("Frame generation", 3, "DLSS Frame Generation (slowest, needs DLSS)");
    headset.draw();
    headset.choose("Frame generation", 0, "OFXR (fast)");
    require(snapshot().desired.motion_vectors == 0,
        "returning from DLSS-G restores OFXR optical flow");
}
void route_multiplier_widgets() {
    auto settings = snapshot().desired;
    settings.frame_generation = 0; settings.backend = 3;
    settings.motion_vectors = 0; settings.triple_frame_gen = false;
    require(request(settings), "initialize independent multipliers");
    Context tray(ImVec2(674, 861), false), headset(ImVec2(1024, 768), true);
    tray.draw(); tray.draw(); headset.draw(); headset.draw();
    tray.choose("Multiplier", 1, "3X");
    require(snapshot().desired.triple_frame_gen, "actual DJ menu selects 3X");
    headset.draw();
    headset.choose("Frame generation", 3, "DLSS Frame Generation (slowest, needs DLSS)");
    require(!snapshot().desired.triple_frame_gen,
        "DLSS-G keeps its own 2X after DJ selects 3X");
    headset.choose("Multiplier", 1, "3X");
    tray.draw(); tray.choose("Frame generation", 0, "OFXR (fast)");
    tray.choose("Multiplier", 0, "2X");
    headset.draw(); headset.choose("Frame generation", 3, "DLSS Frame Generation (slowest, needs DLSS)");
    require(snapshot().desired.triple_frame_gen,
        "DLSS-G keeps its 3X after DJ selects 2X");
    tray.draw(); tray.choose("Frame generation", 0, "OFXR (fast)");
    require(!snapshot().desired.triple_frame_gen, "DJ keeps its own 2X");
}
void actual_widgets() {
    Context tray(ImVec2(674, 861), false);
    Context headset(ImVec2(1024, 768), true);
    const auto before = snapshot();
    tray.draw();
    headset.draw();
    require(snapshot().revision == before.revision, "opening either settings surface changes no values");
    tray.activate("/Generation_", "Enable frame generation");
    tray.draw();
    const auto enabled = snapshot();
    require(enabled.desired.enabled != before.desired.enabled,
        "real generation checkbox publishes hot setting");
    headset.draw();
    require(headset.widgets.draft.enabled == enabled.desired.enabled,
        "headset reflects hot tray checkbox edit");
    require(enabled.desired.lower_vram == 0, "opening settings preserves Lower VRAM off");
    tray.activate("/Generation_", "Separate synthesis");
    tray.draw(); tray.draw();
    require(snapshot().desired.separate_synthesis == 1 && snapshot().desired.lower_vram == 0,
        "separation alone keeps full XR allocation");
    tray.activate("/Generation_", "Reduce XR swapchains");
    tray.draw();
    require(snapshot().desired.lower_vram == 1 && snapshot().desired.separate_synthesis == 1,
        "tray independently enables reduced allocation");
    headset.draw();
    require(headset.widgets.draft.lower_vram == 1, "headset reflects Lower VRAM on");
    headset.activate("/Generation_", "Separate synthesis");
    headset.draw(); headset.draw();
    require(snapshot().desired.separate_synthesis == 0 && snapshot().desired.lower_vram == 1,
        "headset reduction alone preserves explicit separation off");
    headset.activate("/Generation_", "Reduce XR swapchains");
    headset.draw();
    require(snapshot().desired.lower_vram == 0 && snapshot().desired.separate_synthesis == 0,
        "headset returns to full direct output");
    tray.draw();
    require(tray.widgets.draft.lower_vram == 0, "tray reflects Lower VRAM off");
    headset.select_tab("Neural Rendering");
    headset.activate("/Neural_", "Enable Neural Rendering");
    headset.draw();
    require(snapshot().desired.nr_enabled != enabled.desired.nr_enabled,
        "real headset Neural Rendering checkbox publishes hot setting");
    tray.draw();
    require(tray.widgets.draft.nr_enabled == snapshot().desired.nr_enabled,
        "tray reflects hot headset checkbox edit");
    headset.select_tab("Luke Ross");
    headset.activate("/Framewarp_", "Debug framewarp");
    headset.draw();
    require(snapshot().desired.afw_debug != enabled.desired.afw_debug,
        "real Luke debug checkbox publishes hot setting");
    headset.select_tab("Display");
    headset.choose("Headset FPS counter", 5, "Elliottate's follow controller", "/Display_");
    require(snapshot().desired.overlay_position == 5, "headset controller meter publishes existing display setting");
    tray.select_tab("Display");
    tray.choose("Headset FPS counter", 6, "Elliottate's stick to bottom", "/Display_");
    require(snapshot().desired.overlay_position == 6, "tray bottom meter preserves mode 6");
    headset.draw();
    require(headset.widgets.draft.overlay_position == 6, "both surfaces reflect new meter mode");
    headset.activate("/Display_", "Diagnostic logging");
    headset.draw();
    require(snapshot().desired.diagnostics != enabled.desired.diagnostics,
        "real diagnostics checkbox publishes setting");
    headset.select_tab("Frame generation");
    tray.select_tab("Neural Rendering");
    tray.select_tab("Luke Ross");
    tray.select_tab("Display");
    auto status = snapshot().runtime;
    status.applied_revision = snapshot().revision;
    status.ready = 1;
    status.sessions = 1;
    report_runtime(status);
    const auto revision = snapshot().revision;
    tray.draw();
    headset.draw();
    require(snapshot().revision == revision, "applied-status updates are display-only");
    report_overlay_limit(OverlaySwapchain::menu,true);
    tray.draw(true);headset.draw(true);
    require(tray.rendered_text.find("OpenXR swapchain limit reached")!=std::string::npos &&
        headset.rendered_text.find("OpenXR swapchain limit reached")!=std::string::npos,
        "actual desktop and F10 widgets both render the persistent runtime limit warning");
    report_overlay_limit(OverlaySwapchain::menu,false);
    tray.draw(true);headset.draw(true);
    require(tray.rendered_text.find("OpenXR swapchain limit reached")==std::string::npos &&
        headset.rendered_text.find("OpenXR swapchain limit reached")==std::string::npos,
        "actual settings surfaces clear the warning after a successful retry");
    status=snapshot().runtime;status.swapchains.updated_ms=GetTickCount64();
    status.swapchains.fallback=true;status.swapchains.application=3;status.swapchains.total=14;
    status.swapchains.auxiliary_outputs=1;status.swapchains.private_snapshots=3;
    std::strcpy(status.swapchains.details,"App UNCLASSIFIED: 0x823, 16x16, array 1, format 28");
    report_runtime(status);
    tray.select_tab("Luke Ross");headset.select_tab("Luke Ross");
    tray.draw(true);headset.draw(true);
    for(const auto* view:{&tray,&headset}) {
        require(view->rendered_text.find("Framewarp mode")!=std::string::npos &&
            view->rendered_text.find("Known XR handles:")==std::string::npos,
            "Luke controls remain above collapsed swapchain Details");
        require(view->rendered_text.find("Swapchain fallback")!=std::string::npos,
            "status warns even with ownership Details collapsed");
    }
    tray.activate("/Framewarp_","Details");headset.activate("/Framewarp_","Details");
    tray.draw(true);headset.draw(true);
    for(const auto* view:{&tray,&headset}) {
        require(view->rendered_text.find("Framewarp mode")<view->rendered_text.find("Known XR handles:"),
            "expanded swapchain details follow the Luke controls");
        require(view->rendered_text.find("Swapchain fallback")!=std::string::npos,
            "desktop/headset status and warning expose actual fallback");
        require(view->rendered_text.find("Known XR handles: 14")!=std::string::npos &&
            view->rendered_text.find("0x823")!=std::string::npos,
            "Luke tab exposes handle count and unknown app identity");
    }
    auto quality_settings=snapshot().desired;quality_settings.afw_mode=3;
    require(request(quality_settings),"select Combined for live quality control");
    tray.draw();tray.draw();status.afw_shading_rate=0;report_runtime(status);
    tray.choose("AFW shading rate",1,"2x2 (half per axis)","/Framewarp_");
    require(snapshot().desired.afw_shading_rate==4,"live quality dropdown publishes beta6 2x2 index");
    tray.draw(true);
    require(tray.rendered_text.find("Quality change pending")!=std::string::npos &&
        tray.rendered_text.find("next launch")==std::string::npos,"AFW UI exposes active versus pending without claiming a restart");
    status.afw_shading_rate=4;report_runtime(status);tray.draw(true);
    require(tray.rendered_text.find("Quality change pending")==std::string::npos,
        "successful active quality update clears pending explanation");
    const auto content_height=[&](Context& view) {
        for(auto* window:view.context->Windows)
            if(window->Active && std::strstr(window->Name,"/Framewarp_"))return window->ContentSize.y;
        throw std::runtime_error("missing Luke child window");
    };
    tray.draw();tray.draw(); // settle ContentSize after the pending line disappears
    const auto unpadded=content_height(tray);
    std::strcpy(status.swapchains.details,"\n\n  \nApp UNCLASSIFIED: 0x823, 16x16, array 1, format 28\n\n\n");
    report_runtime(status);tray.draw();tray.draw();
    require(std::abs(content_height(tray)-unpadded)<1.F,
        "empty/padded ownership rows do not add a blank block before the final explanation");
    status.swapchains.fallback=false;status.swapchains.other=0;status.swapchains.details[0]=0;
    report_runtime(status);tray.draw(true);
    require(tray.rendered_text.find("Swapchain fallback")==std::string::npos,"fallback warning does not stick after a fresh healthy session");

}
} // namespace

int main(int argc, char** argv) {
    const auto profile = std::filesystem::temp_directory_path() /
        (L"ofxr-settings-widgets-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()));
    SetEnvironmentVariableW(L"XRFG_AUTONOMOUS_PROFILE_DIR", profile.c_str());
    try {
        if (argc == 2 && std::strcmp(argv[1], "--route-motion") == 0) route_motion_widgets();
        else if (argc == 2 && std::strcmp(argv[1], "--route-multiplier") == 0) route_multiplier_widgets();
        else { rebase_and_publish(); actual_widgets(); route_motion_widgets(); route_multiplier_widgets(); }
        std::filesystem::remove(configuration_path());
        std::filesystem::remove(profile);
        std::cout << checks << " shared settings widget checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(configuration_path());
        std::filesystem::remove(profile);
        std::cerr << "settings widgets failed: " << error.what() << '\n';
        return 1;
    }
}
