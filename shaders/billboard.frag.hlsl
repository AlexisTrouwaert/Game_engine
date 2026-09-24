// Billboard, fragment stage: the texture (premultiplied alpha, sRGB read as linear) times the
// color, which is premultiplied already (see BillboardBatcher). An additive billboard has an alpha
// of 0: the blending then only adds its light.
//
// SDL_GPU resource layout for a fragment shader: textures and samplers live in space2.

Texture2D<float4> billboard_texture : register(t0, space2);
SamplerState billboard_sampler : register(s0, space2);

float4 main(float2 uv : TEXCOORD0, float4 color : TEXCOORD1) : SV_Target0 {
    return billboard_texture.Sample(billboard_sampler, uv) * color;
}
