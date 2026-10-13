cmake_minimum_required(VERSION 3.24)

project(XRFrameGenerationBridge VERSION 0.519.0 LANGUAGES CXX)

if(MSVC)
    # The standalone layer is injected into hosts that
    # can preload an older game-local MSVCP140.dll. Initialize mutex/CV storage
    # through that loaded runtime instead of relying on the newer toolset's
    # constexpr representation.
    add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endif()

# Standalone layer and API harnesses use the same optimized Release DLL.
set(XRFG_TEST_LAYER_DLL "${CMAKE_CURRENT_BINARY_DIR}/Release/XR_APILAYER_XRFrameBridge_diagnostic.dll" CACHE FILEPATH "Autonomous DLL tested by layer harnesses")
set(XRFG_LAYER_KIND SHARED)
set(XRFG_LAYER_SOURCE src/layer/openxr_layer.cpp)
set(XRFG_SYNTHESIS_SOURCE src/d3d12/d3d12_frame_synthesizer.cpp)
set(XRFG_SYNTHESIS_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/frame_synthesis.hlsl")
set(XRFG_TEST_NVIDIA_ROUTE "new" CACHE STRING "Runtime route exercised by renderer fixtures")
set_property(CACHE XRFG_TEST_NVIDIA_ROUTE PROPERTY STRINGS new djules)
if(NOT XRFG_TEST_NVIDIA_ROUTE MATCHES "^(new|djules)$")
    message(FATAL_ERROR "Unknown test runtime route")
endif()


option(XRFG_BUILD_LAYER "Build the Windows OpenXR API layer" ON)
option(XRFG_BUILD_TESTS "Build the core test executable" ON)
option(XRFG_BUILD_STANDALONE "Build the Windows standalone tray launcher" ON)
set(XRFG_DEPENDENCY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.local/ofxr-autonomous-deps" CACHE PATH "Standalone vendor SDK and runtime store")
set(XRFG_DLSS_SDK_ROOT "${XRFG_DEPENDENCY_ROOT}/DLSS" CACHE PATH "NVIDIA DLSS SDK")
set(XRFG_FSR_RUNTIME "${XRFG_DEPENDENCY_ROOT}/runtime/fsr-optiscaler/ofxr_amd_fidelityfx_framegeneration_dx12.dll" CACHE FILEPATH "Private FSR FG provider")
set(XRFG_AFW_RUNTIME "${XRFG_DEPENDENCY_ROOT}/runtime/PDAFWPlugin.dll" CACHE FILEPATH "Official AFW beta5 provider")
set(XRFG_OPENVR_INCLUDE_DIR "${XRFG_DEPENDENCY_ROOT}/OpenVR/include" CACHE PATH "OpenVR headers")
set(XRFG_VULKAN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan-Headers/include" CACHE PATH "Vulkan headers")


set(XRFG_OPENXR_SDK_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/external/OpenXR-SDK"
    CACHE PATH
    "Path to Khronos OpenXR-SDK containing include/openxr/openxr.h")

set(XRFG_FIDELITYFX_SDK_ROOT
    "${XRFG_DEPENDENCY_ROOT}/FidelityFX/sdk"
    CACHE PATH
    "Path to the pinned AMD FidelityFX SDK v1.1.4 source tree")

set(XRFG_NVIDIA_OPTICAL_FLOW_SDK_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/external/NVIDIA-Optical-Flow-SDK-5.0.7/Optical_Flow_SDK_5.0.7"
    CACHE PATH
    "Path to the NVIDIA Optical Flow SDK 5.0.7 root")

set(XRFG_FIDELITYFX_API_INCLUDE_DIR
    "${XRFG_DEPENDENCY_ROOT}/FidelityFX/ffx-api/include/ffx_api"
    CACHE PATH
    "Path to the modular FidelityFX API headers used by FSR frame generation")

add_library(xrfg_core STATIC
    src/core/interface_metrics.cpp
    src/core/fps_overlay_model.cpp
    src/core/elliott_meter.cpp
    src/core/image.cpp
    src/core/optical_flow.cpp
    src/core/pose.cpp)
target_include_directories(xrfg_core PUBLIC include)
target_compile_features(xrfg_core PUBLIC cxx_std_20)

add_executable(xrfg_reference src/reference/main.cpp)
target_link_libraries(xrfg_reference PRIVATE xrfg_core)

# Separate-process consumer for V30006's RAM-only frame cadence capture.
# The injected layer never performs a disk write or starts a worker thread.
add_executable(xrfg_pose_cadence_watch
    src/reference/pose_cadence_watch.cpp
    src/common/ram_timing.cpp)
target_include_directories(xrfg_pose_cadence_watch PRIVATE include)
target_compile_features(xrfg_pose_cadence_watch PRIVATE cxx_std_20)

if(MSVC)
    foreach(target xrfg_core xrfg_reference xrfg_pose_cadence_watch)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endforeach()
endif()

if(XRFG_BUILD_TESTS)
    enable_testing()
    if(WIN32)
        add_executable(xrfg_streamline_native_interface_tests tests/streamline_native_interface_tests.cpp)
        target_link_libraries(xrfg_streamline_native_interface_tests PRIVATE d3d12)
        target_include_directories(xrfg_streamline_native_interface_tests PRIVATE include "${CMAKE_CURRENT_SOURCE_DIR}/include/xrfg/native_ingress")
        target_compile_features(xrfg_streamline_native_interface_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_streamline_native_interface_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        if(MSVC)
            target_compile_options(xrfg_streamline_native_interface_tests PRIVATE /W4 /permissive- /EHsc)
        endif()
        add_test(NAME xrfg_streamline_native_interface_tests COMMAND xrfg_streamline_native_interface_tests)
    endif()
    add_executable(xrfg_two_output_schedule_tests tests/two_output_schedule_tests.cpp)
    target_include_directories(xrfg_two_output_schedule_tests PRIVATE include)
    target_compile_features(xrfg_two_output_schedule_tests PRIVATE cxx_std_20)
    add_test(NAME xrfg_two_output_schedule_tests COMMAND xrfg_two_output_schedule_tests)
    add_executable(xrfg_midpoint_schedule_tests tests/midpoint_schedule_tests.cpp)
    target_include_directories(xrfg_midpoint_schedule_tests PRIVATE include)
    target_compile_features(xrfg_midpoint_schedule_tests PRIVATE cxx_std_20)
    add_test(NAME xrfg_midpoint_schedule_tests COMMAND xrfg_midpoint_schedule_tests)
    add_executable(xrfg_luke_color_token_tests tests/luke_color_token_tests.cpp)
    target_include_directories(xrfg_luke_color_token_tests PRIVATE include)
    target_compile_features(xrfg_luke_color_token_tests PRIVATE cxx_std_20)
    add_test(NAME xrfg_luke_color_token_tests COMMAND xrfg_luke_color_token_tests)
    add_executable(xrfg_fsr_cadence_probe_tests tests/fsr_cadence_probe_tests.cpp)
    target_include_directories(xrfg_fsr_cadence_probe_tests PRIVATE include)
    target_compile_features(xrfg_fsr_cadence_probe_tests PRIVATE cxx_std_20)
    target_compile_definitions(xrfg_fsr_cadence_probe_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME xrfg_fsr_cadence_probe_tests COMMAND xrfg_fsr_cadence_probe_tests)
    add_executable(xrfg_three_slot_buffer_tests tests/three_slot_buffer_tests.cpp)
    target_include_directories(xrfg_three_slot_buffer_tests PRIVATE include)
    target_compile_features(xrfg_three_slot_buffer_tests PRIVATE cxx_std_20)
    add_test(NAME xrfg_three_slot_buffer_tests COMMAND xrfg_three_slot_buffer_tests)
    add_executable(xrfg_luke_counter_probe_tests tests/luke_counter_probe_tests.cpp)
    target_include_directories(xrfg_luke_counter_probe_tests PRIVATE include)
    target_compile_features(xrfg_luke_counter_probe_tests PRIVATE cxx_std_20)
    add_test(NAME xrfg_luke_counter_probe_tests COMMAND xrfg_luke_counter_probe_tests)
    add_executable(xrfg_core_tests tests/core_tests.cpp)
    target_link_libraries(xrfg_core_tests PRIVATE xrfg_core)
    if(MSVC)
        target_compile_options(xrfg_core_tests PRIVATE /W4 /permissive- /EHsc)
        target_compile_definitions(xrfg_core_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    add_test(NAME xrfg_core_tests COMMAND xrfg_core_tests)
    if(WIN32)
        add_executable(xrfg_luke_world_scale_config_tests tests/luke_world_scale_config_tests.cpp)
        target_include_directories(xrfg_luke_world_scale_config_tests PRIVATE include)
        target_compile_features(xrfg_luke_world_scale_config_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_luke_world_scale_config_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME xrfg_luke_world_scale_config COMMAND xrfg_luke_world_scale_config_tests)
    endif()
    add_executable(xrfg_fps_overlay_tests tests/fps_overlay_tests.cpp)
    target_link_libraries(xrfg_fps_overlay_tests PRIVATE xrfg_core)
    add_test(NAME xrfg_fps_overlay_tests COMMAND xrfg_fps_overlay_tests)

    add_executable(xrfg_optical_flow_tests tests/optical_flow_tests.cpp)
    target_link_libraries(xrfg_optical_flow_tests PRIVATE xrfg_core)
    if(MSVC)
        target_compile_options(xrfg_optical_flow_tests PRIVATE /W4 /permissive- /EHsc)
        target_compile_definitions(xrfg_optical_flow_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    add_test(NAME xrfg_optical_flow_tests COMMAND xrfg_optical_flow_tests)
endif()

if(WIN32 AND XRFG_BUILD_LAYER)
    if(NOT EXISTS "${XRFG_OPENXR_SDK_ROOT}/include/openxr/openxr.h" OR
       NOT EXISTS "${XRFG_OPENXR_SDK_ROOT}/include/openxr/openxr_platform_defines.h" OR
       NOT EXISTS "${XRFG_OPENXR_SDK_ROOT}/include/openxr/openxr_loader_negotiation.h" OR
       NOT EXISTS "${XRFG_OPENXR_SDK_ROOT}/include/openxr/openxr_platform.h")
        message(FATAL_ERROR
            "XRFG_OPENXR_SDK_ROOT must point to the required Khronos OpenXR-SDK generated headers")
    endif()

    find_program(XRFG_FXC_EXECUTABLE fxc.exe
        HINTS
            "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64")
    if(NOT XRFG_FXC_EXECUTABLE)
        message(FATAL_ERROR "fxc.exe from the Windows SDK is required for frame-synthesis shaders")
    endif()

    set(XRFG_FIDELITYFX_INCLUDE_DIR
        "${XRFG_FIDELITYFX_SDK_ROOT}/include")
    set(XRFG_FIDELITYFX_LIBRARY_DIR
        "${XRFG_FIDELITYFX_SDK_ROOT}/bin/ffx_sdk")
    set(XRFG_FIDELITYFX_OPTICALFLOW_LIBRARY
        "${XRFG_FIDELITYFX_LIBRARY_DIR}/ofxr_ffx_opticalflow_x64.lib")
    set(XRFG_FIDELITYFX_DX12_LIBRARY
        "${XRFG_FIDELITYFX_LIBRARY_DIR}/ofxr_ffx_backend_dx12_x64.lib")
    if(NOT EXISTS "${XRFG_FIDELITYFX_INCLUDE_DIR}/FidelityFX/host/ffx_opticalflow.h" OR
       NOT EXISTS "${XRFG_FIDELITYFX_INCLUDE_DIR}/FidelityFX/host/backends/dx12/ffx_dx12.h" OR
       NOT EXISTS "${XRFG_FIDELITYFX_OPTICALFLOW_LIBRARY}" OR
       NOT EXISTS "${XRFG_FIDELITYFX_DX12_LIBRARY}")
        message(FATAL_ERROR
            "Pinned FidelityFX SDK v1.1.4 headers and Release DX12/Optical Flow libraries are required")
    endif()

    set(XRFG_NVIDIA_OPTICAL_FLOW_INCLUDE_DIR
        "${XRFG_NVIDIA_OPTICAL_FLOW_SDK_ROOT}/NvOFInterface")
    if(NOT EXISTS "${XRFG_NVIDIA_OPTICAL_FLOW_INCLUDE_DIR}/nvOpticalFlowCommon.h" OR
       NOT EXISTS "${XRFG_NVIDIA_OPTICAL_FLOW_INCLUDE_DIR}/nvOpticalFlowD3D12.h")
        message(FATAL_ERROR
            "NVIDIA Optical Flow SDK 5.0.7 interface headers are required")
    endif()

    set(XRFG_GENERATED_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
    set(XRFG_GAME_MOTION_SYNTHESIZE_PIXEL_HEADER "${XRFG_GENERATED_SHADER_DIR}/game_motion_synthesize_midpoint_pixel_shader.hpp")
    set(XRFG_PACK_INPUT_HEADER "${XRFG_GENERATED_SHADER_DIR}/pack_flow_input_shader.hpp")
    set(XRFG_FULLSCREEN_VERTEX_HEADER "${XRFG_GENERATED_SHADER_DIR}/fullscreen_vertex_shader.hpp")
    set(XRFG_SYNTHESIZE_PIXEL_HEADER "${XRFG_GENERATED_SHADER_DIR}/synthesize_midpoint_pixel_shader.hpp")
    set(XRFG_REPROJECT_GENERATED_PIXEL_HEADER "${XRFG_GENERATED_SHADER_DIR}/reproject_generated_pixel_shader.hpp")
    set(XRFG_REPROJECT_LATE_HUD_PIXEL_HEADER "${XRFG_GENERATED_SHADER_DIR}/reproject_late_hud_pixel_shader.hpp")
    set(XRFG_FSR_CINEMA_VERTEX_HEADER "${XRFG_GENERATED_SHADER_DIR}/fsr_cinema_vertex_shader.hpp")
    set(XRFG_FSR_CINEMA_PIXEL_HEADER "${XRFG_GENERATED_SHADER_DIR}/fsr_cinema_pixel_shader.hpp")
    set(XRFG_CINEMA_FLATTEN_VERTEX_HEADER "${XRFG_GENERATED_SHADER_DIR}/cinema_flatten_vertex_shader.hpp")
    set(XRFG_CINEMA_FLATTEN_PIXEL_HEADER "${XRFG_GENERATED_SHADER_DIR}/cinema_flatten_pixel_shader.hpp")
    set(XRFG_NVIDIA_LUMA_INPUT_HEADER "${XRFG_GENERATED_SHADER_DIR}/nvidia_pack_luma_input_shader.hpp")
    set(XRFG_HYBRID_PACK_INPUT_HEADER "${XRFG_GENERATED_SHADER_DIR}/hybrid_pack_flow_input_shader.hpp")
    set(XRFG_HYBRID_SYNTHESIZE_PIXEL_HEADER "${XRFG_GENERATED_SHADER_DIR}/hybrid_synthesize_midpoint_pixel_shader.hpp")
    set(XRFG_NVIDIA_PACK_INPUT_HEADER "${XRFG_GENERATED_SHADER_DIR}/nvidia_pack_flow_input_shader.hpp")
    set(XRFG_NVIDIA_SYNTHESIZE_PIXEL_HEADER "${XRFG_GENERATED_SHADER_DIR}/nvidia_synthesize_midpoint_pixel_shader.hpp")
    set(XRFG_NVIDIA_BIDIRECTIONAL_SYNTHESIZE_PIXEL_HEADER "${XRFG_GENERATED_SHADER_DIR}/nvidia_bidirectional_synthesize_midpoint_pixel_shader.hpp")
    set(XRFG_NVIDIA_FAST_SYNTHESIZE_PIXEL_HEADER "${XRFG_GENERATED_SHADER_DIR}/nvidia_fast_synthesize_midpoint_pixel_shader.hpp")
    set(XRFG_NVIDIA_FAST_BIDIRECTIONAL_SYNTHESIZE_PIXEL_HEADER "${XRFG_GENERATED_SHADER_DIR}/nvidia_fast_bidirectional_synthesize_midpoint_pixel_shader.hpp")
    set(XRFG_LUKE_DEPTH_CONVERT_HEADER "${XRFG_GENERATED_SHADER_DIR}/luke_depth_convert_shader.hpp")
    set(XRFG_LUKE_DEPTH_CALIBRATE_HEADER "${XRFG_GENERATED_SHADER_DIR}/luke_depth_calibrate_shader.hpp")
    set(XRFG_LUKE_COMPOSE_MOTION_HEADER "${XRFG_GENERATED_SHADER_DIR}/luke_compose_motion_shader.hpp")
    add_custom_command(
        OUTPUT
            "${XRFG_GAME_MOTION_SYNTHESIZE_PIXEL_HEADER}"
            "${XRFG_PACK_INPUT_HEADER}"
            "${XRFG_FULLSCREEN_VERTEX_HEADER}"
            "${XRFG_SYNTHESIZE_PIXEL_HEADER}"
            "${XRFG_REPROJECT_GENERATED_PIXEL_HEADER}"
            "${XRFG_REPROJECT_LATE_HUD_PIXEL_HEADER}"
            "${XRFG_FSR_CINEMA_VERTEX_HEADER}"
            "${XRFG_FSR_CINEMA_PIXEL_HEADER}"
            "${XRFG_CINEMA_FLATTEN_VERTEX_HEADER}"
            "${XRFG_CINEMA_FLATTEN_PIXEL_HEADER}"
            "${XRFG_HYBRID_SYNTHESIZE_PIXEL_HEADER}"
            "${XRFG_HYBRID_PACK_INPUT_HEADER}"
            "${XRFG_NVIDIA_PACK_INPUT_HEADER}"
            "${XRFG_NVIDIA_LUMA_INPUT_HEADER}"
            "${XRFG_NVIDIA_SYNTHESIZE_PIXEL_HEADER}"
            "${XRFG_NVIDIA_BIDIRECTIONAL_SYNTHESIZE_PIXEL_HEADER}"
            "${XRFG_NVIDIA_FAST_SYNTHESIZE_PIXEL_HEADER}"
            "${XRFG_NVIDIA_FAST_BIDIRECTIONAL_SYNTHESIZE_PIXEL_HEADER}"
            "${XRFG_LUKE_DEPTH_CONVERT_HEADER}"
            "${XRFG_LUKE_DEPTH_CALIBRATE_HEADER}"
            "${XRFG_LUKE_COMPOSE_MOTION_HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${XRFG_GENERATED_SHADER_DIR}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E SynthesizeGameMotionMidpointPS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_GAME_MOTION_SYNTHESIZE_PIXEL_HEADER}" /Vn g_xrfg_game_motion_synthesize_midpoint_pixel_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T cs_5_1 /E PackFlowInput
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_PACK_INPUT_HEADER}" /Vn g_xrfg_pack_flow_input_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T vs_5_1 /E FullscreenTriangleVS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_FULLSCREEN_VERTEX_HEADER}" /Vn g_xrfg_fullscreen_vertex_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E SynthesizeMidpointPS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_SYNTHESIZE_PIXEL_HEADER}" /Vn g_xrfg_synthesize_midpoint_pixel_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E ReprojectGeneratedPS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_REPROJECT_GENERATED_PIXEL_HEADER}" /Vn g_xrfg_reproject_generated_pixel_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E ReprojectGeneratedWithLateHudPS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_REPROJECT_LATE_HUD_PIXEL_HEADER}" /Vn g_xrfg_reproject_late_hud_pixel_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T vs_5_1 /E CinemaPanelVS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_FSR_CINEMA_VERTEX_HEADER}" /Vn g_xrfg_fsr_cinema_vertex_shader
            "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/fsr_cinema_projection.hlsl"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E CinemaPanelPS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_FSR_CINEMA_PIXEL_HEADER}" /Vn g_xrfg_fsr_cinema_pixel_shader
            "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/fsr_cinema_projection.hlsl"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T cs_5_1 /E PackFlowInput
            /D XRFG_HYBRID_QUARTER_INPUT=1 /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_HYBRID_PACK_INPUT_HEADER}" /Vn g_xrfg_hybrid_pack_flow_input_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E SynthesizeHybridMidpointPS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_HYBRID_SYNTHESIZE_PIXEL_HEADER}" /Vn g_xrfg_hybrid_synthesize_midpoint_pixel_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T cs_5_1 /E PackNvidiaFlowInput
            /D XRFG_NVIDIA_LUMA_INPUT=1 /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_NVIDIA_LUMA_INPUT_HEADER}" /Vn g_xrfg_nvidia_pack_luma_input_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T cs_5_1 /E PackNvidiaFlowInput
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_NVIDIA_PACK_INPUT_HEADER}" /Vn g_xrfg_nvidia_pack_flow_input_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T vs_5_1 /E CinemaFlattenVS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_CINEMA_FLATTEN_VERTEX_HEADER}" /Vn g_xrfg_cinema_flatten_vertex_shader
            "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/fsr_cinema_projection.hlsl"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E CinemaFlattenPS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_CINEMA_FLATTEN_PIXEL_HEADER}" /Vn g_xrfg_cinema_flatten_pixel_shader
            "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/fsr_cinema_projection.hlsl"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E SynthesizeNvidiaMidpointPS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_NVIDIA_SYNTHESIZE_PIXEL_HEADER}" /Vn g_xrfg_nvidia_synthesize_midpoint_pixel_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E SynthesizeNvidiaBidirectionalMidpointPS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_NVIDIA_BIDIRECTIONAL_SYNTHESIZE_PIXEL_HEADER}" /Vn g_xrfg_nvidia_bidirectional_synthesize_midpoint_pixel_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E SynthesizeNvidiaFastMidpointPS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_NVIDIA_FAST_SYNTHESIZE_PIXEL_HEADER}" /Vn g_xrfg_nvidia_fast_synthesize_midpoint_pixel_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E SynthesizeNvidiaFastBidirectionalMidpointPS
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_NVIDIA_FAST_BIDIRECTIONAL_SYNTHESIZE_PIXEL_HEADER}" /Vn g_xrfg_nvidia_fast_bidirectional_synthesize_midpoint_pixel_shader
            "${XRFG_SYNTHESIS_SHADER}"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T cs_5_1 /E ConvertLukeDepth
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_LUKE_DEPTH_CONVERT_HEADER}" /Vn g_xrfg_luke_depth_convert_shader
            "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/luke_depth_convert.hlsl"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T cs_5_1 /E CalibrateLukeDepth
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_LUKE_DEPTH_CALIBRATE_HEADER}" /Vn g_xrfg_luke_depth_calibrate_shader
            "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/luke_depth_convert.hlsl"
        COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T cs_5_1 /E ComposeLukeSameEyeMotion
            /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect
            /Fh "${XRFG_LUKE_COMPOSE_MOTION_HEADER}" /Vn g_xrfg_luke_compose_motion_shader
            "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/luke_compose_motion.hlsl"
        DEPENDS
            "${XRFG_SYNTHESIS_SHADER}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/fsr_cinema_projection.hlsl"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/luke_depth_convert.hlsl"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/luke_compose_motion.hlsl"
        VERBATIM)

    add_library(xrfg_d3d12_history STATIC
        "${XRFG_GAME_MOTION_SYNTHESIZE_PIXEL_HEADER}"
        src/common/dlss_motion_vectors.cpp
        src/common/two_frame_capture.cpp
        src/common/fsr_frame_generation.cpp
        src/d3d11/d3d11_d3d12_interop.cpp
        src/d3d11/d3d11_bridge.cpp
        src/d3d11/d3d11_dlss_guides.cpp
        src/d3d11/d3d11_guide_transport.cpp
        src/vulkan/vulkan_session_bridge.cpp
        src/d3d11/d3d11_color_identity.cpp
        src/d3d12/d3d12_guide_composer.cpp
        src/d3d12/d3d12_history.cpp
        src/d3d12/d3d12_fsr_frame_generator.cpp
        ${XRFG_SYNTHESIS_SOURCE}
        src/d3d12/luke_afw.cpp
        src/d3d12/luke_color_origin.cpp
        "${XRFG_PACK_INPUT_HEADER}"

        "${XRFG_HYBRID_SYNTHESIZE_PIXEL_HEADER}"
            "${XRFG_HYBRID_PACK_INPUT_HEADER}"
        "${XRFG_FULLSCREEN_VERTEX_HEADER}"
        "${XRFG_SYNTHESIZE_PIXEL_HEADER}"
        "${XRFG_REPROJECT_GENERATED_PIXEL_HEADER}"
        "${XRFG_REPROJECT_LATE_HUD_PIXEL_HEADER}"
        "${XRFG_FSR_CINEMA_VERTEX_HEADER}"
        "${XRFG_FSR_CINEMA_PIXEL_HEADER}"
        "${XRFG_NVIDIA_PACK_INPUT_HEADER}"
        "${XRFG_NVIDIA_LUMA_INPUT_HEADER}"
        "${XRFG_NVIDIA_SYNTHESIZE_PIXEL_HEADER}"
        "${XRFG_NVIDIA_BIDIRECTIONAL_SYNTHESIZE_PIXEL_HEADER}"
        "${XRFG_NVIDIA_FAST_SYNTHESIZE_PIXEL_HEADER}"
        "${XRFG_NVIDIA_FAST_BIDIRECTIONAL_SYNTHESIZE_PIXEL_HEADER}"
        "${XRFG_LUKE_DEPTH_CONVERT_HEADER}"
        "${XRFG_LUKE_DEPTH_CALIBRATE_HEADER}"
        "${XRFG_LUKE_COMPOSE_MOTION_HEADER}")
    foreach(stage decide compose)
        set(header "${XRFG_GENERATED_SHADER_DIR}/guide_composer_${stage}_shader.hpp")
        if(stage STREQUAL "decide")
            set(entry Decide)
        else()
            set(entry Compose)
        endif()
        add_custom_command(OUTPUT "${header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${XRFG_GENERATED_SHADER_DIR}"
            COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T cs_5_1 /E ${entry}
                /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect /Fh "${header}"
                /Vn g_xrfg_guide_composer_${stage}_shader "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/guide_composer.hlsl"
            DEPENDS src/d3d12/guide_composer.hlsl VERBATIM)
        target_sources(xrfg_d3d12_history PRIVATE "${header}")
    endforeach()
    include(FetchContent)
    set(SAFETYHOOK_FETCH_ZYDIS ON CACHE BOOL "" FORCE)
    FetchContent_Declare(ofxr_safetyhook
        GIT_REPOSITORY https://github.com/cursey/safetyhook.git
        GIT_TAG c3f3f306a0f12d1811c0b713ad2ed2a8ddc6cf55)
    FetchContent_MakeAvailable(ofxr_safetyhook)
    set(XRFG_DLSSG_RUNTIME "${XRFG_DLSS_SDK_ROOT}/lib/Windows_x86_64/rel/nvngx_dlssg.dll")
    if(NOT EXISTS "${XRFG_DLSS_SDK_ROOT}/include/nvsdk_ngx_helpers_dlssg.h" OR
       NOT EXISTS "${XRFG_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib" OR
       NOT EXISTS "${XRFG_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_d_dbg.lib" OR
       NOT EXISTS "${XRFG_DLSSG_RUNTIME}")
        message(FATAL_ERROR "XRFG_NATIVE_DLSSG requires a complete NVIDIA DLSS SDK at XRFG_DLSS_SDK_ROOT")
    endif()
    foreach(stage pack seed vertex pixel)
        set(header "${XRFG_GENERATED_SHADER_DIR}/native_dlssg_${stage}_shader.hpp")
        if(stage STREQUAL "pack")
            set(profile cs_5_1)
            set(entry PackNativeDlssG)
        elseif(stage STREQUAL "seed")
            set(profile cs_5_1)
            set(entry SeedNativeDlssG)
        elseif(stage STREQUAL "vertex")
            set(profile vs_5_1)
            set(entry NativeDlssGVS)
        else()
            set(profile ps_5_1)
            set(entry NativeDlssGPS)
        endif()
        add_custom_command(OUTPUT "${header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${XRFG_GENERATED_SHADER_DIR}"
            COMMAND "${XRFG_FXC_EXECUTABLE}" /nologo /T ${profile} /E ${entry}
                /O3 /WX /Ges /Qstrip_debug /Qstrip_reflect /Fh "${header}"
                /Vn g_xrfg_native_dlssg_${stage}_shader "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/native_dlssg.hlsl"
            DEPENDS src/d3d12/native_dlssg.hlsl VERBATIM)
        target_sources(xrfg_d3d12_history PRIVATE "${header}")
    endforeach()
    target_include_directories(xrfg_d3d12_history PUBLIC "${XRFG_DLSS_SDK_ROOT}/include")
    target_link_libraries(xrfg_d3d12_history PUBLIC
        optimized "${XRFG_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib"
        debug "${XRFG_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_d_dbg.lib")
    target_compile_definitions(xrfg_d3d12_history PUBLIC XRFG_NATIVE_DLSSG)
    target_sources(xrfg_d3d12_history PRIVATE src/d3d12/d3d12_native_dlssg.cpp)
    add_library(xrfg_dlssg_forwarder SHARED src/d3d12/dlssg_forwarder.cpp)
    target_compile_features(xrfg_dlssg_forwarder PRIVATE cxx_std_20)
    target_compile_definitions(xrfg_dlssg_forwarder PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(xrfg_dlssg_forwarder PRIVATE "${XRFG_DLSS_SDK_ROOT}/include")
    set_target_properties(xrfg_dlssg_forwarder PROPERTIES OUTPUT_NAME "nvngx.dll_dlssg")
    add_custom_command(TARGET xrfg_dlssg_forwarder POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:xrfg_dlssg_forwarder>/ofxr"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:xrfg_dlssg_forwarder>"
            "$<TARGET_FILE_DIR:xrfg_dlssg_forwarder>/ofxr/nvngx.dll_dlssg.dll")
    add_dependencies(xrfg_d3d12_history xrfg_dlssg_forwarder)

    target_include_directories(xrfg_d3d12_history
        PUBLIC include
        PRIVATE
            "${XRFG_GENERATED_SHADER_DIR}"
            "${XRFG_FIDELITYFX_INCLUDE_DIR}"
            "${XRFG_FIDELITYFX_API_INCLUDE_DIR}"
            "${XRFG_NVIDIA_OPTICAL_FLOW_INCLUDE_DIR}")
    if(NOT EXISTS "${XRFG_VULKAN_INCLUDE_DIR}/vulkan/vk_layer.h")
        message(FATAL_ERROR "Provide the pinned Khronos Vulkan headers")
    endif()
    target_include_directories(xrfg_d3d12_history PUBLIC "${XRFG_VULKAN_INCLUDE_DIR}")
    add_library(OFXR_vulkan_queue_layer SHARED src/vulkan_layer/vulkan_queue_layer.cpp)
    target_include_directories(OFXR_vulkan_queue_layer PRIVATE include "${XRFG_VULKAN_INCLUDE_DIR}")
    target_compile_features(OFXR_vulkan_queue_layer PRIVATE cxx_std_20)
    target_compile_definitions(OFXR_vulkan_queue_layer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN VK_NO_PROTOTYPES)
    if(XRFG_BUILD_TESTS)
        add_executable(xrfg_vulkan_queue_layer_tests tests/vulkan_queue_layer_tests.cpp)
        target_include_directories(xrfg_vulkan_queue_layer_tests PRIVATE include "${XRFG_VULKAN_INCLUDE_DIR}")
        target_compile_features(xrfg_vulkan_queue_layer_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_vulkan_queue_layer_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN VK_NO_PROTOTYPES)
        add_test(NAME xrfg_vulkan_queue_layer_tests COMMAND xrfg_vulkan_queue_layer_tests)
        add_executable(xrfg_vulkan_capture_suspended_tests tests/vulkan_capture_suspended_tests.cpp src/ingress/vulkan_native_ingress_disabled.cpp)
        target_include_directories(xrfg_vulkan_capture_suspended_tests PRIVATE include "${XRFG_VULKAN_INCLUDE_DIR}")
        target_compile_features(xrfg_vulkan_capture_suspended_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_vulkan_capture_suspended_tests PRIVATE VK_NO_PROTOTYPES NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME xrfg_vulkan_capture_suspended_tests COMMAND xrfg_vulkan_capture_suspended_tests)

        set_tests_properties(xrfg_vulkan_queue_layer_tests PROPERTIES TIMEOUT 10)
    endif()
    if(MSVC)
        target_compile_options(OFXR_vulkan_queue_layer PRIVATE /W4 /permissive- /EHsc)
    endif()
    target_compile_features(xrfg_d3d12_history PUBLIC cxx_std_20)
    target_compile_options(xrfg_d3d12_history PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/ffx_private_symbols.h")
    target_link_libraries(xrfg_d3d12_history PUBLIC
        xrfg_core
        d3d11
        d3dcompiler
        d3d12
        dxgi
        "${XRFG_FIDELITYFX_OPTICALFLOW_LIBRARY}"
        "${XRFG_FIDELITYFX_DX12_LIBRARY}")
    target_compile_definitions(xrfg_d3d12_history PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(xrfg_d3d12_history PRIVATE
            /W4 /permissive- /EHsc)
    endif()

    add_library(xrfg_bridge_flight_logger STATIC
        src/layer/bridge_flight_logger.cpp
        src/layer/luke_counter_probe.cpp
        src/common/ram_timing.cpp
        include/xrfg/bridge_flight_logger.hpp)
    add_library(xrfg_frame_capture STATIC src/d3d11/frame_capture.cpp)
    target_include_directories(xrfg_frame_capture PUBLIC include)
    target_compile_features(xrfg_frame_capture PUBLIC cxx_std_20)
    target_compile_definitions(xrfg_frame_capture PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xrfg_frame_capture PUBLIC d3d11 d3dcompiler user32)
    target_include_directories(xrfg_bridge_flight_logger PUBLIC include)
    add_library(xrfg_native_result STATIC src/d3d12/d3d12_native_result.cpp)
    target_include_directories(xrfg_native_result PUBLIC include)
    target_compile_features(xrfg_native_result PUBLIC cxx_std_20)
    target_compile_definitions(xrfg_native_result PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xrfg_native_result PUBLIC xrfg_bridge_flight_logger d3d12)
    target_link_libraries(xrfg_d3d12_history PUBLIC xrfg_bridge_flight_logger xrfg_native_result)
    target_compile_features(xrfg_bridge_flight_logger PUBLIC cxx_std_20)
    target_compile_definitions(xrfg_bridge_flight_logger PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        XRFG_IMPLEMENTATION_VERSION=${PROJECT_VERSION_MINOR})
    if(MSVC)
        target_compile_options(xrfg_bridge_flight_logger PRIVATE
            /W4 /permissive- /EHsc)
    endif()

    add_library(xrfg_implicit_layer STATIC
        src/common/embedded_control.cpp
        src/common/implicit_layer.cpp
        include/xrfg/implicit_layer.hpp)
    target_include_directories(xrfg_implicit_layer PUBLIC include)
    target_compile_features(xrfg_implicit_layer PUBLIC cxx_std_20)
    target_compile_definitions(xrfg_implicit_layer PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE)
    target_link_libraries(xrfg_implicit_layer PUBLIC advapi32)
    if(MSVC)
        target_compile_options(xrfg_implicit_layer PRIVATE
            /W4 /permissive- /EHsc)
    endif()

    add_library(xrfg_steamvr_delivery STATIC src/layer/steamvr_delivery.cpp)
    target_include_directories(xrfg_steamvr_delivery PUBLIC include PRIVATE "${XRFG_OPENVR_INCLUDE_DIR}")
    target_compile_features(xrfg_steamvr_delivery PUBLIC cxx_std_20)
    target_compile_definitions(xrfg_steamvr_delivery PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xrfg_steamvr_delivery PUBLIC xrfg_bridge_flight_logger)

    add_library(xrfg_fps_overlay STATIC src/layer/openxr_fps_overlay.cpp)
    target_include_directories(xrfg_fps_overlay PUBLIC include "${XRFG_OPENXR_SDK_ROOT}/include")
    target_compile_features(xrfg_fps_overlay PUBLIC cxx_std_20)
    target_compile_definitions(xrfg_fps_overlay PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN
        XR_USE_GRAPHICS_API_D3D11 XR_USE_GRAPHICS_API_D3D12 XR_USE_PLATFORM_WIN32)
    target_link_libraries(xrfg_fps_overlay PUBLIC xrfg_native_result xrfg_core xrfg_steamvr_delivery d3d11 d3d12 dxgi)
    if(MSVC)
        target_compile_options(xrfg_fps_overlay PRIVATE /W4 /permissive- /EHsc)
    endif()
    if(XRFG_BUILD_TESTS)
        add_executable(xrfg_openxr_elliott_meter_tests tests/openxr_elliott_meter_tests.cpp)
        target_link_libraries(xrfg_openxr_elliott_meter_tests PRIVATE xrfg_fps_overlay xrfg_implicit_layer xrfg_d3d12_history)
        add_test(NAME xrfg_openxr_elliott_meter_d3d12 COMMAND xrfg_openxr_elliott_meter_tests)
        add_test(NAME xrfg_openxr_elliott_meter_d3d11 COMMAND xrfg_openxr_elliott_meter_tests d3d11)
        add_executable(xrfg_openxr_fps_overlay_tests tests/openxr_fps_overlay_tests.cpp)
        target_link_libraries(xrfg_openxr_fps_overlay_tests PRIVATE xrfg_fps_overlay)
        add_test(NAME xrfg_openxr_fps_overlay_d3d12 COMMAND xrfg_openxr_fps_overlay_tests)
        add_test(NAME xrfg_openxr_fps_overlay_d3d11 COMMAND xrfg_openxr_fps_overlay_tests d3d11)
    endif()

    add_library(XR_APILAYER_XRFrameBridge_diagnostic ${XRFG_LAYER_KIND}
        ${XRFG_LAYER_SOURCE})
    target_sources(XR_APILAYER_XRFrameBridge_diagnostic PRIVATE src/layer/ngx_guide_capture.cpp src/layer/provider_api.cpp src/ingress/native_ingress.cpp src/ingress/streamline_camera.cpp src/ingress/d3d11_native_ingress.cpp src/ingress/vulkan_native_ingress.cpp src/ingress/vulkan_command_stream.cpp)
    target_link_libraries(XR_APILAYER_XRFrameBridge_diagnostic PRIVATE safetyhook version)
    if(TARGET xrfg_vulkan_capture_suspended_tests)
        add_dependencies(xrfg_vulkan_capture_suspended_tests XR_APILAYER_XRFrameBridge_diagnostic)
        set_tests_properties(xrfg_vulkan_capture_suspended_tests PROPERTIES
            PASS_REGULAR_EXPRESSION "Shipping Vulkan capture stays suspended")
        add_test(NAME xrfg_shipping_vulkan_scope COMMAND xrfg_vulkan_capture_suspended_tests "$<TARGET_FILE:XR_APILAYER_XRFrameBridge_diagnostic>")
        foreach(game DOOMTheDarkAges TheGreatCircle)
            add_test(NAME xrfg_shipping_luke_bootstrap_${game} COMMAND "${CMAKE_COMMAND}"
                "-DTEST_EXE=$<TARGET_FILE:xrfg_vulkan_capture_suspended_tests>"
                "-DLAYER_DLL=$<TARGET_FILE:XR_APILAYER_XRFrameBridge_diagnostic>"
                "-DLUKE_FIXTURE=$<TARGET_FILE:xrfg_luke_runtime_fixture>" -DMODE=selected
                "-DDESTINATION=${CMAKE_CURRENT_BINARY_DIR}/shipping-luke-${game}"
                "-DGAME_EXE=${game}.exe"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_luke_vulkan_shipping.cmake")
            set_tests_properties(xrfg_shipping_luke_bootstrap_${game} PROPERTIES TIMEOUT 25)
        endforeach()
        add_test(NAME xrfg_shipping_flat_doom_scope COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:xrfg_vulkan_capture_suspended_tests>"
            "-DLAYER_DLL=$<TARGET_FILE:XR_APILAYER_XRFrameBridge_diagnostic>"
            "-DDESTINATION=${CMAKE_CURRENT_BINARY_DIR}/shipping-flat-doom"
            -DGAME_EXE=DOOMTheDarkAges.exe -DMODE=flat
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_luke_vulkan_shipping.cmake")
    endif()
    target_include_directories(XR_APILAYER_XRFrameBridge_diagnostic PRIVATE "${XRFG_DLSS_SDK_ROOT}/include" "${XRFG_DEPENDENCY_ROOT}/Streamline")
    target_include_directories(XR_APILAYER_XRFrameBridge_diagnostic PRIVATE
        include
        "${XRFG_OPENXR_SDK_ROOT}/include")
    target_link_libraries(XR_APILAYER_XRFrameBridge_diagnostic PRIVATE
        xrfg_core
        xrfg_d3d12_history
        xrfg_bridge_flight_logger
        xrfg_fps_overlay
        xrfg_implicit_layer
        psapi xrfg_frame_capture)
    target_compile_features(XR_APILAYER_XRFrameBridge_diagnostic PRIVATE cxx_std_20)
    target_compile_definitions(XR_APILAYER_XRFrameBridge_diagnostic PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        XR_USE_GRAPHICS_API_D3D11
        XR_USE_GRAPHICS_API_D3D12
        XR_USE_PLATFORM_WIN32)
    if(MSVC)
        target_compile_options(XR_APILAYER_XRFrameBridge_diagnostic PRIVATE
            /W4 /permissive- /EHsc)
    endif()

    add_custom_command(TARGET XR_APILAYER_XRFrameBridge_diagnostic POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/layer/ofxr_bridge.ini"
            "$<TARGET_FILE_DIR:XR_APILAYER_XRFrameBridge_diagnostic>/ofxr_bridge.ini")

    if(XRFG_BUILD_STANDALONE)
        add_library(xrfg_standalone_core STATIC
            src/tray/standalone_launcher.cpp
            src/common/ram_timing.cpp
            include/xrfg/standalone_launcher.hpp)
        target_include_directories(xrfg_standalone_core PUBLIC include)
        target_compile_features(xrfg_standalone_core PUBLIC cxx_std_20)
        target_link_libraries(xrfg_standalone_core PUBLIC xrfg_implicit_layer xrfg_core)
        target_compile_definitions(xrfg_standalone_core PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            UNICODE
            _UNICODE)
        if(MSVC)
            target_compile_options(xrfg_standalone_core PRIVATE
                /W4 /permissive- /EHsc)
        endif()

        enable_language(RC)
        add_executable(OFXRBridgeTray WIN32
            src/tray/main.cpp
            src/tray/resources.rc)
        target_link_libraries(OFXRBridgeTray PRIVATE
            xrfg_standalone_core
            comctl32
            gdi32
            ole32
            shell32
            user32)
        target_compile_features(OFXRBridgeTray PRIVATE cxx_std_20)
        target_compile_definitions(OFXRBridgeTray PRIVATE
            XRFG_IMPLEMENTATION_VERSION=${PROJECT_VERSION_MINOR}
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            UNICODE
            _UNICODE)
        if(MSVC)
            target_compile_options(OFXRBridgeTray PRIVATE
                /W4 /permissive- /EHsc)
        endif()
        add_dependencies(OFXRBridgeTray
            XR_APILAYER_XRFrameBridge_diagnostic)
        add_custom_command(TARGET OFXRBridgeTray POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:OFXRBridgeTray>/ofxr"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:XR_APILAYER_XRFrameBridge_diagnostic>"
                "$<TARGET_FILE_DIR:OFXRBridgeTray>/ofxr/XR_APILAYER_XRFrameBridge_diagnostic.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE_DIR:XR_APILAYER_XRFrameBridge_diagnostic>/ofxr_bridge.ini"
                "$<TARGET_FILE_DIR:OFXRBridgeTray>/ofxr/ofxr_bridge.ini")

        if(XRFG_BUILD_TESTS)
            add_executable(xrfg_standalone_launcher_tests
                tests/standalone_launcher_tests.cpp)
            target_link_libraries(xrfg_standalone_launcher_tests PRIVATE
                xrfg_standalone_core)
            target_compile_features(xrfg_standalone_launcher_tests PRIVATE
                cxx_std_20)
            if(MSVC)
                target_compile_options(xrfg_standalone_launcher_tests PRIVATE
                    /W4 /permissive- /EHsc)
                target_compile_definitions(xrfg_standalone_launcher_tests PRIVATE
                    NOMINMAX
                    WIN32_LEAN_AND_MEAN
                    UNICODE
                    _UNICODE)
            endif()
            add_test(NAME xrfg_standalone_launcher_tests
                COMMAND xrfg_standalone_launcher_tests)
            add_executable(xrfg_tray_lifecycle_tests tests/tray_lifecycle_tests.cpp)
            target_link_libraries(xrfg_tray_lifecycle_tests PRIVATE
                xrfg_standalone_core comctl32 ole32 shell32 user32)
            target_compile_definitions(xrfg_tray_lifecycle_tests PRIVATE
                XRFG_IMPLEMENTATION_VERSION=${PROJECT_VERSION_MINOR}
                NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
            if(MSVC)
                target_compile_options(xrfg_tray_lifecycle_tests PRIVATE /W4 /permissive- /EHsc)
            endif()
            add_test(NAME xrfg_tray_lifecycle_tests COMMAND xrfg_tray_lifecycle_tests)
        endif()
    endif()

    if(XRFG_BUILD_TESTS)
        add_executable(xrfg_bridge_flight_logger_tests
            tests/bridge_flight_logger_tests.cpp)
        target_link_libraries(xrfg_bridge_flight_logger_tests PRIVATE
            xrfg_bridge_flight_logger)
        target_compile_features(xrfg_bridge_flight_logger_tests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(xrfg_bridge_flight_logger_tests PRIVATE
                /W4 /permissive- /EHsc)
            target_compile_definitions(xrfg_bridge_flight_logger_tests PRIVATE
                NOMINMAX
                WIN32_LEAN_AND_MEAN)
        endif()
        add_test(NAME xrfg_bridge_flight_logger_tests
                COMMAND xrfg_bridge_flight_logger_tests)
        add_executable(xrfg_ram_timing_tests tests/ram_timing_tests.cpp)
        target_link_libraries(xrfg_ram_timing_tests PRIVATE xrfg_bridge_flight_logger)
        target_compile_features(xrfg_ram_timing_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_ram_timing_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME xrfg_ram_timing_tests COMMAND xrfg_ram_timing_tests)
        add_test(NAME xrfg_ram_timing_disabled COMMAND xrfg_ram_timing_tests disabled)
        set_tests_properties(xrfg_ram_timing_tests PROPERTIES TIMEOUT 30)

        add_executable(xrfg_frame_capture_tests tests/frame_capture_tests.cpp)
        target_link_libraries(xrfg_frame_capture_tests PRIVATE xrfg_frame_capture)
        target_compile_definitions(xrfg_frame_capture_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME xrfg_frame_capture_tests COMMAND xrfg_frame_capture_tests)
        set_tests_properties(xrfg_frame_capture_tests PROPERTIES TIMEOUT 30)
        add_executable(xrfg_frame_capture_ring_tests tests/frame_capture_ring_tests.cpp)
        target_link_libraries(xrfg_frame_capture_ring_tests PRIVATE xrfg_frame_capture)
        target_compile_definitions(xrfg_frame_capture_ring_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME xrfg_frame_capture_ring_tests COMMAND xrfg_frame_capture_ring_tests)
        set_tests_properties(xrfg_frame_capture_ring_tests PROPERTIES TIMEOUT 30)

        add_executable(xrfg_three_slot_director_tests
            tests/three_slot_director_tests.cpp
            src/common/embedded_control.cpp)
        add_executable(xrfg_d3d11_bridge_tests tests/d3d11_bridge_tests.cpp)
        target_link_libraries(xrfg_d3d11_bridge_tests PRIVATE xrfg_d3d12_history)
        target_compile_definitions(xrfg_d3d11_bridge_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME xrfg_d3d11_bridge_tests COMMAND xrfg_d3d11_bridge_tests)
        set_tests_properties(xrfg_d3d11_bridge_tests PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME xrfg_release_bound_guides COMMAND xrfg_d3d12_history_tests release-bound-guides)
        set_tests_properties(xrfg_release_bound_guides PROPERTIES TIMEOUT 30)
        add_test(NAME xrfg_elliott_controller_layer COMMAND xrfg_three_slot_director_tests elliott-controller)
        add_test(NAME xrfg_v474_policy_lifecycle COMMAND xrfg_three_slot_director_tests v474-policy)
        add_test(NAME xrfg_v474_d3d11_bridge COMMAND xrfg_three_slot_director_tests v474-bridge)
        add_test(NAME xrfg_v474_history_wait COMMAND xrfg_d3d12_history_tests v474-wait)
        add_test(NAME xrfg_v474_vd_restore COMMAND xrfg_three_slot_director_tests v474-vd)
        set_tests_properties(xrfg_v474_vd_restore PROPERTIES TIMEOUT 30)
        set_tests_properties(xrfg_v474_policy_lifecycle xrfg_v474_d3d11_bridge xrfg_v474_history_wait PROPERTIES TIMEOUT 30)
        add_test(NAME xrfg_interface_cycle_layer COMMAND xrfg_three_slot_director_tests cycle-metrics)
        set_tests_properties(xrfg_interface_cycle_layer PROPERTIES TIMEOUT 30)
        add_test(NAME xrfg_fsr_release_identity COMMAND xrfg_three_slot_director_tests fsr-release-identity)
        set_tests_properties(xrfg_fsr_release_identity PROPERTIES TIMEOUT 30)

        add_executable(xrfg_d3d11_dlss_guides_tests tests/d3d11_dlss_guides_tests.cpp)
        target_link_libraries(xrfg_d3d11_dlss_guides_tests PRIVATE xrfg_d3d12_history)
        target_compile_definitions(xrfg_d3d11_dlss_guides_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME xrfg_d3d11_dlss_guides_tests COMMAND xrfg_d3d11_dlss_guides_tests)
        set_tests_properties(xrfg_d3d11_dlss_guides_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(xrfg_d3d11_color_identity_tests tests/d3d11_color_identity_tests.cpp)
        add_executable(xrfg_flow_input_encoding_tests tests/flow_input_encoding_tests.cpp)
        target_include_directories(xrfg_flow_input_encoding_tests PRIVATE
            include "${XRFG_GENERATED_SHADER_DIR}")
        target_compile_features(xrfg_flow_input_encoding_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_flow_input_encoding_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(xrfg_flow_input_encoding_tests PRIVATE d3d12 dxgi)
        add_dependencies(xrfg_flow_input_encoding_tests xrfg_d3d12_history)
        add_test(NAME xrfg_flow_input_encoding COMMAND xrfg_flow_input_encoding_tests)
        set_tests_properties(xrfg_flow_input_encoding PROPERTIES TIMEOUT 30)
        add_executable(xrfg_luke_color_origin_tests tests/luke_color_origin_tests.cpp src/d3d12/luke_color_origin.cpp)
        target_include_directories(xrfg_luke_color_origin_tests PRIVATE include)
        target_compile_features(xrfg_luke_color_origin_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_luke_color_origin_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(xrfg_luke_color_origin_tests PRIVATE d3d12 dxgi xrfg_bridge_flight_logger)
        add_test(NAME xrfg_luke_color_origin COMMAND xrfg_luke_color_origin_tests)
        set_tests_properties(xrfg_luke_color_origin PROPERTIES TIMEOUT 30)
        add_executable(xrfg_ngx_feature_metadata_tests tests/ngx_feature_metadata_tests.cpp)
        target_include_directories(xrfg_ngx_feature_metadata_tests PRIVATE include "${XRFG_DLSS_SDK_ROOT}/include")
        target_compile_features(xrfg_ngx_feature_metadata_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_ngx_feature_metadata_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME xrfg_ngx_feature_metadata COMMAND xrfg_ngx_feature_metadata_tests)
        add_library(xrfg_native_export_fixture SHARED tests/native_export_fixture.cpp)
        add_executable(xrfg_native_export_address_tests tests/native_export_address_tests.cpp)
        target_include_directories(xrfg_native_export_address_tests PRIVATE include)
        target_compile_features(xrfg_native_export_address_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_native_export_address_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_dependencies(xrfg_native_export_address_tests xrfg_native_export_fixture)
        add_test(NAME xrfg_native_export_address COMMAND xrfg_native_export_address_tests "$<TARGET_FILE:xrfg_native_export_fixture>")
        target_link_libraries(xrfg_d3d11_color_identity_tests PRIVATE xrfg_d3d12_history)
        target_compile_features(xrfg_d3d11_color_identity_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_d3d11_color_identity_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME xrfg_d3d11_color_identity COMMAND xrfg_d3d11_color_identity_tests)
        set_tests_properties(xrfg_d3d11_color_identity PROPERTIES TIMEOUT 30)
        target_include_directories(xrfg_three_slot_director_tests PRIVATE
            include "${XRFG_OPENXR_SDK_ROOT}/include")
        target_compile_features(xrfg_three_slot_director_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_three_slot_director_tests PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN XR_USE_GRAPHICS_API_D3D11
            XR_USE_GRAPHICS_API_D3D12 XR_USE_PLATFORM_WIN32 OFXR_PROVIDER_API_EXPORTS)
        target_link_libraries(xrfg_three_slot_director_tests PRIVATE
            xrfg_core xrfg_d3d12_history xrfg_bridge_flight_logger
            xrfg_fps_overlay xrfg_implicit_layer psapi xrfg_frame_capture)
        add_test(NAME xrfg_three_slot_director_tests COMMAND xrfg_three_slot_director_tests)
        add_test(NAME xrfg_native_dx11_guide_session COMMAND xrfg_three_slot_director_tests native-dx11-guides)
        set_tests_properties(xrfg_native_dx11_guide_session PROPERTIES TIMEOUT 60)
        add_test(NAME xrfg_depth_history_routes COMMAND xrfg_three_slot_director_tests depth-history)
        add_test(NAME xrfg_blackbar_timing_layer COMMAND xrfg_three_slot_director_tests blackbar-timing)
        add_test(NAME xrfg_pipelined_route_handoff COMMAND xrfg_three_slot_director_tests route-handoff)
        add_test(NAME xrfg_pipelined_route_handoff_steamvr COMMAND xrfg_three_slot_director_tests route-handoff-steamvr)
        set_tests_properties(xrfg_pipelined_route_handoff PROPERTIES TIMEOUT 30)
        set_tests_properties(xrfg_pipelined_route_handoff_steamvr PROPERTIES TIMEOUT 30)
        add_test(NAME xrfg_djules_pipeline_preference COMMAND xrfg_three_slot_director_tests pipeline-preference)
        add_test(NAME xrfg_unified_route_policy COMMAND xrfg_three_slot_director_tests unified-routes)
        add_test(NAME xrfg_unified_route_shapes_gpu COMMAND xrfg_three_slot_d3d12_tests route-shapes)
        add_test(NAME xrfg_paused_route_shapes_gpu COMMAND xrfg_three_slot_d3d12_tests paused-route-shapes)
        set_tests_properties(xrfg_paused_route_shapes_gpu PROPERTIES TIMEOUT 60)
        add_test(NAME xrfg_native_provider_recovery_gpu COMMAND
            "$<TARGET_FILE_DIR:xrfg_three_slot_d3d12_tests>/provider-missing/$<TARGET_FILE_NAME:xrfg_three_slot_d3d12_tests>" provider-recovery)
        set_tests_properties(xrfg_native_provider_recovery_gpu PROPERTIES TIMEOUT 60)
        add_test(NAME xrfg_vulkan_bridge_contract COMMAND xrfg_three_slot_director_tests vulkan-contract)
        if(XRFG_TEST_NVIDIA_ROUTE STREQUAL "djules")
            add_test(NAME xrfg_djules_binding_queue COMMAND xrfg_three_slot_director_tests binding-queue)
            add_test(NAME xrfg_djules_display_clock COMMAND xrfg_three_slot_director_tests display-clock)
            add_test(NAME xrfg_djules_absolute_grid COMMAND xrfg_three_slot_director_tests absolute-grid)
            add_test(NAME xrfg_djules_triple_buffer COMMAND xrfg_three_slot_director_tests triple-buffer)
            add_test(NAME xrfg_djules_triple_inline_gpu COMMAND xrfg_three_slot_d3d12_tests triple-inline)
            add_test(NAME xrfg_djules_triple_director_gpu COMMAND xrfg_three_slot_d3d12_tests triple-core)
            add_test(NAME xrfg_djules_triple_switch_gpu COMMAND xrfg_three_slot_d3d12_tests triple-switch)
            add_test(NAME xrfg_djules_deep_readiness_gpu COMMAND xrfg_three_slot_d3d12_tests deep-readiness)
            add_test(NAME xrfg_djules_deep_switch_gpu COMMAND xrfg_three_slot_d3d12_tests deep-switch)
            add_test(NAME xrfg_djules_deep_readiness COMMAND xrfg_three_slot_director_tests deep-readiness)
            add_test(NAME xrfg_djules_inline_policy COMMAND xrfg_three_slot_director_tests inline-policy)
            add_test(NAME xrfg_djules_phase_acquisition COMMAND xrfg_three_slot_director_tests phase-acquisition)
            set_tests_properties(xrfg_djules_binding_queue PROPERTIES TIMEOUT 15)
        endif()
        add_test(NAME xrfg_custom_time_pairing_recovery COMMAND xrfg_three_slot_director_tests pairing-recovery)
        add_test(NAME xrfg_luke_admission COMMAND xrfg_three_slot_director_tests luke-unit)
        add_test(NAME xrfg_luke_fresh_pacing COMMAND xrfg_three_slot_director_tests luke-fresh-pacing)
        add_test(NAME xrfg_luke_startup_passthrough COMMAND xrfg_three_slot_director_tests luke-startup-pacing)
        set_tests_properties(xrfg_luke_startup_passthrough PROPERTIES TIMEOUT 30)
        add_test(NAME xrfg_luke_clock_continuity COMMAND xrfg_three_slot_director_tests luke-clock-continuity)
        set_tests_properties(xrfg_luke_clock_continuity PROPERTIES TIMEOUT 30)
        set_tests_properties(xrfg_luke_fresh_pacing PROPERTIES TIMEOUT 30)
        add_test(NAME xrfg_luke_inline_diagnostic COMMAND xrfg_three_slot_director_tests luke-inline-diagnostic)
        add_test(NAME xrfg_luke_afw_output_slot COMMAND xrfg_three_slot_director_tests luke-afw-slot)
        add_test(NAME xrfg_luke_post_afw_director_packet COMMAND xrfg_three_slot_director_tests post-afw-packet)
        set_tests_properties(xrfg_luke_post_afw_director_packet PROPERTIES TIMEOUT 30)
        set_tests_properties(xrfg_luke_afw_output_slot PROPERTIES TIMEOUT 30)
        add_test(NAME xrfg_luke_cycle_clock COMMAND xrfg_three_slot_director_tests luke-clock)
        set_tests_properties(xrfg_luke_cycle_clock PROPERTIES TIMEOUT 10)
        add_test(NAME xrfg_luke_gpu_pipeline COMMAND xrfg_three_slot_director_tests luke-gpu)
        add_test(NAME xrfg_luke_ram_pipeline COMMAND xrfg_three_slot_director_tests luke-ram)
        set_tests_properties(xrfg_luke_admission xrfg_luke_gpu_pipeline xrfg_luke_ram_pipeline PROPERTIES TIMEOUT 30)
        add_test(NAME xrfg_three_slot_composition_tests COMMAND xrfg_three_slot_director_tests composition)
        add_test(NAME xrfg_three_slot_capture_integration COMMAND xrfg_three_slot_director_tests capture)
        set_tests_properties(xrfg_three_slot_capture_integration PROPERTIES TIMEOUT 60)
        set_tests_properties(xrfg_three_slot_composition_tests PROPERTIES TIMEOUT 60)
        add_test(NAME xrfg_three_slot_transition_tests COMMAND xrfg_three_slot_director_tests transition)
        set_tests_properties(xrfg_three_slot_transition_tests PROPERTIES TIMEOUT 60)
        set_tests_properties(xrfg_three_slot_director_tests PROPERTIES TIMEOUT 20)
        add_test(NAME xrfg_three_slot_nvidia_d3d11_pipeline
              COMMAND xrfg_three_slot_director_tests gpu)
        add_test(NAME xrfg_ram_timing_d3d11 COMMAND xrfg_three_slot_director_tests ram)
        add_test(NAME xrfg_capture_gap_d3d11 COMMAND xrfg_three_slot_director_tests gap)
        set_tests_properties(xrfg_three_slot_nvidia_d3d11_pipeline PROPERTIES TIMEOUT 30)

        add_executable(xrfg_three_slot_d3d12_tests tests/three_slot_d3d12_tests.cpp
            src/common/embedded_control.cpp)
        target_include_directories(xrfg_three_slot_d3d12_tests PRIVATE include "${XRFG_OPENXR_SDK_ROOT}/include")
        target_compile_features(xrfg_three_slot_d3d12_tests PRIVATE cxx_std_20)
        target_compile_definitions(xrfg_three_slot_d3d12_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
            XR_USE_GRAPHICS_API_D3D11 XR_USE_GRAPHICS_API_D3D12 XR_USE_PLATFORM_WIN32 OFXR_PROVIDER_API_EXPORTS)
        target_link_libraries(xrfg_three_slot_d3d12_tests PRIVATE xrfg_core xrfg_d3d12_history
            xrfg_bridge_flight_logger xrfg_fps_overlay xrfg_implicit_layer psapi xrfg_frame_capture)
        # A separate physical executable directory deliberately omits the
        # DLSS-G provider. NGX really refuses this route; no GPU failure shim.
        add_custom_command(TARGET xrfg_three_slot_d3d12_tests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:xrfg_three_slot_d3d12_tests>/provider-missing"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:xrfg_three_slot_d3d12_tests>"
                "$<TARGET_FILE_DIR:xrfg_three_slot_d3d12_tests>/provider-missing/$<TARGET_FILE_NAME:xrfg_three_slot_d3d12_tests>")
        add_test(NAME xrfg_three_slot_d3d12_split COMMAND xrfg_three_slot_d3d12_tests)
        add_test(NAME xrfg_three_slot_d3d12_capture_busy COMMAND xrfg_three_slot_d3d12_tests capture-busy)
        add_test(NAME xrfg_ram_timing_d3d12 COMMAND xrfg_three_slot_d3d12_tests ram)
        add_test(NAME xrfg_three_slot_d3d12_array COMMAND xrfg_three_slot_d3d12_tests array)
        add_test(NAME xrfg_legacy_nvidia_queue_gpu COMMAND xrfg_three_slot_d3d12_tests legacy-queue)
        add_test(NAME xrfg_legacy_nvidia_queue_vdxr_gpu COMMAND xrfg_three_slot_d3d12_tests legacy-queue-vdxr)
        add_test(NAME xrfg_legacy_nvidia_queue_slow_gpu COMMAND xrfg_three_slot_d3d12_tests legacy-queue-slow)
        add_test(NAME xrfg_legacy_nvidia_early_current_gpu COMMAND xrfg_three_slot_d3d12_tests legacy-queue-early-b)
        add_test(NAME xrfg_new_nvidia_midpoint_cadence_gpu COMMAND xrfg_three_slot_d3d12_tests midpoint-cadence)
        add_test(NAME xrfg_fsr_inline_missing_guides COMMAND xrfg_three_slot_d3d12_tests fsr-inline-wait)
        add_test(NAME xrfg_fsr_post_hud_contract
            COMMAND "${CMAKE_COMMAND}"
                "-DXRFG_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/fsr_post_hud_contract.cmake")
        if(EXISTS "${XRFG_FSR_RUNTIME}")
            add_custom_command(TARGET xrfg_three_slot_d3d12_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${XRFG_FSR_RUNTIME}"
                    "$<TARGET_FILE_DIR:xrfg_three_slot_d3d12_tests>/ofxr_amd_fidelityfx_framegeneration_dx12.dll")
            add_test(NAME xrfg_fsr_inline_gpu COMMAND xrfg_three_slot_d3d12_tests fsr-inline)
            add_test(NAME xrfg_fsr_packed_inline_gpu COMMAND xrfg_three_slot_d3d12_tests fsr-packed)
            add_test(NAME xrfg_fsr_packed_queue_gpu COMMAND xrfg_three_slot_d3d12_tests fsr-queue-packed)
            set_tests_properties(xrfg_fsr_packed_inline_gpu xrfg_fsr_packed_queue_gpu PROPERTIES TIMEOUT 60)
            add_test(NAME xrfg_lower_vram_new_gpu COMMAND xrfg_three_slot_d3d12_tests lower-vram)
            add_test(NAME xrfg_direct_output_new_gpu COMMAND xrfg_three_slot_d3d12_tests direct-output)
            add_test(NAME xrfg_lower_vram_legacy_gpu COMMAND xrfg_three_slot_d3d12_tests legacy-queue-lower-vram)
            add_test(NAME xrfg_lower_vram_fidelityfx_gpu COMMAND xrfg_three_slot_d3d12_tests fidelityfx-lower-vram)
            add_test(NAME xrfg_lower_vram_fsr_gpu COMMAND xrfg_three_slot_d3d12_tests fsr-queue-lower-vram)
            set_tests_properties(xrfg_lower_vram_new_gpu xrfg_direct_output_new_gpu xrfg_lower_vram_legacy_gpu xrfg_lower_vram_fidelityfx_gpu xrfg_lower_vram_fsr_gpu PROPERTIES TIMEOUT 60)
            if(XRFG_TEST_NVIDIA_ROUTE STREQUAL "djules")
                add_test(NAME xrfg_direct_output_3x_gpu COMMAND xrfg_three_slot_d3d12_tests triple-core-direct-output)
                add_test(NAME xrfg_direct_output_3x_inline_gpu COMMAND xrfg_three_slot_d3d12_tests triple-inline-direct-output)
                set_tests_properties(xrfg_direct_output_3x_gpu xrfg_direct_output_3x_inline_gpu PROPERTIES TIMEOUT 60)
            endif()
            add_test(NAME xrfg_fsr_stereo_guard_inline_gpu COMMAND xrfg_three_slot_d3d12_tests fsr-stereo-guard)
            add_test(NAME xrfg_fsr_stereo_guard_pipelined_gpu COMMAND xrfg_three_slot_d3d12_tests fsr-queue-stereo-guard)
            set_tests_properties(xrfg_fsr_stereo_guard_inline_gpu xrfg_fsr_stereo_guard_pipelined_gpu PROPERTIES TIMEOUT 60)
            add_test(NAME xrfg_fsr_steamvr_queue_gpu COMMAND xrfg_three_slot_d3d12_tests fsr-queue)
            add_test(NAME xrfg_fsr_steamvr_queue_slow_gpu COMMAND xrfg_three_slot_d3d12_tests fsr-queue-slow)
            add_test(NAME xrfg_fsr_steamvr_queue_switch_gpu COMMAND xrfg_three_slot_d3d12_tests fsr-queue-switch)
            add_test(NAME xrfg_fsr_steamvr_queue_pending_gpu COMMAND xrfg_three_slot_d3d12_tests fsr-queue-gpu-pending)
            set_tests_properties(xrfg_fsr_inline_gpu PROPERTIES TIMEOUT 60)
        endif()
        set_tests_properties(xrfg_fsr_inline_missing_guides PROPERTIES TIMEOUT 60)
        add_test(NAME xrfg_capture_gap_d3d12_split COMMAND xrfg_three_slot_d3d12_tests gap-split)
        add_test(NAME xrfg_capture_gap_d3d12_array COMMAND xrfg_three_slot_d3d12_tests gap-array)
        set_tests_properties(xrfg_capture_gap_d3d11 xrfg_capture_gap_d3d12_split xrfg_capture_gap_d3d12_array PROPERTIES TIMEOUT 30)
        add_test(NAME xrfg_three_slot_d3d12_repeated_eye COMMAND xrfg_three_slot_d3d12_tests repeat)
        add_test(NAME xrfg_three_slot_d3d12_promotion_split COMMAND xrfg_three_slot_d3d12_tests promotion-split)
        add_test(NAME xrfg_three_slot_d3d12_promotion_array COMMAND xrfg_three_slot_d3d12_tests promotion-array)
        set_tests_properties(xrfg_three_slot_d3d12_split xrfg_three_slot_d3d12_array
            xrfg_three_slot_d3d12_repeated_eye xrfg_three_slot_d3d12_promotion_split
            xrfg_three_slot_d3d12_promotion_array PROPERTIES TIMEOUT 60)

        add_executable(xrfg_layer_smoke tests/layer_smoke.cpp)
        target_include_directories(xrfg_layer_smoke PRIVATE
            "${XRFG_OPENXR_SDK_ROOT}/include")
        target_compile_features(xrfg_layer_smoke PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(xrfg_layer_smoke PRIVATE /W4 /permissive- /EHsc)
            target_compile_definitions(xrfg_layer_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        endif()
        add_test(
            NAME xrfg_layer_negotiation
            COMMAND xrfg_layer_smoke ${XRFG_TEST_LAYER_DLL})
        add_test(NAME xrfg_layer_process_exclusion COMMAND "${CMAKE_COMMAND}"
            "-DSMOKE=$<TARGET_FILE:xrfg_layer_smoke>" "-DLAYER=${XRFG_TEST_LAYER_DLL}"
            "-DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/process-exclusion-fixture"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/layer_process_exclusion.cmake")

        add_test(NAME xrfg_layer_projection_eyes
            COMMAND "${CMAKE_COMMAND}"
                "-DLAYER_DLL=${XRFG_TEST_LAYER_DLL}"
                "-DCALL_CHAIN=$<TARGET_FILE:xrfg_layer_call_chain>"
                "-DENABLED_INI=${CMAKE_CURRENT_SOURCE_DIR}/tests/ofxr_bridge_logging_enabled.ini"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/projection-eyes"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/layer_projection_eyes.cmake")
        set_tests_properties(xrfg_layer_projection_eyes PROPERTIES TIMEOUT 60)
        add_executable(xrfg_layer_call_chain tests/layer_call_chain.cpp)
        target_include_directories(xrfg_layer_call_chain PRIVATE
            "${XRFG_OPENXR_SDK_ROOT}/include")
        target_compile_features(xrfg_layer_call_chain PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(xrfg_layer_call_chain PRIVATE /W4 /permissive- /EHsc)
            target_compile_definitions(xrfg_layer_call_chain PRIVATE
                NOMINMAX
                WIN32_LEAN_AND_MEAN
                XR_USE_GRAPHICS_API_D3D11
                XR_USE_GRAPHICS_API_D3D12
                XR_USE_PLATFORM_WIN32)
        endif()
        target_link_libraries(xrfg_layer_call_chain PRIVATE d3d11 d3d12 dxgi)
        foreach(mode inverted-fov d3d11-inverted-fov)
            add_test(NAME xrfg_layer_${mode} COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-${mode}.log" ${mode})
        endforeach()
        add_dependencies(xrfg_layer_call_chain XR_APILAYER_XRFrameBridge_diagnostic)
        add_executable(xrfg_layer_manual_disarm_tests tests/layer_manual_disarm_tests.cpp)
        target_include_directories(xrfg_layer_manual_disarm_tests PRIVATE "${XRFG_OPENXR_SDK_ROOT}/include")
        target_link_libraries(xrfg_layer_manual_disarm_tests PRIVATE xrfg_implicit_layer d3d11 d3d12 dxgi)
        target_compile_definitions(xrfg_layer_manual_disarm_tests PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN XR_USE_GRAPHICS_API_D3D11 XR_USE_GRAPHICS_API_D3D12 XR_USE_PLATFORM_WIN32)
        if(MSVC)
            target_compile_options(xrfg_layer_manual_disarm_tests PRIVATE /W4 /permissive- /EHsc)
        endif()
        add_dependencies(xrfg_layer_manual_disarm_tests XR_APILAYER_XRFrameBridge_diagnostic)
        foreach(mode inline d3d11 steamvr flight)
            add_test(NAME xrfg_manual_disarm_${mode} COMMAND xrfg_layer_manual_disarm_tests
                ${XRFG_TEST_LAYER_DLL} ${mode})
            set_tests_properties(xrfg_manual_disarm_${mode} PROPERTIES TIMEOUT 30)
        endforeach()
        foreach(marker on off hidden)
            add_test(NAME xrfg_layer_marker_d3d11_${marker} COMMAND xrfg_layer_manual_disarm_tests
                ${XRFG_TEST_LAYER_DLL} d3d11 ${marker})
            set_tests_properties(xrfg_layer_marker_d3d11_${marker} PROPERTIES TIMEOUT 30)
        endforeach()
        add_test(
            NAME xrfg_layer_call_chain
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-call-chain-test.log")
        add_test(NAME xrfg_layer_acquire_ahead
            COMMAND "${CMAKE_COMMAND}"
                "-DLAYER_DLL=${XRFG_TEST_LAYER_DLL}"
                "-DCALL_CHAIN=$<TARGET_FILE:xrfg_layer_call_chain>"
                "-DENABLED_INI=${CMAKE_CURRENT_SOURCE_DIR}/tests/ofxr_bridge_logging_enabled.ini"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/d02-acquire-ahead"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/layer_acquire_ahead.cmake")
        add_test(
            NAME xrfg_layer_presenter_swapchain_lifetime
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-presenter-swapchain-lifetime.log"
                flight-destroy-swapchain)
        add_test(
            NAME xrfg_layer_presenter_space_lifetime
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-presenter-space-lifetime.log"
                steamvr-destroy-space)

        add_test(
            NAME xrfg_layer_split_eye_call_chain
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-split-eye-call-chain-test.log"
                split-eye)

        add_test(
            NAME xrfg_layer_cropped_split_eye_call_chain
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-cropped-split-eye-call-chain-test.log"
                cropped-split-eye)

        add_test(
            NAME xrfg_layer_double_wide_call_chain
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-double-wide-call-chain-test.log"
                double-wide)

        add_test(
            NAME xrfg_layer_d3d11_interop
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-d3d11-interop-test.log"
                d3d11-interop)

        add_test(
            NAME xrfg_layer_d3d11_double_wide
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-d3d11-double-wide-test.log"
                d3d11-double-wide)

        add_test(
            NAME xrfg_layer_steamvr_inline
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-steamvr-inline-test.log"
                steamvr-inline)

        add_test(
            NAME xrfg_layer_steamvr_presenter_transition
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-steamvr-presenter-test.log"
                steamvr-presenter)

        add_test(
            NAME xrfg_layer_pipelined_presenter
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-pipelined-presenter-test.log"
                flight-simulator)

        add_test(
            NAME xrfg_layer_native_time_rewrite
            COMMAND xrfg_layer_call_chain
                ${XRFG_TEST_LAYER_DLL}
                "${CMAKE_CURRENT_BINARY_DIR}/xrfg-layer-native-time-rewrite-test.log"
                native-time-rewrite)

        add_test(
            NAME xrfg_layer_flight_logging
            COMMAND "${CMAKE_COMMAND}"
                "-DLAYER_DLL=${XRFG_TEST_LAYER_DLL}"
                "-DCALL_CHAIN=$<TARGET_FILE:xrfg_layer_call_chain>"
                "-DENABLED_INI=${CMAKE_CURRENT_SOURCE_DIR}/tests/ofxr_bridge_logging_enabled.ini"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/flight-logging-contract"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/layer_flight_logging.cmake")

        add_test(
            NAME xrfg_layer_d3d11_interop_logging
            COMMAND "${CMAKE_COMMAND}"
                "-DLAYER_DLL=${XRFG_TEST_LAYER_DLL}"
                "-DCALL_CHAIN=$<TARGET_FILE:xrfg_layer_call_chain>"
                "-DENABLED_INI=${CMAKE_CURRENT_SOURCE_DIR}/tests/ofxr_bridge_logging_enabled.ini"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/d3d11-interop-logging-contract"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/layer_d3d11_interop_logging.cmake")

        add_executable(xrfg_luke_depth_gpu_tests tests/luke_depth_gpu_tests.cpp)
        add_executable(xrfg_luke_vulkan_interop_tests tests/luke_vulkan_interop_tests.cpp src/d3d12/luke_color_origin.cpp)
        target_include_directories(xrfg_luke_vulkan_interop_tests PRIVATE include "${XRFG_VULKAN_INCLUDE_DIR}" "${XRFG_DLSS_SDK_ROOT}/include")
        target_compile_definitions(xrfg_luke_vulkan_interop_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_features(xrfg_luke_vulkan_interop_tests PRIVATE cxx_std_20)
        target_link_libraries(xrfg_luke_vulkan_interop_tests PRIVATE d3d12 dxgi xrfg_bridge_flight_logger
            "${XRFG_DEPENDENCY_ROOT}/Vulkan/vulkan-1.lib")
        add_test(NAME xrfg_luke_vulkan_interop COMMAND xrfg_luke_vulkan_interop_tests)
        set_tests_properties(xrfg_luke_vulkan_interop PROPERTIES TIMEOUT 25)
        target_include_directories(xrfg_luke_depth_gpu_tests PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
        target_link_libraries(xrfg_luke_depth_gpu_tests PRIVATE d3d12 dxgi)
        target_compile_features(xrfg_luke_depth_gpu_tests PRIVATE cxx_std_20)
        add_dependencies(xrfg_luke_depth_gpu_tests xrfg_implicit_layer xrfg_d3d12_history)
        add_test(NAME xrfg_luke_depth_gpu COMMAND xrfg_luke_depth_gpu_tests)
        add_executable(xrfg_d3d12_history_tests tests/d3d12_history_tests.cpp)
        add_executable(xrfg_afw_terminal_capture_tests tests/afw_terminal_capture_tests.cpp)
        target_include_directories(xrfg_afw_terminal_capture_tests PRIVATE include)
        target_compile_definitions(xrfg_afw_terminal_capture_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_features(xrfg_afw_terminal_capture_tests PRIVATE cxx_std_20)
        target_link_libraries(xrfg_afw_terminal_capture_tests PRIVATE xrfg_d3d12_history d3d11 d3d12 dxgi)
        add_test(NAME xrfg_afw_terminal_capture COMMAND xrfg_afw_terminal_capture_tests)
        set_tests_properties(xrfg_afw_terminal_capture PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
        target_include_directories(xrfg_d3d12_history_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
        target_include_directories(xrfg_d3d12_history_tests PRIVATE include)
        target_include_directories(xrfg_d3d12_history_tests PRIVATE "${XRFG_DLSS_SDK_ROOT}/include")
        target_link_libraries(xrfg_d3d12_history_tests PRIVATE
            xrfg_d3d12_history
            d3d12
            dxgi)
        target_compile_features(xrfg_d3d12_history_tests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(xrfg_d3d12_history_tests PRIVATE
                /W4 /permissive- /EHsc)
            target_compile_definitions(xrfg_d3d12_history_tests PRIVATE
                NOMINMAX
                WIN32_LEAN_AND_MEAN)
        endif()
        if(XRFG_TEST_NVIDIA_ROUTE STREQUAL "djules")
            add_test(NAME xrfg_djules_extra_output_gpu COMMAND xrfg_d3d12_history_tests djules-extra)
        endif()
        add_test(NAME xrfg_d3d12_history_tests COMMAND xrfg_d3d12_history_tests)
        add_test(NAME xrfg_native_dlssg_unsupported COMMAND xrfg_d3d12_history_tests native-unsupported)
        add_test(NAME xrfg_packed_game_vectors_warp COMMAND xrfg_d3d12_history_tests packed-game-vectors)
        add_test(NAME xrfg_packed_game_vectors_nvidia COMMAND xrfg_d3d12_history_tests packed-game-vectors nvidia)
        add_test(NAME xrfg_native_dlssg_late_discovery COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:xrfg_d3d12_history_tests>"
            "-DFORWARDER=$<TARGET_FILE:xrfg_dlssg_forwarder>"
            "-DPROVIDER=${XRFG_DLSSG_RUNTIME}"
            "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/native-late-fixtures"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_late_discovery.cmake")
        set_tests_properties(xrfg_packed_game_vectors_warp xrfg_packed_game_vectors_nvidia xrfg_native_dlssg_late_discovery PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
            ENVIRONMENT "LOCALAPPDATA=${CMAKE_CURRENT_BINARY_DIR}/native-late-test-profile")
        set(XRFG_TEST_LEGACY_DLSSG_PROVIDER "" CACHE FILEPATH "Optional real older game FG provider for compatibility regression")
        if(XRFG_TEST_LEGACY_DLSSG_PROVIDER)
            add_test(NAME xrfg_native_dlssg_core_compat COMMAND "${CMAKE_COMMAND}"
                "-DTEST_EXE=$<TARGET_FILE:xrfg_d3d12_history_tests>"
                "-DFORWARDER=$<TARGET_FILE:xrfg_dlssg_forwarder>"
                "-DPROVIDER=${XRFG_DLSSG_RUNTIME}"
                "-DLEGACY_PROVIDER=${XRFG_TEST_LEGACY_DLSSG_PROVIDER}"
                "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/native-core-fixtures"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_core_compat.cmake")
            set_tests_properties(xrfg_native_dlssg_core_compat PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
                ENVIRONMENT "LOCALAPPDATA=${CMAKE_CURRENT_BINARY_DIR}/native-core-test-profile")
        endif()

        add_test(NAME xrfg_luke_protected_guides COMMAND xrfg_d3d12_history_tests protected-guides)



        add_executable(xrfg_vertical_fov_tests tests/vertical_fov_tests.cpp)
        target_include_directories(xrfg_vertical_fov_tests PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
        target_link_libraries(xrfg_vertical_fov_tests PRIVATE xrfg_d3d12_history d3d12 dxgi)
        target_compile_features(xrfg_vertical_fov_tests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(xrfg_vertical_fov_tests PRIVATE /W4 /permissive- /EHsc)
            target_compile_definitions(xrfg_vertical_fov_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        endif()
        add_test(NAME xrfg_vertical_fov_tests COMMAND xrfg_vertical_fov_tests)
        add_executable(xrfg_synthetic_marker_tests tests/synthetic_marker_tests.cpp)
        target_include_directories(xrfg_synthetic_marker_tests PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
        target_link_libraries(xrfg_synthetic_marker_tests PRIVATE xrfg_d3d12_history d3d12 dxgi)
        target_compile_features(xrfg_synthetic_marker_tests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(xrfg_synthetic_marker_tests PRIVATE /W4 /permissive- /EHsc)
            target_compile_definitions(xrfg_synthetic_marker_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        endif()
        add_test(NAME xrfg_synthetic_marker_tests COMMAND xrfg_synthetic_marker_tests)

        add_executable(xrfg_nvidia_optical_flow_probe
            tests/nvidia_optical_flow_probe.cpp)
        target_include_directories(xrfg_nvidia_optical_flow_probe PRIVATE
            "${XRFG_NVIDIA_OPTICAL_FLOW_INCLUDE_DIR}")
        target_link_libraries(xrfg_nvidia_optical_flow_probe PRIVATE d3d12 dxgi)
        target_compile_features(xrfg_nvidia_optical_flow_probe PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(xrfg_nvidia_optical_flow_probe PRIVATE
                /W4 /permissive- /EHsc)
            target_compile_definitions(xrfg_nvidia_optical_flow_probe PRIVATE
                NOMINMAX
                WIN32_LEAN_AND_MEAN)
        endif()
    endif()
endif()

if(XRFG_BUILD_TESTS AND XRFG_TEST_NVIDIA_ROUTE STREQUAL "djules")
    foreach(test_target xrfg_three_slot_director_tests xrfg_three_slot_d3d12_tests xrfg_d3d12_history_tests)
        if(TARGET ${test_target})
            target_compile_definitions(${test_target} PRIVATE OFXR_TEST_DJULES_ROUTE=1)
        endif()
    endforeach()
endif()

include(cmake/neural_rendering.cmake)
include(cmake/vulkan_contracts.cmake)
include(cmake/ingress_contracts.cmake)
include(cmake/autonomous_ui.cmake)
if(TARGET xrfg_streamline_native_interface_tests)
    target_link_libraries(xrfg_streamline_native_interface_tests PRIVATE xrfg_d3d12_history)
endif()
if(XRFG_BUILD_TESTS AND XRFG_BUILD_LAYER)
    foreach(t xrfg_three_slot_director_tests xrfg_three_slot_d3d12_tests xrfg_openxr_elliott_meter_tests)
        target_sources(${t} PRIVATE src/layer/ngx_guide_capture.cpp src/ingress/native_ingress.cpp src/ingress/streamline_camera.cpp src/ingress/d3d11_native_ingress.cpp src/ingress/vulkan_native_ingress.cpp src/ingress/vulkan_command_stream.cpp)
        target_include_directories(${t} PRIVATE "${XRFG_DLSS_SDK_ROOT}/include" "${XRFG_DEPENDENCY_ROOT}/Streamline")
        target_link_libraries(${t} PRIVATE xrfg_autonomous_control xrfg_neural_rendering safetyhook version)
        target_compile_definitions(${t} PRIVATE XRFG_NATIVE_DLSSG)
    endforeach()
endif()
if(XRFG_BUILD_LAYER)
  target_compile_definitions(XR_APILAYER_XRFrameBridge_diagnostic PRIVATE OFXR_PROVIDER_API_EXPORTS XRFG_NATIVE_DLSSG)
  target_link_libraries(XR_APILAYER_XRFrameBridge_diagnostic PRIVATE xrfg_neural_rendering)
  add_dependencies(XR_APILAYER_XRFrameBridge_diagnostic xrfg_neural_forwarder)
  target_compile_definitions(OFXRBridgeTray PRIVATE XRFG_NATIVE_DLSSG)
  add_dependencies(OFXRBridgeTray OFXR_vulkan_queue_layer xrfg_neural_forwarder)
  add_dependencies(OFXRBridgeTray xrfg_dlssg_forwarder)
  add_custom_command(TARGET OFXRBridgeTray POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:xrfg_dlssg_forwarder>" "$<TARGET_FILE_DIR:OFXRBridgeTray>/ofxr/nvngx.dll_dlssg.dll")
  add_custom_command(TARGET OFXRBridgeTray POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:OFXR_vulkan_queue_layer>" "$<TARGET_FILE_DIR:OFXRBridgeTray>/ofxr/OFXR_vulkan_queue_layer.dll")
  add_custom_command(TARGET OFXRBridgeTray POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${XRFG_DLSSG_RUNTIME}" "$<TARGET_FILE_DIR:OFXRBridgeTray>/ofxr/nvngx_dlssg.dll")
  add_custom_command(TARGET OFXRBridgeTray POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:xrfg_neural_forwarder>" "$<TARGET_FILE_DIR:OFXRBridgeTray>/ofxr/nvngx.dll_dlssnr.dll")
  foreach(runtime_file "${XRFG_FSR_RUNTIME}" "${XRFG_AFW_RUNTIME}")
    if(NOT EXISTS "${runtime_file}")
      message(FATAL_ERROR "Autonomous package provider missing: ${runtime_file}")
    endif()
    add_custom_command(TARGET OFXRBridgeTray POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${runtime_file}" "$<TARGET_FILE_DIR:OFXRBridgeTray>/ofxr/")
  endforeach()
endif()

include(cmake/streamline_abi_tests.cmake)
if(TARGET xrfg_three_slot_director_tests AND TARGET xrfg_sl_fixture_legacy)
    add_dependencies(xrfg_three_slot_director_tests xrfg_sl_fixture_legacy)
    add_test(NAME xrfg_luke_ngx_actual_capture COMMAND xrfg_three_slot_director_tests
        luke-ngx-actual-capture "$<TARGET_FILE:xrfg_luke_ngx_dispatch_fixture>"
        "$<TARGET_FILE:xrfg_sl_fixture_legacy>")
    set_tests_properties(xrfg_luke_ngx_actual_capture PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    add_test(NAME xrfg_kcd_logical_capture COMMAND xrfg_three_slot_director_tests
        kcd-ngx-logical-capture "$<TARGET_FILE:xrfg_luke_ngx_dispatch_fixture>"
        "$<TARGET_FILE:xrfg_sl_fixture_legacy>")
    set_tests_properties(xrfg_kcd_logical_capture PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "KCD public source:")
    add_test(NAME xrfg_kcd_scene_capture COMMAND xrfg_three_slot_director_tests
        kcd-ngx-scene-capture "$<TARGET_FILE:xrfg_luke_ngx_dispatch_fixture>"
        "$<TARGET_FILE:xrfg_sl_fixture_legacy>")
    set_tests_properties(xrfg_kcd_scene_capture PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "KCD scene camera:")
    add_test(NAME xrfg_luke_private_capture COMMAND xrfg_three_slot_director_tests
        luke-ngx-private-capture "$<TARGET_FILE:xrfg_luke_ngx_dispatch_fixture>"
        "$<TARGET_FILE:xrfg_sl_fixture_legacy>")
    set_tests_properties(xrfg_luke_private_capture PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Luke private ingress:")
    add_test(NAME xrfg_outlaws_logical_capture COMMAND xrfg_three_slot_director_tests
        outlaws-ngx-logical-capture "$<TARGET_FILE:xrfg_luke_ngx_dispatch_fixture>"
        "$<TARGET_FILE:xrfg_sl_fixture_legacy>")
    set_tests_properties(xrfg_outlaws_logical_capture PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Outlaws public RR:")
endif()

if(XRFG_BUILD_TESTS AND TARGET xrfg_three_slot_director_tests)
    add_library(xrfg_luke_runtime_fixture SHARED tests/luke_runtime_fixture.cpp)
    target_include_directories(xrfg_luke_runtime_fixture PRIVATE "${XRFG_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(xrfg_luke_runtime_fixture PRIVATE VK_NO_PROTOTYPES)
    set_target_properties(xrfg_luke_runtime_fixture PROPERTIES OUTPUT_NAME "luke-runtime-fixture")
    add_dependencies(xrfg_three_slot_director_tests xrfg_luke_runtime_fixture)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/RealVR.ini" CONTENT "[Horizon]\nWorldScale=1.0\n")
    add_custom_command(TARGET xrfg_three_slot_director_tests POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${XRFG_AFW_RUNTIME}" "$<TARGET_FILE_DIR:xrfg_three_slot_director_tests>/PDAFWPlugin.dll")
    add_test(NAME xrfg_luke_standalone_context COMMAND xrfg_three_slot_director_tests luke-standalone-context)
    add_test(NAME xrfg_outlaws_startup COMMAND xrfg_three_slot_director_tests outlaws-startup
        "$<TARGET_FILE:xrfg_luke_runtime_fixture>" "${XRFG_AFW_RUNTIME}")
    set_tests_properties(xrfg_outlaws_startup PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Outlaws startup:")
    add_test(NAME xrfg_luke_missing_producer_bypass COMMAND xrfg_layer_manual_disarm_tests
        ${XRFG_TEST_LAYER_DLL} luke-no-guides "$<TARGET_FILE:xrfg_luke_runtime_fixture>")
    set_tests_properties(xrfg_luke_missing_producer_bypass PROPERTIES TIMEOUT 45)
    foreach(game DOOMTheDarkAges TheGreatCircle)
        add_test(NAME xrfg_luke_vulkan_bootstrap_${game} COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:xrfg_three_slot_director_tests>"
            "-DLUKE_FIXTURE=$<TARGET_FILE:xrfg_luke_runtime_fixture>"
            "-DDESTINATION=${CMAKE_CURRENT_BINARY_DIR}/luke-vulkan-${game}"
            "-DGAME_EXE=${game}.exe"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_luke_vulkan_bootstrap.cmake")
        set_tests_properties(xrfg_luke_vulkan_bootstrap_${game} PROPERTIES TIMEOUT 30)
    endforeach()
    set_tests_properties(xrfg_luke_standalone_context PROPERTIES TIMEOUT 30)
endif()

if(WIN32 AND XRFG_BUILD_TESTS)
 add_executable(xrfg_luke_provider_continuity_tests tests/luke_provider_continuity_tests.cpp)
 target_include_directories(xrfg_luke_provider_continuity_tests PRIVATE include)
 target_compile_features(xrfg_luke_provider_continuity_tests PRIVATE cxx_std_20)
 target_compile_definitions(xrfg_luke_provider_continuity_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
 target_link_libraries(xrfg_luke_provider_continuity_tests PRIVATE d3d12 dxgi)
 add_test(NAME xrfg_luke_provider_continuity COMMAND xrfg_luke_provider_continuity_tests "${XRFG_AFW_RUNTIME}")
 set_tests_properties(xrfg_luke_provider_continuity PROPERTIES TIMEOUT 30)
 if(TARGET xrfg_d3d12_history)
  add_executable(xrfg_luke_kcd_quality_tests tests/luke_kcd_quality_tests.cpp)
  target_include_directories(xrfg_luke_kcd_quality_tests PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
  target_compile_features(xrfg_luke_kcd_quality_tests PRIVATE cxx_std_20)
  target_compile_definitions(xrfg_luke_kcd_quality_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(xrfg_luke_kcd_quality_tests PRIVATE xrfg_d3d12_history d3d12 dxgi)
  add_test(NAME xrfg_luke_kcd_quality_bundled COMMAND "${CMAKE_COMMAND}"
      "-DTEST_EXE=$<TARGET_FILE:xrfg_luke_kcd_quality_tests>" "-DPROVIDER=${XRFG_AFW_RUNTIME}"
      "-DDESTINATION=${CMAKE_CURRENT_BINARY_DIR}/kcd-quality-bundled"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_luke_kcd_quality.cmake")
  set_tests_properties(xrfg_luke_kcd_quality_bundled PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE
      PASS_REGULAR_EXPRESSION "KCD full quality:")
  set(XRFG_KCD_GAME_AFW_PROVIDER "" CACHE FILEPATH "Optional read-only actual KCD PDAFW provider for quality comparison")
  if(EXISTS "${XRFG_KCD_GAME_AFW_PROVIDER}")
   add_test(NAME xrfg_luke_kcd_quality_game COMMAND "${CMAKE_COMMAND}"
       "-DTEST_EXE=$<TARGET_FILE:xrfg_luke_kcd_quality_tests>" "-DPROVIDER=${XRFG_KCD_GAME_AFW_PROVIDER}"
       "-DDESTINATION=${CMAKE_CURRENT_BINARY_DIR}/kcd-quality-game"
       -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_luke_kcd_quality.cmake")
   set_tests_properties(xrfg_luke_kcd_quality_game PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE
       PASS_REGULAR_EXPRESSION "KCD full quality:")
  endif()
 endif()
endif()
