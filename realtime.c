#include <string.h>
#include <math.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vk_enum_string_helper.h>
#include "vk_mem_alloc.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "cglm/cglm.h"
#include "cglm/struct.h"

#include "utils.c"
#include "vk_helpers.c"

#define MAX_FRAMES_IN_FLIGHT 2
const VkFormat SWAPCHAIN_IMAGE_FORMAT = VK_FORMAT_B8G8R8A8_SRGB;

const VkFormat COLOR_MAP_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;
const VkFormat NORMAL_MAP_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;

#define MAP_WIDTH 1920
#define MAP_HEIGHT 1080

struct State {
    GLFWwindow *window;
    VkSurfaceKHR surface;

    int window_w;
    int window_h;

    VkSwapchainKHR swapchain;
    VkExtent2D swapchain_extent;
    uint32_t swapchain_image_count;
    VkImage* swapchain_images;
    VkImageView *swapchain_image_views;

    Image depth_att;

    Texture color_map;
    Texture depth_map;
    Texture normal_map;

    VkSemaphore image_acquired_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence fences[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore *render_complete_semaphores;
    uint32_t frame_index;
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
        (*p_image_views)[i] = create_image_view((*p_images)[i], SWAPCHAIN_IMAGE_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    vkDestroySwapchainKHR(device, swapchain_ci.oldSwapchain, NULL);
    return swapchain;
}

int main() {
    // TODO create state_defaults_init function
    st.cam_pos[0] = 0.0f;
    st.cam_pos[1] = 0.0f;
    st.cam_pos[2] = -6.0f;

    st.window = create_window();
    glfwGetWindowSize(st.window, &st.window_w, &st.window_h);

    const char* dev_extensions[1] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    uint32_t ext_count;
    const char** extensions = glfwGetRequiredInstanceExtensions(&ext_count);
    vkg_init(1, dev_extensions, ext_count, extensions);

    if (glfwCreateWindowSurface(vkg.instance, st.window, NULL, &st.surface) != VK_SUCCESS) {
        fatal("Failed to create window surface.");
    }

    st.swapchain = create_swapchain_with_views(
            vkg.physical_device,
            vkg.device,
            st.surface,
            st.window_w,
            st.window_h,
            &st.swapchain_image_count,
            &st.swapchain_images,
            &st.swapchain_image_views,
            &st.swapchain_extent,
            VK_NULL_HANDLE
    );

    st.depth_att = create_image(DEPTH_MAP_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, st.window_w, st.window_h, false);

    VkSemaphoreCreateInfo semaphore_ci = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                  .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        st.fences[i] = create_fence(true);
        if (vkCreateSemaphore(vkg.device, &semaphore_ci, NULL,
                              &st.image_acquired_semaphores[i]) != VK_SUCCESS) {
            fatal("Failed to create semaphore for image acquisition.");
        }
    }

    st.render_complete_semaphores = malloc(sizeof(VkSemaphore) * st.swapchain_image_count);
    for (int i = 0; i < st.swapchain_image_count; i++) {
        if (vkCreateSemaphore(vkg.device, &semaphore_ci, NULL,
                      &st.render_complete_semaphores[i]) != VK_SUCCESS) {
            fatal("Failed to create semaphore for rendering completion.");
        }
    }

    VkCommandBufferAllocateInfo cb_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vkg.command_pool,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    if (vkAllocateCommandBuffers(vkg.device, &cb_alloc_info,
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
    chk(vkCreateShaderModule(vkg.device, &shader_module_ci, NULL, &shader_module));

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
    vkCreateDescriptorSetLayout(vkg.device, &desc_set_layout_ci, NULL, &st.desc_set_layout);

    VkPipelineLayoutCreateInfo pipeline_layout_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &st.desc_set_layout,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = NULL};
    chk(vkCreatePipelineLayout(vkg.device, &pipeline_layout_ci, NULL,
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
        .depthAttachmentFormat = st.depth_att.format};
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
                vkg.device, VK_NULL_HANDLE, 1, &pipeline_ci, NULL, &st.pipeline));

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
	chk(vkCreateDescriptorPool(vkg.device, &desc_pool_ci, NULL, &st.descriptor_pool));

    VkDescriptorSetAllocateInfo desc_set_alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = NULL,
        .descriptorPool = st.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &st.desc_set_layout
    };
	chk(vkAllocateDescriptorSets(vkg.device, &desc_set_alloc, &st.desc_set));

    st.color_map = load_binary_texture("offline-output/color.bin", COLOR_MAP_FORMAT,
            VK_IMAGE_ASPECT_COLOR_BIT, MAP_WIDTH, MAP_HEIGHT);
    st.normal_map = load_binary_texture("offline-output/normal.bin", NORMAL_MAP_FORMAT,
            VK_IMAGE_ASPECT_COLOR_BIT, MAP_WIDTH, MAP_HEIGHT);
    st.depth_map = load_binary_texture("offline-output/depth.bin", DEPTH_MAP_FORMAT,
            VK_IMAGE_ASPECT_DEPTH_BIT, MAP_WIDTH, MAP_HEIGHT);

    VkDescriptorImageInfo desc_infos[3] = {
        st.color_map.desc_info,
        st.normal_map.desc_info,
        st.depth_map.desc_info,
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
	vkUpdateDescriptorSets(vkg.device, 1, &write_desc_set, 0, NULL);

    while (!glfwWindowShouldClose(st.window)) {
        glfwPollEvents();

        chk(vkWaitForFences(vkg.device, 1, &st.fences[st.frame_index], true, UINT64_MAX));
        chk(vkResetFences(vkg.device, 1, &st.fences[st.frame_index]));
        chkSwapchain(vkAcquireNextImageKHR(vkg.device, st.swapchain, UINT64_MAX,
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
                .image = st.depth_att.image,
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
            .imageView = st.depth_att.view,
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
        chk(vkQueueSubmit(vkg.queue, 1, &submitInfo, st.fences[st.frame_index]));
        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &st.render_complete_semaphores[st.image_index],
            .swapchainCount = 1,
            .pSwapchains = &st.swapchain,
            .pImageIndices = &st.image_index};

        chkSwapchain(vkQueuePresentKHR(vkg.queue, &presentInfo));
        if (st.update_swapchain) {
            st.update_swapchain = false;
            glfwGetFramebufferSize(st.window, &st.window_w, &st.window_h);
            chk(vkDeviceWaitIdle(vkg.device));
            
            for (int i = 0; i < st.swapchain_image_count; i++) {
                vkDestroyImageView(vkg.device, st.swapchain_image_views[i], NULL);
            }
            free(st.swapchain_image_views);
            free(st.swapchain_images);
            st.swapchain = create_swapchain_with_views(
                    vkg.physical_device,
                    vkg.device,
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
                vkDestroySemaphore(vkg.device, st.render_complete_semaphores[i], NULL);
            }
            st.render_complete_semaphores = malloc(sizeof(VkSemaphore) * st.swapchain_image_count);
            for (int i=0; i < st.swapchain_image_count; i++) {
                chk(vkCreateSemaphore(vkg.device, &semaphore_ci, NULL, &st.render_complete_semaphores[i]));
            }
        }

        st.frame_index = (st.frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    chk(vkDeviceWaitIdle(vkg.device));

    vkDestroyPipeline(vkg.device, st.pipeline, NULL);
    vkDestroyPipelineLayout(vkg.device, st.pipeline_layout, NULL);
    vkDestroyShaderModule(vkg.device, shader_module, NULL);
    vkDestroyDescriptorPool(vkg.device, st.descriptor_pool, NULL);
    vkDestroyDescriptorSetLayout(vkg.device, st.desc_set_layout, NULL);

    destroy_image(&st.depth_att);

    destroy_texture(&st.color_map);
    destroy_texture(&st.normal_map);
    destroy_texture(&st.depth_map);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyFence(vkg.device, st.fences[i], NULL);
        vkDestroySemaphore(vkg.device, st.image_acquired_semaphores[i], NULL);
    }
    for (int i = 0; i < st.swapchain_image_count; i++) {
        vkDestroySemaphore(vkg.device, st.render_complete_semaphores[i], NULL);
    }
    for (int i = 0; i < st.swapchain_image_count; i++) {
        vkDestroyImageView(vkg.device, st.swapchain_image_views[i], NULL);
    }

    vkDestroySwapchainKHR(vkg.device, st.swapchain, NULL);
    vkDestroySurfaceKHR(vkg.instance, st.surface, NULL);

    vkg_shutdown();

    glfwDestroyWindow(st.window);
    glfwTerminate();

    return 0;
}
