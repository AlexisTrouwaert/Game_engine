// 3D mesh, vertex stage, instanced: one draw call per batch of instances sharing a mesh and its
// textures (see MeshBatcher). Each instance brings its matrices and its material factors.
//
// SDL_GPU resource layout for a vertex shader: uniform buffers live in space1.
// Vertex attributes are matched by TEXCOORD index = attribute location: 0 to 3 per vertex,
// 4 to 12 per instance (MeshInstance, in mesh_batcher.hpp).
//
// With SKINNED defined (mesh_skinned.vert.hlsl), the vertices first follow the joints of their
// pose: 13 and 14 are the vertex's joints and weights (VertexSkin), and the palette comes from a
// storage buffer (skinning.hlsli).

#ifdef SKINNED
#include "skinning.hlsli"
#endif

cbuffer Frame : register(b0, space1) {
    float4x4 view_projection;  // world -> clip space, the camera of the frame
    float4 debug;              // x: for the weights view, the palette entry whose weights are shown (-1: none)
};

struct Input {
    float3 position : TEXCOORD0;  // metres, object space
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;   // xyz along increasing u; w: which way the bitangent goes
    // Per instance. Rows of the object -> world matrix (the fourth is always 0, 0, 0, 1).
    float4 world0 : TEXCOORD4;
    float4 world1 : TEXCOORD5;
    float4 world2 : TEXCOORD6;
    // Rows of its inverse transpose: keeps normals perpendicular under non-uniform scaling.
    float4 normal0 : TEXCOORD7;
    float4 normal1 : TEXCOORD8;
    float4 normal2 : TEXCOORD9;
    float4 base_color : TEXCOORD10;  // linear
    float4 factors : TEXCOORD11;     // metallic, roughness, normal scale, occlusion strength
    float4 emissive : TEXCOORD12;    // rgb; w: first palette matrix of a skinned instance
#ifdef SKINNED
    uint4 joints : TEXCOORD13;       // in the palette
    float4 weights : TEXCOORD14;     // add up to 1
#endif
};

struct Output {
    float3 world_position : TEXCOORD0;
    float3 normal : TEXCOORD1;   // world space, not normalized (interpolation shortens it anyway)
    float4 tangent : TEXCOORD2;  // world space; w unchanged
    float2 uv : TEXCOORD3;
    // The material, the same on the whole triangle: not interpolated.
    nointerpolation float4 base_color : TEXCOORD4;
    nointerpolation float4 factors : TEXCOORD5;
    nointerpolation float4 emissive : TEXCOORD6;
    float joint_weight : TEXCOORD7;  // weights view: the weight of debug.x; -1 for a mesh without skin
    float4 position : SV_Position;
};

Output main(Input input) {
#ifdef SKINNED
    // Into model space, as the pose places it. Joints are not scaled unevenly: their 3 x 3 part
    // carries normals and tangents as well.
    const float3x4 skin = skin_matrix((uint)input.emissive.w, input.joints, input.weights);
    const float3 position = mul(skin, float4(input.position, 1.0));
    const float3 normal = mul((float3x3)skin, input.normal);
    const float3 tangent = mul((float3x3)skin, input.tangent.xyz);
    const uint4 chosen = (uint4)(int)debug.x;
    const float joint_weight = debug.x < 0.0 ? 0.0 : dot(input.weights, (float4)(input.joints == chosen));
#else
    const float3 position = input.position;
    const float3 normal = input.normal;
    const float3 tangent = input.tangent.xyz;
    const float joint_weight = -1.0;
#endif
    Output output;
    const float4 p = float4(position, 1.0);
    const float3 world_position = float3(dot(input.world0, p), dot(input.world1, p), dot(input.world2, p));
    output.position = mul(view_projection, float4(world_position, 1.0));
    output.world_position = world_position;
    output.normal = float3(dot(input.normal0.xyz, normal), dot(input.normal1.xyz, normal), dot(input.normal2.xyz, normal));
    // Tangents lie in the surface: they follow the surface itself, so the plain world matrix.
    output.tangent = float4(dot(input.world0.xyz, tangent), dot(input.world1.xyz, tangent), dot(input.world2.xyz, tangent),
                            input.tangent.w);
    output.uv = input.uv;
    output.base_color = input.base_color;
    output.factors = input.factors;
    output.emissive = input.emissive;
    output.joint_weight = joint_weight;
    return output;
}
