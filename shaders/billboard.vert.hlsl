// Billboard (a sprite in the 3D world), vertex stage. The corners were placed and turned towards
// the camera on the CPU (BillboardBatcher): only the camera's projection is left to apply.
//
// SDL_GPU resource layout for a vertex shader: uniform buffers live in space1.

cbuffer Frame : register(b0, space1) {
    float4x4 view_projection;
};

struct Input {
    float3 position : TEXCOORD0;  // world, metres
    float2 uv : TEXCOORD1;
    float4 color : TEXCOORD2;     // premultiplied, linear
};

struct Output {
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float4 position : SV_Position;
};

Output main(Input input) {
    Output output;
    output.position = mul(view_projection, float4(input.position, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
