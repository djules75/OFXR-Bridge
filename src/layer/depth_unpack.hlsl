// A bridged D3D11 game's packed depth-stencil buffer, its depth plane as one
// float per texel (V521). D3D11 shares no packed depth-stencil format with
// D3D12 - Skyrim's R24G8, Ready or Not's R32G8X24 - so the NGX capture runs
// this on the game's immediate context ahead of DLSS, into a shared R32_FLOAT
// texture, over the rectangle the guide reads.

Texture2D<float> Depth : register(t0);
RWTexture2D<float> Unpacked : register(u0);

cbuffer Region : register(b0) {
    uint4 Rect;  // x, y, width, height
};

[numthreads(8, 8, 1)]
void UnpackDepth(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Rect.z || id.y >= Rect.w) return;
    const uint2 texel = Rect.xy + id.xy;
    Unpacked[texel] = Depth.Load(int3(texel, 0));
}
