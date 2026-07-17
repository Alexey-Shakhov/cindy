#version 450
#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430, buffer_reference_align = 4) buffer SceneUniforms
{
    mat4x4 view;
    mat4x4 proj;
};

layout(push_constant, std430) uniform PushConstants
{
    mat4x4 model;
    uint objectID;
    SceneUniforms scene_uniforms;
} pc;

layout(location = 0) in vec3 in_view_normal;

layout(location=0) out vec4 outID;
layout(location=1) out vec4 outNormal;

uint wangHash(uint seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

void main()
{
    uint id = pc.objectID;
    uint hash = wangHash(id);
    float r = float((hash >> 0) & 0xFF) / 255.0;
    float g = float((hash >> 8) & 0xFF) / 255.0;
    float b = float((hash >> 16) & 0xFF) / 255.0;

    outID = vec4(r, g, b, 1.0);

    vec3 n = normalize(in_view_normal);
    outNormal = vec4(n * 0.5 + 0.5, 1.0);

    return;
}
