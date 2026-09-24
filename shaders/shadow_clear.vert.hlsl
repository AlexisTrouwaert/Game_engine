// Clears one tile of the point shadow atlas: a triangle covering the whole viewport at the far
// depth (1), drawn with the depth test off. A render pass can only clear its whole target, and the
// other tiles hold shadows kept from earlier frames. Used with the empty shadow.frag.hlsl.

float4 main(uint id : SV_VertexID) : SV_Position {
    const float2 corner = float2((id << 1) & 2, id & 2);  // (0, 0), (2, 0), (0, 2)
    return float4(corner * 2.0 - 1.0, 1.0, 1.0);
}
