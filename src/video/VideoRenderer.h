#pragma once

#include <cstdint>
#include <thread>
#include <atomic>
#include <vector>
#include "../video/VideoPlayer.h"
#include "../video/VideoTexture.h"
#include "../video/CpuFramePool.h"

class VideoRenderer {
public:
    VideoRenderer(VideoPlayer& player, VideoTexture& texture, CpuFramePool& cpuPool);
    ~VideoRenderer();

    void update(float deltaTime, uint32_t frameIndex);
    void reset();
    void resize(uint32_t width, uint32_t height);

private:
    VideoPlayer& videoPlayer;
    VideoTexture& videoTexture;
    CpuFramePool& cpuFramePool;
    double accumulatedTime = 0.0f;
    bool firstFrame = true;

    // ── Async decode thread ───────────────────────────────────────────
    struct RingSlot {
        std::vector<uint8_t> data;
        std::atomic<bool> ready{false};
        RingSlot() = default;
        RingSlot(RingSlot&&) = delete;
        RingSlot(const RingSlot&) = delete;
        RingSlot& operator=(RingSlot&&) = delete;
        RingSlot& operator=(const RingSlot&) = delete;
    };
    static constexpr size_t RING_SIZE = 4;
    std::array<RingSlot, RING_SIZE> ringBuffer{};
    std::atomic<size_t> ringWriteIdx{0};
    std::atomic<size_t> ringReadIdx{0};

    std::thread decoderThread;
    std::atomic<bool> decoderRunning{false};

    void startDecoder();
    void stopDecoder();
    void decoderLoop();
};
