// Point light shadow, vertex stage: one face of the cube around the light, instanced.
//
// Same inputs as shadow.vert.hlsl: the position of the mesh vertices (location 0) and the rows of
// the instance's world matrix (locations 1 to 3). The world position goes on to the fragment
// stage, which writes the distance to the light as the depth.
//
// With SKINNED defined (point_shadow_skinned.vert.hlsl): the vertices first follow the joints of their pose
// (skinning.hlsli); 4 is the instance's emissive row (its palette), 5 and 6 the vertex's joints and weights.
//
// With MASKED defined (point_shadow_masked*.vert.hlsl, review of milestones 1 to 7, 3.1): an
// alpha-tested material; the inputs that follow are those of shadow.vert.hlsl's MASKED variant.

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

cbuffer Face : register(b0, space1) {
    float4x4 face_view_projection;  // world -> the clip space of this face
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

struct Output {
    float3 world_position : TEXCOORD0;
#ifdef MASKED
    float2 uv : TEXCOORD1;
    nointerpolation float2 alpha : TEXCOORD2;  // x: alpha factor, y: cutoff
    nointerpolation float4 dissolve : TEXCOORD3;  // xyz: the instance's place, w: the dissolve
#endif
    float4 position : SV_Position;
};

Output main(Input input) {
#ifdef SKINNED
    const float4 p = float4(mul(skin_matrix((uint)input.emissive.w, input.joints, input.weights), float4(input.position, 1.0)), 1.0);
#else
    const float4 p = float4(input.position, 1.0);
#endif
    const float3 world_position = float3(dot(input.world0, p), dot(input.world1, p), dot(input.world2, p));
    Output output;
    output.world_position = world_position;
    output.position = mul(face_view_projection, float4(world_position, 1.0));
#ifdef MASKED
    output.uv = input.uv;
    output.alpha = float2(input.base_color.a, input.normal0.w);
    output.dissolve = float4(input.world0.w, input.world1.w, input.world2.w, f16tof32(input.effects.z));
#endif
    return output;
}
