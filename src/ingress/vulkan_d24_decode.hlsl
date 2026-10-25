Texture2D<uint> PackedDepth : register(t0);
RWTexture2D<float> Depth : register(u0);
cbuffer Extent : register(b0) { uint Width; uint Height; };

// Vulkan depth-plane copies carry D24 in bits 0..23. The upper eight bits
// are undefined; they are neither depth nor a transported stencil plane.
[numthreads(8, 8, 1)]
void DecodeDepth(uint3 id : SV_DispatchThreadID) {
    if (id.x < Width && id.y < Height) {
        // The first scale is exact in float. Correct the UNORM denominator
        // using the first two terms of 1/(1-2^-24); omitted terms are below
        // 2^-48. This avoids an approximate shader reciprocal of 16777215.
        precise float scaled = float(PackedDepth.Load(int3(id.xy, 0)) & 0x00ffffffu) * (1.0f / 16777216.0f);
        Depth[id.xy] = scaled + scaled * (1.0f / 16777216.0f);
    }
}
