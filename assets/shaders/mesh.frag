#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUv;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D baseColorTex;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    float alpha;
    float previewLightBoost;
} pc;

void main() {
    vec4 texColor = texture(baseColorTex, fragUv);
    vec3 n = normalize(fragNormal);
    vec3 shadedColor;

    if (pc.previewLightBoost > 1.01) {
        const vec3 keyLightDir = normalize(vec3(0.55, 0.85, 0.35));
        const vec3 fillLightDir = normalize(vec3(-0.65, 0.35, -0.45));
        float keyLight = max(dot(n, keyLightDir), 0.0);
        float fillLight = max(dot(n, fillLightDir), 0.0);
        float studioLight = clamp(0.72 + keyLight * 0.58 + fillLight * 0.30, 0.0, 1.45);
        vec3 liftedAlbedo = mix(texColor.rgb, vec3(1.0), 0.10);
        shadedColor = liftedAlbedo * studioLight;
    } else {
        const vec3 lightDir = normalize(vec3(0.6, 1.0, 0.4));
        float diff = clamp(dot(n, lightDir), 0.0, 1.0);
        float light = clamp((0.25 + diff * 0.75) * max(0.0, pc.previewLightBoost), 0.0, 1.35);
        shadedColor = texColor.rgb * light;
    }

    outColor = vec4(shadedColor, texColor.a * pc.alpha);
}
