#version 450

layout(location = 0) in vec3 fragWorldNormal;
layout(location = 1) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    vec4 highlightColor;
    vec4 cameraWorldPos;
} pc;

void main() {
    vec3 n = normalize(fragWorldNormal);
    vec3 v = normalize(pc.cameraWorldPos.xyz - fragWorldPos);

    // Toon-like rim: strongest near tangent (normal dot view near 0).
    float ndv = abs(dot(n, v));
    float rim = 1.0 - ndv;

    float rimBand = 0.0;
    rimBand += step(0.45, rim) * 0.35;
    rimBand += step(0.65, rim) * 0.35;
    rimBand += step(0.82, rim) * 0.30;

    float alpha = clamp(rimBand * pc.highlightColor.a, 0.0, 1.0);
    outColor = vec4(pc.highlightColor.rgb, alpha);
}
