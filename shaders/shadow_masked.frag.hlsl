// Shadow map of an alpha-tested material, fragment stage (review of milestones 1 to 7, 3.1): what the
// cutoff removes casts no shadow; the depth is written by the hardware, as in shadow.frag.hlsl.
//
// SDL_GPU resource layout for a fragment shader: textures and samplers live in space2.

#include "alpha_test.hlsli"
#include "material_effects.hlsli"

Texture2D<float4> base_color_texture : register(t0, space2);
SamplerState base_color_sampler : register(s0, space2);

struct Input {
    float2 uv : TEXCOORD0;
    nointerpolation float2 alpha : TEXCOORD1;  // x: alpha factor, y: cutoff
    float3 local : TEXCOORD2;                  // relative to the instance's place
    nointerpolation float dissolve : TEXCOORD3;
};

void main(Input input) {
    dissolve(input.local, input.dissolve);  // a dissolving object's shadow goes with it
    alpha_test(input.alpha.x * base_color_texture.Sample(base_color_sampler, input.uv).a, input.alpha.y);
}
