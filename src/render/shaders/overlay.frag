DRAW_BLOCK {
    mat4 world;
    vec4 color;
    float alphaRef;
    uint flags;
    vec4 emissive; // lit draws: the material's emissive colour
} draw;

const uint kTexture0 = 1u;
const uint kAlphaTest = 4u;

TEXTURE0 uTexture0;

IN(0) vec4 vColor;
IN(1) vec2 vUv;

layout(location = 0) out vec4 oColor;

void main() {
    vec4 c = vColor * draw.color;
    if ((draw.flags & kTexture0) != 0u)
        c *= texture(uTexture0, vUv);
    if ((draw.flags & kAlphaTest) != 0u && c.a < draw.alphaRef)
        discard;
    oColor = c;
}
