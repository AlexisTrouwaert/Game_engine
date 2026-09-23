// Textured sprite, vertex stage.
//
// SDL_GPU resource layout for a vertex shader: uniform buffers live in space1.
// Vertex attributes are matched by TEXCOORD index = attribute location.

cbuffer Transform : register(b0, space1) {
    float4x4 view_projection;  // pixel coordinates -> clip space, sent once per frame
};

struct Input {
    float2 position : TEXCOORD0;  // pixels, already in window coordinates
    float2 uv : TEXCOORD1;
    float4 color : TEXCOORD2;     // tint, 4 bytes normalized to [0, 1]
};

struct Output {
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float4 position : SV_Position;
};

Output main(Input input) {
    Output output;
    output.position = mul(view_projection, float4(input.position, 0.0, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
