#pragma once
// V679/PID7324 F8, publications 2165/2166. Current packet and original SDK
// clip history are captured scalars. Previous native pose is recovered from
// the captured native relative matrix and that current packet. Both positions
// use a common translated origin before float encoding, retaining the small
// recovered relative offset without rounding it against a 5km world position.
// Expected history is the independent original SDK matrix, not our transform.
#include "xrfg/luke_native_motion.hpp"
namespace xrfg::tests {
struct GtaNativeCameraFixture {
    DlssNativeCameraHistory current, previous;
    std::array<float,16> original_clip_history;
};
inline const auto gta_native_camera_fixtures=[] {
    std::array<GtaNativeCameraFixture,2> samples{};
    samples[0].current.position={0.0F,0.0F,0.0F};
    samples[0].current.right={0.936455607F,0.350697368F,0.00789966248F};
    samples[0].current.up={-0.0334257148F,0.0667929351F,0.997206807F};
    samples[0].current.forward={0.349190146F,-0.934103966F,0.0742709264F};
    samples[0].current.projection={0.680695891F,0.0F,0.0F,0.0F,0.0F,0.854080677F,0.0F,0.0F,0.0F,0.0F,5.00002488e-06F,-1.0F,0.0F,0.0F,0.0500002503F,0.0F};
    samples[0].current.inverse_projection={1.46908486F,0.0F,0.0F,0.0F,0.0F,1.17084956F,0.0F,0.0F,0.0F,0.0F,0.0F,19.9999008F,0.0F,0.0F,-1.0F,9.99999975e-05F};
    samples[0].previous.position={-0.0581054688F,-0.021484375F,-0.000495910645F};
    samples[0].previous.right={0.936356008F,0.350961477F,0.00795464311F};
    samples[0].previous.up={-0.0334976688F,0.0667689368F,0.997206151F};
    samples[0].previous.forward={0.349449843F,-0.934006453F,0.0742758885F};
    samples[0].previous.projection={0.680695891F,0.0F,0.0F,0.0F,0.0F,0.854080677F,0.0F,0.0F,0.0F,0.0F,5.00002488e-06F,-1.0F,0.0F,0.0F,0.0500002503F,0.0F};
    samples[0].previous.inverse_projection={1.46908486F,0.0F,0.0F,0.0F,0.0F,1.17084956F,0.0F,0.0F,0.0F,0.0F,0.0F,19.9999008F,0.0F,0.0F,-1.0F,9.99999975e-05F};
    samples[0].original_clip_history={1.00000012F,-9.51033071e-05F,2.03767758e-09F,-0.000407533487F,6.04160159e-05F,1.00000024F,1.6269425e-11F,-3.25386895e-06F,0.843399048F,-0.000296939485F,1.0F,-0.00550469337F,0.000193063985F,2.35933567e-06F,-4.54747351e-13F,1.00000012F};
    samples[1].current.position={0.0F,0.0F,0.0F};
    samples[1].current.right={0.936533689F,0.350488037F,0.00791714247F};
    samples[1].current.up={-0.0334357247F,0.066817306F,0.9972049F};
    samples[1].current.forward={0.348979443F,-0.934180737F,0.0742954835F};
    samples[1].current.projection={0.680695891F,0.0F,0.0F,0.0F,0.0F,0.854080677F,0.0F,0.0F,0.0F,0.0F,5.00002488e-06F,-1.0F,0.0F,0.0F,0.0500002503F,0.0F};
    samples[1].current.inverse_projection={1.46908486F,0.0F,0.0F,0.0F,0.0F,1.17084956F,0.0F,0.0F,0.0F,0.0F,0.0F,19.9999008F,0.0F,0.0F,-1.0F,9.99999975e-05F};
    samples[1].previous.position={0.0581054688F,0.021484375F,0.000480651855F};
    samples[1].previous.right={0.936455548F,0.350697339F,0.00789966155F};
    samples[1].previous.up={-0.0334257185F,0.0667929426F,0.997206867F};
    samples[1].previous.forward={0.349190146F,-0.934103966F,0.0742709264F};
    samples[1].previous.projection={0.680695891F,0.0F,0.0F,0.0F,0.0F,0.854080677F,0.0F,0.0F,0.0F,0.0F,5.00002488e-06F,-1.0F,0.0F,0.0F,0.0500002503F,0.0F};
    samples[1].previous.inverse_projection={1.46908486F,0.0F,0.0F,0.0F,0.0F,1.17084956F,0.0F,0.0F,0.0F,0.0F,0.0F,19.9999008F,0.0F,0.0F,-1.0F,9.99999975e-05F};
    samples[1].original_clip_history={1.00000024F,1.06220864e-06F,1.64609915e-09F,-0.000329218194F,-6.68031419e-07F,1.00000024F,-1.54581348e-10F,3.09161151e-05F,-0.843398511F,0.000476625981F,1.0F,0.00513828499F,0.000148307532F,-2.2549495e-05F,-1.36424205e-12F,1.00000024F};
    return samples;
}();
} // namespace xrfg::tests
