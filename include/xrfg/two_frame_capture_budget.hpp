#pragma once
#include <string_view>

namespace xrfg {
// Once AFW is consuming a capture, retain its exact consumed guides, inputs,
// returned image and XR terminals. Later upstream NGX images can otherwise
// exhaust the bounded readback budget before the second AFW evaluation.
struct TwoFrameCaptureStageBudget {
    bool afw{};
    bool accept(std::string_view stage) noexcept {
        if (stage.starts_with("afw_")) afw = true;
        return !afw || !stage.starts_with("ngx_");
    }
};
}
