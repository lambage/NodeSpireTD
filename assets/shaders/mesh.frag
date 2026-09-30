#version 450

layout(location = 0) in vec3 fragWorldNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec2 fragUv;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D baseColorTex;
layout(set = 0, binding = 2) uniform sampler2D normalTex;
layout(set = 0, binding = 3) uniform sampler2D ormTex;
layout(set = 0, binding = 4) uniform sampler2D emissiveTex;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    float alpha;
    float previewLightBoost;
} pc;

mat3 cotangentFrame(vec3 n, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;

    float denom = max(dot(t, t), dot(b, b));
    if (denom <= 1e-8) {
        vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        t = normalize(cross(up, n));
        b = normalize(cross(n, t));
        return mat3(t, b, n);
    }

    float invScale = inversesqrt(denom);
    return mat3(t * invScale, b * invScale, n);
}

void main() {
    vec4 texColor = texture(baseColorTex, fragUv);

    vec3 nGeom = normalize(fragWorldNormal);
    vec3 nMap = texture(normalTex, fragUv).xyz * 2.0 - 1.0;
    mat3 tbn = cotangentFrame(nGeom, fragWorldPos, fragUv);
    vec3 n = normalize(tbn * nMap);

    vec3 orm = texture(ormTex, fragUv).rgb;
    float ambientOcclusion = clamp(orm.r, 0.0, 1.0);
    float roughness = clamp(orm.g, 0.04, 1.0);
    float metallic = clamp(orm.b, 0.0, 1.0);
    vec3 emissive = texture(emissiveTex, fragUv).rgb;

    vec3 diffuseBase = mix(texColor.rgb, texColor.rgb * 0.08, metallic);
    vec3 specTint = mix(vec3(0.04), texColor.rgb, metallic);
    float specStrength = (1.0 - roughness) * (1.0 - roughness);
    vec3 shadedColor;

    if (pc.previewLightBoost > 1.01) {
        const vec3 keyLightDir = normalize(vec3(0.55, 0.85, 0.35));
        const vec3 fillLightDir = normalize(vec3(-0.65, 0.35, -0.45));
        float keyLight = max(dot(n, keyLightDir), 0.0);
        float fillLight = max(dot(n, fillLightDir), 0.0);
        float studioLight = clamp(0.72 + keyLight * 0.58 + fillLight * 0.30, 0.0, 1.45);
        float studioSpec = (pow(keyLight, mix(8.0, 64.0, 1.0 - roughness)) +
                            0.5 * pow(fillLight, mix(8.0, 64.0, 1.0 - roughness))) *
                           specStrength;
        vec3 liftedAlbedo = mix(diffuseBase, vec3(1.0), 0.10);
        shadedColor = liftedAlbedo * studioLight + specTint * studioSpec;
    } else {
        const vec3 lightDir = normalize(vec3(0.6, 1.0, 0.4));
        float diff = clamp(dot(n, lightDir), 0.0, 1.0);
        float light = clamp((0.25 + diff * 0.75) * max(0.0, pc.previewLightBoost), 0.0, 1.35);
        float spec = pow(diff, mix(4.0, 64.0, 1.0 - roughness)) * specStrength;
        shadedColor = diffuseBase * light + specTint * spec;
    }

    shadedColor = shadedColor * ambientOcclusion + emissive;

    float outAlpha = texColor.a * pc.alpha;
    outColor = vec4(shadedColor * outAlpha, outAlpha);
}
