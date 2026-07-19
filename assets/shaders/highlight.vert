#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

layout(set = 0, binding = 1) uniform SkinPalette {
    mat4 joints[128];
} skin;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    vec4 highlightColor;
    vec4 cameraWorldPos;
} pc;

layout(location = 0) out vec3 fragWorldNormal;
layout(location = 1) out vec3 fragWorldPos;

void main() {
    vec4 localPos = vec4(inPosition, 1.0);
    vec3 localNrm = inNormal;

    float weightSum = inWeights.x + inWeights.y + inWeights.z + inWeights.w;
    if (weightSum > 0.0) {
        vec4 w = inWeights / weightSum;
        mat4 skinMatrix =
            w.x * skin.joints[inJoints.x] +
            w.y * skin.joints[inJoints.y] +
            w.z * skin.joints[inJoints.z] +
            w.w * skin.joints[inJoints.w];
        localPos = skinMatrix * localPos;
        localNrm = mat3(skinMatrix) * localNrm;
    }

    vec4 worldPos = pc.model * localPos;
    gl_Position = pc.mvp * localPos;
    fragWorldPos = worldPos.xyz;
    fragWorldNormal = mat3(pc.model) * localNrm;
}
