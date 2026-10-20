#include "xrfg/benchmark_log.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
int main() {
    using namespace xrfg;
    const auto root = std::filesystem::temp_directory_path() / (L"ofxr-benchmark-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    SetEnvironmentVariableW(L"XRFG_AUTONOMOUS_PROFILE_DIR", root.c_str());
    try {
        std::ofstream(root / L"ofxr_bridge.ini") << "[diagnostics]\nlogging_enabled=1\n";
        auto& logger = bridge_flight_logger(); logger.initialize(root);
        const auto control = autonomous::snapshot();
        auto status = control.runtime; status.process_id=GetCurrentProcessId(); status.applied_revision=control.revision;
        autonomous::report_runtime(status);
        if (!interface_measurement_requested() || autonomous::interface_telemetry_requested())
            throw std::runtime_error("hidden benchmark sampling requires logging, not an open menu");
        InterfaceTelemetry m{};
        m.output_fps=80; m.game_fps=40; m.generated_fps=40; m.display_hz=90; m.rates_valid=true;
        m.output_source=InterfaceSource::steamvr_delivery;
        m.ofxr_time_ms=7; m.cycle_length_ms=55; m.cycle_estimated_ms=44;
        m.ofxr_time_valid=m.cycle_valid=m.cycle_estimated_valid=true;
        m.stages_valid=true;m.game_time_ms=20;m.generation_time_ms=5;m.delivery_time_ms=19;
        log_benchmark_telemetry(m);
        logger.flush();
        const auto path=benchmark_log_path(logger.log_path());
        std::ifstream input(path); std::stringstream text; text << input.rdbuf();
        if(text.str().find("op=benchmark_rates result=2049 dur_us=0 a=343597383720000 b=171798691930000") == std::string::npos ||
            text.str().find("op=benchmark_cycles result=7 dur_us=0 a=30064771127000 b=44000") == std::string::npos ||
            text.str().find("op=benchmark_stages result=1 dur_us=0 a=85899345925000 b=19000") == std::string::npos)
            throw std::runtime_error("menu FPS/cycle values were not recorded with their units and source");
        if(autonomous::snapshot().revision != control.revision) throw std::runtime_error("benchmark reconfigured renderer");
        logger.shutdown();
        std::error_code ec;
        for(const auto& entry:std::filesystem::directory_iterator(root)) if(entry.is_regular_file()) std::filesystem::remove(entry.path(),ec);
        std::filesystem::remove(root,ec);
        std::cout << "Hidden diagnostic menu metrics/source/units recorded without renderer reconfiguration PASS\n";
        return 0;
    } catch(const std::exception& e) { bridge_flight_logger().shutdown(); std::cerr<<e.what()<<'\n'; return 1; }
}
