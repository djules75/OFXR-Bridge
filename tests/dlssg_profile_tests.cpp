#include "xrfg/dlssg_profile.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace xrfg::standalone;
    try {
        DlssgProfile profile; std::wstring error;
        if (read_dlssg_profile(L"relative.exe", profile, error) || error.empty())
            throw std::runtime_error("relative paths must fail before driver access");
        if (write_dlssg_profile(process_executable(), static_cast<DlssgPreset>(2), error) || error.empty())
            throw std::runtime_error("unsupported preset must fail before driver access");
        // Read-only real driver access: this test never saves a NVIDIA profile.
        const auto preset = process_dlssg_preset();
        if ((preset != 0 && preset != 1 && preset != 3) || process_dlssg_preset() != preset)
            throw std::runtime_error("startup receipt must be valid and immutable");
        if (process_executable().empty()) throw std::runtime_error("actual process executable unavailable");
        std::cout << "Preset validation and immutable read-only process receipt PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
