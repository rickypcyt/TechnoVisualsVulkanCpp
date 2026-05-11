#pragma once
#include <cstdint>
#include <cassert>
#include <iostream>
#include <cstring>

// FrameLayout - stride-aware frame layout abstraction
// Captures the actual memory layout of video frames including padding/stride
struct FrameLayout {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;  // bytes per line (linesize[0] from FFmpeg)

    // Default constructor
    FrameLayout() = default;

    // Constructor from dimensions (assumes compact layout)
    FrameLayout(uint32_t w, uint32_t h)
        : width(w), height(h), stride(w * 4) {}

    // Constructor with explicit stride
    FrameLayout(uint32_t w, uint32_t h, uint32_t s)
        : width(w), height(h), stride(s) {
        assert(s >= w * 4 && "Stride must be at least width * 4 for RGBA");
    }

    // Calculate total size including stride padding
    size_t totalSize() const {
        return static_cast<size_t>(stride) * height;
    }

    // Calculate compact size (without padding)
    size_t compactSize() const {
        return static_cast<size_t>(width) * height * 4;
    }

    // Check if layout is compact (no padding)
    bool isCompact() const {
        return stride == width * 4;
    }

    // Validate that stride is sufficient for RGBA
    bool isValid() const {
        return stride >= width * 4;
    }

    // Create from FFmpeg frame properties
    static FrameLayout fromFFmpeg(int w, int h, int linesize) {
        return FrameLayout(
            static_cast<uint32_t>(w),
            static_cast<uint32_t>(h),
            static_cast<uint32_t>(linesize)
        );
    }

    // Print layout info for debugging
    void print(const char* label = "FrameLayout") const {
        std::cout << "[" << label << "] "
                  << width << "x" << height
                  << " stride=" << stride
                  << " total=" << totalSize()
                  << " compact=" << compactSize()
                  << " padded=" << (isCompact() ? "no" : "yes")
                  << std::endl;
    }
};

// Helper function to copy frame data with stride awareness
// Copies from source with srcStride to destination with dstStride
inline void copyFrameWithStride(
    uint8_t* dst, uint32_t dstStride,
    const uint8_t* src, uint32_t srcStride,
    uint32_t width, uint32_t height)
{
    // Copy line by line to respect stride differences
    const uint32_t lineBytes = width * 4;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* srcLine = src + static_cast<size_t>(y) * srcStride;
        uint8_t* dstLine = dst + static_cast<size_t>(y) * dstStride;
        std::memcpy(dstLine, srcLine, lineBytes);
    }
}
