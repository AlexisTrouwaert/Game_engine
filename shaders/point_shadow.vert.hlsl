// Point light shadow, vertex stage: one face of the cube around the light, instanced.
//
// Same inputs as shadow.vert.hlsl: the position of the mesh vertices (location 0) and the rows of
// the instance's world matrix (locations 1 to 3). The world position goes on to the fragment
// stage, which writes the distance to the light as the depth.

cbuffer Face : register(b0, space1) {
    float4x4 face_view_projection;  // world -> the clip space of this face
};

struct Input {
    float3 position : TEXCOORD0;
    float4 world0 : TEXCOORD1;
    float4 world1 : TEXCOORD2;
    float4 world2 : TEXCOORD3;
};

struct Output {
    float3 world_position : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(Input input) {
    const float4 p = float4(input.position, 1.0);
    const float3 world_position = float3(dot(input.world0, p), dot(input.world1, p), dot(input.world2, p));
    Output output;
    output.world_position = world_position;
    output.position = mul(face_view_projection, float4(world_position, 1.0));
    return output;
}
