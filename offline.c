#include <string.h>
#include <math.h>
#include <stdio.h>

#include <vulkan/vk_enum_string_helper.h>
#include "vk_mem_alloc.h"
#include "cglm/cglm.h"
#include "cglm/struct.h"

#include "utils.c"
#include "vk_helpers.c"

typedef struct Vertex {
    vec3 pos;
    vec3 normal;
} Vertex;

typedef struct SceneUniforms {
    mat4 view;
    mat4 proj;
} SceneUniforms;

typedef struct PushConstants {
    mat4 model;
    uint32_t object_id;
    VkDeviceAddress scene_uniforms;
} PushConstants;

const VkFormat COLOR_IMAGE_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;
const VkFormat NORMAL_IMAGE_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;

Image create_normal_attachment_with_view(int width, int height)
{
    Image normal_image;
    VkFormat format = NORMAL_IMAGE_FORMAT;
    normal_image.format = format;
    normal_image.image = create_vkimage(&normal_image.alloc, format,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, width, height, false);
    normal_image.view = create_image_view(normal_image.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    return normal_image;
}

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
    Image color_att;

    Vertex *vertices;
    size_t vertex_count;
    uint32_t *indices;
    size_t index_count;
    VmaAllocatedBuffer shader_data_buffer;
    VkCommandBuffer cb;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    vec3 cam_pos;

    cam_pos[0] = 0.0f;
    cam_pos[1] = 0.0f;
    cam_pos[2] = -6.0f;

    vkg_init(0, NULL, 0, NULL);

    const int image_height = 1080;
    const int image_width = 1920;

    color_att.image = create_vkimage(&color_att.alloc, COLOR_IMAGE_FORMAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, image_width, image_height, false);
    color_att.view = create_image_view(color_att.image, COLOR_IMAGE_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);

    Image depth_att = create_depth_attachment_with_view(image_width,image_height);
    Image normal_att = create_normal_attachment_with_view(image_width, image_height);

 float positions[24] = { 1.000000, -1.000000, -1.000000,
 1.000000, -1.000000, 1.000000,
 -1.000000, -1.000000, 1.000000,
 -1.000000, -1.000000, -1.000000,
 1.000000, 1.000000, -0.999999,
 0.999999, 1.000000, 1.000001,
 -1.000000, 1.000000, 1.000000,
 -1.000000, 1.000000, -1.000000 };

 float texcoords[28] = {1.000000, 0.333333,
 1.000000, 0.666667,
 0.666667, 0.666667,
 0.666667, 0.333333,
 0.666667, 0.000000,
 0.000000, 0.333333,
 0.000000, 0.000000,
 0.333333, 0.000000,
 0.333333, 1.000000,
 0.000000, 1.000000,
 0.000000, 0.666667,
 0.333333, 0.333333,
 0.333333, 0.666667,
 1.000000, 0.000000, };

float normals[18] = { 0.000000, -1.000000, 0.000000,
 0.000000, 1.000000, 0.000000,
 1.000000, 0.000000, 0.000000,
 -0.000000, 0.000000, 1.000000,
 -1.000000, -0.000000, -0.000000,
 0.000000, 0.000000, -1.000000 };

 int ind[108] = { 2,1,1, 3,2,1, 4,3,1,
 8,1,2, 7,4,2, 6,5,2,
 5,6,3, 6,7,3, 2,8,3,
 6,8,4, 7,5,4, 3,4,4,
 3,9,5, 7,10,5, 8,11,5,
 1,12,6, 4,13,6, 8,11,6,
 1,4,1, 2,1,1, 4,3,1,
 5,14,2, 8,1,2, 6,5,2,
 1,12,3, 5,6,3, 2,8,3,
 2,12,4, 6,8,4, 3,4,4,
 4,13,5, 3,9,5, 8,11,5,
 5,6,6, 1,12,6, 8,11,6 };

    index_count = 36;
    vertex_count = 36;
    indices = malloc(sizeof(uint32_t) * index_count);
    vertices = malloc(sizeof(Vertex) * vertex_count);
    for (uint32_t i = 0; i < index_count * 3; i++) {
        ind[i] -= 1;
    }
    for (uint32_t i = 0; i < index_count; i++) {
        int p = ind[i * 3];
        int t = ind[i * 3 + 1];
        int n = ind[i * 3 + 2];
        Vertex vertex = {
            .pos = {positions[p * 3], -positions[p * 3 + 1],
                    positions[p * 3 + 2]},
            .normal = {normals[n * 3], -normals[n * 3 + 1],
                       normals[n * 3 + 2]},
        };
        vertices[i] = vertex;
        indices[i] = i;
    }

    VkDeviceSize vbuf_size = sizeof(Vertex) * vertex_count;
    VkDeviceSize ibuf_size = sizeof(uint32_t) * index_count;
    VmaAllocatedBuffer vibuf = allocate_buffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT, ibuf_size + vbuf_size);

    memcpy(vibuf.alloc_info.pMappedData, vertices, vbuf_size);
    memcpy((char *)vibuf.alloc_info.pMappedData + vbuf_size, indices,
           ibuf_size);

    shader_data_buffer = allocate_buffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT, sizeof(SceneUniforms));

    cb = allocate_command_buffer();

    size_t code_size = 0;
    uint32_t* spirv;
    if (read_binary_file("shaders/bake.spirv", (char**) &spirv, &code_size)) {
        fatal("Failed to read shader SPIR-V.");
    }
    VkShaderModuleCreateInfo shader_module_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size,
        .pCode = spirv,
    };
    VkShaderModule shader_module;
    chk(vkCreateShaderModule(vkg.device, &shader_module_ci, NULL, &shader_module));

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
         .module = shader_module,
         .pName = "vertex_main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = shader_module,
         .pName = "fragment_main"}};
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
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .cullMode = VK_CULL_MODE_FRONT_BIT,
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
    VkGraphicsPipelineCreateInfo pipeline_ci = {
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
        .layout = pipeline_layout};
    chk(vkCreateGraphicsPipelines(vkg.device, VK_NULL_HANDLE, 1, &pipeline_ci, NULL, &pipeline));

    SceneUniforms uniforms;
    glm_perspective(glm_rad(45.0f), (float)image_width / (float)image_height,
        0.1f, 32.0f, uniforms.proj);
    mat4 mat_identity; glm_mat4_identity(mat_identity);
    glm_translate_to(mat_identity, cam_pos, uniforms.view);
    memcpy(shader_data_buffer.alloc_info.pMappedData,
           &uniforms, sizeof(SceneUniforms));

    PushConstants pc = {
        .object_id = 1225,
        .model = {},
        .scene_uniforms = shader_data_buffer.device_address,
    };
    vec3 model_pos = { 1.5f, 0.0f, 0.0f };
    glm_translate_to(mat_identity, model_pos, pc.model);

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
        .renderArea = {.extent = {.width = (uint32_t)(image_width),
                                  .height = (uint32_t)(image_height)}},
        .layerCount = 1,
        .colorAttachmentCount = 2,
        .pColorAttachments = color_attachment_infos,
        .pDepthAttachment = &depthAttachmentInfo};
    vkCmdBeginRendering(cb, &renderingInfo);
    VkViewport vp = {.width = (float)(image_width),
                  .height = (float)(image_height),
                  .minDepth = 0.0f,
                  .maxDepth = 1.0f};
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D scissor = {
        .extent = {.width = (uint32_t)(image_width),
                .height = (uint32_t)(image_height)}};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdSetScissor(cb, 0, 1, &scissor);
    VkDeviceSize vOffset = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vibuf.buffer, &vOffset);
    vkCmdBindIndexBuffer(cb, vibuf.buffer, vbuf_size, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(cb, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PushConstants), &pc);
    vkCmdDrawIndexed(cb, index_count, 1, 0, 0, 0);
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

    vkDestroyPipeline(vkg.device, pipeline, NULL);
    vkDestroyPipelineLayout(vkg.device, pipeline_layout, NULL);
    vkDestroyShaderModule(vkg.device, shader_module, NULL);
    vmaDestroyBuffer(vkg.vma, shader_data_buffer.buffer, shader_data_buffer.alloc);

    destroy_image(&color_att);
    destroy_image(&depth_att);
    destroy_image(&normal_att);

    vmaDestroyBuffer(vkg.vma, vibuf.buffer, vibuf.alloc);

    vkg_shutdown();

    return 0;
}
