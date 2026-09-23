// Textured sprite, fragment stage.
//
// SDL_GPU resource layout for a fragment shader: textures and samplers live in space2.
//
// Textures are premultiplied (color already multiplied by alpha), and the pipeline blends with
// "source + background x (1 - source alpha)". The tint therefore has to be premultiplied too:
// its alpha fades the whole pixel, its color only multiplies the color.

Texture2D<float4> sprite_texture : register(t0, space2);
SamplerState sprite_sampler : register(s0, space2);

float4 main(float2 uv : TEXCOORD0, float4 color : TEXCOORD1) : SV_Target0 {
    const float4 tint = float4(color.rgb * color.a, color.a);
    return sprite_texture.Sample(sprite_sampler, uv) * tint;
}
