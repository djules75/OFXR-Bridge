#pragma once

#include <openxr/openxr.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XRFG_PRESENTATION_TRANSFORM_VERSION_1 1U
#define XRFG_SET_PRESENTATION_TRANSFORM_FUNCTION_NAME \
    "xrSetPresentationTransformOFXR"

typedef struct XrfgPresentationTransformV1 {
    uint32_t version;
    uint32_t active;
    uint32_t view_count;
    uint32_t reserved;
    uint64_t frame_id;
    float clip_positions[2][16];
} XrfgPresentationTransformV1;

typedef XrResult(XRAPI_PTR* PFN_xrSetPresentationTransformOFXR)(
    XrSession session,
    const XrfgPresentationTransformV1* transform);

#ifdef __cplusplus
}
#endif
