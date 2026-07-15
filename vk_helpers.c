const VkFormat DEPTH_MAP_FORMAT = VK_FORMAT_D32_SFLOAT;

typedef struct VulkanGlobals {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_fam;
    VmaAllocator vma;
    VkCommandPool command_pool;
} VulkanGlobals;

VulkanGlobals vkg;

typedef struct Image {
    VkImage image;
    VmaAllocation alloc;
    VkImageView view;
    VkFormat format;
} Image;

void destroy_image(Image* img) {
    vkDestroyImageView(vkg.device, img->view, NULL);
    vmaDestroyImage(vkg.vma, img->image, img->alloc);
}

typedef struct Texture {
    Image img;
    VkSampler sampler;
    VkDescriptorImageInfo desc_info;
} Texture;

int get_format_pixel_size(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_D32_SFLOAT:
            return 4;
        case VK_FORMAT_R16G16B16_SFLOAT:
            return 6;
        default:
            fatal("get_format_pixel_size: unknown format");
            return 0;
    }
}

void destroy_texture(Texture* tex) {
    destroy_image(&tex->img);
    vkDestroySampler(vkg.device, tex->sampler, NULL);
}

typedef struct VmaAllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation alloc;
    VmaAllocationInfo alloc_info;
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
                                  .apiVersion = VK_API_VERSION_1_4};
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

VkPhysicalDevice choose_physical_device() {
    VkInstance instance = vkg.instance;
    uint32_t dev_count;
    vkEnumeratePhysicalDevices(instance, &dev_count, NULL);
    VkPhysicalDevice *devices = scratch_alloc(sizeof(VkPhysicalDevice) * dev_count);
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
    return chosen_dev;
}

uint32_t choose_queue_family() {
    VkInstance instance = vkg.instance;
    VkPhysicalDevice physical_device = vkg.physical_device;

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count,
                                             NULL);
    VkQueueFamilyProperties *queue_families =
        scratch_alloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count,
                                             queue_families);
    uint32_t chosen_queue_fam = 0;
    for (int i = 0; i < queue_family_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            chosen_queue_fam = i;
            break;
        }
    }

    return chosen_queue_fam;
}

VkDevice create_logical_device(uint32_t extension_count, const char* const * extensions) {
    VkInstance instance = vkg.instance;
    VkPhysicalDevice physical_device = vkg.physical_device;
    uint32_t queue_family = vkg.queue_fam;

    const float queue_fam_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_fam_priority};

     VkPhysicalDeviceVulkan11Features enabled_vk11_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .shaderDrawParameters = true};
    VkPhysicalDeviceVulkan12Features enabled_vk12_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &enabled_vk11_features,
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
    VkPhysicalDeviceVulkan14Features enabled_vk14_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = &enabled_vk13_features,
        .maintenance5 = true};
    VkPhysicalDeviceFeatures enabled_vk10_features = {.samplerAnisotropy = VK_TRUE};

    VkDeviceCreateInfo device_ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled_vk14_features,
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

VmaAllocator create_vma() {
    VmaAllocatorCreateInfo allocator_ci = {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = vkg.physical_device,
        .device = vkg.device,
        .instance = vkg.instance,
        .vulkanApiVersion = VK_API_VERSION_1_4,
    };
    VmaAllocator vma;
    if (vmaCreateAllocator(&allocator_ci, &vma) != VK_SUCCESS) {
        fatal("Failed to create Vulkan Memory Allocator.");
    }
    return vma;
}

VkImage create_vkimage(
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
    if (vmaCreateImage(vkg.vma, &image_ci, &alloc_ci,
                       &image, p_allocation,
                       NULL) != VK_SUCCESS) {
        fatal("Failed to create image.");
    }

    return image;
}

VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect_mask) {
    VkImageViewCreateInfo view_ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {.aspectMask = aspect_mask,
                             .levelCount = 1,
                             .layerCount = 1}};
    VkImageView image_view;
    if (vkCreateImageView(vkg.device, &view_ci, NULL, &image_view) != VK_SUCCESS) {
        fatal("Failed to create image view.");
    }
    return image_view;
}

Image create_image(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect_mask,
        int width, int height, bool cpu_side)
{
    Image image;
    image.format = format;
    image.image = create_vkimage(&image.alloc, format, usage, width, height, cpu_side);
    image.view = create_image_view(image.image, format, aspect_mask);
    return image;
}

VkCommandPool create_command_pool() {
    VkCommandPoolCreateInfo command_pool_ci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vkg.queue_fam,
    };
    VkCommandPool command_pool;
    if (vkCreateCommandPool(vkg.device, &command_pool_ci, NULL,
                            &command_pool) != VK_SUCCESS) {
        fatal("Failed to create command pool.");
    }
    return command_pool;
}

void vkg_init(uint32_t dev_ext_count, const char** dev_extensions,
        uint32_t inst_ext_count, const char** instance_extensions)
{
    Marker mem = scratch_begin();

    vkg.instance = create_instance(inst_ext_count, instance_extensions);
    vkg.physical_device = choose_physical_device();
    vkg.queue_fam = choose_queue_family();
    vkg.device = create_logical_device(dev_ext_count, dev_extensions);
    vkGetDeviceQueue(vkg.device, vkg.queue_fam, 0, &vkg.queue);
    vkg.vma = create_vma();
    vkg.command_pool = create_command_pool();

    scratch_end(mem);
}

void vkg_shutdown() {
    vkDestroyCommandPool(vkg.device, vkg.command_pool, NULL);
    vmaDestroyAllocator(vkg.vma);
    vkDestroyDevice(vkg.device, NULL);
    vkDestroyInstance(vkg.instance, NULL);
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
        image_memory_barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_GENERAL:
        image_memory_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
        break;
    default:
        fatal("Can't handle image layout in set_image_layout.");
        break;
    }

    switch (new_image_layout)
    {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_GENERAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
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

VkFence create_fence(bool signaled) {
    VkFence fence;
    VkFenceCreateInfo fence_ci = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
    };
    chk(vkCreateFence(vkg.device, &fence_ci, NULL, &fence));
    return fence;
}

VkCommandBuffer allocate_command_buffer() {
    VkCommandBufferAllocateInfo cb_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vkg.command_pool,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb;
    if (vkAllocateCommandBuffers(vkg.device, &cb_alloc_info, &cb) != VK_SUCCESS) {
        fatal("Failed to allocate command buffers.");
    }
    return cb;
}

VkCommandBuffer begin_command_buffer(VkCommandBufferUsageFlags flags) {
    VkCommandBuffer cb = allocate_command_buffer();
    chk(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo cb_bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = flags};
    chk(vkBeginCommandBuffer(cb, &cb_bi));
    return cb;
}

void end_one_time_command_buffer(VkCommandBuffer cb) {
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
}

VmaAllocatedBuffer allocate_buffer(VkBufferUsageFlags usage, VmaAllocationCreateFlags flags, size_t size)
{
    VmaAllocatedBuffer buf;
    VkBufferCreateInfo buf_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
    };
    VmaAllocationCreateInfo buf_alloc_ci = {
        .usage = VMA_MEMORY_USAGE_AUTO,
        .flags = flags,
    };
    vmaCreateBuffer(vkg.vma, &buf_ci, &buf_alloc_ci, &buf.buffer, &buf.alloc, &buf.alloc_info);

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo buf_addr_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buf.buffer
        };
        buf.device_address = vkGetBufferDeviceAddress(vkg.device, &buf_addr_info);
    } else {
        buf.device_address = 0;
    }

    return buf;
}

VkFormat find_optimal_tiling_format(int format_count, VkFormat* formats, VkFormatFeatureFlags2 features) {
    VkFormat format = VK_FORMAT_UNDEFINED;
    for (int i = 0; i < format_count; i++) {
        VkFormatProperties2 format_properties = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
        vkGetPhysicalDeviceFormatProperties2(vkg.physical_device, formats[i], &format_properties);
        if (format_properties.formatProperties.optimalTilingFeatures & features) {
            format = formats[i];
            break;
        }
    }
    if (format == VK_FORMAT_UNDEFINED) {
        fatal("Failed to find a suitable depth format.");
    }
    return format;
}

Texture load_binary_texture(const char* filename, VkFormat format, VkImageAspectFlags aspect_mask, int width, int height)
{
    Texture tex;
    Image* img = &tex.img;

    tex.img.format = format;

    int pixel_size = get_format_pixel_size(format);
    int buf_size = pixel_size * width * height;
    VmaAllocatedBuffer staging_buf = allocate_buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT, buf_size);

    char* staging_buf_p = staging_buf.alloc_info.pMappedData;
    FILE* file = fopen(filename, "rb");
    if (!file) {
        fatal("Failed to open texture file.");
    }
    fread(staging_buf_p, buf_size, 1, file);
    fclose(file);

    img->image = create_vkimage(&img->alloc, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            width, height, false);

    VkCommandBuffer cb = begin_command_buffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    transition_image_layout(cb, img->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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
    vkCmdCopyBufferToImage(cb, staging_buf.buffer, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    transition_image_layout(cb, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            aspect_mask, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    end_one_time_command_buffer(cb);

    vmaDestroyBuffer(vkg.vma, staging_buf.buffer, staging_buf.alloc);

    img->view = create_image_view(img->image, format, aspect_mask);

    VkSamplerCreateInfo sampler_ci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = NULL,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .anisotropyEnable = VK_FALSE,
    };
    chk(vkCreateSampler(vkg.device, &sampler_ci, NULL, &tex.sampler));
    tex.desc_info = (VkDescriptorImageInfo) {
        .sampler = tex.sampler,
        .imageView = img->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    return tex;
}
