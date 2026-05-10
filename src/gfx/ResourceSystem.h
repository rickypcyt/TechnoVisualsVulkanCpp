#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>
#include "gfx/MemoryAllocator.h"

enum class ResourceType {
    Unknown,
    Buffer,
    Image
};

struct ResourceHandle {
    ResourceType type = ResourceType::Unknown;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    MemoryAllocation allocation;

    ResourceHandle(const ResourceHandle&) = delete;
    ResourceHandle& operator=(const ResourceHandle&) = delete;

    ResourceHandle(ResourceHandle&& other) noexcept
        : type(other.type)
        , buffer(other.buffer)
        , memory(other.memory)
        , image(other.image)
        , allocation(other.allocation)
    {
        other.type = ResourceType::Unknown;
        other.buffer = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.image = VK_NULL_HANDLE;
        other.allocation = {};
    }

    ResourceHandle& operator=(ResourceHandle&& other) noexcept {
        if (this != &other) {
            type = other.type;
            buffer = other.buffer;
            memory = other.memory;
            image = other.image;
            allocation = other.allocation;

            other.type = ResourceType::Unknown;
            other.buffer = VK_NULL_HANDLE;
            other.memory = VK_NULL_HANDLE;
            other.image = VK_NULL_HANDLE;
            other.allocation = {};
        }
        return *this;
    }

    ResourceHandle() = default;
};

// ResourceSystem - manages Vulkan buffers and images with automatic memory allocation
class ResourceSystem {
public:
    void init(VkDevice deviceHandle, VkPhysicalDevice physicalDeviceHandle);
    
    ResourceHandle createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    ResourceHandle createImage(uint32_t width, uint32_t height, VkFormat format,
                               VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties);
    void* map(const ResourceHandle& handle);
    void unmap(const ResourceHandle& handle);
    void destroy(ResourceHandle& handle);
    void cleanup();

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    MemoryAllocator allocator;
};
