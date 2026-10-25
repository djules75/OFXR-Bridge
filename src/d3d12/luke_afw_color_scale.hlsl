Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Destination : register(u0);
cbuffer Extents : register(b0) { uint2 SourceSize; uint2 DestinationSize; };

[numthreads(8,8,1)]
void ScaleColor(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= DestinationSize)) return;
    float2 ratio = float2(SourceSize) / float2(DestinationSize);
    if (any(SourceSize > DestinationSize)) {
        // Area integration preserves colour and subpixel phase, including
        // non-divisible eye extents. No discarded border or integer crop.
        float2 lo = float2(id.xy) * ratio;
        float2 hi = float2(id.xy + 1) * ratio;
        float4 sum = 0;
        for (uint y = (uint)floor(lo.y); y < (uint)ceil(hi.y); ++y)
            for (uint x = (uint)floor(lo.x); x < (uint)ceil(hi.x); ++x) {
                float2 coverage = max(0.0, min(hi, float2(x+1,y+1)) - max(lo,float2(x,y)));
                sum += Source.Load(int3(min(uint2(x,y),SourceSize-1),0)) * coverage.x * coverage.y;
            }
        Destination[id.xy] = sum / (ratio.x * ratio.y);
    } else {
        float2 p = (float2(id.xy) + .5) * ratio - .5;
        int2 a = (int2)floor(p);
        float2 t = frac(p);
        int2 limit = int2(SourceSize)-1;
        float4 c00 = Source.Load(int3(clamp(a,0,limit),0));
        float4 c10 = Source.Load(int3(clamp(a+int2(1,0),0,limit),0));
        float4 c01 = Source.Load(int3(clamp(a+int2(0,1),0,limit),0));
        float4 c11 = Source.Load(int3(clamp(a+1,0,limit),0));
        Destination[id.xy] = lerp(lerp(c00,c10,t.x),lerp(c01,c11,t.x),t.y);
    }
}
