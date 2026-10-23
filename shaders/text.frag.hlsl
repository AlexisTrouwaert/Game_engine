// Glyph of the distance-field atlas (milestone 9 bis, part 4), fragment stage.
//
// SDL_GPU resource layout for a fragment shader: textures and samplers live in space2, uniform
// buffers in space3.
//
// The atlas holds a multi-channel signed distance field (rgb: the median is the distance, sharp at
// corners) and the true distance (a: rounded, right far from the edge), 0.5 on the edge, the range
// spread over [0, 1]. The fill uses the median; the outline and the softness of a shadow use the
// true distance, which stays right beyond the corners.
//
// Output premultiplied, as the sprites (blending: source + background x (1 - source alpha)).

Texture2D<float4> atlas : register(t0, space2);
SamplerState atlas_sampler : register(s0, space2);

cbuffer Atlas : register(b0, space3) {
    float4 atlas_info;  // page width, page height (texels), distance range (texels), unused
};

float median(float3 v) {
    return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}

float4 main(float2 uv : TEXCOORD0, float4 color : TEXCOORD1, float4 outline_color : TEXCOORD2,
            float3 params : TEXCOORD3) : SV_Target0 {
    const float4 texel = atlas.Sample(atlas_sampler, uv);
    // How many screen pixels the distance range covers here (at least one, so that tiny text
    // still has an edge).
    const float2 unit_range = atlas_info.zz / atlas_info.xy;
    const float2 screen_texel = 1.0 / fwidth(uv);
    const float screen_range = max(0.5 * dot(unit_range, screen_texel), 1.0);

    const float dilate = params.x;
    const float outline = params.y;
    const float softness = max(params.z, 1.0);
    const float fill_distance = (median(texel.rgb) - 0.5) * screen_range + dilate;
    const float outer_distance = (texel.a - 0.5) * screen_range + dilate + outline;
    const float fill = saturate(fill_distance / softness + 0.5);
    const float outer = outline_color.a > 0.0 ? saturate(outer_distance / softness + 0.5) : 0.0;

    // The fill over the outline.
    const float fill_alpha = fill * color.a;
    const float outline_alpha = outer * outline_color.a * (1.0 - fill_alpha);
    const float3 rgb = color.rgb * fill_alpha + outline_color.rgb * outline_alpha;
    return float4(rgb, fill_alpha + outline_alpha);
}
