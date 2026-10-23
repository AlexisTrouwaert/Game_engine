// Glyph of the distance-field atlas (milestone 9 bis, part 4), vertex stage.
//
// SDL_GPU resource layout for a vertex shader: uniform buffers live in space1.

cbuffer Transform : register(b0, space1) {
    float4x4 view_projection;  // pixel coordinates -> clip space, sent once per frame
};

struct Input {
    float2 position : TEXCOORD0;      // pixels
    float2 uv : TEXCOORD1;            // on the atlas page
    float4 color : TEXCOORD2;         // fill, straight alpha
    float4 outline_color : TEXCOORD3; // straight alpha; 0: no outline
    float3 params : TEXCOORD4;        // dilation, outline width, softness, in pixels
};

struct Output {
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float4 outline_color : TEXCOORD2;
    float3 params : TEXCOORD3;
    float4 position : SV_Position;
};

Output main(Input input) {
    Output output;
    output.position = mul(view_projection, float4(input.position, 0.0, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    output.outline_color = input.outline_color;
    output.params = input.params;
    return output;
}
