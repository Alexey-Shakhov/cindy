struct SceneUniforms {
    float4x4 view;
    float4x4 proj;
};
[[vk::binding(0, 0)]] ConstantBuffer<SceneUniforms> scene;

struct PushConstants {
    float4x4 model;
    uint     objectID;
    Ptr<SceneUniforms> scene_uniforms;
};
[[vk::push_constant]] PushConstants pc;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal   : NORMAL;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 viewNormal : TEXCOORD0;
};

struct FSOutput {
    [[vk::location(0)]] float4 outID     : SV_Target0;
    [[vk::location(1)]] float4 outNormal : SV_Target1;
};

[shader("vertex")]
VSOutput vertex_main(VSInput input) {
    VSOutput output;

    float4x4 view = pc.scene_uniforms->view;
    float4x4 proj = pc.scene_uniforms->proj;

    float4 worldPos = mul(pc.model, float4(input.position, 1.0));
    float4 viewPos  = mul(view, worldPos);
    output.position = mul(proj, viewPos);

    float3x3 viewModelMat = (float3x3)mul(view, pc.model);
    output.viewNormal = normalize(mul(viewModelMat, input.normal));

    return output;
}

[shader("fragment")]
FSOutput fragment_main(VSOutput input) {
    FSOutput output;

    uint id = pc.objectID;
    output.outID = float4(
        float((id >> 0) & 0xFFu) / 255.0,
        float((id >> 8) & 0xFFu) / 255.0,
        float((id >> 16) & 0xFFu) / 255.0,
        1.0
    );

    float3 n = normalize(input.viewNormal);
    output.outNormal = float4(n * 0.5 + 0.5, 1.0);

    return output;
}
