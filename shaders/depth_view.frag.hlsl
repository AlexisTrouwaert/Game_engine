// Debug view of a depth texture (a shadow map), fragment stage, with tonemap.vert.hlsl. Near the
// light is bright, nothing (the far depth) is black; a square root spreads the grays (shadow maps
// crowd their depths in the middle of the range).
//
// SDL_GPU resource layout for a fragment shader: textures and samplers live in space2.

Texture2D<float> depth_texture : register(t0, space2);
SamplerState depth_sampler : register(s0, space2);

float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    const float value = sqrt(saturate(1.0 - depth_texture.Sample(depth_sampler, uv)));
    return float4(value, value, value, 1.0);
}
