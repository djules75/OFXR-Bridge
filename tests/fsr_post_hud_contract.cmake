if(NOT DEFINED XRFG_SOURCE)
    message(FATAL_ERROR "XRFG_SOURCE is required")
endif()

file(READ "${XRFG_SOURCE}/src/d3d12/d3d12_fsr_frame_generator.cpp" fsr)
file(READ "${XRFG_SOURCE}/src/d3d12/d3d12_frame_synthesizer.cpp" synth)
file(READ "${XRFG_SOURCE}/src/d3d12/frame_synthesis.hlsl" shader)

string(FIND "${fsr}" "HUDLessColor" sdk_ui_position)
if(NOT sdk_ui_position EQUAL -1)
    message(FATAL_ERROR "FSR must not receive HUDLessColor in the post-HUD route")
endif()
string(FIND "${synth}" "ID3D12Resource* color_resource = hudless_pair_ready" scene_input_position)
string(FIND "${synth}" "fsr_post_hud_pipeline.Get()" post_pipeline_position)
string(FIND "${shader}" "base_color + final_color.rgb - scene.rgb" post_delta_position)
if(scene_input_position EQUAL -1 OR post_pipeline_position EQUAL -1 OR
   post_delta_position EQUAL -1)
    message(FATAL_ERROR "Scene-only FSR input and post-FSR HUD composition must remain coupled")
endif()
