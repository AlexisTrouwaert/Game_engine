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
SamplerState base_color_sampler : register(s0, space2);
SamplerState metallic_roughness_sampler : register(s1, space2);
SamplerState normal_sampler : register(s2, space2);
SamplerState occlusion_sampler : register(s3, space2);
SamplerState emissive_sampler : register(s4, space2);
SamplerState environment_sampler : register(s5, space2);

cbuffer Material : register(b0, space3) {
    float4 base_color_factor;
    float4 factors;   // metallic, roughness, normal scale, occlusion strength
    float4 emissive_factor;
};

#define MAX_POINT_LIGHTS 32

cbuffer Frame : register(b1, space3) {
    float4 eye;                  // xyz: camera position; w: number of point lights
    float4 to_sun;               // xyz
    float4 sun;                  // rgb: color x intensity
    float4 environment;          // x: intensity, y: highest mip level of environment_texture
    float4 irradiance[9];        // spherical harmonics, rgb (see environment.cpp)
    float4 light_position[MAX_POINT_LIGHTS];  // xyz, w: range
    float4 light_color[MAX_POINT_LIGHTS];     // rgb: color x intensity
};

static const float PI = 3.14159265;

struct Input {
    float3 world_position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
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

float4 main(Input input) : SV_Target0 {
    const float4 base_color = base_color_factor * base_color_texture.Sample(base_color_sampler, input.uv);
    const float4 metal_rough = metallic_roughness_texture.Sample(metallic_roughness_sampler, input.uv);
    const float metallic = saturate(factors.x * metal_rough.b);
    // A perfectly smooth surface would turn a point light into an infinitely small highlight.
    const float roughness = clamp(factors.y * metal_rough.g, 0.04, 1.0);
    const float alpha = roughness * roughness;

    // The surface frame, turned around on the back of a double-sided surface.
    float3 n = normalize(input.normal);
    float3 t = input.tangent.xyz - n * dot(n, input.tangent.xyz);
    t = dot(t, t) > 1e-12 ? normalize(t) : float3(1.0, 0.0, 0.0);
    float3 b = cross(n, t) * input.tangent.w;
    if (!input.front) {
        n = -n;
        t = -t;
        b = -b;
    }
    float3 mapped = normal_texture.Sample(normal_sampler, input.uv).xyz * 2.0 - 1.0;
    mapped.xy *= factors.z;
    n = normalize(t * mapped.x + b * mapped.y + n * mapped.z);

    const float3 v = normalize(eye.xyz - input.world_position);
    const float3 diffuse_color = base_color.rgb * (1.0 - metallic);
    const float3 f0 = lerp(0.04, base_color.rgb, metallic);

    // Direct light: the sun, then the point lights.
    float3 color = shade(n, v, to_sun.xyz, sun.rgb, diffuse_color, f0, alpha);
    const int light_count = (int)eye.w;
    for (int i = 0; i < light_count; ++i) {
        const float3 to_light = light_position[i].xyz - input.world_position;
        const float distance2 = max(dot(to_light, to_light), 1e-4);
        const float range = light_position[i].w;
        // Inverse square, brought smoothly to zero at the range (KHR_lights_punctual).
        const float ratio = distance2 / (range * range);
        const float window = saturate(1.0 - ratio * ratio);
        const float3 radiance = light_color[i].rgb * (window * window / distance2);
        color += shade(n, v, to_light * rsqrt(distance2), radiance, diffuse_color, f0, alpha);
    }

    // Light from the surroundings, dimmed by the occlusion map (crevices see less of the sky).
    const float n_dot_v = max(dot(n, v), 1e-4);
    const float3 r = reflect(-v, n);
    const float3 prefiltered = environment_texture.SampleLevel(environment_sampler, equirect(r), roughness * environment.y).rgb;
    const float2 scale_bias = environment_brdf(roughness, n_dot_v);
    const float3 ambient = diffuse_color * evaluate_irradiance(n) + prefiltered * (f0 * scale_bias.x + scale_bias.y);
    const float occlusion = lerp(1.0, occlusion_texture.Sample(occlusion_sampler, input.uv).r, factors.w);
    color += ambient * occlusion * environment.x;

    color += emissive_factor.rgb * emissive_texture.Sample(emissive_sampler, input.uv).rgb;
    return float4(color, 1.0);
}
