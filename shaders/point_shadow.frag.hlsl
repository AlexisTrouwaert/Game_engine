// Point light shadow, fragment stage: the depth written is the distance to the light divided by
// its range, not the depth of the projection. Distances along a ray from the light grow like the
// projected depth, so the depth test still keeps the nearest surface, and the shading can compare
// plain distances with a bias in metres (see mesh.frag.hlsl).
//
// SDL_GPU resource layout for a fragment shader: uniform buffers live in space3.

cbuffer Light : register(b0, space3) {
    float4 light;  // xyz: position; w: 1 / range
};

float main(float3 world_position : TEXCOORD0) : SV_Depth {
    return saturate(length(world_position - light.xyz) * light.w);
}
