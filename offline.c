#include <string.h>
#include <math.h>
#include <stdio.h>

#include <vulkan/vk_enum_string_helper.h>
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
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

VkImage create_depth_attachment_with_view(
        VkPhysicalDevice physical_device,
        VkDevice device,
        VmaAllocator vma,
        VmaAllocation* p_allocation,
        VkImageView* p_image_view,
        VkFormat* p_format,
        int width,
        int height)
{
    #define DEPTH_FORMAT_COUNT 1
    VkFormat depth_format_list[DEPTH_FORMAT_COUNT] = {
        VK_FORMAT_D32_SFLOAT
    };
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    for (int i = 0; i < DEPTH_FORMAT_COUNT; i++) {
        VkFormatProperties2 format_properties = {
            .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
        vkGetPhysicalDeviceFormatProperties2(physical_device, depth_format_list[i],
                                             &format_properties);
        if (format_properties.formatProperties.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depth_format = depth_format_list[i];
            break;
        }
    }
    if (depth_format == VK_FORMAT_UNDEFINED) {
        fatal("Failed to find a suitable depth format.");
    }
    *p_format = depth_format;
    VkImage depth_image = create_image(vma, p_allocation, depth_format,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, width, height, false);
    *p_image_view = create_image_view(device, depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
    return depth_image;
}

VkImage create_normal_attachment_with_view(
        VkPhysicalDevice physical_device,
        VkDevice device,
        VmaAllocator vma,
        VmaAllocation* p_allocation,
        VkImageView* p_image_view,
        VkFormat* p_format,
        int width,
        int height)
{
    VkFormat format = COLOR_IMAGE_FORMAT;
    *p_format = format;
    VkImage normal_image = create_image(vma, p_allocation, format,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, width, height, false);
    *p_image_view = create_image_view(device, normal_image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    return normal_image;
}

void save_texture(
    VkDevice device,
    VkQueue queue,
    VmaAllocator vma,
    VkCommandPool command_pool,
    VkImage image,
    VkFormat format,
    VkImageLayout image_layout,
    VkImageAspectFlags aspect_mask,
    VkPipelineStageFlags initial_stage,
    int image_width,
    int image_height,
    const char* filename
) {
    VkCommandBuffer cb = allocate_command_buffer(device, command_pool);
    chk(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo cb_bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    chk(vkBeginCommandBuffer(cb, &cb_bi));

    transition_image_layout(
            cb, image, image_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            aspect_mask, initial_stage, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT
    );
    VmaAllocation save_image_allocation;
    VkImage save_image = create_image(
            vma,
            &save_image_allocation,
            format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            image_width,
            image_height,
            true
    );
    transition_image_layout(cb, save_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            aspect_mask, initial_stage, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT
    );
    VkImageCopy image_copy = create_image_copy(aspect_mask, image_width, image_height);
    vkCmdCopyImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, save_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &image_copy);
    transition_image_layout(cb, save_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            aspect_mask, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT
    );
    chk(vkEndCommandBuffer(cb));
    VkFence fence = create_fence(device);
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
    chk(vkQueueSubmit(queue, 1, &submit_info, fence));
    chk(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(device, fence, NULL);

    VkImageSubresource subresource = {
        .aspectMask = aspect_mask,
    };
    VkSubresourceLayout subresource_layout;
    vkGetImageSubresourceLayout(device, save_image, &subresource, &subresource_layout);

    VmaAllocationInfo alloc_info;
    vmaGetAllocationInfo(vma, save_image_allocation, &alloc_info);
    char* pixel_data = alloc_info.pMappedData + subresource_layout.offset;

    FILE* file = fopen(filename, "wb");
    if (!file) {
        fatal("Failed to open color image for writing.");
    }
    fprintf(file, "P6\n%d\n%d\n255\n", image_width, image_height);

    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
            for (uint32_t y = 0; y < image_height; y++) {
                for (uint32_t x = 0; x < image_width; x++) {
                    fwrite(pixel_data + x * 4 + 2, 1, 1, file);
                    fwrite(pixel_data + x * 4 + 1, 1, 1, file);
                    fwrite(pixel_data + x * 4, 1, 1, file);
                }
                pixel_data += subresource_layout.rowPitch;
            }
            break;
        default:
            fclose(file);
            fatal("Can't handle input image format in save_texture.");
    }

    fclose(file);

    vmaDestroyImage(vma, save_image, save_image_allocation);
}

int main() {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_fam;
    VmaAllocator vma;
    VkImage color_image;
    VmaAllocation color_image_allocation;
    VkImageView color_image_view;
    VkFormat color_format;
    VkImage depth_image;
    VmaAllocation depth_image_allocation;
    VkImageView depth_image_view;
    VkFormat depth_format;
    VkImage normal_image;
    VmaAllocation normal_image_allocation;
    VkImageView normal_image_view;
    VkFormat normal_format;
    Vertex *vertices;
    size_t vertex_count;
    uint32_t *indices;
    size_t index_count;
    VmaAllocation vibuf_alloc;
    VkBuffer vibuf;
    VmaAllocatedBuffer shader_data_buffer;
    VkCommandPool command_pool;
    VkCommandBuffer cb;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    vec3 cam_pos;

    cam_pos[0] = 0.0f;
    cam_pos[1] = 0.0f;
    cam_pos[2] = -6.0f;

    instance = create_instance(0, NULL);
    physical_device = choose_physical_device(instance);
    queue_fam = choose_queue_family(instance, physical_device);
    device = create_logical_device(instance, physical_device, queue_fam, 0, NULL);
    vkGetDeviceQueue(device, queue_fam, 0, &queue);
    vma = create_vma(physical_device, device, instance);
    command_pool = create_command_pool(device, queue_fam);

    const int image_height = 1080;
    const int image_width = 1920;

    color_image = create_image(vma, &color_image_allocation, COLOR_IMAGE_FORMAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, image_width, image_height, false);
    color_image_view = create_image_view(device, color_image, COLOR_IMAGE_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);

    depth_image = create_depth_attachment_with_view(
            physical_device,
            device,
            vma,
            &depth_image_allocation,
            &depth_image_view,
            &depth_format,
            image_width,
            image_height
    );
    normal_image = create_normal_attachment_with_view(
            physical_device,
            device,
            vma,
            &normal_image_allocation,
            &normal_image_view,
            &normal_format,
            image_width,
            image_height
    );

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
    VkBufferCreateInfo buffer_ci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                    .size = ibuf_size + vbuf_size,
                                    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
    VmaAllocationCreateInfo buffer_alloc_ci = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO};
    VmaAllocationInfo vibuf_alloc_info = {0};
    if (vmaCreateBuffer(vma, &buffer_ci, &buffer_alloc_ci, &vibuf,
                        &vibuf_alloc, &vibuf_alloc_info) != VK_SUCCESS) {
        fatal("Failed to create vertex / index buffer.");
    }
    memcpy(vibuf_alloc_info.pMappedData, vertices, vbuf_size);
    memcpy((char *)vibuf_alloc_info.pMappedData + vbuf_size, indices,
           ibuf_size);

    VkBufferCreateInfo ubuf_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(SceneUniforms),
        .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };
    VmaAllocationCreateInfo ubuf_alloc_ci = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    if (vmaCreateBuffer(vma, &ubuf_ci, &ubuf_alloc_ci,
                        &shader_data_buffer.buffer,
                        &shader_data_buffer.alloc,
                        &shader_data_buffer.alloc_info) != VK_SUCCESS) {
        fatal("Failed to creates shader uniform buffer.");
    }
    VkBufferDeviceAddressInfo ubuf_addr_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = shader_data_buffer.buffer
    };
    shader_data_buffer.device_address = vkGetBufferDeviceAddress(device, &ubuf_addr_info);

    cb = allocate_command_buffer(device, command_pool);

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
    chk(vkCreateShaderModule(device, &shader_module_ci, NULL, &shader_module));

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
    chk(vkCreatePipelineLayout(device, &pipeline_layout_ci, NULL,
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
        normal_format
    };
    VkPipelineRenderingCreateInfo rendering_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 2,
        .pColorAttachmentFormats = color_attachment_formats,
        .depthAttachmentFormat = depth_format};
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
    chk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, NULL, &pipeline));

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
            cb, color_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    );
    transition_image_layout(
            cb, normal_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    );
    transition_image_layout(
            cb, depth_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT, VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
    );

    VkRenderingAttachmentInfo color_attachment_infos[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = color_image_view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {{0.8f, 0.8f, 0.8f, 1.0f}}}
        },
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = normal_image_view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {{0.0f, 0.0f, 0.0f, 0.0f}}}
        }
    };
    VkRenderingAttachmentInfo depthAttachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth_image_view,
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
    vkCmdBindVertexBuffers(cb, 0, 1, &vibuf, &vOffset);
    vkCmdBindIndexBuffer(cb, vibuf, vbuf_size, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(cb, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PushConstants), &pc);
    vkCmdDrawIndexed(cb, index_count, 1, 0, 0, 0);
    vkCmdEndRendering(cb);

    chk(vkEndCommandBuffer(cb));
    VkFence fence = create_fence(device);
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
    chk(vkQueueSubmit(queue, 1, &submit_info, fence));
    chk(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(device, fence, NULL);

    save_texture(
            device,
            queue,
            vma,
            command_pool,
            color_image,
            COLOR_IMAGE_FORMAT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            image_width, image_height, "offline-output/color.ppm"
    );
    save_texture(
            device,
            queue,
            vma,
            command_pool,
            normal_image,
            normal_format,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            image_width, image_height, "offline-output/normal.ppm"
    );

    chk(vkDeviceWaitIdle(device));

    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyShaderModule(device, shader_module, NULL);
    vmaDestroyBuffer(vma, shader_data_buffer.buffer, shader_data_buffer.alloc);

    vmaDestroyImage(vma, color_image, color_image_allocation);
    vkDestroyImageView(device, color_image_view, NULL);
    vkDestroyImageView(device, depth_image_view, NULL);
    vmaDestroyImage(vma, depth_image, depth_image_allocation);
    vkDestroyImageView(device, normal_image_view, NULL);
    vmaDestroyImage(vma, normal_image, normal_image_allocation);

    vkDestroyCommandPool(device, command_pool, NULL);
    vmaDestroyBuffer(vma, vibuf, vibuf_alloc);
    vmaDestroyAllocator(vma);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    return 0;
}
