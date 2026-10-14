#include "xrfg/settings_widgets.hpp"
#include "imgui.h"
#include <windows.h>

#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdio>

namespace xrfg::autonomous {
void apply_settings_style() {
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    // Upstream status-panel sRGB palette, adapted to ImGui controls.
    s.Colors[ImGuiCol_WindowBg] = ImVec4(14/255.F,18/255.F,28/255.F,232/255.F);
    s.Colors[ImGuiCol_ChildBg] = ImVec4(0,0,0,0);
    s.Colors[ImGuiCol_Text] = ImVec4(238/255.F,241/255.F,247/255.F,1);
    s.Colors[ImGuiCol_TextDisabled] = ImVec4(148/255.F,158/255.F,178/255.F,1);
    s.Colors[ImGuiCol_Border] = ImVec4(64/255.F,74/255.F,98/255.F,1);
    s.Colors[ImGuiCol_Separator] = ImVec4(48/255.F,56/255.F,76/255.F,1);
    s.WindowRounding = 0; s.FrameRounding = 4;
}
namespace {

void status(const Snapshot& state) {
    const auto& m = state.runtime.telemetry;
    const auto now = GetTickCount64();
    const bool current = state.connected && m.updated_ms && now >= m.updated_ms && now - m.updated_ms < 2000;
    ImGui::TextDisabled("OFXR 0.3.0 (v474)");
    const auto value = [](const char* label, float number, bool valid, bool rate,
                          const char* tip, bool late = false) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", label);
        char text[64] = "N/A";
        if (valid && std::isfinite(number)) {
            if (rate) std::snprintf(text, sizeof(text), "%.0f", number);
            else if (late && number < 0) std::snprintf(text, sizeof(text), "Late by %.1f ms", -number);
            else std::snprintf(text, sizeof(text), "%.1f ms", number);
        }
        ImGui::PushFont(ImGui::GetFont(), rate ? 28.F : 21.F);
        ImGui::TextWrapped("%s", text);
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };
    if (ImGui::BeginTable("Interface metrics", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        value("Output FPS", m.output_fps, current && m.rates_valid, true,
            m.output_source == InterfaceSource::steamvr_delivery ?
            "Presented SteamVR compositor frames per second. Late presentation is included; repeated scanouts counted once. Source scene identity is not measured. Native DLSS-G is capped by confirmed interpolation." :
            "Accepted distinct real and generated submissions per second, not measured scanout. Repeats/native copies excluded.");
        value("Game FPS", m.game_fps, current && m.rates_valid, true, "Distinct real frames successfully submitted per second.");
        value("Generated FPS", m.generated_fps, current && m.rates_valid, true, "Fresh generated frames successfully submitted per second. This is not measured display delivery. Native DLSS-G requires confirmed GPU interpolation.");
        value("Display Hz", m.display_hz, current && m.display_hz > 0, true,
            m.display_source == InterfaceSource::steamvr_display ? "SteamVR physical display frequency." : "OpenXR shortest observed predicted display period; may reflect runtime throttling.");
        ImGui::TableNextRow();
        value("OFXR time", m.ofxr_time_ms, current && m.ofxr_time_valid, false,
            "Wall time in this frame's app Wait/Begin/Acquire/Wait/Release/End calls, including blocking runtime calls and waits after submission. Concurrent calls counted once; game code between calls excluded.");
        value("Cycle length", m.cycle_length_ms, current && m.cycle_valid, false,
            "CPU cycle: entry into this frame's app WaitFrame to the later of its real submission accepted by the runtime and app EndFrame return. Includes game production and all producer waits; not GPU completion or measured display latency.");
        value("Cycle estimated", m.cycle_estimated_ms, current && m.cycle_estimated_valid, false,
            "Estimated full cycle: real frame assigned to the first display slot after the generated frame that can receive it. Includes the measured cycle without adding waits twice. Uses SteamVR display period when available, otherwise OpenXR period. Assumes distinct outputs are shown; not measured scanout or streaming. N/A without a confirmed generated frame and known period.");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("State");
        const auto text = interface_state_text(state);
        const auto color = text.starts_with("Generating") ? ImVec4(.36F,.88F,.56F,1) :
            text == "Fallback" || text == "Applying settings" ? ImVec4(1,.75F,.28F,1) : ImVec4(.75F,.8F,.88F,1);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndTable();
    }
    ImGui::Text("Vectors: %s", interface_vectors_text(state, now).data());
    const auto explanation = interface_status_text(state);
    ImGui::TextWrapped("Status: %s", explanation.c_str());
    ImGui::Separator();
}

bool generation(SettingsWidgetState& ui, const Snapshot& state) {
    auto& draft = ui.draft;
    bool changed = ImGui::Checkbox("Enable frame generation", &draft.enabled);
    int selected_generation = generation_ui_index(draft.frame_generation);
    if (ImGui::Combo("Frame generation", &selected_generation,
            "OFXR (fast)\0Hybrid (slow, needs DLSS)\0FSR 3.1 (slow, needs DLSS)\0DLSS Frame Generation (slowest, needs DLSS)\0")) {
        select_generation(draft, generation_from_ui(selected_generation), draft.backend);
        changed = true;
    }
    if (draft.frame_generation == 0) {
        // UI order differs from persisted backend IDs: 0.2.13 is backend 3,
        // while 0.3.0 is backend 2. Keep each existing profile on its engine.
        constexpr int engine_order[] = {0, 1, 3, 2};
        int selected_engine = engine_order[std::clamp(draft.backend, 0, 3)];
        if (ImGui::Combo("OFXR engine", &selected_engine,
                "FidelityFX (low latency, medium compatibility)\0Nvidia 0.2.1 (low latency, low compatibility)\0Nvidia 0.2.13 (medium latency, high compatibility)\0Nvidia 0.3.0 (high latency, high performance, medium compatibility)\0")) {
            select_generation(draft, 0, engine_order[selected_engine]);
            changed = true;
        }
        changed |= ImGui::Combo("Motion source", &draft.motion_vectors,
            "Optical flow\0Game motion vectors\0");
        if (draft.backend != 0 && draft.motion_vectors == 0) {
            changed |= ImGui::Combo("Flow quality", &draft.preset, "Slow\0Medium\0Fast\0");
            changed |= ImGui::Combo("Flow resolution", &draft.scale, "100%\0" "75%\0" "50%\0");
            changed |= ImGui::Checkbox("Bidirectional flow", &draft.backward);
        }
    } else if (draft.frame_generation==3) {
        ImGui::TextWrapped("Game motion vectors + FidelityFX optical flow (25% per axis). If game guides are unavailable or rejected, uses FidelityFX optical flow.");
    } else {
        ImGui::TextDisabled("Uses game motion vectors and depth");
    }
    const bool djules = draft.frame_generation == 0 && draft.backend == 3;
    const bool dlssg = draft.frame_generation == 2;
    if (djules || dlssg) {
        int multiplier = draft.triple_frame_gen ? 1 : 0;
        ImGui::BeginDisabled(draft.lower_vram && !draft.separate_synthesis);
        if (ImGui::Combo("Multiplier", &multiplier, "2X\0" "3X\0")) {
            draft.triple_frame_gen = multiplier == 1;
            changed = true;
        }
        ImGui::EndDisabled();
    }
    if (djules) {
        ImGui::BeginDisabled(draft.triple_frame_gen);
        changed |= ImGui::Checkbox("Prefer FPS over latency", &draft.prefer_fps);
        ImGui::EndDisabled();
        if (draft.triple_frame_gen) ImGui::TextDisabled("Prefer FPS is enabled with 3X");
    }
    if (dlssg) {
        changed |= ImGui::SliderInt("Generated resolution", &draft.native_scale, 25, 100, "%d%%");
        ui.scale_editing |= ImGui::IsItemActive();
        ImGui::TextWrapped("Requires compatible NVIDIA DLSS-G hardware and runtime");
    }
    bool separate = draft.separate_synthesis != 0;
    if(ImGui::Checkbox("Separate synthesis", &separate)) {
        draft.separate_synthesis = separate ? 1 : 0; changed = true;
    }
    bool lower_vram = draft.lower_vram != 0;
    if(ImGui::Checkbox("Reduce XR swapchains", &lower_vram)) {
        draft.lower_vram = lower_vram ? 1 : 0; changed = true;
    }
    if(lower_vram && !separate && draft.triple_frame_gen) {
        draft.triple_frame_gen=false; changed=true;
    }
    ImGui::TextWrapped("Independent D3D12 transport controls. Direct reduced output uses 2X and may wait for image reuse. AFW interop retains its own transport.");
    changed |= ImGui::Checkbox("Reduce black bars during headset rotation###R19 synthetic camera (experimental)", &draft.r19_camera);
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Details")) return changed;
    const auto& telemetry = state.runtime.telemetry;
    if (state.connected && telemetry.updated_ms && GetTickCount64()-telemetry.updated_ms<2000 && telemetry.cpu_valid)
        ImGui::Text("CPU frame production: %.1f ms", telemetry.cpu_render_ms);
    else ImGui::TextDisabled("CPU frame production: N/A");
    ImGui::TextDisabled("Requested %llu / applied %llu",
        static_cast<unsigned long long>(state.revision),
        static_cast<unsigned long long>(state.runtime.applied_revision));
    if (!state.connected) {
        ImGui::TextWrapped("No active game report; upscaler inputs are not yet known.");
    } else {
        const auto& runtime=state.runtime;
        if (!runtime.upscaler_evaluations)
            ImGui::TextWrapped("Upscaler inputs: no native DLSS SR/RR evaluation observed.");
        else {
            const auto age=GetTickCount64()-runtime.upscaler_last_seen_ms;
            ImGui::TextWrapped("Native DLSS SR/RR: %s. Motion vectors: %s. Depth: %s.",
                age<5000 ? "receiving inputs" : "last seen more than 5 seconds ago",
                runtime.upscaler_motion ? "present" : "missing",
                runtime.upscaler_depth ? "present" : "missing");
        }
        ImGui::TextWrapped("Owned guides: %llu published / %llu XR matches / %llu uses",
            static_cast<unsigned long long>(runtime.guide_publications),
            static_cast<unsigned long long>(runtime.guide_matches),
            static_cast<unsigned long long>(runtime.guide_uses));
        ImGui::TextWrapped("Input detection does not prove a compatible stereo pair. Native ingress currently supports DLSS SR/RR; FSR/XeSS upscaler ingress is unavailable.");
        if (ui.dirty || runtime.applied_revision!=state.revision)
            ImGui::TextWrapped("Applying the requested generation settings...");
        else {
            const auto message=generation_status_text(runtime.generation_mode,runtime.motion_source,runtime.generation_state);
            ImGui::TextWrapped("%.*s",static_cast<int>(message.size()),message.data());
        }
    }
    return changed;
}

bool framewarp(Settings& draft, const Snapshot& state) {
    bool changed = ImGui::Combo("Framewarp mode", &draft.afw_mode,
        "Off\0Alternate eye (for debug)\0Previous frame (for debug)\0Combined\0");
    changed |= ImGui::Checkbox("Debug framewarp", &draft.afw_debug);
    ImGui::TextWrapped("Requires Luke Ross and RealVR.ini. Camera and stereo motion correction is always applied; same-eye history reconstructs the missing camera interval.");
    changed |= ImGui::Checkbox("Clear before framewarp", &draft.afw_clear_before_warping);
    changed |= ImGui::SliderFloat("Ignore motion", &draft.afw_ignore_motion_threshold, 0, 16, "%.2f");
    ImGui::TextWrapped("(reduce flicker, increase artefacts)");
    changed |= ImGui::Combo("Depth dilation", &draft.afw_depth_dilate_mode,
        "Off\0Horizontal\0Square\0");
    ImGui::TextWrapped("(reduce ghosting around main char)");
    if (state.connected) ImGui::TextDisabled("Ready frames: %llu / projections: %llu",
        static_cast<unsigned long long>(state.runtime.afw_ready_frames),
        static_cast<unsigned long long>(state.runtime.afw_projection_frames));
    return changed;
}

bool neural(SettingsWidgetState& ui, const Snapshot& state) {
    auto& draft = ui.draft;
    bool changed = ImGui::Checkbox("Enable Neural Rendering", &draft.nr_enabled);
    changed |= ImGui::Checkbox("Run before super resolution", &draft.nr_before_sr);
    changed |= ImGui::SliderFloat("Detail strength", &draft.nr_transfer_strength, 0, 2, "%.2f");
    changed |= ImGui::SliderFloat("Colour strength", &draft.nr_colour_strength, 0, 1, "%.2f");
    changed |= ImGui::SliderFloat("Intensity", &draft.nr_intensity, 0, 2, "%.2f");
    changed |= ImGui::Combo("Model preset", &draft.nr_preset, "Default\0Preset 1\0Preset 2\0Preset 3\0");
    changed |= ImGui::Combo("Style", &draft.nr_style, "Standard\0Natural\0Cinematic\0");
    int scale = static_cast<int>(draft.nr_working_scale * 100.F + .5F);
    if (ImGui::SliderInt("Model resolution", &scale, 25, 100, "%d%%")) {
        draft.nr_working_scale = scale / 100.F;
        changed = true;
    }
    ui.scale_editing |= ImGui::IsItemActive();
    if (ImGui::CollapsingHeader("Model options")) {
        changed |= ImGui::SliderFloat("Local structure", &draft.nr_local_structure, 0, 2, "%.2f");
        changed |= ImGui::SliderFloat("Local tone", &draft.nr_local_tone, 0, 2, "%.2f");
        changed |= ImGui::SliderFloat("Skin structure", &draft.nr_skin_structure, -1, 2, "%.2f");
        changed |= ImGui::Checkbox("Auto skin mask", &draft.nr_auto_mask);
        changed |= ImGui::SliderFloat("Paper white", &draft.nr_white_point_scale, .25F, 4, "%.2fx");
        changed |= ImGui::SliderFloat("Luminance limit", &draft.nr_max_ratio, 1, 8, "%.2fx");
    }
    if (ImGui::CollapsingHeader("Compare and inspect")) {
        changed |= ImGui::Combo("Compare", &draft.nr_compare, "Off\0Side by side\0Wipe\0");
        if (draft.nr_compare != 0) {
            changed |= ImGui::Checkbox("Swap sides", &draft.nr_compare_swap);
            if (draft.nr_compare == 1)
                changed |= ImGui::SliderFloat("Compare zoom", &draft.nr_compare_zoom, 1, 2, "%.2fx");
            else changed |= ImGui::SliderFloat("Wipe position", &draft.nr_compare_split, 0, 1, "%.2f");
        }
        changed |= ImGui::Combo("Inspect", &draft.nr_debug_view,
            "Off\0Model input\0Raw model output\0Difference\0");
    }
    if (state.connected && state.runtime.vulkan_capture_suspended)
        ImGui::TextWrapped("Vulkan Neural Rendering input capture is suspended.");
    if (draft.nr_enabled && !state.runtime.nr_running)
        ImGui::TextWrapped("Waiting for compatible DLSS frames and the Neural Rendering runtime");
    else if (state.runtime.nr_running)
        ImGui::TextColored(ImVec4(.4F, .9F, .6F, 1), "Neural Rendering active");
    return changed;
}

bool display(Settings& draft, const Snapshot& state) {
    bool changed = ImGui::Combo("Headset FPS counter", &draft.overlay_position,
        "Off\0Upper left\0Upper right\0Lower left\0Lower right\0Elliottate's follow controller\0Elliottate's stick to bottom\0");
    changed |= ImGui::Checkbox("Diagnostic logging", &draft.diagnostics);
    ImGui::TextWrapped("Logging can be switched during the current game session");
    if (state.connected) ImGui::TextDisabled(state.runtime.logging_active
        ? (draft.diagnostics ? "Logging active" : "Stopping logging...")
        : (draft.diagnostics ? "Starting logging..." : "Logging disabled"));
    return changed;
}
} // namespace

void synchronize_settings_widgets(SettingsWidgetState& ui, const Snapshot& state) noexcept {
    if (!ui.initialized) {
        ui.draft = ui.base = state.desired;
        ui.initialized = true;
    } else if (ui.observed_revision != state.revision) {
        ui.draft = ui.dirty ? merge_changes(state.desired, ui.base, ui.draft) : state.desired;
        ui.base = state.desired;
    }
    ui.observed_revision = state.revision;
}

bool publish_settings_widgets(SettingsWidgetState& ui) noexcept {
    if (!ui.dirty || ui.scale_editing) return false;
    if (!request_changes(ui.base, ui.draft)) {
        ui.save_failed = true;
        return false;
    }
    // A competing publisher may already have changed another field. Reload the
    // actual shared snapshot instead of claiming our former full draft is current.
    // snapshot() may briefly return its last coherent value while another peer
    // holds the mutex. Retire this published delta before attempting that reload
    // so a subsequent local edit cannot accidentally replay it.
    ui.draft = sanitize(ui.draft);
    ui.base = ui.draft;
    ui.dirty = false;
    ui.save_failed = false;
    synchronize_settings_widgets(ui, snapshot());
    return true;
}

void finish_settings_widget_edits(SettingsWidgetState& ui) noexcept {
    ui.scale_editing = false;
    static_cast<void>(publish_settings_widgets(ui));
}

bool render_settings_widgets(SettingsWidgetState& ui, const Snapshot& state,
    SettingsWidgetOptions options) {
    set_interface_visible(options.compact ? InterfaceSurface::headset : InterfaceSurface::desktop, true);
    synchronize_settings_widgets(ui, state);
    ui.scale_editing = false;
    ImGui::PushID("OFXRAutonomousSettings");
    if (options.compact) ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 7));
    status(state);
    if (ui.save_failed)
        ImGui::TextColored(ImVec4(1, .5F, .4F, 1), "Could not save settings. Check access to the local profile.");
    bool changed{};
    if (ImGui::BeginTabBar("Options", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        if (ImGui::BeginTabItem("Frame generation")) {
            ImGui::BeginChild("Generation", ImVec2(0, 0));
            ImGui::PushItemWidth(std::clamp(ImGui::GetContentRegionAvail().x * .56F, 180.F, 580.F));
            changed |= generation(ui, state);
            ImGui::PopItemWidth();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Luke Ross")) {
            ImGui::BeginChild("Framewarp", ImVec2(0, 0));
            ImGui::PushItemWidth(std::clamp(ImGui::GetContentRegionAvail().x * .56F, 180.F, 580.F));
            changed |= framewarp(ui.draft, state);
            ImGui::PopItemWidth();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Neural Rendering")) {
            ImGui::BeginChild("Neural", ImVec2(0, 0));
            ImGui::PushItemWidth(std::clamp(ImGui::GetContentRegionAvail().x * .56F, 180.F, 580.F));
            changed |= neural(ui, state);
            ImGui::PopItemWidth();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Display")) {
            ImGui::BeginChild("Display", ImVec2(0, 0));
            ImGui::PushItemWidth(std::clamp(ImGui::GetContentRegionAvail().x * .56F, 180.F, 580.F));
            changed |= display(ui.draft, state);
            ImGui::PopItemWidth();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (options.compact) ImGui::PopStyleVar();
    ImGui::PopID();
    ui.dirty |= changed;
    return !options.defer_publication && publish_settings_widgets(ui);
}

} // namespace xrfg::autonomous
