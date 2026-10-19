// V530: the hybrid depth search's tile map, built from the game's depth
// before the pair's draws read it.
//
// The depth search (frame_synthesis.hlsl, V505) reads the game's depth at up
// to sixteen points along the motion and starts a solve only where a point is
// nearer than the answer. Over most of an image nothing within its reach is
// nearer, and every pixel still pays the reads. BuildDepthTiles writes, per
// 16 x 16 depth texels, the nearest value inside them; DilateDepthTiles then
// the nearest within Radius tiles of each tile. A pixel whose start points all
// fall within that window and whose dilated value is not nearer than its
// answer skips the search: none of its points could have started a solve, so
// its result is the one the search would have given.
//
// Same root signature as frame_synthesis.hlsl: an SRV table t0-t9, a UAV
// table u0-u2 and the synthesis constants at b0, of which these read the
// guide's rectangle and depth flags (set as for the draw) and reuse three
// fields the tile passes have no other use for.

Texture2DArray<float> GameDepth : register(t7);
Texture2DArray<float> DepthTiles : register(t9);
RWTexture2DArray<float> DepthTilesOut : register(u0);

cbuffer SynthesisParameters : register(b0) {
    uint TilesWidth;      // Width: the tile grid's columns
    uint TilesHeight;     // Height: its rows
    uint ArraySize;
    uint Radius;          // PackedWidth: the dilation, in tiles
    uint PackedHeight;
    uint PackedEyeStride;
    uint FlowWidth;
    uint FlowHeight;
    uint FlowBlockSize;
    uint ViewIndex;
    uint UseGameMotion;
    uint GameMotionPadding;
    float4 GameMotionRect;
    float2 GameMotionScale;
    float2 GameMotionJitterDelta;
    float4 PreviousMappings[10];
    uint Slice;           // the eye's slice of the tile maps
    uint SynthesisFlags;
};

static const uint kTileSize = 16u;
groupshared float tile_nearest[kTileSize * kTileSize];

// frame_synthesis.hlsl's game_nearness: inverted depth as it comes,
// conventional depth turned round.
float nearness(float depth) {
    return (GameMotionPadding & 0x80000000u) != 0u ? depth : 1.0 - depth;
}

// The texels the depth search can read: game_depth_texel's clamp.
int2 lowest_texel() { return int2(GameMotionRect.xy); }
int2 highest_texel() { return int2(GameMotionRect.xy + GameMotionRect.zw - 1.0); }

[numthreads(16, 16, 1)]
void BuildDepthTiles(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID,
                     uint index : SV_GroupIndex) {
    const int2 tile = int2(group.xy) + (lowest_texel() >> 4);
    const int2 texel = tile * int(kTileSize) + int2(local.xy);
    float value = asfloat(0xff800000u);
    if (all(texel >= lowest_texel()) && all(texel <= highest_texel())) {
        value = nearness(GameDepth.Load(int4(texel, 0, 0)));
    }
    tile_nearest[index] = value;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint stride = kTileSize * kTileSize / 2u; stride > 0u; stride >>= 1u) {
        if (index < stride) {
            tile_nearest[index] = max(tile_nearest[index], tile_nearest[index + stride]);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (index == 0u && tile.x < int(TilesWidth) && tile.y < int(TilesHeight)) {
        DepthTilesOut[uint3(tile, Slice)] = tile_nearest[0];
    }
}

// Reads the first pass's map at t9 (a view of this eye's slice) and writes
// the dilated one. Tiles below the rectangle's first were never written and
// are not read: the window is clamped to the ones the first pass covered.
[numthreads(8, 8, 1)]
void DilateDepthTiles(uint3 id : SV_DispatchThreadID) {
    const int2 tile = int2(id.xy) + (lowest_texel() >> 4);
    if (tile.x >= int(TilesWidth) || tile.y >= int(TilesHeight)) return;
    const int2 first = lowest_texel() >> 4;
    const int2 last = int2(TilesWidth, TilesHeight) - 1;
    const int2 from = max(tile - int(Radius), first);
    const int2 to = min(tile + int(Radius), last);
    float value = asfloat(0xff800000u);
    [loop] for (int y = from.y; y <= to.y; ++y) {
        [loop] for (int x = from.x; x <= to.x; ++x) {
            value = max(value, DepthTiles.Load(int4(x, y, 0, 0)));
        }
    }
    DepthTilesOut[uint3(tile, Slice)] = value;
}
