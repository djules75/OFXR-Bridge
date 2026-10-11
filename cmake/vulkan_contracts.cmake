# Standalone fixture compiles the production dispatcher/tracker/submit code.
# It uses no OptiScaler target, runtime, vendor model, or external import shim.
if(XRFG_BUILD_TESTS AND WIN32)
    add_executable(xrfg_vulkan_command_tests
        src/ingress/vulkan_command_tests.cpp
        src/ingress/vulkan_command_stream.cpp)
    target_include_directories(xrfg_vulkan_command_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include" "${XRFG_VULKAN_INCLUDE_DIR}")
    target_compile_features(xrfg_vulkan_command_tests PRIVATE cxx_std_20)
    target_compile_definitions(xrfg_vulkan_command_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(xrfg_vulkan_command_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME xrfg_vulkan_command_contracts COMMAND xrfg_vulkan_command_tests)
endif()
