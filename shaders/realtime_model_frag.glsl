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

layout(location = 0) in vec3 in_normal;

layout(location=0) out vec4 out_color;

void main()
{
    out_color = vec4(in_normal, 1.0);
}
