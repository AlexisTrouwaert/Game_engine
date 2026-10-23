// Point light shadow of an alpha-tested material, fragment stage (review of milestones 1 to 7, 3.1):
// what the cutoff removes casts no shadow; what stays writes its distance to the light, as in
// point_shadow.frag.hlsl.
//
// SDL_GPU resource layout for a fragment shader: textures and samplers in space2, uniform buffers
// in space3.

#include "alpha_test.hlsli"
#include "material_effects.hlsli"

Texture2D<float4> base_color_texture : register(t0, space2);
SamplerState base_color_sampler : register(s0, space2);

cbuffer Light : register(b0, space3) {
    float4 light;  // xyz: position; w: 1 / range
};

struct Input {
    float3 world_position : TEXCOORD0;
    float2 uv : TEXCOORD1;
    nointerpolation float2 alpha : TEXCOORD2;  // x: alpha factor, y: cutoff
    nointerpolation float4 dissolve : TEXCOORD3;  // xyz: the instance's place, w: the dissolve
};

float main(Input input) : SV_Depth {
    dissolve(input.world_position - input.dissolve.xyz, input.dissolve.w);  // a dissolving object's shadow goes with it
    alpha_test(input.alpha.x * base_color_texture.Sample(base_color_sampler, input.uv).a, input.alpha.y);
    return saturate(length(input.world_position - light.xyz) * light.w);
}
