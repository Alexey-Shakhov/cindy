typedef struct Texture {
    VmaAllocation alloc;
    VkImage image;
    VkImageView view;
    VkSampler sampler;
} Texture;

typedef struct VmaAllocatedBuffer {
    VmaAllocation alloc;
    VmaAllocationInfo alloc_info;
    VkBuffer buffer;
    VkDeviceAddress device_address;
} VmaAllocatedBuffer;

static inline void chk(VkResult result) {
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Vulkan error. Code: %s.\n", string_VkResult(result));
        exit(EXIT_FAILURE);
    }
}

VkInstance create_instance(uint32_t extension_count, const char* const * extensions) {
    VkApplicationInfo app_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                  .pApplicationName = "Cindy",
                                  .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo instance_ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
    };
    VkInstance instance;
    if (vkCreateInstance(&instance_ci, NULL, &instance) != VK_SUCCESS) {
        fatal("Failed to create Vulkan instance.");
    }
    return instance;
}

VkPhysicalDevice choose_physical_device(VkInstance instance) {
    uint32_t dev_count;
    vkEnumeratePhysicalDevices(instance, &dev_count, NULL);
    VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * dev_count);
    vkEnumeratePhysicalDevices(instance, &dev_count, devices);
    VkPhysicalDevice chosen_dev = VK_NULL_HANDLE;
    for (int i = 0; i < dev_count; i++) {
        VkPhysicalDeviceProperties2 device_props = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        vkGetPhysicalDeviceProperties2(devices[i], &device_props);
        if (device_props.properties.deviceType ==
            VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            chosen_dev = devices[i];
        }
    }
    if (chosen_dev == VK_NULL_HANDLE) {
        chosen_dev = devices[0];
    }
    free(devices);
    return chosen_dev;
}

uint32_t choose_queue_family(VkInstance instance, VkPhysicalDevice physical_device) {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count,
                                             NULL);
    VkQueueFamilyProperties *queue_families =
        malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count,
                                             queue_families);
    uint32_t chosen_queue_fam = 0;
    for (int i = 0; i < queue_family_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            chosen_queue_fam = i;
            break;
        }
    }
    free(queue_families);

    return chosen_queue_fam;
}

VkDevice create_logical_device(VkInstance instance, VkPhysicalDevice physical_device, uint32_t queue_family,
        uint32_t extension_count, const char* const * extensions) {
    const float queue_fam_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_fam_priority};
    VkPhysicalDeviceVulkan12Features enabled_vk12_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .descriptorIndexing = true,
        .shaderSampledImageArrayNonUniformIndexing = true,
        .descriptorBindingVariableDescriptorCount = true,
        .runtimeDescriptorArray = true,
        .bufferDeviceAddress = true};
    VkPhysicalDeviceVulkan13Features enabled_vk13_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &enabled_vk12_features,
        .synchronization2 = true,
        .dynamicRendering = true};
    VkPhysicalDeviceFeatures enabled_vk10_features = {.samplerAnisotropy =
                                                          VK_TRUE};

    VkDeviceCreateInfo device_ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled_vk13_features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_ci,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
        .pEnabledFeatures = &enabled_vk10_features};
    VkDevice device;
    VkResult vk_result = vkCreateDevice(physical_device, &device_ci, NULL, &device);
    if (vk_result != VK_SUCCESS) {
        fatal("Failed to create logical device.");
    }

    return device;
}

VmaAllocator create_vma(VkPhysicalDevice physical_device, VkDevice device, VkInstance instance) {
    VmaAllocatorCreateInfo allocator_ci = {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = physical_device,
        .device = device,
        .instance = instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    VmaAllocator vma;
    if (vmaCreateAllocator(&allocator_ci, &vma) != VK_SUCCESS) {
        fatal("Failed to create Vulkan Memory Allocator.");
    }
    return vma;
}

VkImage create_image(
        VmaAllocator vma,
        VmaAllocation* p_allocation,
        VkFormat format,
        VkImageUsageFlags usage,
        int width,
        int height,
        bool cpu_side)
{
    VkImageTiling tiling = cpu_side ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
    VkImageCreateInfo image_ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = (uint32_t)width,
                   .height = (uint32_t)height,
                   .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = tiling,
        .usage = usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateFlags flags = cpu_side ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        | VMA_ALLOCATION_CREATE_MAPPED_BIT : VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    VmaAllocationCreateInfo alloc_ci = {
        .flags = flags,
        .usage = VMA_MEMORY_USAGE_AUTO};
    VkImage image;
    if (vmaCreateImage(vma, &image_ci, &alloc_ci,
                       &image, p_allocation,
                       NULL) != VK_SUCCESS) {
        fatal("Failed to create image.");
    }

    return image;
}

VkImageView create_image_view(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect_mask) {
    VkImageViewCreateInfo view_ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {.aspectMask = aspect_mask,
                             .levelCount = 1,
                             .layerCount = 1}};
    VkImageView image_view;
    if (vkCreateImageView(device, &view_ci, NULL, &image_view) != VK_SUCCESS) {
        fatal("Failed to create image view.");
    }
    return image_view;
}

VkCommandPool create_command_pool(VkDevice device, uint32_t queue_fam) {
    VkCommandPoolCreateInfo command_pool_ci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_fam,
    };
    VkCommandPool command_pool;
    if (vkCreateCommandPool(device, &command_pool_ci, NULL,
                            &command_pool) != VK_SUCCESS) {
        fatal("Failed to create command pool.");
    }
    return command_pool;
}

// Copied and adapted from Sascha Willems' Vulkan Examples
void transition_image_layout(
    VkCommandBuffer cmdbuffer,
    VkImage image,
    VkImageLayout old_image_layout,
    VkImageLayout new_image_layout,
    VkImageAspectFlags aspect_mask,
    VkPipelineStageFlags src_stage_mask,
    VkPipelineStageFlags dst_stage_mask)
{
    VkImageMemoryBarrier2 image_memory_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .oldLayout = old_image_layout,
        .newLayout = new_image_layout,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspect_mask,
            .levelCount = 1,
            .layerCount = 1
        },
        .srcStageMask = src_stage_mask,
        .dstStageMask = dst_stage_mask,
    };
    switch (old_image_layout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        image_memory_barrier.srcAccessMask = 0;
        break;
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        image_memory_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_GENERAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        break;
    default:
        fatal("Can't handle image layout in set_image_layout.");
        break;
    }

    switch (new_image_layout)
    {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_GENERAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        break;
    default:
        fatal("Can't handle image layout in set_image_layout.");
        break;
    }

    VkDependencyInfo barrier_dependency_info = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &image_memory_barrier
    };
    vkCmdPipelineBarrier2(cmdbuffer, &barrier_dependency_info);
}

VkImageCopy create_image_copy(VkImageAspectFlags aspect_mask, uint32_t width, uint32_t height) {
    VkImageCopy copy = {
        .srcSubresource.aspectMask = aspect_mask,
        .srcSubresource.layerCount = 1,
        .dstSubresource.aspectMask = aspect_mask,
        .dstSubresource.layerCount = 1,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
    };
    return copy;
}

VkFence create_fence(VkDevice device) {
    VkFence fence;
    VkFenceCreateInfo fence_ci = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    chk(vkCreateFence(device, &fence_ci, NULL, &fence));
    return fence;
}

VkCommandBuffer allocate_command_buffer(VkDevice device, VkCommandPool command_pool) {
    VkCommandBufferAllocateInfo cb_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb;
    if (vkAllocateCommandBuffers(device, &cb_alloc_info, &cb) != VK_SUCCESS) {
        fatal("Failed to allocate command buffers.");
    }
    return cb;
}
