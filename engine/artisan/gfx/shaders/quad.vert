#version 450

// One instanced quad per object: shapes are signed distance fields evaluated per pixel (sharp at
// any zoom, outlines and glows for free), sprites are the same quad with a texture.

layout(location = 0) in vec4 iPosSize; // center (world), size (world)
layout(location = 1) in vec4 iParams;  // rotation (rad), shape, corner radius, outline width
layout(location = 2) in vec4 iColor;
layout(location = 3) in vec4 iOutline;
layout(location = 4) in vec4 iExtra;   // glow, lighting mode, textured, -
layout(location = 5) in vec4 iUV;      // u0 v0 u1 v1

layout(push_constant) uniform Push {
    vec4 camera;   // center x, y, zoom, time
    vec4 viewport; // width, height (px), -, -
} pc;

layout(location = 0) out vec2 vLocal;  // world units from the center, unrotated
layout(location = 1) flat out vec4 vSizeParams; // size.xy, corner, outline
layout(location = 2) flat out vec4 vColor;
layout(location = 3) flat out vec4 vOutline;
layout(location = 4) flat out vec4 vExtra; // glow, lighting, textured, shape
layout(location = 5) out vec2 vUV;
layout(location = 6) out vec2 vWorld;

const vec2 kCorners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main() {
    vec2 c = kCorners[gl_VertexIndex];
    vec2 halfSize = iPosSize.zw * 0.5;
    float shape = iParams.y;
    // Room around the shape for its glow, its outline and one pixel of anti-aliasing.
    float margin = shape > 3.5 ? 0.0 : iExtra.x * 3.0 + 2.0 / max(pc.camera.z, 1e-4);
    vec2 local = c * (halfSize + margin);
    float s = sin(iParams.x), co = cos(iParams.x);
    vec2 world = iPosSize.xy + vec2(local.x * co - local.y * s, local.x * s + local.y * co);
    vec2 ndc = (world - pc.camera.xy) * pc.camera.z / (pc.viewport.xy * 0.5);
    gl_Position = vec4(ndc, 0.0, 1.0);

    vLocal = local;
    vWorld = world;
    vSizeParams = vec4(iPosSize.zw, iParams.z, iParams.w);
    vColor = iColor;
    vOutline = iOutline;
    vExtra = vec4(iExtra.x, iExtra.y, iExtra.z, shape);
    vec2 t = local / max(halfSize, vec2(1e-4)) * 0.5 + 0.5;
    vUV = mix(iUV.xy, iUV.zw, t);
}
