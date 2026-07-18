#include <string.h>
#include <math.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vk_enum_string_helper.h>
#include "vk_mem_alloc.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
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
    VkDeviceAddress scene_uniforms;
} PushConstants;

Arena g_perm;
bool update_swapchain;

#define MAX_FRAMES_IN_FLIGHT 2
const VkFormat SWAPCHAIN_IMAGE_FORMAT = VK_FORMAT_B8G8R8A8_SRGB;

const VkFormat COLOR_MAP_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;
const VkFormat NORMAL_MAP_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;

#define MAP_WIDTH 1920
#define MAP_HEIGHT 1080

bool chk_swapchain(VkResult result) {
	if (result < VK_SUCCESS) {
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			return true;
		}
        fprintf(stderr, "Vulkan error. Code: %s.\n", string_VkResult(result));
        exit(EXIT_FAILURE);
	}
    return false;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    update_swapchain = true;
}

GLFWwindow* create_window() {
    if (!glfwInit()) {
        fatal("Failed to init GLFW.");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "Cindy", monitor, NULL);
    if (!window) {
        fatal("Failed to create window.");
    }
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    return window;
}

VkSwapchainKHR create_swapchain_with_views(
        VkPhysicalDevice physical_device,
        VkDevice device,
        VkSurfaceKHR surface,
        int window_w,
        int window_h,
        u32* p_image_count,
        VkImage** p_images,
        VkImageView** p_image_views,
        VkExtent2D* p_extent,
        VkSwapchainKHR old_swapchain)
{
    VkSurfaceCapabilitiesKHR surface_capabilities = {0};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            physical_device, surface, &surface_capabilities) != VK_SUCCESS) {
        fatal("Failed to get physical device surface capabilities.");
    }

    *p_extent = surface_capabilities.currentExtent;
    // Handle Wayland
    if (surface_capabilities.currentExtent.width == 0xFFFFFFFF) {
        *p_extent = (VkExtent2D) {.width = (u32)window_w, .height = (u32)window_h};
    }

    VkSwapchainCreateInfoKHR swapchain_ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = surface_capabilities.minImageCount,
        .imageFormat = SWAPCHAIN_IMAGE_FORMAT,
        .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = {.width = (*p_extent).width,
                        .height = (*p_extent).height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .oldSwapchain = old_swapchain,
    };

    VkSwapchainKHR swapchain;
    if (vkCreateSwapchainKHR(device, &swapchain_ci, NULL, &swapchain) !=
        VK_SUCCESS) {
        fatal("Failed to create swapchain.");
    }
    if (vkGetSwapchainImagesKHR(device, swapchain, p_image_count, NULL) != VK_SUCCESS) {
        fatal("Failed to get swapchain image count.");
    }
    *p_images = arena_alloc(&g_perm, sizeof(VkImage) * (*p_image_count));
    if (vkGetSwapchainImagesKHR(device, swapchain, p_image_count, *p_images) != VK_SUCCESS) {
        fatal("Failed to get swapchain images.");
    }

    *p_image_views = arena_alloc(&g_perm, sizeof(VkImageView) * (*p_image_count));
    for (int i = 0; i < *p_image_count; i++) {
        (*p_image_views)[i] = create_image_view((*p_images)[i], SWAPCHAIN_IMAGE_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    vkDestroySwapchainKHR(device, swapchain_ci.oldSwapchain, NULL);
    return swapchain;
}

int main() {
    u32 frame_index = 0;
    u32 image_index = 0;
    update_swapchain = false;

    memory_init(MBS(256), MBS(1));
    g_perm = arena_init(&memory.total, MBS(256 - 1), "permanent");

    GLFWwindow *window = create_window();
    int window_w;
    int window_h;
    glfwGetWindowSize(window, &window_w, &window_h);

    const char* dev_extensions[1] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    u32 ext_count;
    const char** extensions = glfwGetRequiredInstanceExtensions(&ext_count);
    vkg_init(1, dev_extensions, ext_count, extensions);

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(vkg.instance, window, NULL, &surface) != VK_SUCCESS) {
        fatal("Failed to create window surface.");
    }

    VkSwapchainKHR swapchain;
    VkExtent2D swapchain_extent;
    u32 swapchain_image_count;
    VkImage* swapchain_images;
    VkImageView *swapchain_image_views;

    swapchain = create_swapchain_with_views(
            vkg.physical_device,
            vkg.device,
            surface,
            window_w,
            window_h,
            &swapchain_image_count,
            &swapchain_images,
            &swapchain_image_views,
            &swapchain_extent,
            VK_NULL_HANDLE
    );

    Image depth_att = create_image(DEPTH_MAP_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, window_w, window_h, false);

    VkSemaphore image_acquired_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence fences[MAX_FRAMES_IN_FLIGHT];
    VkSemaphoreCreateInfo semaphore_ci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        fences[i] = create_fence(true);
        if (vkCreateSemaphore(vkg.device, &semaphore_ci, NULL, &image_acquired_semaphores[i]) != VK_SUCCESS) {
            fatal("Failed to create semaphore for image acquisition.");
        }
    }

    VkSemaphore* render_complete_semaphores = arena_alloc(&g_perm, sizeof(VkSemaphore) * swapchain_image_count);
    for (int i = 0; i < swapchain_image_count; i++) {
        if (vkCreateSemaphore(vkg.device, &semaphore_ci, NULL,
                      &render_complete_semaphores[i]) != VK_SUCCESS) {
            fatal("Failed to create semaphore for rendering completion.");
        }
    }

    VkCommandBufferAllocateInfo cb_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vkg.command_pool,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateCommandBuffers(vkg.device, &cb_alloc_info,
                                 command_buffers) != VK_SUCCESS) {
        fatal("Failed to allocate command buffers.");
    }

    // 3D MODEL PIPELINE
    Pipeline model_pipeline;
    VkShaderModule md_vert_shader_module = create_shader_module(
            "../shaders/realtime_model_vert.glsl", shaderc_glsl_vertex_shader);
    VkShaderModule md_frag_shader_module = create_shader_module(
            "../shaders/realtime_model_frag.glsl", shaderc_glsl_fragment_shader);

    VkPushConstantRange md_push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(PushConstants)
    };
    VkPipelineLayoutCreateInfo md_pipeline_layout_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pSetLayouts = NULL,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &md_push_constant_range
    };
    VkPipelineLayout md_pipeline_layout;
    chk(vkCreatePipelineLayout(vkg.device, &md_pipeline_layout_ci, NULL,
                               &md_pipeline_layout));
    VkPipelineShaderStageCreateInfo md_shader_stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = md_vert_shader_module,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = md_frag_shader_module,
         .pName = "main"}};
    VkVertexInputBindingDescription md_vertex_binding = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    #define VERTEX_ATTR_COUNT 2
    VkVertexInputAttributeDescription md_vertex_attributes[VERTEX_ATTR_COUNT] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0},
        {.location = 1,
         .binding = 0,
         .format = VK_FORMAT_R32G32B32_SFLOAT,
         .offset = offsetof(Vertex, normal)},
    };
    VkPipelineVertexInputStateCreateInfo md_vertex_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &md_vertex_binding,
        .vertexAttributeDescriptionCount = VERTEX_ATTR_COUNT,
        .pVertexAttributeDescriptions = md_vertex_attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo md_input_assembly_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkDynamicState md_dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo md_dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = md_dynamic_states};
    VkPipelineViewportStateCreateInfo md_viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo md_rasterization_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .lineWidth = 1.0f,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .cullMode = VK_CULL_MODE_BACK_BIT,
    };
    VkPipelineMultisampleStateCreateInfo md_multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo md_depth_stencil_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
    VkPipelineColorBlendAttachmentState md_blend_attachment_state = {.colorWriteMask = 0xF};
    VkPipelineColorBlendStateCreateInfo md_color_blend_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &md_blend_attachment_state};
    VkPipelineRenderingCreateInfo md_rendering_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &SWAPCHAIN_IMAGE_FORMAT,
        .depthAttachmentFormat = depth_att.format};
    model_pipeline = (Pipeline) { .info = 
        {
           .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
           .pNext = &md_rendering_ci,
           .stageCount = 2,
           .pStages = md_shader_stages,
           .pVertexInputState = &md_vertex_input_state,
           .pInputAssemblyState = &md_input_assembly_state,
           .pViewportState = &md_viewport_state,
           .pRasterizationState = &md_rasterization_state,
           .pMultisampleState = &md_multisample_state,
           .pDepthStencilState = &md_depth_stencil_state,
           .pColorBlendState = &md_color_blend_state,
           .pDynamicState = &md_dynamic_state,
           .layout = md_pipeline_layout
        }
    };
    pipeline_init(&model_pipeline);

    // STATIC BACKGROUND PIPELINE
    Pipeline bg_pipeline;
    VkDescriptorSetLayout desc_set_layout;
        VkShaderModule vert_shader_module = create_shader_module("../shaders/realtime_bg_vert.glsl", shaderc_glsl_vertex_shader);
        VkShaderModule frag_shader_module = create_shader_module("../shaders/realtime_bg_frag.glsl", shaderc_glsl_fragment_shader);

        VkDescriptorSetLayoutBinding bindings[3];
        for (int i=0; i < 3; i++) {
            bindings[i] = (VkDescriptorSetLayoutBinding) {
                .binding = i,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            };
        }
        VkDescriptorSetLayoutCreateInfo desc_set_layout_ci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .bindingCount = 3,
            .pBindings = bindings,
        };
        vkCreateDescriptorSetLayout(vkg.device, &desc_set_layout_ci, NULL, &desc_set_layout);

        VkPipelineLayoutCreateInfo pipeline_layout_ci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &desc_set_layout,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = NULL};
        VkPipelineLayout pipeline_layout;
        chk(vkCreatePipelineLayout(vkg.device, &pipeline_layout_ci, NULL, &pipeline_layout));
        VkPipelineShaderStageCreateInfo shader_stages[2] = {
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vert_shader_module,
             .pName = "main"},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = frag_shader_module,
             .pName = "main"}};
        VkPipelineVertexInputStateCreateInfo vertex_input_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 0,
            .pVertexBindingDescriptions = NULL,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions = NULL,
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
        VkPipelineMultisampleStateCreateInfo multisample_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
        VkPipelineColorBlendAttachmentState blend_attachment_state = {.colorWriteMask = 0xF};
        VkPipelineColorBlendStateCreateInfo color_blend_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &blend_attachment_state};
        VkFormat color_attachment_format = SWAPCHAIN_IMAGE_FORMAT;
        VkPipelineRenderingCreateInfo rendering_ci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &color_attachment_format,
            .depthAttachmentFormat = depth_att.format};
        VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
        VkPipelineRasterizationStateCreateInfo rasterization_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .lineWidth = 1.0f,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .cullMode = VK_CULL_MODE_NONE,
        };
        bg_pipeline = (Pipeline) { .info = {
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
            .layout = pipeline_layout}
        };
        pipeline_init(&bg_pipeline);

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 3
    };
	VkDescriptorPoolCreateInfo desc_pool_ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size
    };

    VkDescriptorPool descriptor_pool;
	chk(vkCreateDescriptorPool(vkg.device, &desc_pool_ci, NULL, &descriptor_pool));

    VkDescriptorSetAllocateInfo desc_set_alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = NULL,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &desc_set_layout
    };
    VkDescriptorSet desc_set;
	chk(vkAllocateDescriptorSets(vkg.device, &desc_set_alloc, &desc_set));


    Texture color_map = load_binary_texture("offline-output/color.bin", COLOR_MAP_FORMAT,
            VK_IMAGE_ASPECT_COLOR_BIT, MAP_WIDTH, MAP_HEIGHT);
    Texture normal_map = load_binary_texture("offline-output/normal.bin", NORMAL_MAP_FORMAT,
            VK_IMAGE_ASPECT_COLOR_BIT, MAP_WIDTH, MAP_HEIGHT);
    Texture depth_map = load_binary_texture("offline-output/depth.bin", DEPTH_MAP_FORMAT,
            VK_IMAGE_ASPECT_DEPTH_BIT, MAP_WIDTH, MAP_HEIGHT);

    VkDescriptorImageInfo desc_infos[3] = {
        color_map.desc_info,
        normal_map.desc_info,
        depth_map.desc_info,
    };
	VkWriteDescriptorSet write_desc_set = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = NULL,
        .dstSet = desc_set,
        .dstBinding = 0,
        .descriptorCount = 3,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = desc_infos
    };
	vkUpdateDescriptorSets(vkg.device, 1, &write_desc_set, 0, NULL);

    time_t frag_timestamp = get_file_timestamp("../shaders/realtime_bg_frag.glsl");
    while (!glfwWindowShouldClose(window)) {
        time_t new_frag_timestamp = get_file_timestamp("../shaders/realtime_bg_frag.glsl");
        if (new_frag_timestamp > frag_timestamp) {
            frag_timestamp = new_frag_timestamp;
            change_shader(&bg_pipeline, "../shaders/realtime_bg_frag.glsl", shaderc_glsl_fragment_shader, &g_perm);
        }

        glfwPollEvents();

        chk(vkWaitForFences(vkg.device, 1, &(fences[frame_index]), true, UINT64_MAX));
        chk(vkResetFences(vkg.device, 1, &(fences[frame_index])));
        update_swapchain = chk_swapchain(vkAcquireNextImageKHR(vkg.device, swapchain, UINT64_MAX,
                                           image_acquired_semaphores[frame_index],
                                           VK_NULL_HANDLE, &image_index));
        VkCommandBuffer cb = command_buffers[frame_index];
        chk(vkResetCommandBuffer(cb, 0));
        VkCommandBufferBeginInfo cb_bi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        chk(vkBeginCommandBuffer(cb, &cb_bi));
        VkImageMemoryBarrier2 output_barriers[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = 0,
                .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .image = swapchain_images[image_index],
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .levelCount = 1,
                                  .layerCount = 1}
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .image = depth_att.image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                  .levelCount = 1,
                                  .layerCount = 1}
            }
        };
        VkDependencyInfo barrier_dependency_info = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 2,
            .pImageMemoryBarriers = output_barriers
        };
        vkCmdPipelineBarrier2(cb, &barrier_dependency_info);

        VkRenderingAttachmentInfo color_attachment_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchain_image_views[image_index],
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {{0.6f, 0.6f, 0.6f, 1.0f}}}
        };
        VkRenderingAttachmentInfo depth_attachment_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depth_att.view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        };

        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = {.width = (u32)(window_w),
                                      .height = (u32)(window_h)}},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment_info,
            .pDepthAttachment = &depth_attachment_info};
        vkCmdBeginRendering(cb, &renderingInfo);
        VkViewport vp = {.width = (float)(window_w),
                      .height = (float)(window_h),
                      .minDepth = 0.0f,
                      .maxDepth = 1.0f};
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D scissor = {
            .extent = {
                .width = (u32)(window_w),
                .height = (u32)(window_h)
            }
        };
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, bg_pipeline.pl);
        vkCmdSetScissor(cb, 0, 1, &scissor);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, bg_pipeline.info.layout, 0, 1, &desc_set, 0, NULL);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRendering(cb);
        VkImageMemoryBarrier2 barrier_present = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = swapchain_images[image_index],
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .levelCount = 1,
                              .layerCount = 1}};
        VkDependencyInfo barrier_present_dep_info = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier_present};
        vkCmdPipelineBarrier2(cb, &barrier_present_dep_info);
        chk(vkEndCommandBuffer(cb));

        VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &image_acquired_semaphores[frame_index],
            .pWaitDstStageMask = &wait_stages,
            .commandBufferCount = 1,
            .pCommandBuffers = &cb,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &render_complete_semaphores[image_index],
        };
        chk(vkQueueSubmit(vkg.queue, 1, &submitInfo, fences[frame_index]));
        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &render_complete_semaphores[image_index],
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &image_index};

        update_swapchain = chk_swapchain(vkQueuePresentKHR(vkg.queue, &presentInfo));
        if (update_swapchain) {
            update_swapchain = false;
            glfwGetFramebufferSize(window, &window_w, &window_h);
            chk(vkDeviceWaitIdle(vkg.device));
            
            for (int i = 0; i < swapchain_image_count; i++) {
                vkDestroyImageView(vkg.device, swapchain_image_views[i], NULL);
            }
            swapchain = create_swapchain_with_views(
                    vkg.physical_device,
                    vkg.device,
                    surface,
                    window_w,
                    window_h,
                    &swapchain_image_count,
                    &swapchain_images,
                    &swapchain_image_views,
                    &swapchain_extent,
                    swapchain
            );
            
            for (int i=0; i < swapchain_image_count; i++) {
                vkDestroySemaphore(vkg.device, render_complete_semaphores[i], NULL);
            }
            render_complete_semaphores = arena_alloc(&g_perm, sizeof(VkSemaphore) * swapchain_image_count);
            for (int i=0; i < swapchain_image_count; i++) {
                chk(vkCreateSemaphore(vkg.device, &semaphore_ci, NULL, &render_complete_semaphores[i]));
            }
        }

        frame_index = (frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    chk(vkDeviceWaitIdle(vkg.device));

    destroy_pipeline(&model_pipeline);
    destroy_pipeline(&bg_pipeline);
    vkDestroyDescriptorPool(vkg.device, descriptor_pool, NULL);
    vkDestroyDescriptorSetLayout(vkg.device, desc_set_layout, NULL);

    destroy_image(&depth_att);

    destroy_texture(&color_map);
    destroy_texture(&normal_map);
    destroy_texture(&depth_map);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyFence(vkg.device, fences[i], NULL);
        vkDestroySemaphore(vkg.device, image_acquired_semaphores[i], NULL);
    }
    for (int i = 0; i < swapchain_image_count; i++) {
        vkDestroySemaphore(vkg.device, render_complete_semaphores[i], NULL);
    }
    for (int i = 0; i < swapchain_image_count; i++) {
        vkDestroyImageView(vkg.device, swapchain_image_views[i], NULL);
    }

    vkDestroySwapchainKHR(vkg.device, swapchain, NULL);
    vkDestroySurfaceKHR(vkg.instance, surface, NULL);

    vkg_shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    arena_report(&memory.scratch);
    arena_report(&g_perm);
    memory_shutdown();

    return 0;
}
