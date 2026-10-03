#version 450
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 outputColor;
layout(set = 0, binding = 0) uniform sampler2D image;
layout(push_constant) uniform Settings { vec2 viewport; float distanceScale; float mode; float edge; } settings;
float coverage(vec2 coordinate, float width) {
    return smoothstep(settings.edge - width, settings.edge + width, texture(image, coordinate).r);
}
void main() {
    if (settings.mode < 0.5) {
        outputColor = color * texture(image, uv);
    } else {
        vec2 dx = dFdx(uv), dy = dFdy(uv);
        vec2 dimensions = vec2(textureSize(image, 0));
        float footprint = max(length(dx * dimensions), length(dy * dimensions));
        float width = max(0.25 * footprint * settings.distanceScale, 1.0 / 255.0);
        float alpha = (coverage(uv + (dx + dy) * 0.25, width)
                     + coverage(uv + (dx - dy) * 0.25, width)
                     + coverage(uv + (-dx + dy) * 0.25, width)
                     + coverage(uv - (dx + dy) * 0.25, width)) * 0.25;
        outputColor = vec4(color.rgb, color.a * alpha);
    }
}
