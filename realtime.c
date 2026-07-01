#include <string.h>
#include <math.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vk_enum_string_helper.h>
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vk_mem_alloc.h"
#include "cglm/cglm.h"
#include "cglm/struct.h"

#include "utils.c"

#define MAX_FRAMES_IN_FLIGHT 2
const VkFormat SWAPCHAIN_IMAGE_FORMAT = VK_FORMAT_B8G8R8A8_SRGB;

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

struct State {
    GLFWwindow *window;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_fam;
    VkSurfaceKHR surface;
    VmaAllocator vma;

    int window_w;
    int window_h;

    VkSwapchainKHR swapchain;
    VkExtent2D swapchain_extent;
    uint32_t swapchain_image_count;
    VkImage* swapchain_images;
    VkImageView *swapchain_image_views;

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

    VmaAllocatedBuffer shader_data_buffers[MAX_FRAMES_IN_FLIGHT];

    VkSemaphore image_acquired_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence fences[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore *render_complete_semaphores;
    uint32_t frame_index;

    VkCommandPool command_pool;
    VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

    VkDescriptorPool descriptor_pool;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;

    uint32_t image_index;

    bool update_swapchain;

    vec3 cam_pos;
} st = {0};


static inline void chkSwapchain(VkResult result) {
	if (result < VK_SUCCESS) {
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			st.update_swapchain = true;
			return;
		}
        fprintf(stderr, "Vulkan error. Code: %s.\n", string_VkResult(result));
        exit(EXIT_FAILURE);
	}
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    st.update_swapchain = true;
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
        uint32_t* p_image_count,
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
    if (surface_capabilities.currentExtent.width == 0xFFFFFFFF) {
        *p_extent = (VkExtent2D) {.width = (uint32_t)window_w, .height = (uint32_t)window_h};
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
    *p_images = malloc(sizeof(VkImage) * (*p_image_count));
    if (vkGetSwapchainImagesKHR(device, swapchain, p_image_count, *p_images) != VK_SUCCESS) {
        fatal("Failed to get swapchain images.");
    }

    *p_image_views = malloc(sizeof(VkImageView) * (*p_image_count));
    for (int i = 0; i < *p_image_count; i++) {
        (*p_image_views)[i] = create_image_view(device, (*p_images)[i],
                SWAPCHAIN_IMAGE_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    vkDestroySwapchainKHR(device, swapchain_ci.oldSwapchain, NULL);
    return swapchain;
}

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
    #define DEPTH_FORMAT_COUNT 2
    VkFormat depth_format_list[DEPTH_FORMAT_COUNT] = {
        VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
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
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, width, height);
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
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    *p_format = format;
    VkImage normal_image = create_image(vma, p_allocation, format,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, width, height);
    *p_image_view = create_image_view(device, normal_image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    return normal_image;
}

int main() {
    // TODO create state_defaults_init function
    st.cam_pos[0] = 0.0f;
    st.cam_pos[1] = 0.0f;
    st.cam_pos[2] = -6.0f;

    st.window = create_window();
    glfwGetWindowSize(st.window, &st.window_w, &st.window_h);
    st.instance = create_instance();
    st.physical_device = choose_physical_device(st.instance);
    st.queue_fam = choose_queue_family(st.instance, st.physical_device);
    st.device = create_logical_device(st.instance, st.physical_device, st.queue_fam);
    vkGetDeviceQueue(st.device, st.queue_fam, 0, &st.queue);
    st.vma = create_vma(st.physical_device, st.device, st.instance);

    if (glfwCreateWindowSurface(st.instance, st.window, NULL, &st.surface) != VK_SUCCESS) {
        fatal("Failed to create window surface.");
    }

    st.swapchain = create_swapchain_with_views(
            st.physical_device,
            st.device,
            st.surface,
            st.window_w,
            st.window_h,
            &st.swapchain_image_count,
            &st.swapchain_images,
            &st.swapchain_image_views,
            &st.swapchain_extent,
            VK_NULL_HANDLE
    );
    st.command_pool = create_command_pool(st.device, st.queue_fam);

    st.depth_image = create_depth_attachment_with_view(
            st.physical_device,
            st.device,
            st.vma,
            &st.depth_image_allocation,
            &st.depth_image_view,
            &st.depth_format,
            st.swapchain_extent.width,
            st.swapchain_extent.height
    );

    st.normal_image = create_normal_attachment_with_view(
            st.physical_device,
            st.device,
            st.vma,
            &st.normal_image_allocation,
            &st.normal_image_view,
            &st.normal_format,
            st.swapchain_extent.width,
            st.swapchain_extent.height
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

    st.index_count = 36;
    st.vertex_count = 36;
    st.indices = malloc(sizeof(uint32_t) * st.index_count);
    st.vertices = malloc(sizeof(Vertex) * st.vertex_count);
    for (uint32_t i = 0; i < st.index_count * 3; i++) {
        ind[i] -= 1;
    }
    for (uint32_t i = 0; i < st.index_count; i++) {
        int p = ind[i * 3];
        int t = ind[i * 3 + 1];
        int n = ind[i * 3 + 2];
        Vertex vertex = {
            .pos = {positions[p * 3], -positions[p * 3 + 1],
                    positions[p * 3 + 2]},
            .normal = {normals[n * 3], -normals[n * 3 + 1],
                       normals[n * 3 + 2]},
        };
        st.vertices[i] = vertex;
        st.indices[i] = i;
    }

    VkDeviceSize vbuf_size = sizeof(Vertex) * st.vertex_count;
    VkDeviceSize ibuf_size = sizeof(uint32_t) * st.index_count;
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
    if (vmaCreateBuffer(st.vma, &buffer_ci, &buffer_alloc_ci, &st.vibuf,
                        &st.vibuf_alloc, &vibuf_alloc_info) != VK_SUCCESS) {
        fatal("Failed to create vertex / index buffer.");
    }
    memcpy(vibuf_alloc_info.pMappedData, st.vertices, vbuf_size);
    memcpy((char *)vibuf_alloc_info.pMappedData + vbuf_size, st.indices,
           ibuf_size);

    VkSemaphoreCreateInfo semaphore_ci = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                  .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateFence(st.device, &fence_ci, NULL, &st.fences[i]) !=
            VK_SUCCESS) {
            fatal("Failed to create fence.");
        }
        if (vkCreateSemaphore(st.device, &semaphore_ci, NULL,
                              &st.image_acquired_semaphores[i]) != VK_SUCCESS) {
            fatal("Failed to create semaphore for image acquisition.");
        }
    }

    st.render_complete_semaphores = malloc(sizeof(VkSemaphore) * st.swapchain_image_count);
    for (int i = 0; i < st.swapchain_image_count; i++) {
        if (vkCreateSemaphore(st.device, &semaphore_ci, NULL,
                              &st.render_complete_semaphores[i]) !=
            VK_SUCCESS) {
            fatal("Failed to create semaphore for rendering completion.");
        }
    }

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
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vmaCreateBuffer(st.vma, &ubuf_ci, &ubuf_alloc_ci,
                            &st.shader_data_buffers[i].buffer,
                            &st.shader_data_buffers[i].alloc,
                            &st.shader_data_buffers[i].alloc_info) != VK_SUCCESS) {
            fatal("Failed to creates shader uniform buffer.");
        }
        VkBufferDeviceAddressInfo ubuf_addr_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = st.shader_data_buffers[i].buffer
        };
        st.shader_data_buffers[i].device_address =
            vkGetBufferDeviceAddress(st.device, &ubuf_addr_info);
    }

    VkCommandBufferAllocateInfo cb_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = st.command_pool,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    if (vkAllocateCommandBuffers(st.device, &cb_alloc_info,
                                 st.command_buffers) != VK_SUCCESS) {
        fatal("Failed to allocate command buffers.");
    }

    size_t code_size = 0;
    uint32_t* spirv;
    if (read_binary_file("shaders/compiled/bake.spirv", (char**) &spirv, &code_size)) {
        fatal("Failed to read shader SPIR-V.");
    }
    VkShaderModuleCreateInfo shader_module_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size,
        .pCode = spirv,
    };
    VkShaderModule shader_module;
    chk(vkCreateShaderModule(st.device, &shader_module_ci, NULL, &shader_module));

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
    chk(vkCreatePipelineLayout(st.device, &pipeline_layout_ci, NULL,
                               &st.pipeline_layout));
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
        SWAPCHAIN_IMAGE_FORMAT,
        st.normal_format
    };
    VkPipelineRenderingCreateInfo rendering_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 2,
        .pColorAttachmentFormats = color_attachment_formats,
        .depthAttachmentFormat = st.depth_format};
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
        .layout = st.pipeline_layout};
    chk(vkCreateGraphicsPipelines(
                st.device, VK_NULL_HANDLE, 1, &pipeline_ci, NULL, &st.pipeline));

    while (!glfwWindowShouldClose(st.window)) {
        glfwPollEvents();

        chk(vkWaitForFences(st.device, 1, &st.fences[st.frame_index], true, UINT64_MAX));
        chk(vkResetFences(st.device, 1, &st.fences[st.frame_index]));
        chkSwapchain(vkAcquireNextImageKHR(st.device, st.swapchain, UINT64_MAX,
                                           st.image_acquired_semaphores[st.frame_index],
                                           VK_NULL_HANDLE, &st.image_index));
        SceneUniforms uniforms;
        glm_perspective(glm_rad(45.0f), (float)st.window_w / (float)st.window_h,
            0.1f, 32.0f, uniforms.proj);
        mat4 mat_identity; glm_mat4_identity(mat_identity);
        glm_translate_to(mat_identity, st.cam_pos, uniforms.view);
        memcpy(st.shader_data_buffers[st.frame_index].alloc_info.pMappedData,
               &uniforms, sizeof(SceneUniforms));

        PushConstants pc = {
            .object_id = 3,
            .model = {},
            .scene_uniforms = st.shader_data_buffers[st.frame_index].device_address,
        };
        vec3 model_pos = { -3.0f + fmod(glfwGetTime(), 3.0) * 3.0, 0.0f, 0.0f };
        glm_translate_to(mat_identity, model_pos, pc.model);

        VkCommandBuffer cb = st.command_buffers[st.frame_index];
        chk(vkResetCommandBuffer(cb, 0));
        VkCommandBufferBeginInfo cb_bi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        chk(vkBeginCommandBuffer(cb, &cb_bi));
        VkImageMemoryBarrier2 output_barriers[3] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = 0,
                .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .image = st.swapchain_images[st.image_index],
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .levelCount = 1,
                                  .layerCount = 1}},
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = 0,
                .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .image = st.normal_image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .levelCount = 1,
                                  .layerCount = 1}},
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .image = st.depth_image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                                                VK_IMAGE_ASPECT_STENCIL_BIT,
                                  .levelCount = 1,
                                  .layerCount = 1}}
        };
        VkDependencyInfo barrier_dependency_info = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 3,
            .pImageMemoryBarriers = output_barriers
        };
        vkCmdPipelineBarrier2(cb, &barrier_dependency_info);

        VkRenderingAttachmentInfo color_attachment_infos[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = st.swapchain_image_views[st.image_index],
                .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {{0.8f, 0.8f, 0.8f, 1.0f}}}
            },
            {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = st.normal_image_view,
                .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}}
            }
        };
        VkRenderingAttachmentInfo depthAttachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = st.depth_image_view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.depthStencil = {1.0f, 0}}
        };

        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = {.width = (uint32_t)(st.window_w),
                                      .height = (uint32_t)(st.window_h)}},
            .layerCount = 1,
            .colorAttachmentCount = 2,
            .pColorAttachments = color_attachment_infos,
            .pDepthAttachment = &depthAttachmentInfo};
        vkCmdBeginRendering(cb, &renderingInfo);
        VkViewport vp = {.width = (float)(st.window_w),
                      .height = (float)(st.window_h),
                      .minDepth = 0.0f,
                      .maxDepth = 1.0f};
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D scissor = {
            .extent = {.width = (uint32_t)(st.window_w),
                    .height = (uint32_t)(st.window_h)}};
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, st.pipeline);
        vkCmdSetScissor(cb, 0, 1, &scissor);
        VkDeviceSize vOffset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &st.vibuf, &vOffset);
        vkCmdBindIndexBuffer(cb, st.vibuf, vbuf_size, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cb, st.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PushConstants), &pc);
        vkCmdDrawIndexed(cb, st.index_count, 1, 0, 0, 0);
        vkCmdEndRendering(cb);
        VkImageMemoryBarrier2 barrier_present = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = st.swapchain_images[st.image_index],
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
            .pWaitSemaphores = &st.image_acquired_semaphores[st.frame_index],
            .pWaitDstStageMask = &wait_stages,
            .commandBufferCount = 1,
            .pCommandBuffers = &cb,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &st.render_complete_semaphores[st.image_index],
        };
        chk(vkQueueSubmit(st.queue, 1, &submitInfo, st.fences[st.frame_index]));
        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &st.render_complete_semaphores[st.image_index],
            .swapchainCount = 1,
            .pSwapchains = &st.swapchain,
            .pImageIndices = &st.image_index};

        chkSwapchain(vkQueuePresentKHR(st.queue, &presentInfo));
        if (st.update_swapchain) {
            st.update_swapchain = false;
            glfwGetFramebufferSize(st.window, &st.window_w, &st.window_h);
            chk(vkDeviceWaitIdle(st.device));
            
            for (int i = 0; i < st.swapchain_image_count; i++) {
                vkDestroyImageView(st.device, st.swapchain_image_views[i], NULL);
            }
            free(st.swapchain_image_views);
            free(st.swapchain_images);
            st.swapchain = create_swapchain_with_views(
                    st.physical_device,
                    st.device,
                    st.surface,
                    st.window_w,
                    st.window_h,
                    &st.swapchain_image_count,
                    &st.swapchain_images,
                    &st.swapchain_image_views,
                    &st.swapchain_extent,
                    st.swapchain
            );
            
            for (int i=0; i < st.swapchain_image_count; i++) {
                vkDestroySemaphore(st.device, st.render_complete_semaphores[i], NULL);
            }
            st.render_complete_semaphores = malloc(sizeof(VkSemaphore) * st.swapchain_image_count);
            for (int i=0; i < st.swapchain_image_count; i++) {
                chk(vkCreateSemaphore(st.device, &semaphore_ci, NULL, &st.render_complete_semaphores[i]));
            }

            vmaDestroyImage(st.vma, st.depth_image, st.depth_image_allocation);
            vkDestroyImageView(st.device, st.depth_image_view, NULL);
            st.depth_image = create_depth_attachment_with_view(
                    st.physical_device,
                    st.device,
                    st.vma,
                    &st.depth_image_allocation,
                    &st.depth_image_view,
                    &st.depth_format,
                    st.swapchain_extent.width,
                    st.swapchain_extent.height
            );
        }

        st.frame_index = (st.frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    chk(vkDeviceWaitIdle(st.device));

    vkDestroyPipeline(st.device, st.pipeline, NULL);
    vkDestroyPipelineLayout(st.device, st.pipeline_layout, NULL);
    vkDestroyShaderModule(st.device, shader_module, NULL);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vmaDestroyBuffer(st.vma, st.shader_data_buffers[i].buffer,
                         st.shader_data_buffers[i].alloc);
    }

    vkDestroyImageView(st.device, st.depth_image_view, NULL);
    vmaDestroyImage(st.vma, st.depth_image, st.depth_image_allocation);

    vkDestroyImageView(st.device, st.normal_image_view, NULL);
    vmaDestroyImage(st.vma, st.normal_image, st.normal_image_allocation);

    vkDestroyCommandPool(st.device, st.command_pool, NULL);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyFence(st.device, st.fences[i], NULL);
        vkDestroySemaphore(st.device, st.image_acquired_semaphores[i], NULL);
    }
    for (int i = 0; i < st.swapchain_image_count; i++) {
        vkDestroySemaphore(st.device, st.render_complete_semaphores[i], NULL);
    }

    vmaDestroyBuffer(st.vma, st.vibuf, st.vibuf_alloc);

    for (int i = 0; i < st.swapchain_image_count; i++) {
        vkDestroyImageView(st.device, st.swapchain_image_views[i], NULL);
    }

    vkDestroySwapchainKHR(st.device, st.swapchain, NULL);
    vkDestroySurfaceKHR(st.instance, st.surface, NULL);
    vmaDestroyAllocator(st.vma);
    vkDestroyDevice(st.device, NULL);
    vkDestroyInstance(st.instance, NULL);
    glfwDestroyWindow(st.window);
    glfwTerminate();

    return 0;
}
