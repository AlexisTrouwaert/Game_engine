// Billboard, fragment stage: the texture (premultiplied alpha, sRGB read as linear) times the
// color, which is premultiplied already (see BillboardBatcher). An additive billboard has an alpha
// of 0: the blending then only adds its light.
//
// The fog of war (milestone 6, part 8) dims it as it dims the meshes (see mesh.frag.hlsl): a glow
// in a room never seen does not shine through the dark.
//
// Soft particles (milestone 7, part 9): a billboard with a fade distance gets fainter as it nears
// the surface behind it, read in the distance the meshes wrote (scene_depth, metres along the
// view, resolved with MSAA): smoke on the ground no longer stops in a hard line. Without a fade
// distance the texture is not read and the result is the same as before, bit for bit.
//
// SDL_GPU resource layout for a fragment shader: textures and samplers live in space2, uniform
// buffers in space3.

Texture2D<float4> billboard_texture : register(t0, space2);
Texture2D<float> fog_texture : register(t1, space2);
Texture2D<float> scene_depth : register(t2, space2);
SamplerState billboard_sampler : register(s0, space2);
SamplerState fog_sampler : register(s1, space2);
SamplerState scene_depth_sampler : register(s2, space2);  // nearest texel

cbuffer Frame : register(b0, space3) {
    float4 fog;       // x on, y brightness never seen, z brightness explored, w saturation out of sight
    float4 fog_rect;  // xy: world (x, z) of the texture's corner; zw: 1 / its size in metres
    float4 eye;           // xyz: camera position
    float4 view_forward;  // xyz: the direction the camera looks (unit)
    float4 target;        // xy: 1 / the size of the scene target in pixels
};

float4 main(float2 uv : TEXCOORD0, float4 color : TEXCOORD1, float3 world_position : TEXCOORD2,
            nointerpolation float soft : TEXCOORD3, float4 position : SV_Position) : SV_Target0 {
    float4 result = billboard_texture.Sample(billboard_sampler, uv) * color;
    if (soft > 0.0) {
        const float behind = scene_depth.SampleLevel(scene_depth_sampler, position.xy * target.xy, 0.0);
        const float mine = dot(world_position - eye.xyz, view_forward.xyz);
        result *= saturate((behind - mine) / soft);  // premultiplied: color and alpha together
    }
    if (fog.x > 0.5) {
        const float2 f = (world_position.xz - fog_rect.xy) * fog_rect.zw;
        float seen = 0.0;
        if (all(f >= 0.0) && all(f <= 1.0)) {
            seen = fog_texture.SampleLevel(fog_sampler, f, 0.0);
        }
        const float brightness = seen < 0.5 ? lerp(fog.y, fog.z, seen * 2.0) : lerp(fog.z, 1.0, seen * 2.0 - 1.0);
        const float saturation = seen < 0.5 ? fog.w : lerp(fog.w, 1.0, seen * 2.0 - 1.0);
        const float luma = dot(result.rgb, float3(0.2126, 0.7152, 0.0722));
        result.rgb = lerp(luma.xxx, result.rgb, saturation) * brightness;
    }
    return result;
}
