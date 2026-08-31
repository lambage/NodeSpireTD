#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    vec4 outlineColor;
} pc;

layout(location = 0) out vec2 fragLocalXZ;

void main() {
    // inPosition is on the unit disc (radius 1, XZ plane) -- xz doubles as the local
    // radius used for the soft edge fade in the fragment shader.
    fragLocalXZ = inPosition.xz;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
