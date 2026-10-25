# V570: reduced AFW continuity recovery

Parent V569 d3f8f2e1116e7280e0afe9f0b2179edda3829766. The owner reports visual
corruption, with unchanged performance, at both 2x2 and 4x4, probably during
the 0.2.1 to 0.2.13 benchmark transition. F9 was off. The complete Ghost V569
benchmark is retained separately; its counters do not identify corrupt pixels.

## Reproduction

The actual shipping AFW implementation and unchanged bundled beta6 provider
fail when a warmed Combined sequence skips one original color. Keep the same
producer epoch, geometry, original native guides, cameras and selected quality.
The discontinuity requires a temporary Alternate evaluation, then Combined
resumes. Both 2x2 and 4x4 fail at frame 12: ClearRenderTargetView expects RTV
state 0x4 for m_otherEyeLookupDesc but the resource remains shader-read 0xC0.
V568 handled producer/geometry resets, which are absent in this reproduction.
This reproduces a concrete fault compatible with the report, not an exact
pixel replay of the user's unlogged corruption.

## Change

At a warm, reduced Combined discontinuity, replace the retired provider with
the existing private byte-identical provider mechanism. Use the allocator's
existing completion fence, retain the camera origin and calibrated units, and
prime fresh temporal state. Diagnostic stage 1047 distinguishes this from
quality (1044), producer (1045) and geometry (1046) replacement.

The continuity test uses the existing publication/eye contract; it never admits
a missing temporal color. A continuous sequence does not replace its provider.
Full-rate continuity is unchanged. No motion scaling/signs, depth subtraction,
camera reconstruction, shaders, capture timing, OFXR pacing, swapchain counts,
meters, benchmark layout or provider bytes are changed. The recovery applies
to reduced Luke AFW on either runtime. V569's Ghost SteamVR motion declaration
and its unchanged VDXR declaration are retained.

## Validation and owner trial

Focused checks exercise one/two skipped colors with 1x1, 2x2 and 4x4, both
native motion spans, full/odd raster and all three AFW modes. Additional reduced
interleaved-stream cases verify retained producer identities. Pixel comparisons
and D3D12 validation must pass; each affected Combined scenario replaces once
and removes its private module after retirement. Prior epoch/geometry recovery,
live quality, terminal/post-AFW ownership, Ghost runtime scope, meter and suite
are checked separately. See V570_VALIDATION.json for final results.

Headset validation is pending. First retry the 0.2.1 to 0.2.13 transition with
AFW Combined at 2x2 and 4x4. A full benchmark is unnecessary for that verdict.
No push, live installation, registry change or automatic game launch.
