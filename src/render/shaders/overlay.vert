// 2D overlay: UI, HUD, Dear ImGui. Positions are mapped by frame.proj.

FRAME_BLOCK {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPos;
    vec4 fogColor;
    vec4 fogParams;
    vec4 ambient;
    vec4 lightDir[3];
    vec4 lightColor[3];
} frame;

ATTR(0) vec2 aPosition;
ATTR(1) vec2 aUv;
ATTR(2) vec4 aColor;

OUT(0) vec4 vColor;
OUT(1) vec2 vUv;

void main() {
    gl_Position = clipFixup(frame.viewProj * vec4(aPosition, 0.0, 1.0));
    vColor = aColor;
    vUv = aUv;
}
