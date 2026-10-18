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
// The id pass (milestone 9, part 7): the selectable entity under each pixel, for the outline.
Texture2D<uint> ids : register(t1, space2);
SamplerState ids_sampler : register(s1, space2);  // unused (Load), bound with the texture

cbuffer ToneMapping : register(b0, space3) {
    float4 settings;  // x: exposure (multiplies the scene before tone mapping); y: 1 skips the tone
                      // mapping (debug views, whose values are meant to be seen as they are)
    float4 outline_color;  // linear rgb, a: strength
    float4 outline;   // x: the outlined pick id (0: none); y: thickness in pixels of the id texture;
                      // zw: the id texture's size
};

// Whether the pixel at `uv` is just outside the outlined entity: not it, and it within the thickness.
bool on_outline(float2 uv) {
    if (outline.x < 0.5) {
        return false;
    }
    const uint wanted = (uint)(outline.x + 0.5);
    const int2 at = (int2)(uv * outline.zw);
    if (ids.Load(int3(at, 0)) == wanted) {
        return false;
    }
    const int reach = (int)outline.y;
    for (int y = -reach; y <= reach; ++y) {
        for (int x = -reach; x <= reach; ++x) {
            const int2 p = clamp(at + int2(x, y), int2(0, 0), (int2)outline.zw - 1);
            if (ids.Load(int3(p, 0)) == wanted) {
                return true;
            }
        }
    }
    return false;
}

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
    float3 mapped = settings.y > 0.5 ? saturate(hdr) : pbr_neutral(max(hdr, 0.0));
    if (on_outline(uv)) {
        mapped = lerp(mapped, outline_color.rgb, outline_color.a);
    }
    return float4(linear_to_srgb(mapped), 1.0);
}
