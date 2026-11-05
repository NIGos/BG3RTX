// Native NR codec/guides, ported from the user-owned DCS Control
// nr_codec_shader.hpp and nr_guide_shader.hpp. Fixed entries; no variants.

[[vk::binding(0,0)]] Texture2D<float4> Source0;
[[vk::binding(1,0)]] Texture2D<float4> Source1;
[[vk::binding(2,0)]] Texture2D<float4> Source2;
[[vk::binding(3,0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output0;
[[vk::binding(4,0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output1;

[[vk::binding(5,0)]] cbuffer CodecConstants {
    uint2 Size;
    uint2 SourceSize;
    uint2 SourceBase;
    uint2 ProxySize;
    float PaperWhiteScale;
    float TransferStrength;
    float ColorStrength;
    uint HdrMode;
    uint2 RegionBase;
    uint2 RegionSize;
    float FoveationWidth;
    float FoveationHeight;
    float FoveationRoundness;
    float FoveationFeather;
    uint ShowAlignmentBorder;
    uint MaskCount;
    uint2 MaskPadding;
    float4 MaskBounds[4];
};

float FoveationShapeDistance(float2 pixel) {
    if (MaskCount != 0) {
        const float2 uv = (pixel - float2(RegionBase) + 0.5) / max(float2(RegionSize), 1.0);
        float distance = 1e10;
        [unroll] for (uint i = 0; i < 4; ++i) if (i < MaskCount) {
            const float4 b = MaskBounds[i];
            const float2 p = abs(2.0 * uv - b.xy - b.zw) / max(b.zw - b.xy, 0.0001);
            distance = min(distance, lerp(max(p.x, p.y), length(p), saturate(FoveationRoundness)));
        }
        return distance;
    }
    const float2 centered =
        (pixel - float2(RegionBase) + 0.5) /
        (0.5 * max(float2(RegionSize), 1.0)) - 1.0;
    const float2 scaled = abs(centered);
    return lerp(
        max(scaled.x, scaled.y),
        length(scaled),
        saturate(FoveationRoundness)
    );
}

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float SrgbEncodeChannel(float value) {
    value = saturate(value);
    return value <= 0.0031308
        ? 12.92 * value
        : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

float3 SrgbEncode(float3 color) {
    return float3(
        SrgbEncodeChannel(color.r),
        SrgbEncodeChannel(color.g),
        SrgbEncodeChannel(color.b)
    );
}

float SrgbDecodeChannel(float value) {
    value = saturate(value);
    return value <= 0.04045
        ? value / 12.92
        : pow((value + 0.055) / 1.055, 2.4);
}

float3 SrgbDecode(float3 color) {
    return float3(
        SrgbDecodeChannel(color.r),
        SrgbDecodeChannel(color.g),
        SrgbDecodeChannel(color.b)
    );
}

float4 LoadSource0Bilinear(float2 position, uint2 origin, uint2 dimensions) {
    const float2 base = floor(position);
    const float2 fraction = position - base;
    const int2 minimum = int2(origin);
    const int2 maximum = minimum + int2(dimensions) - 1;
    const int2 p00 = clamp(int2(base), minimum, maximum);
    const int2 p10 = clamp(int2(base) + int2(1, 0), minimum, maximum);
    const int2 p01 = clamp(int2(base) + int2(0, 1), minimum, maximum);
    const int2 p11 = clamp(int2(base) + int2(1, 1), minimum, maximum);
    float4 result = Source0.Load(int3(p00, 0));
    [branch] if (any(ProxySize != SourceSize)) {
        result = lerp(
            lerp(result, Source0.Load(int3(p10, 0)), fraction.x),
            lerp(Source0.Load(int3(p01, 0)), Source0.Load(int3(p11, 0)), fraction.x),
            fraction.y
        );
    }
    return result;
}

float4 LoadSource1Bilinear(float2 position, uint2 dimensions) {
    const float2 base = floor(position);
    const float2 fraction = position - base;
    const int2 maximum = int2(dimensions) - 1;
    const int2 p00 = clamp(int2(base), int2(0, 0), maximum);
    const int2 p10 = clamp(int2(base) + int2(1, 0), int2(0, 0), maximum);
    const int2 p01 = clamp(int2(base) + int2(0, 1), int2(0, 0), maximum);
    const int2 p11 = clamp(int2(base) + int2(1, 1), int2(0, 0), maximum);
    float4 result = Source1.Load(int3(p00, 0));
    [branch] if (any(ProxySize != RegionSize)) {
        result = lerp(
            lerp(result, Source1.Load(int3(p10, 0)), fraction.x),
            lerp(Source1.Load(int3(p01, 0)), Source1.Load(int3(p11, 0)), fraction.x),
            fraction.y
        );
    }
    return result;
}

float4 LoadSource2Bilinear(float2 position, uint2 dimensions) {
    const float2 base = floor(position);
    const float2 fraction = position - base;
    const int2 maximum = int2(dimensions) - 1;
    const int2 p00 = clamp(int2(base), int2(0, 0), maximum);
    const int2 p10 = clamp(int2(base) + int2(1, 0), int2(0, 0), maximum);
    const int2 p01 = clamp(int2(base) + int2(0, 1), int2(0, 0), maximum);
    const int2 p11 = clamp(int2(base) + int2(1, 1), int2(0, 0), maximum);
    float4 result = Source2.Load(int3(p00, 0));
    [branch] if (any(ProxySize != RegionSize)) {
        result = lerp(
            lerp(result, Source2.Load(int3(p10, 0)), fraction.x),
            lerp(Source2.Load(int3(p01, 0)), Source2.Load(int3(p11, 0)), fraction.x),
            fraction.y
        );
    }
    return result;
}

float3 UpgradeToneMap(float3 original, float3 proxy, float3 neural) {
    float original_y = Luminance(original);
    float proxy_y = Luminance(proxy);
    float neural_y = Luminance(neural);
    float ratio;
    if (original_y < proxy_y) {
        ratio = proxy_y > 0.0 ? original_y / proxy_y : 0.0;
    } else {
        float new_y = neural_y + max(0.0, original_y - proxy_y);
        ratio = neural_y > 0.0 ? new_y / neural_y : 0.0;
    }
    return lerp(original, max(neural * ratio, 0.0), TransferStrength);
}

[numthreads(16, 16, 1)]
void EncodeMain(uint3 dispatch_id : SV_DispatchThreadID) {
    if (all(dispatch_id.xy < Size)) {
        Output0[dispatch_id.xy] = Source0.Load(int3(SourceBase + dispatch_id.xy, 0));
    }
    if (all(dispatch_id.xy < ProxySize)) {
        const float2 source_position =
            float2(SourceBase) +
            (float2(dispatch_id.xy) + 0.5) * float2(SourceSize) /
            float2(ProxySize) - 0.5;
        const float4 proxy_source = LoadSource0Bilinear(
            source_position,
            SourceBase,
            SourceSize
        );
        const float3 linear_color = max(
            proxy_source.rgb / max(PaperWhiteScale, 0.0001),
            0.0
        );
        const float3 encoded = HdrMode != 0 ? SrgbEncode(linear_color) : proxy_source.rgb;
        Output1[dispatch_id.xy] = float4(encoded, proxy_source.a);
    }
}

[numthreads(16, 16, 1)]
void DecodeMain(uint3 dispatch_id : SV_DispatchThreadID) {
    if (any(dispatch_id.xy >= Size)) return;
    const float4 original_sample = Source0.Load(int3(dispatch_id.xy, 0));
    const bool inside_region = all(dispatch_id.xy >= RegionBase) &&
        all(dispatch_id.xy < RegionBase + RegionSize);
    const float distance_from_center = FoveationShapeDistance(dispatch_id.xy);
    const float distance_per_pixel = max(
        abs(FoveationShapeDistance(float2(dispatch_id.xy) + float2(1.0, 0.0)) -
            distance_from_center),
        abs(FoveationShapeDistance(float2(dispatch_id.xy) + float2(0.0, 1.0)) -
            distance_from_center)
    );
    const bool alignment_border = ShowAlignmentBorder != 0U &&
        inside_region && distance_from_center <= 1.0 &&
        distance_from_center >= 1.0 - 5.0 * distance_per_pixel;
    if (alignment_border) {
        Output0[SourceBase + dispatch_id.xy] = float4(0.0, 1.0, 0.0, 1.0);
        return;
    }
    const float normalized_feather = FoveationFeather /
        max(0.0001, min(FoveationWidth, FoveationHeight));
    const float foveation_weight = FoveationFeather <= 0.0
        ? (distance_from_center <= 1.0 ? 1.0 : 0.0)
        : 1.0 - smoothstep(
            max(0.0, 1.0 - normalized_feather),
            1.0,
            distance_from_center
        );
    if (!inside_region || foveation_weight <= 0.0) {
        Output0[SourceBase + dispatch_id.xy] = original_sample;
        return;
    }
    const float2 proxy_position =
        (float2(dispatch_id.xy - RegionBase) + 0.5) * float2(ProxySize) /
        float2(RegionSize) - 0.5;
    const float4 proxy_sample = LoadSource1Bilinear(proxy_position, ProxySize);
    const float4 neural_sample = LoadSource2Bilinear(proxy_position, ProxySize);
    if (HdrMode == 0) {
        const float4 processed = float4(lerp(original_sample.rgb, neural_sample.rgb,
            TransferStrength * ColorStrength), original_sample.a);
        Output0[SourceBase + dispatch_id.xy] = lerp(original_sample, processed, foveation_weight);
        return;
    }
    const float3 original = max(
        original_sample.rgb / max(PaperWhiteScale, 0.0001),
        0.0
    );
    const float3 proxy = SrgbDecode(proxy_sample.rgb);
    const float3 neural = SrgbDecode(neural_sample.rgb);
    const float3 upgraded = UpgradeToneMap(original, proxy, neural);
    const float3 decoded = lerp(original, upgraded, ColorStrength) * PaperWhiteScale;
    const float4 processed = float4(max(decoded, 0.0), original_sample.a);
    Output0[SourceBase + dispatch_id.xy] = lerp(original_sample, processed, foveation_weight);
}




[[vk::binding(6,0)]] [[vk::image_format("rg32f")]] RWTexture2D<float2> OutMotion;
[[vk::binding(7,0)]] [[vk::image_format("r32f")]] RWTexture2D<float> OutDepth;
[[vk::binding(8,0)]] cbuffer GuideConstants {
 uint2 GuideSize, GuideRegionBase, GuideRegionSize, GuideProcessing;
 uint2 GuideMotionFull, GuideDepthFull;
 float2 GuideMotionOrigin, GuideDepthOrigin, GuideMotionScale, GuideMotionOffset;
};
// Exact floor(a*b/divisor) without optional shader-double support. The float
// estimate is within one at texture-sized quotients; verify it with 64-bit
// products expressed as pairs of uints.
void multiplyWide(uint a,uint b,out uint lo,out uint hi) {
 uint p0=(a&65535)*(b&65535), p1=(a>>16)*(b&65535);
 uint p2=(a&65535)*(b>>16), p3=(a>>16)*(b>>16);
 uint middle=(p0>>16)+(p1&65535)+(p2&65535);
 lo=(p0&65535)|(middle<<16); hi=p3+(p1>>16)+(p2>>16)+(middle>>16);
}
bool greater(uint lo, uint hi, uint otherLo, uint otherHi) {
 return hi > otherHi || (hi == otherHi && lo > otherLo);
}
uint divideProduct(uint a, uint b, uint divisor) {
 uint lo,hi,ql,qh; multiplyWide(a,b,lo,hi);
 uint q=uint(float(a)*float(b)/float(divisor));
 multiplyWide(q,divisor,ql,qh);
 if(greater(ql,qh,lo,hi)) --q;
 else { multiplyWide(q+1,divisor,ql,qh); if(!greater(ql,qh,lo,hi)) ++q; }
 return q;
}
uint sampleAxis(uint pixel,uint base,uint region,uint outputSize,uint fullSize,uint processingSize) {
 return divideProduct(2*base*outputSize+(2*pixel+1)*region,fullSize,2*outputSize*processingSize);
}
[numthreads(8,8,1)]
void GuidesMain(uint3 id : SV_DispatchThreadID) {
 if (any(id.xy >= GuideSize)) return;
 // Use full-view pixel centers, never the rounded enclosing guide rectangle.
 uint mw,mh,dw,dh; Source0.GetDimensions(mw,mh); Source1.GetDimensions(dw,dh);
 uint2 mpFull=uint2(sampleAxis(id.x,GuideRegionBase.x,GuideRegionSize.x,GuideSize.x,GuideMotionFull.x,GuideProcessing.x),
                    sampleAxis(id.y,GuideRegionBase.y,GuideRegionSize.y,GuideSize.y,GuideMotionFull.y,GuideProcessing.y));
 uint2 dpFull=uint2(sampleAxis(id.x,GuideRegionBase.x,GuideRegionSize.x,GuideSize.x,GuideDepthFull.x,GuideProcessing.x),
                    sampleAxis(id.y,GuideRegionBase.y,GuideRegionSize.y,GuideSize.y,GuideDepthFull.y,GuideProcessing.y));
 int2 mp=clamp(int2(GuideMotionOrigin)+int2(mpFull),int2(0,0),int2(mw,mh)-1);
 int2 dp=clamp(int2(GuideDepthOrigin)+int2(dpFull),int2(0,0),int2(dw,dh)-1);
 float2 mv=Source0.Load(int3(mp,0)).xy;
 OutMotion[id.xy] = any(!isfinite(mv)) || any(abs(mv)>1e15) ? mv : mv*GuideMotionScale+GuideMotionOffset;
 OutDepth[id.xy] = Source1.Load(int3(dp,0)).x;
}
