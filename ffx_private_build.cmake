function(ofxr_isolate_backend)
    target_compile_options(ffx_opticalflow_x64 PRIVATE
        "/FI${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ffx_private_symbols.h")
    set_target_properties(ffx_opticalflow_x64 PROPERTIES OUTPUT_NAME ofxr_ffx_opticalflow_x64)
    target_compile_options(ffx_backend_dx12_x64 PRIVATE
        "/FI${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ffx_private_symbols.h")
    set_target_properties(ffx_backend_dx12_x64 PROPERTIES OUTPUT_NAME ofxr_ffx_backend_dx12_x64)
endfunction()
cmake_language(DEFER CALL ofxr_isolate_backend)
