struct SceneUniforms {
};
[[vk::binding(0, 0)]] ConstantBuffer<SceneUniforms> scene;

struct PushConstants {
};
[[vk::push_constant]] PushConstants pc;

struct VSOutput {
    float4 position : SV_Position;
};

struct FSOutput {
};

[shader("vertex")]
VSOutput vertex_main(uint vertexIndex: SV_VertexID) {
    float x = -1.0 + float((vertexIndex & 1) << 2);
    float y = -1.0 + float((vertexIndex & 2) << 1);

    VSOutput output;
    output.position = float4(x, y, 0.0, 1.0);
    return output;
}

[shader("fragment")]
FSOutput fragment_main(VSOutput input) {
}
