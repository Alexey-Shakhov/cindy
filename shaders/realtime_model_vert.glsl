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
    SceneUniforms scene_uniforms;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 out_normal;

void main()
{
    mat4x4 view = pc.scene_uniforms.view;
    mat4x4 proj = pc.scene_uniforms.proj;
    gl_Position = proj * view * pc.model * vec4(in_position, 1.0);

    out_normal = normalize(pc.model * vec4(in_normal, 1.0)).xyz;
}
