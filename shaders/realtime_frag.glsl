#version 450

layout(set=0, binding = 0) uniform sampler2D color_map;
layout(set=0, binding = 1) uniform sampler2D normal_map;
layout(set=0, binding = 2) uniform sampler2D depth_map;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 light_dir = normalize(vec3(0.0, 0.0, 1.0));
    vec3 normal = normalize(texture(normal_map, in_uv).xyz);
    float light_int = 0.9;
    float diff_refl = 0.8;
    float brightness = light_int * diff_refl * max(dot(normal, light_dir), 0);
    brightness += 0.5;

    vec4 light_color = vec4(255 / 255.0, 255 / 255.0, 200 / 255.0, 1.0);

    out_color = texture(color_map, in_uv) * light_color * brightness;
    out_color = pow(out_color, vec4(6.0));

    return;
}
