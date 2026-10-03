// Shadow map, vertex stage: the mesh seen from the sun, depth only, instanced.
//
// SDL_GPU resource layout for a vertex shader: uniform buffers live in space1. The shadow pipeline
// declares the position of the mesh vertices (location 0) and the rows of the instance's world
// matrix (locations 1 to 3). Locations must be contiguous from 0: the shader compiler renumbers
// the inputs in order, so TEXCOORD0, 4, 5, 6 would become 0, 1, 2, 3 and no longer match.
//
// With SKINNED defined (shadow_skinned.vert.hlsl): the vertices first follow the joints of their pose
// (skinning.hlsli); 4 is the instance's emissive row (its palette), 5 and 6 the vertex's joints and weights.

#ifdef SKINNED
#include "skinning.hlsli"
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
};

float4 main(Input input) : SV_Position {
#ifdef SKINNED
    const float4 p = float4(mul(skin_matrix((uint)input.emissive.w, input.joints, input.weights), float4(input.position, 1.0)), 1.0);
#else
    const float4 p = float4(input.position, 1.0);
#endif
    const float3 world_position = float3(dot(input.world0, p), dot(input.world1, p), dot(input.world2, p));
    return mul(light_view_projection, float4(world_position, 1.0));
}
