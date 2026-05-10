#include "gfx/ResourceSystem.h"

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

    vkBindBufferMemory(device, handle.buffer, handle.allocation.memory, handle.allocation.offset);
    return handle;
}

ResourceHandle ResourceSystem::createImage(uint32_t width, uint32_t height, VkFormat format,
                                          VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties) {
    ResourceHandle handle;
    handle.type = ResourceType::Image;

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

    vkBindImageMemory(device, handle.image, handle.allocation.memory, handle.allocation.offset);
    return handle;
}

void* ResourceSystem::map(const ResourceHandle& handle) {
    if (handle.type == ResourceType::Unknown) return nullptr;
    return allocator.map(handle.allocation);
}

void ResourceSystem::unmap(const ResourceHandle& handle) {
    if (handle.type == ResourceType::Unknown) return;
    allocator.unmap(handle.allocation);
}

void ResourceSystem::destroy(ResourceHandle& handle) {
    if (handle.type == ResourceType::Buffer) {
        if (handle.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, handle.buffer, nullptr);
            handle.buffer = VK_NULL_HANDLE;
        }
        allocator.deallocate(handle.allocation);
        handle.memory = VK_NULL_HANDLE;
    } else if (handle.type == ResourceType::Image) {
        if (handle.image != VK_NULL_HANDLE) {
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
    allocator.cleanup();
    device = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
}
