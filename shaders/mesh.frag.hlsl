// 3D mesh, fragment stage: physically based shading, the glTF metal / roughness model.
//
// SDL_GPU resource layout for a fragment shader: textures and samplers live in space2, uniform
// buffers in space3. Output: linear HDR radiance (tone mapped later, see tonemap.frag.hlsl).
//
// Light = environment (diffuse from spherical harmonics, specular from the prefiltered image)
//       + sun (directional) + point lights, each with the Cook-Torrance GGX specular term and a
//       Lambert diffuse term, as in the glTF reference renderer.

Texture2D<float4> base_color_texture : register(t0, space2);          // sRGB: read as linear
Texture2D<float4> metallic_roughness_texture : register(t1, space2);  // G roughness, B metal
Texture2D<float4> normal_texture : register(t2, space2);              // tangent space, green up
Texture2D<float4> occlusion_texture : register(t3, space2);           // R
Texture2D<float4> emissive_texture : register(t4, space2);            // sRGB: read as linear
Texture2D<float4> environment_texture : register(t5, space2);         // equirect, a mip per roughness
Texture2D<float> shadow_map : register(t6, space2);                   // depth seen from the sun
Texture2D<float> point_shadow_atlas : register(t7, space2);           // distance / range, 6 tiles per light
SamplerState base_color_sampler : register(s0, space2);
SamplerState metallic_roughness_sampler : register(s1, space2);
SamplerState normal_sampler : register(s2, space2);
SamplerState occlusion_sampler : register(s3, space2);
SamplerState emissive_sampler : register(s4, space2);
SamplerState environment_sampler : register(s5, space2);
SamplerComparisonState shadow_sampler : register(s6, space2);         // compares, filters 2 x 2
SamplerComparisonState point_shadow_sampler : register(s7, space2);   // the same

#define MAX_POINT_LIGHTS 32
#define MAX_SHADOWED_POINT_LIGHTS 8

cbuffer Frame : register(b0, space3) {
    float4 eye;                  // xyz: camera position; w: number of point lights
    float4 to_sun;               // xyz
    float4 sun;                  // rgb: color x intensity
    float4 environment;          // x: intensity, y: highest mip level of environment_texture
    float4 irradiance[9];        // spherical harmonics, rgb (see environment.cpp)
    float4 light_position[MAX_POINT_LIGHTS];  // xyz, w: range
    float4 light_color[MAX_POINT_LIGHTS];     // rgb: color x intensity; w: row of its shadow in the atlas, or -1
    float4x4 light_view_projection;           // world -> the sun's shadow map
    float4 shadow;  // x: on, y: metres per shadow texel, z: normal offset (texels), w: depth bias
    float4x4 point_shadow_faces[MAX_SHADOWED_POINT_LIGHTS * 6];  // world -> each face, row * 6 + face
    float4 point_shadow_tiles;   // x, y: size of a tile in atlas coordinates; z: 1 / tile size in texels; w: normal offset (texels)
    float4 point_shadow_params;  // x, y: size of a texel in atlas coordinates; z: texel size per metre of distance; w: depth bias (m)
    float4 debug_view;  // x: 0 lit, 1 wireframe (lit), 2 normals, 3 base color, 4 distance; y: distance shown black (m)
};

static const float PI = 3.14159265;

struct Input {
    float3 world_position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
    // The material factors of the instance (see mesh.vert.hlsl).
    nointerpolation float4 base_color : TEXCOORD4;  // linear
    nointerpolation float4 factors : TEXCOORD5;     // metallic, roughness, normal scale, occlusion strength
    nointerpolation float4 emissive : TEXCOORD6;    // rgb
    bool front : SV_IsFrontFace;
};

// What a white Lambert surface facing n reflects of the environment (IrradianceSH::evaluate).
float3 evaluate_irradiance(float3 n) {
    float3 result = irradiance[0].rgb * 0.282095;
    result += irradiance[1].rgb * (0.488603 * n.y);
    result += irradiance[2].rgb * (0.488603 * n.z);
    result += irradiance[3].rgb * (0.488603 * n.x);
    result += irradiance[4].rgb * (1.092548 * n.x * n.y);
    result += irradiance[5].rgb * (1.092548 * n.y * n.z);
    result += irradiance[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0));
    result += irradiance[7].rgb * (1.092548 * n.x * n.z);
    result += irradiance[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(result, 0.0);
}

// direction_to_equirect() of environment.cpp.
float2 equirect(float3 d) {
    return float2(atan2(d.z, d.x) / (2.0 * PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
}

// The split-sum scale and bias of F0, fitted analytically (Karis, "Physically Based Shading on
// Mobile", 2014): close to the precomputed table, without a texture.
float2 environment_brdf(float roughness, float n_dot_v) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * n_dot_v)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

// One light arriving from direction l with the given radiance: Lambert diffuse + GGX specular.
float3 shade(float3 n, float3 v, float3 l, float3 radiance, float3 diffuse_color, float3 f0, float alpha) {
    const float n_dot_l = saturate(dot(n, l));
    if (n_dot_l <= 0.0) {
        return 0.0;
    }
    const float3 h = normalize(v + l);
    const float n_dot_v = max(dot(n, v), 1e-4);
    const float n_dot_h = saturate(dot(n, h));
    const float v_dot_h = saturate(dot(v, h));
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - v_dot_h, 5.0);
    const float a2 = alpha * alpha;
    const float denominator = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    const float distribution = a2 / (PI * denominator * denominator);
    // Height-correlated Smith visibility, already divided by 4 * NoL * NoV.
    const float visibility = 0.5 / (n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - a2) + a2) +
                                    n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - a2) + a2));
    const float3 specular = fresnel * distribution * visibility;
    const float3 diffuse = (1.0 - fresnel) * diffuse_color / PI;
    return (diffuse + specular) * radiance * n_dot_l;
}

// How much of the sun reaches this point: 1 lit, 0 in shadow, in between on soft edges. The lookup
// is pushed off the surface along its normal (more at grazing angles), which keeps a surface from
// shadowing itself; 3 x 3 filtered comparisons soften the edge (percentage-closer filtering).
float sun_visibility(float3 world_position, float3 geometric_normal, float n_dot_l) {
    if (shadow.x < 0.5) {
        return 1.0;
    }
    const float offset = shadow.y * shadow.z * (1.0 - 0.5 * n_dot_l);
    const float4 clip = mul(light_view_projection, float4(world_position + geometric_normal * offset, 1.0));
    const float3 ndc = clip.xyz / clip.w;
    const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0) {
        return 1.0;  // outside the map: nothing there can shadow it
    }
    float width, height;
    shadow_map.GetDimensions(width, height);
    const float2 texel = 1.0 / float2(width, height);
    const float depth = ndc.z - shadow.w;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += shadow_map.SampleCmpLevelZero(shadow_sampler, uv + float2(x, y) * texel, depth);
        }
    }
    return lit / 9.0;
}

// How much of a point light reaches this point, from its six tiles in the atlas (see
// point_shadow.frag.hlsl). The face is chosen like point_shadow_face() in shadow.cpp: along the
// largest component of the direction from the light.
float point_visibility(int row, float3 world_position, float3 geometric_normal, float3 light_position, float range) {
    // Off the surface by a few texels, whose size grows with the distance to the light.
    const float distance = length(world_position - light_position);
    const float offset = point_shadow_tiles.w * point_shadow_params.z * distance;
    const float3 d = world_position + geometric_normal * offset - light_position;
    const float3 a = abs(d);
    int face;
    if (a.x >= a.y && a.x >= a.z) {
        face = d.x >= 0.0 ? 0 : 1;
    } else if (a.y >= a.z) {
        face = d.y >= 0.0 ? 2 : 3;
    } else {
        face = d.z >= 0.0 ? 4 : 5;
    }
    const float4 clip = mul(point_shadow_faces[row * 6 + face], float4(light_position + d, 1.0));
    float2 uv = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
    // Never read the neighbouring tile: the face has a margin of 2 texels, the filter needs 1.5.
    const float margin = 1.5 * point_shadow_tiles.z;
    uv = clamp(uv, margin, 1.0 - margin);
    const float2 atlas_uv = (float2(face, row) + uv) * point_shadow_tiles.xy;
    const float depth = (length(d) - point_shadow_params.w) / range;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += point_shadow_atlas.SampleCmpLevelZero(point_shadow_sampler, atlas_uv + float2(x, y) * point_shadow_params.xy, depth);
        }
    }
    return lit / 9.0;
}

float4 main(Input input) : SV_Target0 {
    const float4 base_color = input.base_color * base_color_texture.Sample(base_color_sampler, input.uv);
    const float4 metal_rough = metallic_roughness_texture.Sample(metallic_roughness_sampler, input.uv);
    const float metallic = saturate(input.factors.x * metal_rough.b);
    // A perfectly smooth surface would turn a point light into an infinitely small highlight.
    const float roughness = clamp(input.factors.y * metal_rough.g, 0.04, 1.0);
    const float alpha = roughness * roughness;

    // The surface frame, turned around on the back of a double-sided surface.
    float3 n = normalize(input.normal);
    const float3 geometric_normal = input.front ? n : -n;
    float3 t = input.tangent.xyz - n * dot(n, input.tangent.xyz);
    t = dot(t, t) > 1e-12 ? normalize(t) : float3(1.0, 0.0, 0.0);
    float3 b = cross(n, t) * input.tangent.w;
    if (!input.front) {
        n = -n;
        t = -t;
        b = -b;
    }
    // Only x and y are read: a two-channel map (BC5) has no z, and z follows from x and y for a
    // unit normal. Then glTF's scale, on x and y only.
    float3 mapped;
    mapped.xy = normal_texture.Sample(normal_sampler, input.uv).xy * 2.0 - 1.0;
    mapped.z = sqrt(saturate(1.0 - dot(mapped.xy, mapped.xy)));
    mapped.xy *= input.factors.z;
    n = normalize(t * mapped.x + b * mapped.y + n * mapped.z);

    // Debug views: values meant to be seen as they are (the compose pass skips the tone mapping).
    const int view = (int)debug_view.x;
    if (view == 2) {
        return float4(pow(n * 0.5 + 0.5, 2.2), 1.0);  // the shading normal; encoded back to n * 0.5 + 0.5 on screen
    }
    if (view == 3) {
        return float4(base_color.rgb, 1.0);
    }
    if (view == 4) {
        const float near = saturate(1.0 - length(eye.xyz - input.world_position) / debug_view.y);
        return float4(pow(near.xxx, 2.2), 1.0);
    }

    const float3 v = normalize(eye.xyz - input.world_position);
    const float3 diffuse_color = base_color.rgb * (1.0 - metallic);
    const float3 f0 = lerp(0.04, base_color.rgb, metallic);

    // Direct light: the sun, then the point lights.
    const float sun_light = sun_visibility(input.world_position, geometric_normal, saturate(dot(geometric_normal, to_sun.xyz)));
    float3 color = shade(n, v, to_sun.xyz, sun.rgb * sun_light, diffuse_color, f0, alpha);
    const int light_count = (int)eye.w;
    for (int i = 0; i < light_count; ++i) {
        const float3 to_light = light_position[i].xyz - input.world_position;
        const float distance2 = max(dot(to_light, to_light), 1e-4);
        const float range = light_position[i].w;
        // Inverse square, brought smoothly to zero at the range (KHR_lights_punctual).
        const float ratio = distance2 / (range * range);
        const float window = saturate(1.0 - ratio * ratio);
        float3 radiance = light_color[i].rgb * (window * window / distance2);
        const int row = (int)light_color[i].w;
        if (row >= 0 && window > 0.0) {
            radiance *= point_visibility(row, input.world_position, geometric_normal, light_position[i].xyz, range);
        }
        color += shade(n, v, to_light * rsqrt(distance2), radiance, diffuse_color, f0, alpha);
    }

    // Light from the surroundings, dimmed by the occlusion map (crevices see less of the sky).
    const float n_dot_v = max(dot(n, v), 1e-4);
    const float3 r = reflect(-v, n);
    const float3 prefiltered = environment_texture.SampleLevel(environment_sampler, equirect(r), roughness * environment.y).rgb;
    const float2 scale_bias = environment_brdf(roughness, n_dot_v);
    const float3 ambient = diffuse_color * evaluate_irradiance(n) + prefiltered * (f0 * scale_bias.x + scale_bias.y);
    const float occlusion = lerp(1.0, occlusion_texture.Sample(occlusion_sampler, input.uv).r, input.factors.w);
    color += ambient * occlusion * environment.x;

    color += input.emissive.rgb * emissive_texture.Sample(emissive_sampler, input.uv).rgb;
    return float4(color, 1.0);
}
