// Shadow map, vertex stage: the mesh seen from the sun, depth only, instanced.
//
// SDL_GPU resource layout for a vertex shader: uniform buffers live in space1. The shadow pipeline
// declares the position of the mesh vertices (location 0) and the rows of the instance's world
// matrix (locations 1 to 3). Locations must be contiguous from 0: the shader compiler renumbers
// the inputs in order, so TEXCOORD0, 4, 5, 6 would become 0, 1, 2, 3 and no longer match.

cbuffer Frame : register(b0, space1) {
    float4x4 light_view_projection;  // world -> the sun's clip space
};

struct Input {
    float3 position : TEXCOORD0;
    float4 world0 : TEXCOORD1;
    float4 world1 : TEXCOORD2;
    float4 world2 : TEXCOORD3;
};

float4 main(Input input) : SV_Position {
    const float4 p = float4(input.position, 1.0);
    const float3 world_position = float3(dot(input.world0, p), dot(input.world1, p), dot(input.world2, p));
    return mul(light_view_projection, float4(world_position, 1.0));
}
