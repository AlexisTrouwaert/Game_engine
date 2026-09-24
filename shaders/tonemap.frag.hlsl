// Full-screen pass, fragment stage: the linear HDR image of the 3D scene -> the screen.
//
// SDL_GPU resource layout for a fragment shader: textures and samplers live in space2, uniform
// buffers in space3.
//
// Mirrors moteur/color.cpp (tonemap_pbr_neutral, linear_to_srgb), which the unit tests check.
// The swapchain is plain UNORM (the 2D keeps blending the way it always has), so the sRGB encoding
// is done here rather than by the hardware.

Texture2D<float4> scene : register(t0, space2);
SamplerState scene_sampler : register(s0, space2);  // linear: also scales a reduced render size up

cbuffer ToneMapping : register(b0, space3) {
    float4 settings;  // x: exposure (multiplies the scene before tone mapping); y: 1 skips the tone
                      // mapping (debug views, whose values are meant to be seen as they are)
};

float3 pbr_neutral(float3 color) {
    const float start_compression = 0.8 - 0.04;
    const float desaturation = 0.15;
    const float x = min(color.r, min(color.g, color.b));
    const float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    const float peak = max(color.r, max(color.g, color.b));
    if (peak < start_compression) {
        return color;
    }
    const float d = 1.0 - start_compression;
    const float new_peak = 1.0 - d * d / (peak + d - start_compression);
    color *= new_peak / peak;
    const float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return lerp(color, new_peak.xxx, g);
}

float3 linear_to_srgb(float3 color) {  // not "linear": that is an HLSL keyword
    color = saturate(color);
    // step(x, edge) is 1 where x <= edge: the linear segment near black, per component.
    return lerp(1.055 * pow(color, 1.0 / 2.4) - 0.055, color * 12.92, step(color, 0.0031308));
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    const float3 hdr = scene.Sample(scene_sampler, uv).rgb * settings.x;
    const float3 mapped = settings.y > 0.5 ? saturate(hdr) : pbr_neutral(max(hdr, 0.0));
    return float4(linear_to_srgb(mapped), 1.0);
}
