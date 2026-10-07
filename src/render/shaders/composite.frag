TEXTURE0 uScene;

IN(0) vec2 vUv;

layout(location = 0) out vec4 oColor;

void main() {
    oColor = vec4(texture(uScene, vUv).rgb, 1.0);
}
