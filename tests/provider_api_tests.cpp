#include "xrfg/provider_api.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks{};
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class Function> Function resolve(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    require(address != nullptr, name);
    return reinterpret_cast<Function>(address);
}
}
int main(int count, char** arguments) {
    try {
        require(count == 2, "autonomous layer path supplied");
        const HMODULE module = LoadLibraryA(arguments[1]);
        require(module != nullptr, "load actual autonomous layer");
        using Version = std::uint32_t(*)() noexcept;
        using Control1 = int(*)(OFXR_ControlSnapshotV1*) noexcept;
        using Control2 = int(*)(OFXR_ControlSnapshotV2*) noexcept;
        using Request1 = int(*)(const OFXR_ControlSettingsV1*) noexcept;
        using Request2 = int(*)(const OFXR_ControlSettingsV2*) noexcept;
        using Statistics1 = int(*)(OFXR_DlssMotionVectorStatisticsV1*) noexcept;
        using Statistics2 = int(*)(OFXR_DlssGuideStatisticsV2*) noexcept;
        using Publication2 = int(*)(const OFXR_DlssGuidePublicationV2*) noexcept;
        const auto version1 = resolve<Version>(module, "OFXR_GetProviderApiVersionV1");
        const auto version2 = resolve<Version>(module, "OFXR_GetProviderApiVersionV2");
        const auto control1 = resolve<Control1>(module, "OFXR_GetControlSnapshotV1");
        const auto control2 = resolve<Control2>(module, "OFXR_GetControlSnapshotV2");
        const auto request1 = resolve<Request1>(module, "OFXR_RequestControlV1");
        const auto request2 = resolve<Request2>(module, "OFXR_RequestControlV2");
        const auto statistics1 = resolve<Statistics1>(module, "OFXR_GetDlssMotionVectorStatisticsV1");
        const auto statistics2 = resolve<Statistics2>(module, "OFXR_GetDlssGuideStatisticsV2");
        const auto publish2 = resolve<Publication2>(module, "OFXR_PublishDlssGuidesV2");
        require(version1() == 1 && version2() == 2, "versioned native producer ABI");
        for (const auto name : {"OFXR_OptiScalerProviderV1", "OFXR_RegisterOptiScalerProviderV2",
            "OFXR_UnregisterOptiScalerProviderV2"})
            require(GetProcAddress(module, name) == nullptr, "retired host cooperation export absent");
        OFXR_ControlSnapshotV1 first;
        OFXR_ControlSnapshotV2 second;
        require(control1(&first) != 0 && first.api_version == 1, "V1 snapshot");
        require(control2(&second) != 0 && second.api_version == 2, "V2 snapshot");
        require(second.providers == 0, "autonomous layer requires no external provider registration");
        require(first.desired.backend == second.desired.backend && first.revision == second.revision,
            "both ABI generations observe identical runtime ownership");
        require(control1(nullptr) == 0 && control2(nullptr) == 0, "null snapshots rejected");
        first.struct_size = sizeof(first) - 1;
        second.struct_size = sizeof(second) - 1;
        require(control1(&first) == 0 && control2(&second) == 0, "undersized snapshots rejected");
        OFXR_ControlSettingsV1 invalid1;
        OFXR_ControlSettingsV2 invalid2;
        invalid1.backend = invalid2.backend = 9;
        require(request1(&invalid1) == 0 && request2(&invalid2) == 0, "invalid engine rejected");
        require(request1(nullptr) == 0 && request2(nullptr) == 0, "null requests rejected");
        invalid1.struct_size = invalid2.struct_size = 0;
        require(request1(&invalid1) == 0 && request2(&invalid2) == 0, "undersized requests rejected");
        OFXR_DlssMotionVectorStatisticsV1 motion;
        OFXR_DlssGuideStatisticsV2 guides;
        require(statistics1(&motion) != 0 && motion.api_version == 1, "native motion statistics");
        require(statistics2(&guides) != 0 && guides.api_version == 2, "native motion/depth statistics");
        require(statistics1(nullptr) == 0 && statistics2(nullptr) == 0, "null statistics rejected");
        motion.struct_size = guides.struct_size = 0;
        require(statistics1(&motion) == 0 && statistics2(&guides) == 0, "undersized statistics rejected");
        OFXR_DlssGuidePublicationV2 empty;
        require(publish2(nullptr) == 0 && publish2(&empty) == 0, "missing native GPU resources rejected");
        empty.api_version = 99;
        require(publish2(&empty) == 0, "unknown guide ABI rejected");
        empty.api_version = 2;
        empty.struct_size = sizeof(empty) - 1;
        require(publish2(&empty) == 0, "undersized guide publication rejected");
        FreeLibrary(module);
        std::cout << checks << " autonomous native producer ABI checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "native producer ABI failed: " << error.what() << '\n';
        return 1;
    }
}
