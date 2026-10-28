#version 450

layout(set = 0, binding = 0) uniform sampler2D uTexture;

const int kMaxLights = 32;
layout(std140, set = 1, binding = 0) uniform Frame {
    vec4 ambient;              // rgb, light count
    vec4 lights[kMaxLights * 2]; // (x, y, radius, intensity), (r, g, b, -)
} frame;

layout(push_constant) uniform Push {
    vec4 camera;
    vec4 viewport;
} pc;

layout(location = 0) in vec2 vLocal;
layout(location = 1) flat in vec4 vSizeParams;
layout(location = 2) flat in vec4 vColor;
layout(location = 3) flat in vec4 vOutline;
layout(location = 4) flat in vec4 vExtra;
layout(location = 5) in vec2 vUV;
layout(location = 6) in vec2 vWorld;

layout(location = 0) out vec4 outColor; // premultiplied alpha

float sdBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

vec3 lightAt(vec2 p) {
    vec3 sum = vec3(0.0);
    int count = int(frame.ambient.w);
    for (int i = 0; i < count; ++i) {
        vec4 a = frame.lights[i * 2];
        vec3 color = frame.lights[i * 2 + 1].rgb;
        float d = length(p - a.xy) / max(a.z, 1e-3);
        float att = clamp(1.0 - d, 0.0, 1.0);
        sum += color * (att * att) * a.w;
    }
    return sum;
}

void main() {
    int shape = int(vExtra.w + 0.5);
    vec2 size = vSizeParams.xy;

    // The editor's grid: lines every `corner` units, a stronger one every 8.
    if (shape == 4) {
        float step = vSizeParams.z;
        vec2 px = fwidth(vWorld);
        vec2 g = abs(fract(vWorld / step - 0.5) - 0.5) * step / px;
        vec2 g8 = abs(fract(vWorld / (step * 8.0) - 0.5) - 0.5) * step * 8.0 / px;
        float minor = 1.0 - min(min(g.x, g.y), 1.0);
        float major = 1.0 - min(min(g8.x, g8.y), 1.0);
        vec2 axis = abs(vWorld) / px;
        float axes = 1.0 - min(min(axis.x, axis.y), 1.0);
        vec4 c = vColor * minor;
        c = mix(c, vOutline, major);
        c = mix(c, vec4(vOutline.rgb, 1.0) * 0.9, axes);
        outColor = vec4(c.rgb * c.a, c.a);
        return;
    }

    vec2 halfSize = size * 0.5;
    float minHalf = min(halfSize.x, halfSize.y);
    float d;
    if (shape == 1) d = sdBox(vLocal, halfSize, min(vSizeParams.z, minHalf));
    else if (shape == 2) d = (length(vLocal / max(halfSize, vec2(1e-4))) - 1.0) * minHalf;
    else if (shape == 3) d = sdBox(vLocal, halfSize, minHalf);
    else d = sdBox(vLocal, halfSize, 0.0);

    float aa = max(fwidth(d), 1e-4);
    float fill = 1.0 - smoothstep(-0.5 * aa, 0.5 * aa, d);
    float outline = vSizeParams.w;
    float inner = outline > 0.0 ? 1.0 - smoothstep(-outline - 0.5 * aa, -outline + 0.5 * aa, d) : fill;

    vec4 base = vColor;
    if (vExtra.z > 0.5) base *= texture(uTexture, vUV);

    // Lighting: 1 = shaded (ambient + lights), 2 = the air (lights tint it without darkening it).
    int lighting = int(vExtra.y + 0.5);
    if (lighting == 1) base.rgb *= frame.ambient.rgb + lightAt(vWorld);
    else if (lighting == 2) base.rgb += lightAt(vWorld) * 0.12;

    vec4 body = vec4(base.rgb * base.a, base.a) * inner;
    vec4 rim = vec4(vOutline.rgb * vOutline.a, vOutline.a) * (fill - inner);
    vec4 result = body + rim;

    // Glow: emissive light that falls off outside the shape.
    float glow = vExtra.x;
    if (glow > 0.0) {
        float t = clamp(1.0 - max(d, 0.0) / (glow * 3.0), 0.0, 1.0);
        float g = t * t * t * 0.75 * vColor.a * (1.0 - fill);
        result += vec4(vColor.rgb * g, g * 0.6);
    }
    outColor = result;
}
