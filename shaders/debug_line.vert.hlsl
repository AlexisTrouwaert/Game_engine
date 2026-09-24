// Debug line, vertex stage: world position and color, projected by the camera of the frame.
//
// SDL_GPU resource layout for a vertex shader: uniform buffers live in space1.

cbuffer Frame : register(b0, space1) {
    float4x4 view_projection;
};

struct Output {
    float4 color : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(float3 position : TEXCOORD0, float4 color : TEXCOORD1) {
    Output output;
    output.position = mul(view_projection, float4(position, 1.0));
    output.color = color;
    return output;
}
