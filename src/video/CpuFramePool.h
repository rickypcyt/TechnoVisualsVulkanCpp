#pragma once
#include <vector>
#include <cstdint>
#include <cassert>
#include "FrameLayout.h"

// CPU Frame Pool - resize-safe dynamic frame storage
// Isolates FFmpeg decode output from GPU staging buffers
struct CpuFrame {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;  // Actual stride from FFmpeg
    size_t size = 0;
    std::vector<uint8_t> data;
};

class CpuFramePool {
public:
    explicit CpuFramePool(size_t poolSize = 4) 
        : frames(poolSize), writeIndex(0) {}

    // Resize all frames to new resolution with stride validation
    // This is the ONLY place where frame size changes
    void resize(uint32_t w, uint32_t h, uint32_t srcStride = 0) {
        // If no stride provided, assume compact layout
        uint32_t stride = (srcStride == 0) ? w * 4 : srcStride;
        
        // Validate stride is sufficient
        if (stride < w * 4) {
            std::cerr << "[CpuFramePool] Invalid stride: " << stride 
                      << " < " << (w * 4) << " for " << w << "x" << h 
                      << ", using compact layout" << std::endl;
            stride = w * 4;
        }

        // Add 64 bytes of padding for SIMD alignment safety (sws_scale may
        // write slightly past the end of each line for SSE/AVX alignment)
        size_t newSize = static_cast<size_t>(stride) * h + 64;

        for (auto& f : frames) {
            f.width = w;
            f.height = h;
            f.stride = stride;
            f.size = newSize;
            f.data.resize(newSize);
        }

        currentWidth = w;
        currentHeight = h;
        currentStride = stride;
    }

    // Get next writable frame
    CpuFrame& acquireWriteFrame() {
        CpuFrame& frame = frames[writeIndex];
        writeIndex = (writeIndex + 1) % frames.size();
        return frame;
    }

    // Get frame by index (for debugging/validation)
    const CpuFrame& getFrame(size_t index) const {
        assert(index < frames.size());
        return frames[index];
    }

    uint32_t getCurrentWidth() const { return currentWidth; }
    uint32_t getCurrentHeight() const { return currentHeight; }
    uint32_t getCurrentStride() const { return currentStride; }

    size_t getPoolSize() const { return frames.size(); }

private:
    std::vector<CpuFrame> frames;
    size_t writeIndex;
    uint32_t currentWidth = 0;
    uint32_t currentHeight = 0;
    uint32_t currentStride = 0;
};
