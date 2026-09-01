#version 450

layout(location = 0) in vec2 fragLocalXZ;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    vec4 outlineColor;
} pc;

void main() {
    const float dist = length(fragLocalXZ);
    const float edgeFade = 1.0 - smoothstep(0.85, 1.0, dist);
    const vec4 fill = vec4(pc.color.rgb, pc.color.a * edgeFade);

    // Solid rim near the disc's edge, used to mark the selected circle -- outlineColor.a is 0
    // for plain (hover) circles, so this contributes nothing for those.
    const float rimMask = clamp(smoothstep(0.94, 0.95, dist) - smoothstep(0.99, 1.0, dist), 0.0, 1.0);
    const vec4 outline = vec4(pc.outlineColor.rgb, pc.outlineColor.a * rimMask);

    const vec3 rgb = mix(fill.rgb, outline.rgb, outline.a);
    const float alpha = max(fill.a, outline.a);
    outColor = vec4(rgb, alpha);
}
