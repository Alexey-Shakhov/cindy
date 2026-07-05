#include <string.h>
#include <math.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vk_enum_string_helper.h>
#include "vk_mem_alloc.h"
#include "cglm/cglm.h"
#include "cglm/struct.h"

#include "utils.c"
#include "vk_helpers.c"

#define MAX_FRAMES_IN_FLIGHT 2
const VkFormat SWAPCHAIN_IMAGE_FORMAT = VK_FORMAT_B8G8R8A8_SRGB;

const VkFormat COLOR_MAP_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;
const VkFormat NORMAL_MAP_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;
const VkFormat DEPTH_MAP_FORMAT = VK_FORMAT_D32_SFLOAT;

#define MAP_WIDTH 1920
#define MAP_HEIGHT 1080

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

    VkImage depth_att;
    VmaAllocation depth_att_allocation;
    VkImageView depth_att_view;
    VkFormat depth_att_format;

    VkImage color_map;
    VmaAllocation color_map_allocation;
    VkImageView color_map_view;
    VkFormat color_map_format;
    VkSampler color_map_sampler;
    VkDescriptorImageInfo color_map_desc_info;

    VkImage depth_map;
    VmaAllocation depth_map_allocation;
    VkImageView depth_map_view;
    VkFormat depth_map_format;
    VkSampler depth_map_sampler;
    VkDescriptorImageInfo depth_map_desc_info;

    VkImage normal_map;
    VmaAllocation normal_map_allocation;
    VkImageView normal_map_view;
    VkFormat normal_map_format;
    VkSampler normal_map_sampler;
    VkDescriptorImageInfo normal_map_desc_info;

    VkSemaphore image_acquired_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence fences[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore *render_complete_semaphores;
    uint32_t frame_index;

    VkCommandPool command_pool;
    VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout desc_set_layout;
    VkDescriptorSet desc_set;

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
    // Handle Wayland
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
    /*
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
    */
    VkFormat depth_format = DEPTH_MAP_FORMAT;
    *p_format = depth_format;
    VkImage depth_image = create_image(vma, p_allocation, depth_format,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, width, height, false);
    *p_image_view = create_image_view(device, depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
    return depth_image;
}

VkImage load_world_tex_map(VkImageView* p_view, VmaAllocation* p_alloc, VkSampler* p_sampler, VkDescriptorImageInfo* p_desc_info,
        VkDevice device, VmaAllocator vma, VkCommandPool command_pool,
        const char* filename, VkFormat format, VkImageAspectFlags aspect_mask, int width, int height)
{
    int pixel_size = get_format_pixel_size(format);
    int buf_size = pixel_size * width * height;
    VmaAllocatedBuffer staging_buf = allocate_buffer(vma, device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT, buf_size);

    char* staging_buf_p = staging_buf.alloc_info.pMappedData;
    FILE* file = fopen(filename, "rb");
    if (!file) {
        fatal("Failed to open texture map file.");
    }
    fread(staging_buf_p, buf_size, 1, file);
    fclose(file);

    VkImage image = create_image(vma, p_alloc, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            width, height, false);

    VkCommandBuffer cb = begin_command_buffer(device, command_pool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    transition_image_layout(cb, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            aspect_mask, VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
    VkBufferImageCopy copy = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource.aspectMask = aspect_mask,
        .imageSubresource.mipLevel = 0,
        .imageSubresource.baseArrayLayer = 0,
        .imageSubresource.layerCount = 1,
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyBufferToImage(cb, staging_buf.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    transition_image_layout(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            aspect_mask, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    end_one_time_command_buffer(st.device, cb, st.queue);

    vmaDestroyBuffer(vma, staging_buf.buffer, staging_buf.alloc);

    *p_view = create_image_view(device, image, format, aspect_mask);

    VkSamplerCreateInfo sampler_ci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = NULL,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .anisotropyEnable = VK_FALSE,
    };
    chk(vkCreateSampler(device, &sampler_ci, NULL, p_sampler));
    *p_desc_info = (VkDescriptorImageInfo) {
        .sampler = *p_sampler,
        .imageView = *p_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    return image;
}

int main() {
    // TODO create state_defaults_init function
    st.cam_pos[0] = 0.0f;
    st.cam_pos[1] = 0.0f;
    st.cam_pos[2] = -6.0f;

    st.window = create_window();
    glfwGetWindowSize(st.window, &st.window_w, &st.window_h);

    uint32_t ext_count;
    const char** extensions = glfwGetRequiredInstanceExtensions(&ext_count);
    st.instance = create_instance(ext_count, extensions);

    st.physical_device = choose_physical_device(st.instance);
    st.queue_fam = choose_queue_family(st.instance, st.physical_device);

    const char* dev_extensions[1] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    st.device = create_logical_device(st.instance, st.physical_device, st.queue_fam, 1, dev_extensions);
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

    st.depth_att = create_depth_attachment_with_view(
            st.physical_device, st.device, st.vma,
            &st.depth_att_allocation, &st.depth_att_view,
            &st.depth_att_format, st.window_w, st.window_h
    );

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
                      &st.render_complete_semaphores[i]) != VK_SUCCESS) {
            fatal("Failed to create semaphore for rendering completion.");
        }
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
    if (read_binary_file("shaders/realtime.spirv", (char**) &spirv, &code_size)) {
        fatal("Failed to read shader SPIR-V.");
    }
    VkShaderModuleCreateInfo shader_module_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size,
        .pCode = spirv,
    };
    VkShaderModule shader_module;
    chk(vkCreateShaderModule(st.device, &shader_module_ci, NULL, &shader_module));

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
    vkCreateDescriptorSetLayout(st.device, &desc_set_layout_ci, NULL, &st.desc_set_layout);

    VkPipelineLayoutCreateInfo pipeline_layout_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &st.desc_set_layout,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = NULL};
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
        .depthAttachmentFormat = st.depth_att_format};
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
	chk(vkCreateDescriptorPool(st.device, &desc_pool_ci, NULL, &st.descriptor_pool));

    VkDescriptorSetAllocateInfo desc_set_alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = NULL,
        .descriptorPool = st.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &st.desc_set_layout
    };
	chk(vkAllocateDescriptorSets(st.device, &desc_set_alloc, &st.desc_set));

    st.color_map = load_world_tex_map(&st.color_map_view, &st.color_map_allocation, &st.color_map_sampler,
            &st.color_map_desc_info, st.device, st.vma, st.command_pool, "offline-output/color.bin", COLOR_MAP_FORMAT,
            VK_IMAGE_ASPECT_COLOR_BIT, MAP_WIDTH, MAP_HEIGHT);
    st.normal_map = load_world_tex_map(&st.normal_map_view, &st.normal_map_allocation, &st.normal_map_sampler,
            &st.normal_map_desc_info, st.device, st.vma, st.command_pool, "offline-output/normal.bin", NORMAL_MAP_FORMAT,
            VK_IMAGE_ASPECT_COLOR_BIT, MAP_WIDTH, MAP_HEIGHT);
    st.depth_map = load_world_tex_map(&st.depth_map_view, &st.depth_map_allocation, &st.depth_map_sampler,
            &st.depth_map_desc_info, st.device, st.vma, st.command_pool, "offline-output/depth.bin", DEPTH_MAP_FORMAT,
            VK_IMAGE_ASPECT_DEPTH_BIT, MAP_WIDTH, MAP_HEIGHT);

    VkDescriptorImageInfo desc_infos[3] = {
        st.color_map_desc_info,
        st.normal_map_desc_info,
        st.depth_map_desc_info,
    };
	VkWriteDescriptorSet write_desc_set = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = NULL,
        .dstSet = st.desc_set,
        .dstBinding = 0,
        .descriptorCount = 3,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = desc_infos
    };
	vkUpdateDescriptorSets(st.device, 1, &write_desc_set, 0, NULL);

    while (!glfwWindowShouldClose(st.window)) {
        glfwPollEvents();

        chk(vkWaitForFences(st.device, 1, &st.fences[st.frame_index], true, UINT64_MAX));
        chk(vkResetFences(st.device, 1, &st.fences[st.frame_index]));
        chkSwapchain(vkAcquireNextImageKHR(st.device, st.swapchain, UINT64_MAX,
                                           st.image_acquired_semaphores[st.frame_index],
                                           VK_NULL_HANDLE, &st.image_index));
        VkCommandBuffer cb = st.command_buffers[st.frame_index];
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
                .image = st.swapchain_images[st.image_index],
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
                .image = st.depth_att,
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
            .imageView = st.swapchain_image_views[st.image_index],
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {{0.6f, 0.6f, 0.6f, 1.0f}}}
        };
        VkRenderingAttachmentInfo depth_attachment_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = st.depth_att_view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        };

        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = {.width = (uint32_t)(st.window_w),
                                      .height = (uint32_t)(st.window_h)}},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment_info,
            .pDepthAttachment = &depth_attachment_info};
        vkCmdBeginRendering(cb, &renderingInfo);
        VkViewport vp = {.width = (float)(st.window_w),
                      .height = (float)(st.window_h),
                      .minDepth = 0.0f,
                      .maxDepth = 1.0f};
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D scissor = {
            .extent = {
                .width = (uint32_t)(st.window_w),
                .height = (uint32_t)(st.window_h)
            }
        };
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, st.pipeline);
        vkCmdSetScissor(cb, 0, 1, &scissor);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, st.pipeline_layout, 0, 1, &st.desc_set, 0, NULL);
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
        }

        st.frame_index = (st.frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    chk(vkDeviceWaitIdle(st.device));

    vkDestroyPipeline(st.device, st.pipeline, NULL);
    vkDestroyPipelineLayout(st.device, st.pipeline_layout, NULL);
    vkDestroyShaderModule(st.device, shader_module, NULL);
    vkDestroyDescriptorPool(st.device, st.descriptor_pool, NULL);
    vkDestroyDescriptorSetLayout(st.device, st.desc_set_layout, NULL);

    vkDestroyImageView(st.device, st.depth_att_view, NULL);
    vmaDestroyImage(st.vma, st.depth_att, st.depth_att_allocation);

    vkDestroyImageView(st.device, st.color_map_view, NULL);
    vmaDestroyImage(st.vma, st.color_map, st.color_map_allocation);
    vkDestroySampler(st.device, st.color_map_sampler, NULL);

    vkDestroyImageView(st.device, st.depth_map_view, NULL);
    vmaDestroyImage(st.vma, st.depth_map, st.depth_map_allocation);
    vkDestroySampler(st.device, st.depth_map_sampler, NULL);

    vkDestroyImageView(st.device, st.normal_map_view, NULL);
    vmaDestroyImage(st.vma, st.normal_map, st.normal_map_allocation);
    vkDestroySampler(st.device, st.normal_map_sampler, NULL);

    vkDestroyCommandPool(st.device, st.command_pool, NULL);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyFence(st.device, st.fences[i], NULL);
        vkDestroySemaphore(st.device, st.image_acquired_semaphores[i], NULL);
    }
    for (int i = 0; i < st.swapchain_image_count; i++) {
        vkDestroySemaphore(st.device, st.render_complete_semaphores[i], NULL);
    }
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
