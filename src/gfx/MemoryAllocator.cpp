#include "gfx/MemoryAllocator.h"
#include <algorithm>

void MemoryAllocator::init(VkDevice deviceHandle, VkPhysicalDevice physicalDeviceHandle) {
    device = deviceHandle;
    physicalDevice = physicalDeviceHandle;
}

MemoryAllocation MemoryAllocator::allocate(VkDeviceSize size, VkDeviceSize alignment, 
                                           VkMemoryPropertyFlags properties, VkMemoryRequirements memReqs) {
    uint32_t memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties);
    
    // Try existing blocks
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].memoryTypeIndex != memoryTypeIndex) continue;
        
        auto alloc = allocateFromBlock(blocks[i], size, alignment);
        if (alloc.memory != VK_NULL_HANDLE) {
            alloc.blockIndex = static_cast<uint32_t>(i);
            return alloc;
        }
    }
    
    // Create new block
    VkDeviceSize blockSize = std::max(size, VkDeviceSize(64 * 1024 * 1024)); // 64MB minimum
    createBlock(blockSize, memoryTypeIndex, properties);
    
    return allocate(size, alignment, properties, memReqs);
}

void MemoryAllocator::deallocate(const MemoryAllocation& alloc) {
    if (alloc.blockIndex >= blocks.size()) return;
    
    MemoryBlock& block = blocks[alloc.blockIndex];
    
    // Add to free list
    FreeRange range{alloc.offset, alloc.size};
    block.freeList.push_back(range);
    
    // Merge adjacent free ranges
    mergeFreeRanges(block.freeList);
    
    block.used -= alloc.size;
}

void* MemoryAllocator::map(const MemoryAllocation& alloc) {
    if (alloc.blockIndex >= blocks.size()) return nullptr;
    
    MemoryBlock& block = blocks[alloc.blockIndex];
    
    // Map the block if not already mapped
    if (block.mappedData == nullptr) {
        if (vkMapMemory(device, block.memory, 0, block.size, 0, &block.mappedData) != VK_SUCCESS) {
            return nullptr;
        }
    }
    
    return static_cast<char*>(block.mappedData) + alloc.offset;
}

void MemoryAllocator::unmap(const MemoryAllocation& alloc) {
    if (alloc.blockIndex >= blocks.size()) return;
    
    MemoryBlock& block = blocks[alloc.blockIndex];
    
    // Note: We DON'T unmap here. Block stays mapped until cleanup.
    // This allows multiple allocations in the same block to be mapped simultaneously.
}

void MemoryAllocator::cleanup() {
    for (auto& block : blocks) {
        if (block.mappedData != nullptr) {
            vkUnmapMemory(device, block.memory);
            block.mappedData = nullptr;
        }
        if (block.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, block.memory, nullptr);
        }
    }
    blocks.clear();
    device = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
}

MemoryAllocation MemoryAllocator::allocateFromBlock(MemoryBlock& block, VkDeviceSize size, VkDeviceSize alignment) {
    for (auto it = block.freeList.begin(); it != block.freeList.end(); ++it) {
        VkDeviceSize alignedOffset = alignUp(it->offset, alignment);
        VkDeviceSize padding = alignedOffset - it->offset;
        
        if (it->size >= size + padding) {
            MemoryAllocation alloc{
                block.memory,
                alignedOffset,
                size
            };
            
            // Adjust free range
            VkDeviceSize remaining = it->size - (size + padding);
            it->offset += size + padding;
            it->size = remaining;
            
            if (it->size == 0) {
                block.freeList.erase(it);
            }
            
            block.used += size;
            return alloc;
        }
    }
    
    return MemoryAllocation{};
}

void MemoryAllocator::createBlock(VkDeviceSize size, uint32_t memoryTypeIndex, VkMemoryPropertyFlags properties) {
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    
    VkDeviceMemory memory;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate memory block");
    }
    
    MemoryBlock block;
    block.memory = memory;
    block.size = size;
    block.used = 0;
    block.memoryTypeIndex = memoryTypeIndex;
    block.freeList.push_back(FreeRange{0, size});
    
    blocks.push_back(block);
}

void MemoryAllocator::mergeFreeRanges(std::vector<FreeRange>& freeList) {
    if (freeList.size() < 2) return;
    
    std::sort(freeList.begin(), freeList.end(), 
        [](const FreeRange& a, const FreeRange& b) { return a.offset < b.offset; });
    
    std::vector<FreeRange> merged;
    merged.push_back(freeList[0]);
    
    for (size_t i = 1; i < freeList.size(); ++i) {
        FreeRange& last = merged.back();
        const FreeRange& current = freeList[i];
        
        if (last.offset + last.size == current.offset) {
            last.size += current.size;
        } else {
            merged.push_back(current);
        }
    }
    
    freeList = std::move(merged);
}

VkDeviceSize MemoryAllocator::alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t MemoryAllocator::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    throw std::runtime_error("failed to find suitable memory type");
}
