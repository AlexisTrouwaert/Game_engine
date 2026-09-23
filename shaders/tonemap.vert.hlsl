// Full-screen pass, vertex stage: one triangle that covers the whole target, without any vertex
// buffer. Vertex 0, 1, 2 land at clip (-1, -1), (3, -1), (-1, 3): the part outside the screen is
// clipped away, which costs less than two triangles meeting along the diagonal.

struct Output {
    float2 uv : TEXCOORD0;  // (0, 0) at the top left of the image, like texture coordinates
    float4 position : SV_Position;
};

Output main(uint id : SV_VertexID) {
    const float2 corner = float2((id << 1) & 2, id & 2);  // (0, 0), (2, 0), (0, 2)
    Output output;
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    output.uv = float2(corner.x, 1.0 - corner.y);
    return output;
}
