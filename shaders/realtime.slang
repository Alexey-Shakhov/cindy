[[vk::binding(0, 0)]] Sampler2D color_map;
[[vk::binding(1, 0)]] Sampler2D normal_map;
[[vk::binding(2, 0)]] Sampler2D depth_map;

struct VSOutput {
    float4 position: SV_Position;
    float2 uv:  TEXCOORD0;
};

struct FSOutput {
    [[vk::location(0)]] float4 color: SV_Target0;
};

[shader("vertex")]
VSOutput vertex_main(uint vertexIndex: SV_VertexID) {
    float x = -1.0 + float((vertexIndex & 1) << 2);
    float y = -1.0 + float((vertexIndex & 2) << 1);

    VSOutput output;
    output.position = float4(x, y, 0.0, 1.0);
    output.uv = float2((x + 1.0) * 0.5, (y + 1.0) * 0.5);
    return output;
}

[shader("fragment")]
FSOutput fragment_main(VSOutput input) {
    float3 light_dir = normalize(float3(0.0, 0.0, 1.0));
    float3 normal = normalize(normal_map.Sample(input.uv).xyz);
    float light_int = 1.9;
    float diff_refl = 0.9;
    float int = light_int * diff_refl * max(dot(normal, light_dir), 0);
    int += 0.0;

    float4 light_color = float4(255 / 255.0, 238 / 255.0, 191 / 255.0, 1.0);

    FSOutput output;
    output.color = color_map.Sample(input.uv) * light_color * int;

    output.color = pow(output.color, float4(6.0));
    return output;
}
