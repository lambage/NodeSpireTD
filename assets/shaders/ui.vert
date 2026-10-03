#version 450
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 tint;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;
layout(push_constant) uniform Settings { vec2 viewport; float distanceScale; float mode; float edge; } settings;
void main() {
    gl_Position = vec4(position / settings.viewport * 2.0 - 1.0, 0.0, 1.0);
    uv = texCoord;
    color = tint;
}
