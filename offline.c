#include <string.h>
#include <math.h>
#include <stdio.h>

#include <vulkan/vk_enum_string_helper.h>
#include "vk_mem_alloc.h"
#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include "cglm/cglm.h"
#include "cglm/struct.h"

#include "decls.h"
#include "typedefs.c"
#include "linux.c"
#include "memory.c"
#include "utils.c"
#include "vk_helpers.c"
#include "scene.c"

typedef struct SceneUniforms {
    mat4 view;
    mat4 proj;
} SceneUniforms;

typedef struct PushConstants {
    mat4 model;
    u32 object_id;
    VkDeviceAddress scene_uniforms;
} PushConstants;

const VkFormat COLOR_IMAGE_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;
const VkFormat NORMAL_IMAGE_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;

void save_texture(
    VkImage image,
    VkFormat format,
    VkImageLayout image_layout,
    VkImageAspectFlags aspect_mask,
    VkPipelineStageFlags initial_stage,
    int image_width,
    int image_height,
    const char* filename
) {
    VkCommandBuffer cb = begin_command_buffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    transition_image_layout(
            cb, image, image_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            aspect_mask, initial_stage, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT
    );

    int pixel_size = get_format_pixel_size(format);
    VmaAllocatedBuffer dest_buf = allocate_buffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT, pixel_size * image_width * image_height);

    VkBufferImageCopy copy = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource.aspectMask = aspect_mask,
        .imageSubresource.mipLevel = 0,
        .imageSubresource.baseArrayLayer = 0,
        .imageSubresource.layerCount = 1,
        .imageOffset = {0, 0, 0},
        .imageExtent = {image_width, image_height, 1},
    };
    vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest_buf.buffer, 1, &copy);

    end_one_time_command_buffer(cb);

    char* pixel_data = dest_buf.alloc_info.pMappedData;

    FILE* file = fopen(filename, "wb");
    if (!file) {
        fatal("Failed to open image for writing.");
    }

    fwrite(pixel_data, pixel_size * image_width * image_height, 1, file);

    fclose(file);
    vmaDestroyBuffer(vkg.vma, dest_buf.buffer, dest_buf.alloc);
}

int main() {
    memory_init(MBS(256), MBS(32));

    VmaAllocatedBuffer shader_data_buffer;
    VkCommandBuffer cb;
    VkPipelineLayout pipeline_layout;

    vkg_init(0, NULL, 0, NULL);

    const int image_height = 1080;
    const int image_width = 1920;

    Image color_att = create_image(COLOR_IMAGE_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT, image_width, image_height, false);
    Image depth_att = create_image(DEPTH_MAP_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, image_width, image_height, false);
    Image normal_att = create_image(NORMAL_IMAGE_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT, image_width, image_height, false);

    Scene scene = load_gltf_scene("../assets/teacup.glb", &memory.total);

    shader_data_buffer = allocate_buffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT, sizeof(SceneUniforms));

    cb = allocate_command_buffer();

    VkShaderModule vert_shader_module = create_shader_module("../shaders/bake_vert.glsl", shaderc_glsl_vertex_shader);
    VkShaderModule frag_shader_module = create_shader_module("../shaders/bake_frag.glsl", shaderc_glsl_fragment_shader);

    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT ,
        .size = sizeof(PushConstants)
    };
    VkPipelineLayoutCreateInfo pipeline_layout_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pSetLayouts = NULL,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range};
    chk(vkCreatePipelineLayout(vkg.device, &pipeline_layout_ci, NULL,
                               &pipeline_layout));
    VkPipelineShaderStageCreateInfo shader_stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vert_shader_module,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = frag_shader_module,
         .pName = "main"}};
    VkVertexInputBindingDescription vertex_binding = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    #define VERTEX_ATTR_COUNT 2
    VkVertexInputAttributeDescription vertex_attributes[VERTEX_ATTR_COUNT] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0},
        {.location = 1,
         .binding = 0,
         .format = VK_FORMAT_R32G32B32_SFLOAT,
         .offset = offsetof(Vertex, normal)},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = VERTEX_ATTR_COUNT,
        .pVertexAttributeDescriptions = vertex_attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states};
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .lineWidth = 1.0f,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .cullMode = VK_CULL_MODE_BACK_BIT,
    };
    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
    VkPipelineColorBlendAttachmentState blend_attachment_states[2] = {
        {.colorWriteMask = 0xF},
        {.colorWriteMask = 0xF},
    };
    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = blend_attachment_states};
    VkFormat color_attachment_formats[2] = {
        COLOR_IMAGE_FORMAT,
        normal_att.format
    };
    VkPipelineRenderingCreateInfo rendering_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 2,
        .pColorAttachmentFormats = color_attachment_formats,
        .depthAttachmentFormat = depth_att.format};
    Pipeline pipeline = { .info = 
        {
           .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
           .pNext = &rendering_ci,
           .stageCount = 2,
           .pStages = shader_stages,
           .pVertexInputState = &vertex_input_state,
           .pInputAssemblyState = &input_assembly_state,
           .pViewportState = &viewport_state,
           .pRasterizationState = &rasterization_state,
           .pMultisampleState = &multisample_state,
           .pDepthStencilState = &depth_stencil_state,
           .pColorBlendState = &color_blend_state,
           .pDynamicState = &dynamic_state,
           .layout = pipeline_layout
        }
    };
    pipeline_init(&pipeline);

    chk(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo cb_bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    chk(vkBeginCommandBuffer(cb, &cb_bi));

    transition_image_layout(
            cb, color_att.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    );
    transition_image_layout(
            cb, normal_att.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    );
    transition_image_layout(
            cb, depth_att.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT, VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
    );

    VkRenderingAttachmentInfo color_attachment_infos[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = color_att.view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {{0.0f, 0.0f, 0.0f, 0.0f}}}
        },
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = normal_att.view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {{0.0f, 0.0f, 0.0f, 0.0f}}}
        }
    };
    VkRenderingAttachmentInfo depthAttachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth_att.view,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.depthStencil = {1.0f, 0}}
    };

    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = {.width = (u32)(image_width),
                                  .height = (u32)(image_height)}},
        .layerCount = 1,
        .colorAttachmentCount = 2,
        .pColorAttachments = color_attachment_infos,
        .pDepthAttachment = &depthAttachmentInfo};
    vkCmdBeginRendering(cb, &renderingInfo);
    VkViewport vp = {
        .x = 0.0f,
        .y = (float)(image_height),
        .width = (float)(image_width),
        .height = -(float)(image_height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D scissor = {.extent = {.width = (u32)(image_width), .height = (u32)(image_height)}};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pl);
    vkCmdSetScissor(cb, 0, 1, &scissor);

    SceneUniforms uniforms;
    float dist = 50.0f;
    vec3 eye = {dist, dist, dist};
    vec3 center = {0.0f, 0.0f, 0.0f};
    vec3 up = {0.0f, 1.0f, 0.0f};
    glm_lookat(eye, center, up, uniforms.view);
    glm_ortho(-0.1f, 0.1f, -0.1f, 0.1f, 1.0f, 100.0f, uniforms.proj);
    memcpy(shader_data_buffer.alloc_info.pMappedData, &uniforms, sizeof(SceneUniforms));

    VkDeviceSize vert_offset = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &scene.vibuf.buffer, &vert_offset);
    for (int i=0; i < scene.node_count; i++) {
        Node* node = &scene.nodes[i];
        PushConstants pc = {
            .object_id = i,
            .model = {},
            .scene_uniforms = shader_data_buffer.device_address,
        };

        node_global_matrix(&scene, node, pc.model);

        Mesh* mesh = node->mesh;
        assert(mesh);
        for (int p=0; p < mesh->primitives_count; p++) {
            Primitive* primitive = &mesh->primitives[p];
            VkDeviceSize vbuf_size = scene.vertex_count * sizeof(Vertex);
            vkCmdBindIndexBuffer2(cb, scene.vibuf.buffer, vbuf_size + (primitive->index_offset) * sizeof(vert_index),
                    primitive->index_count * sizeof(vert_index), VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(cb, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(PushConstants), &pc);
            vkCmdDrawIndexed(cb, primitive->index_count, 1, 0, 0, 0);
        }
    }

    vkCmdEndRendering(cb);

    chk(vkEndCommandBuffer(cb));
    VkFence fence = create_fence(false);
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = NULL,
        .pWaitDstStageMask = NULL,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = NULL,
    };
    chk(vkQueueSubmit(vkg.queue, 1, &submit_info, fence));
    chk(vkWaitForFences(vkg.device, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(vkg.device, fence, NULL);

    save_texture(
            color_att.image,
            COLOR_IMAGE_FORMAT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            image_width, image_height, "offline-output/color.bin"
    );
    save_texture(
            normal_att.image,
            normal_att.format,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            image_width, image_height, "offline-output/normal.bin"
    );

    save_texture(
            depth_att.image,
            depth_att.format,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            image_width, image_height, "offline-output/depth.bin"
    );

    chk(vkDeviceWaitIdle(vkg.device));

    destroy_pipeline(&pipeline);

    vkDestroyPipelineLayout(vkg.device, pipeline_layout, NULL);
    vmaDestroyBuffer(vkg.vma, shader_data_buffer.buffer, shader_data_buffer.alloc);

    destroy_image(&color_att);
    destroy_image(&depth_att);
    destroy_image(&normal_att);

    vmaDestroyBuffer(vkg.vma, scene.vibuf.buffer, scene.vibuf.alloc);

    vkg_shutdown();
    arena_report(&memory.total); arena_report(&memory.scratch);
    memory_shutdown();

    return 0;
}
