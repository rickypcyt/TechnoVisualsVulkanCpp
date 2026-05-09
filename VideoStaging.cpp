#include "VideoStaging.h"
#include <cstring>
#include <cstdlib>

bool VideoStaging::init(uint32_t slotCount, size_t slotSize) {
    m_slotCount = slotCount;
    m_slotSize = slotSize;
    m_epoch = 1;

    m_slots = static_cast<VideoStagingSlot*>(std::calloc(slotCount, sizeof(VideoStagingSlot)));
    if (!m_slots) {
        return false;
    }

    return true;
}

void VideoStaging::destroy() {
    if (m_slots) {
        std::free(m_slots);
        m_slots = nullptr;
    }
    m_slotCount = 0;
    m_slotSize = 0;
    m_epoch = 0;
}

VideoStagingWriteResult VideoStaging::writeVideoStagingSlot(
    uint32_t slot,
    const void* data,
    size_t size,
    uint64_t epoch
) {
    if (epoch != m_epoch) {
        return { false };
    }

    if (slot >= m_slotCount) {
        return { false };
    }

    if (size > m_slotSize) {
        return { false };
    }

    void* dst = m_slots[slot].mapped;
    if (!dst) {
        return { false };
    }

    std::memcpy(dst, data, size);
    return { true };
}

const VideoStagingSlot& VideoStaging::getSlot(uint32_t slot) const {
    if (slot >= m_slotCount) {
        static VideoStagingSlot emptySlot;
        return emptySlot;
    }
    return m_slots[slot];
}

void VideoStaging::setSlot(uint32_t slot, VkBuffer buffer, VkDeviceMemory memory, void* mapped, size_t capacity) {
    if (slot >= m_slotCount) {
        return;
    }
    m_slots[slot].buffer = buffer;
    m_slots[slot].memory = memory;
    m_slots[slot].mapped = mapped;
    m_slots[slot].capacity = capacity;
}

void VideoStaging::clearSlot(uint32_t slot) {
    if (slot >= m_slotCount) {
        return;
    }
    m_slots[slot] = {};
}
