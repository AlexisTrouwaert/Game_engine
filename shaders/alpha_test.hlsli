// The alpha test of a masked material (glTF alphaMode MASK, review of milestones 1 to 7, 3.1), the
// same in the scene and in every shadow: a pixel whose alpha (the material's factor times its base
// color texture) is below the cutoff is not drawn. A cutoff of 0 means an opaque material.

void alpha_test(float alpha, float cutoff) {
    if (cutoff > 0.0 && alpha < cutoff) {
        discard;
    }
}
