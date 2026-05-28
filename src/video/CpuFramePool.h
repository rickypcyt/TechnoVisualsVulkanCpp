#pragma once
#include <vector>
#include <cstdint>
#include <cassert>
#include <iostream>
extern "C" {
#include <libavutil/imgutils.h>
}
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
        size_t newSize = 0;
        uint32_t stride = 0;

        if (srcStride == 0) {
            // Compact layout: use FFmpeg's exact aligned buffer size (align=16)
            // to match what sws_scale / grabFrameInto expects.
            // +256 padding because sws_scale may write past the last row for SIMD.
            newSize = static_cast<size_t>(av_image_get_buffer_size(AV_PIX_FMT_RGBA, w, h, 16)) + 256;
            stride = static_cast<uint32_t>(av_image_get_linesize(AV_PIX_FMT_RGBA, w, 16));
        } else {
            stride = srcStride;
            if (stride < w * 4) {
                std::cerr << "[CpuFramePool] Invalid stride: " << stride
                          << " < " << (w * 4) << " for " << w << "x" << h
                          << ", using compact layout" << std::endl;
                stride = w * 4;
            }
            newSize = static_cast<size_t>(stride) * h + 256;
        }

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
