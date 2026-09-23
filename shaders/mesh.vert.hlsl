// 3D mesh, vertex stage.
//
// SDL_GPU resource layout for a vertex shader: uniform buffers live in space1.
// Vertex attributes are matched by TEXCOORD index = attribute location.

cbuffer Object : register(b0, space1) {
    float4x4 view_projection;  // world -> clip space, the camera of the frame
    float4x4 world;            // object -> world
    float4x4 normal_matrix;    // inverse transpose of `world` (upper 3x3 used): keeps normals
                               // perpendicular under non-uniform scaling
};

struct Input {
    float3 position : TEXCOORD0;  // metres, object space
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;   // xyz along increasing u; w: which way the bitangent goes
};

struct Output {
    float3 world_position : TEXCOORD0;
    float3 normal : TEXCOORD1;   // world space, not normalized (interpolation shortens it anyway)
    float4 tangent : TEXCOORD2;  // world space; w unchanged
    float2 uv : TEXCOORD3;
    float4 position : SV_Position;
};

Output main(Input input) {
    Output output;
    const float4 world_position = mul(world, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.world_position = world_position.xyz;
    output.normal = mul((float3x3)normal_matrix, input.normal);
    // Tangents lie in the surface: they follow the surface itself, so the plain world matrix.
    output.tangent = float4(mul((float3x3)world, input.tangent.xyz), input.tangent.w);
    output.uv = input.uv;
    return output;
}
