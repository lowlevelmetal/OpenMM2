// Full-screen triangle that scales the 3D scene target onto the window.

OUT(0) vec2 vUv;

void main() {
    vec2 p = vec2(float((VERTEX_ID << 1) & 2), float(VERTEX_ID & 2));
    vUv = p;
#ifdef MM2_OPENGL
    // OpenGL render targets are stored bottom row first.
    vUv.y = 1.0 - vUv.y;
#endif
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
