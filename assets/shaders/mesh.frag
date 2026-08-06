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
    const vec3 lightDir = normalize(vec3(0.6, 1.0, 0.4));
    vec3 n    = normalize(fragNormal);
    float diff    = clamp(dot(n, lightDir), 0.0, 1.0);
    float ambient = 0.25;
    float light   = (ambient + diff * 0.75) * max(0.0, pc.previewLightBoost);
    light = clamp(light, 0.0, 1.35);

    vec4 texColor = texture(baseColorTex, fragUv);
    outColor = vec4(texColor.rgb * light, texColor.a * pc.alpha);
}
