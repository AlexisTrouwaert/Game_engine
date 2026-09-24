// Full-screen pass, fragment stage: FXAA, the anti-aliasing filter of the tone-mapped scene
// (Renderer, AntiAliasing::Fxaa). Follows the "quality" variant of Timothy Lottes' FXAA 3.11:
//
//   1. pixels whose neighbourhood has little contrast are left alone;
//   2. otherwise, the edge through the pixel is found (horizontal or vertical, and on which side);
//   3. the edge is followed both ways until its ends: the nearer end and the edge's length say how
//      far this pixel sits along the stair step, so how much of the other side to blend in;
//   4. a sub-pixel term also softens single-pixel details (thin lines, specks).
//
// The blend is a single texture read shifted towards the other side of the edge (the linear
// sampler mixes the two). It works on screen colors (sRGB-encoded), where the contrast is the one
// the eye sees: the tone mapping runs first, into its own texture.
//
// SDL_GPU resource layout for a fragment shader: textures and samplers live in space2, uniform
// buffers in space3.

Texture2D<float4> image : register(t0, space2);
SamplerState image_sampler : register(s0, space2);  // linear, clamped to the edges

cbuffer Fxaa : register(b0, space3) {
    float4 texel;  // xy: the size of one texel of the image, in texture coordinates
};

static const float kContrastThreshold = 0.0312;  // below this contrast (absolute), nothing to do
static const float kRelativeThreshold = 0.125;   // ... or below this fraction of the brightest neighbour
static const float kSubpixelAmount = 0.75;       // 0: sharper, 1: softer
static const int kSearchSteps = 12;
// How far each search step goes, in texels: small steps first, then longer ones for long edges.
static const float kStepLengths[kSearchSteps] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0};

float luma(float3 color) {
    return dot(color, float3(0.299, 0.587, 0.114));
}

float luma_at(float2 uv) {
    return luma(image.SampleLevel(image_sampler, uv, 0.0).rgb);
}

float luma_near(float2 uv, float x, float y) {
    return luma_at(uv + float2(x, y) * texel.xy);
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    const float3 color = image.SampleLevel(image_sampler, uv, 0.0).rgb;
    const float center = luma(color);
    // v grows downwards: "north" is the row above.
    const float north = luma_near(uv, 0.0, -1.0);
    const float south = luma_near(uv, 0.0, 1.0);
    const float east = luma_near(uv, 1.0, 0.0);
    const float west = luma_near(uv, -1.0, 0.0);

    // 1. Contrast of the neighbourhood.
    const float lowest = min(center, min(min(north, south), min(east, west)));
    const float highest = max(center, max(max(north, south), max(east, west)));
    const float contrast = highest - lowest;
    if (contrast < max(kContrastThreshold, highest * kRelativeThreshold)) {
        return float4(color, 1.0);
    }

    // 2. Direction of the edge: where the brightness changes most, from row to row (a horizontal
    // edge) or from column to column (a vertical one).
    const float north_west = luma_near(uv, -1.0, -1.0);
    const float north_east = luma_near(uv, 1.0, -1.0);
    const float south_west = luma_near(uv, -1.0, 1.0);
    const float south_east = luma_near(uv, 1.0, 1.0);
    const float across_rows = abs(north_west + south_west - 2.0 * west) + 2.0 * abs(north + south - 2.0 * center) +
                              abs(north_east + south_east - 2.0 * east);
    const float across_columns = abs(north_west + north_east - 2.0 * north) + 2.0 * abs(west + east - 2.0 * center) +
                                 abs(south_west + south_east - 2.0 * south);
    const bool horizontal = across_rows >= across_columns;

    // Which side of the pixel the edge is on: the neighbour that differs most.
    const float before = horizontal ? north : west;
    const float after = horizontal ? south : east;
    const float gradient_before = abs(before - center);
    const float gradient_after = abs(after - center);
    float step = horizontal ? texel.y : texel.x;  // towards the edge, across it
    float edge_luma;                               // halfway between this pixel and the other side
    if (gradient_before >= gradient_after) {
        step = -step;
        edge_luma = 0.5 * (before + center);
    } else {
        edge_luma = 0.5 * (after + center);
    }
    const float gradient = 0.25 * max(gradient_before, gradient_after);

    // 3. Along the edge, both ways, until the brightness no longer matches the edge's.
    float2 on_edge = uv;
    if (horizontal) {
        on_edge.y += 0.5 * step;
    } else {
        on_edge.x += 0.5 * step;
    }
    const float2 along = horizontal ? float2(texel.x, 0.0) : float2(0.0, texel.y);
    float2 uv_back = on_edge;
    float2 uv_forward = on_edge;
    float end_back = 0.0;
    float end_forward = 0.0;
    bool done_back = false;
    bool done_forward = false;
    [unroll] for (int i = 0; i < kSearchSteps; ++i) {
        if (!done_back) {
            uv_back -= along * kStepLengths[i];
            end_back = luma_at(uv_back) - edge_luma;
            done_back = abs(end_back) >= gradient;
        }
        if (!done_forward) {
            uv_forward += along * kStepLengths[i];
            end_forward = luma_at(uv_forward) - edge_luma;
            done_forward = abs(end_forward) >= gradient;
        }
    }
    const float distance_back = horizontal ? uv.x - uv_back.x : uv.y - uv_back.y;
    const float distance_forward = horizontal ? uv_forward.x - uv.x : uv_forward.y - uv.y;
    const bool back_is_nearer = distance_back < distance_forward;
    const float nearest = min(distance_back, distance_forward);
    const float edge_length = distance_back + distance_forward;
    // Blend only when the nearer end goes the other way from this pixel (the end of a stair step,
    // not of the whole edge): the closer to that end, the more.
    const float end = back_is_nearer ? end_back : end_forward;
    const bool center_darker = center < edge_luma;
    float offset = ((end < 0.0) != center_darker) ? 0.5 - nearest / edge_length : 0.0;

    // 4. Sub-pixel term: how much the pixel stands out from the average of its eight neighbours.
    const float average = (2.0 * (north + south + east + west) + north_west + north_east + south_west + south_east) / 12.0;
    const float stand_out = saturate(abs(average - center) / contrast);
    const float smooth = (3.0 - 2.0 * stand_out) * stand_out * stand_out;
    offset = max(offset, smooth * smooth * kSubpixelAmount);

    float2 blended = uv;
    if (horizontal) {
        blended.y += offset * step;
    } else {
        blended.x += offset * step;
    }
    return float4(image.SampleLevel(image_sampler, blended, 0.0).rgb, 1.0);
}
