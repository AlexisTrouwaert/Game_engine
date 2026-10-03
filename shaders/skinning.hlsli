// Linear blend skinning (milestone 5, part 5), shared by the skinned variants of the mesh, sun
// shadow and point shadow vertex shaders.
//
// The palettes of the frame, in one storage buffer: three float4 rows per matrix (the fourth row of
// a skinning matrix is always 0, 0, 0, 1). A skinned instance says where its palette starts (in
// matrices) in the w of its emissive row; a vertex names up to four matrices of it, with weights
// that add up to 1.
//
// SDL_GPU resource layout for a vertex shader: storage buffers are t registers in space0.

StructuredBuffer<float4> palettes : register(t0, space0);

float3x4 palette_matrix(uint index) {
    return float3x4(palettes[index * 3], palettes[index * 3 + 1], palettes[index * 3 + 2]);
}

// The weighted sum of the vertex's matrices: mesh -> model space in the current pose.
float3x4 skin_matrix(uint first, uint4 joints, float4 weights) {
    return palette_matrix(first + joints.x) * weights.x + palette_matrix(first + joints.y) * weights.y +
           palette_matrix(first + joints.z) * weights.z + palette_matrix(first + joints.w) * weights.w;
}
