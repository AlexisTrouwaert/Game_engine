// Shadow map, vertex stage: the mesh seen from the sun, depth only, instanced.
//
// SDL_GPU resource layout for a vertex shader: uniform buffers live in space1. The shadow pipeline
// declares the position of the mesh vertices (location 0) and the rows of the instance's world
// matrix (locations 1 to 3). Locations must be contiguous from 0: the shader compiler renumbers
// the inputs in order, so TEXCOORD0, 4, 5, 6 would become 0, 1, 2, 3 and no longer match.
//
// With SKINNED defined (shadow_skinned.vert.hlsl): the vertices first follow the joints of their pose
// (skinning.hlsli); 4 is the instance's emissive row (its palette), 5 and 6 the vertex's joints and weights.
//
// With MASKED defined (shadow_masked*.vert.hlsl, review of milestones 1 to 7, 3.1): an alpha-tested
// material (glTF MASK) casts the shadow of its cut-out shape. The vertex's uv, the instance's base
// color and its first normal row (w: the alpha cutoff) follow the inputs above, and go to the
// fragment stage (shadow_masked.frag.hlsl), which drops what the cutoff removes.

#ifdef SKINNED
#include "skinning.hlsli"
#endif

#if defined(MASKED) && defined(SKINNED)
#define UV_INPUT TEXCOORD7
#define BASE_COLOR_INPUT TEXCOORD8
#define CUTOFF_INPUT TEXCOORD9
#define EFFECTS_INPUT TEXCOORD10
#elif defined(MASKED)
#define UV_INPUT TEXCOORD4
#define BASE_COLOR_INPUT TEXCOORD5
#define CUTOFF_INPUT TEXCOORD6
#define EFFECTS_INPUT TEXCOORD7
#endif

cbuffer Frame : register(b0, space1) {
    float4x4 light_view_projection;  // world -> the sun's clip space
};

struct Input {
    float3 position : TEXCOORD0;
    float4 world0 : TEXCOORD1;
    float4 world1 : TEXCOORD2;
    float4 world2 : TEXCOORD3;
#ifdef SKINNED
    float4 emissive : TEXCOORD4;  // the instance's emissive row: w is its first palette matrix
    uint4 joints : TEXCOORD5;
    float4 weights : TEXCOORD6;
#endif
#ifdef MASKED
    float2 uv : UV_INPUT;
    float4 base_color : BASE_COLOR_INPUT;  // a: the material's alpha factor
    float4 normal0 : CUTOFF_INPUT;         // w: the alpha cutoff
    uint4 effects : EFFECTS_INPUT;         // MeshInstance::effects: z, the dissolve (milestone 9 bis, part 2)
#endif
};

#ifdef MASKED
struct Output {
    float2 uv : TEXCOORD0;
    nointerpolation float2 alpha : TEXCOORD1;  // x: alpha factor, y: cutoff
    float3 local : TEXCOORD2;                  // the point relative to the instance's place (the dissolve's pattern)
    nointerpolation float dissolve : TEXCOORD3;
    float4 position : SV_Position;
};
#endif

float4 world_position_of(Input input) {
#ifdef SKINNED
    const float4 p = float4(mul(skin_matrix((uint)input.emissive.w, input.joints, input.weights), float4(input.position, 1.0)), 1.0);
#else
    const float4 p = float4(input.position, 1.0);
#endif
    return float4(dot(input.world0, p), dot(input.world1, p), dot(input.world2, p), 1.0);
}

#ifdef MASKED
Output main(Input input) {
    Output output;
    const float4 world_position = world_position_of(input);
    output.position = mul(light_view_projection, world_position);
    output.uv = input.uv;
    output.alpha = float2(input.base_color.a, input.normal0.w);
    output.local = world_position.xyz - float3(input.world0.w, input.world1.w, input.world2.w);
    output.dissolve = f16tof32(input.effects.z);
    return output;
}
#else
float4 main(Input input) : SV_Position {
    return mul(light_view_projection, world_position_of(input));
}
#endif
