#include "gfx/ResourceSystem.h"
#include <unordered_set>

void ResourceSystem::init(VkDevice deviceHandle, VkPhysicalDevice physicalDeviceHandle) {
    device = deviceHandle;
    physicalDevice = physicalDeviceHandle;
    allocator.init(deviceHandle, physicalDeviceHandle);
}

ResourceHandle ResourceSystem::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    ResourceHandle handle;
    handle.type = ResourceType::Buffer;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &handle.buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, handle.buffer, &memRequirements);

    handle.allocation = allocator.allocate(memRequirements.size, memRequirements.alignment, properties, memRequirements);
    handle.memory = handle.allocation.memory;

    // Validate that allocated memory has required properties
    validateMemoryProperties(properties, handle.allocation.properties, "buffer");

    // Validate binding offset
    if (handle.allocation.offset % memRequirements.alignment != 0) {
        std::cerr << "Error: Buffer memory offset not aligned to requirements" << std::endl;
        throw std::runtime_error("buffer memory alignment error");
    }

    if (vkBindBufferMemory(device, handle.buffer, handle.allocation.memory, handle.allocation.offset) != VK_SUCCESS) {
        throw std::runtime_error("failed to bind buffer memory");
    }
    
    trackResource(handle);
    return handle;
}

ResourceHandle ResourceSystem::createImage(uint32_t width, uint32_t height, VkFormat format,
                                          VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties) {
    ResourceHandle handle;
    handle.type = ResourceType::Image;

    // Validate format and usage compatibility
    validateImageFormatUsage(format, usage);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &handle.image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, handle.image, &memRequirements);

    handle.allocation = allocator.allocate(memRequirements.size, memRequirements.alignment, properties, memRequirements);
    handle.memory = handle.allocation.memory;

    // Validate that allocated memory has required properties
    validateMemoryProperties(properties, handle.allocation.properties, "image");

    // Validate binding offset
    if (handle.allocation.offset % memRequirements.alignment != 0) {
        std::cerr << "Error: Image memory offset not aligned to requirements" << std::endl;
        throw std::runtime_error("image memory alignment error");
    }

    if (vkBindImageMemory(device, handle.image, handle.allocation.memory, handle.allocation.offset) != VK_SUCCESS) {
        throw std::runtime_error("failed to bind image memory");
    }
    
    trackResource(handle);
    return handle;
}

void* ResourceSystem::map(const ResourceHandle& handle) {
    if (handle.type == ResourceType::Unknown) {
        std::cerr << "Error: Attempting to map unknown resource type" << std::endl;
        return nullptr;
    }
    
    // Validate that memory is host-visible before attempting to map
    if (!(handle.allocation.properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        std::cerr << "Error: Attempting to map non-host-visible memory (device-local)" << std::endl;
        std::cerr << "This requires a staging buffer for transfer" << std::endl;
        return nullptr;
    }
    
    return allocator.map(handle.allocation);
}

void ResourceSystem::unmap(const ResourceHandle& handle) {
    if (handle.type == ResourceType::Unknown) {
        std::cerr << "Error: Attempting to unmap unknown resource type" << std::endl;
        return;
    }
    
    // Note: If memory is non-coherent, caller must flush before unmap
    // This is a known limitation - consider adding explicit flush API
    allocator.unmap(handle.allocation);
}

void ResourceSystem::destroy(ResourceHandle& handle) {
    if (handle.type == ResourceType::Buffer) {
        if (handle.buffer != VK_NULL_HANDLE) {
            untrackResource(handle);
            vkDestroyBuffer(device, handle.buffer, nullptr);
            handle.buffer = VK_NULL_HANDLE;
        }
        allocator.deallocate(handle.allocation);
        handle.memory = VK_NULL_HANDLE;
    } else if (handle.type == ResourceType::Image) {
        if (handle.image != VK_NULL_HANDLE) {
            untrackResource(handle);
            vkDestroyImage(device, handle.image, nullptr);
            handle.image = VK_NULL_HANDLE;
        }
        allocator.deallocate(handle.allocation);
        handle.memory = VK_NULL_HANDLE;
    }
    handle.type = ResourceType::Unknown;
    handle.allocation = MemoryAllocation{};
}

void ResourceSystem::cleanup() {
    // Warn if resources are still alive (should be destroyed before cleanup)
    if (!trackedBuffers.empty() || !trackedImages.empty()) {
        std::cerr << "Warning: ResourceSystem::cleanup called with " << trackedBuffers.size() 
                  << " buffers and " << trackedImages.size() << " images still alive" << std::endl;
        std::cerr << "This may cause undefined behavior - resources should be destroyed before cleanup" << std::endl;
    }
    
    // Clear tracking sets
    trackedBuffers.clear();
    trackedImages.clear();
    
    // Cleanup allocator last (after all resources are destroyed)
    allocator.cleanup();
    device = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
}

void ResourceSystem::validateMemoryProperties(VkMemoryPropertyFlags required, VkMemoryPropertyFlags actual, const char* resourceType) {
    // Check if all required flags are present in actual properties
    if ((required & actual) != required) {
        std::cerr << "Error: Memory property mismatch for " << resourceType << std::endl;
        std::cerr << "Required: 0x" << std::hex << required << std::dec << std::endl;
        std::cerr << "Actual: 0x" << std::hex << actual << std::dec << std::endl;
        throw std::runtime_error("memory property validation failed");
    }
}

void ResourceSystem::validateImageFormatUsage(VkFormat format, VkImageUsageFlags usage) {
    // Basic validation for common format/usage combinations
    // In a full implementation, this would query VkPhysicalDeviceFormatProperties
    
    // Check for color attachment usage with depth formats
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        if (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT || 
            format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
            format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            std::cerr << "Warning: Depth format used as color attachment" << std::endl;
        }
    }
    
    // Check for depth/stencil usage with color formats
    if (usage & (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
        if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ||
            format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB) {
            std::cerr << "Warning: Color format used as depth/stencil attachment" << std::endl;
        }
    }
}

void ResourceSystem::trackResource(const ResourceHandle& handle) {
    if (handle.type == ResourceType::Buffer && handle.buffer != VK_NULL_HANDLE) {
        trackedBuffers.insert(handle.buffer);
    } else if (handle.type == ResourceType::Image && handle.image != VK_NULL_HANDLE) {
        trackedImages.insert(handle.image);
    }
}

void ResourceSystem::untrackResource(const ResourceHandle& handle) {
    if (handle.type == ResourceType::Buffer && handle.buffer != VK_NULL_HANDLE) {
        trackedBuffers.erase(handle.buffer);
    } else if (handle.type == ResourceType::Image && handle.image != VK_NULL_HANDLE) {
        trackedImages.erase(handle.image);
    }
}
