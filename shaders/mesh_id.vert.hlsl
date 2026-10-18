// The id pass (milestone 9, part 7), vertex stage: the meshes of the scene again, writing for each
// pixel which selectable entity it shows (for the hover, read back with a frame of delay). The same
// per-instance attributes as mesh.vert.hlsl (MeshInstance): only the position, the world rows, the
// palette (skinned) and the pick id (the w of the second normal row, a whole number as a float) are
// read; the other attributes are declared so that the vertex layout is the scene's.
//
// SDL_GPU resource layout for a vertex shader: uniform buffers live in space1.

#ifdef SKINNED
#include "skinning.hlsli"
#endif

cbuffer Frame : register(b0, space1) {
    float4x4 view_projection;  // world -> clip space, the camera of the frame
};

struct Input {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    float4 world0 : TEXCOORD4;
    float4 world1 : TEXCOORD5;
    float4 world2 : TEXCOORD6;
    float4 normal0 : TEXCOORD7;
    float4 normal1 : TEXCOORD8;   // w: the pick id (0: none, an occluder)
    float4 normal2 : TEXCOORD9;
    float4 base_color : TEXCOORD10;
    float4 factors : TEXCOORD11;
    float4 emissive : TEXCOORD12;  // w: first palette matrix of a skinned instance
#ifdef SKINNED
    uint4 joints : TEXCOORD13;
    float4 weights : TEXCOORD14;
#endif
};

struct Output {
    nointerpolation uint id : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(Input input) {
#ifdef SKINNED
    const float3x4 skin = skin_matrix((uint)input.emissive.w, input.joints, input.weights);
    const float3 position = mul(skin, float4(input.position, 1.0));
#else
    const float3 position = input.position;
#endif
    const float4 p = float4(position, 1.0);
    const float3 world_position = float3(dot(input.world0, p), dot(input.world1, p), dot(input.world2, p));
    Output output;
    output.position = mul(view_projection, float4(world_position, 1.0));
    output.id = (uint)(input.normal1.w + 0.5);
    return output;
}
