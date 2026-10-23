// The effects of a material that change an object's look for a moment (milestone 9 bis, part 2),
// the same in the scene and in every shadow:
// - dissolve: the surface burns away in a noise pattern, from 0 (whole) to 1 (gone), a glowing band
//   at the edge; the pattern is fixed to the object (its position relative to the instance's place),
//   so it does not swim as the object moves;
// - flash: the lit color pulled towards a color (a hit), by an amount from 0 to 1.
// The noise is computed (no texture): a value noise of two octaves, about 6 cells per metre.

float effects_hash(float3 p) {
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float effects_value_noise(float3 p) {
    const float3 i = floor(p);
    const float3 f = frac(p);
    const float3 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(effects_hash(i + float3(0, 0, 0)), effects_hash(i + float3(1, 0, 0)), u.x),
                     lerp(effects_hash(i + float3(0, 1, 0)), effects_hash(i + float3(1, 1, 0)), u.x), u.y),
                lerp(lerp(effects_hash(i + float3(0, 0, 1)), effects_hash(i + float3(1, 0, 1)), u.x),
                     lerp(effects_hash(i + float3(0, 1, 1)), effects_hash(i + float3(1, 1, 1)), u.x), u.y),
                u.z);
}

// MeshInstance::effects, packed in one attribute (SDL_GPU allows 16):
// x the flash RGBA8 (rgb / 4, a the amount), y the dissolve edge RGBA8 (rgb times a x 16),
// z half floats (dissolve, emissive pulse in Hz), w half floats (uv scroll per second).
float4 effects_unorm4(uint packed) {
    return float4(packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF, packed >> 24) / 255.0;
}
float4 effects_flash(uint4 effects) {
    const float4 v = effects_unorm4(effects.x);
    return float4(v.rgb * 4.0, v.a);
}
float3 effects_dissolve_edge(uint4 effects) {
    const float4 v = effects_unorm4(effects.y);
    return v.rgb * v.a * 16.0;
}
float effects_dissolve(uint4 effects) {
    return f16tof32(effects.z);
}
float effects_emissive_pulse(uint4 effects) {
    return f16tof32(effects.z >> 16);
}
float2 effects_uv_scroll(uint4 effects) {
    return float2(f16tof32(effects.w), f16tof32(effects.w >> 16));
}

// 0 to 1, at a point relative to the object's place (metres).
float dissolve_noise(float3 local) {
    return 0.65 * effects_value_noise(local * 6.0) + 0.35 * effects_value_noise(local * 15.0 + 7.3);
}

// Drops the pixel if the dissolve has eaten it; returns how far inside the glowing edge it is (1 at
// the edge, 0 a little further in, 0 without dissolve).
float dissolve(float3 local, float amount) {
    if (amount <= 0.0) {
        return 0.0;
    }
    // Slightly beyond [0, 1] so that 1 removes everything and a small amount starts with specks.
    const float threshold = amount * 1.08;
    const float n = dissolve_noise(local);
    if (n < threshold) {
        discard;
    }
    return 1.0 - saturate((n - threshold) / 0.06);
}
