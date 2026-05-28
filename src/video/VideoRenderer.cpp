#include "VideoRenderer.h"
#include <iostream>
#include <cstring>
#include <chrono>
extern "C" {
#include <libavutil/imgutils.h>
}

VideoRenderer::VideoRenderer(VideoPlayer& player, VideoTexture& texture, CpuFramePool& cpuPool)
    : videoPlayer(player)
    , videoTexture(texture)
    , cpuFramePool(cpuPool)
{
    // Pre-size ring buffers if video is ready so decoderLoop never has to resize
    if (videoPlayer.isReady()) {
        int w = videoPlayer.width();
        int h = videoPlayer.height();
        size_t needed = static_cast<size_t>(av_image_get_buffer_size(AV_PIX_FMT_RGBA, w, h, 16)) + 256;
        for (auto& slot : ringBuffer) {
            slot.data.resize(needed);
        }
    }
    startDecoder();
}

VideoRenderer::~VideoRenderer() {
    stopDecoder();
}

void VideoRenderer::startDecoder() {
    if (decoderRunning.exchange(true)) {
        return; // already running
    }
    decoderThread = std::thread(&VideoRenderer::decoderLoop, this);
}

void VideoRenderer::stopDecoder() {
    if (!decoderRunning.exchange(false)) {
        return; // already stopped
    }
    if (decoderThread.joinable()) {
        decoderThread.join();
    }
}

void VideoRenderer::decoderLoop() {
    while (decoderRunning.load(std::memory_order_relaxed)) {
        if (!videoPlayer.isReady()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        // Back-pressure: sleep if ring buffer is full
        size_t wIdx = ringWriteIdx.load(std::memory_order_relaxed);
        size_t rIdx = ringReadIdx.load(std::memory_order_relaxed);
        if (wIdx - rIdx >= RING_SIZE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        size_t idx = wIdx % RING_SIZE;
        RingSlot& slot = ringBuffer[idx];

        // Ensure buffer is large enough.
        // av_image_get_buffer_size with align=16 gives FFmpeg's padded size, but
        // sws_scale may write past the end of the LAST row for SIMD alignment,
        // so keep the original +256 safety padding.
        int w = videoPlayer.width();
        int h = videoPlayer.height();
        size_t needed = static_cast<size_t>(av_image_get_buffer_size(AV_PIX_FMT_RGBA, w, h, 16)) + 256;
        if (slot.data.size() < needed) {
            slot.data.resize(needed);
        }

        int outWidth = 0, outHeight = 0;
        if (videoPlayer.grabFrameInto(slot.data.data(), slot.data.size(), outWidth, outHeight)) {
            slot.ready.store(true, std::memory_order_release);
            ringWriteIdx.fetch_add(1, std::memory_order_release);

            // Throttle to ~video frame rate so we don't burn a whole core.
            // Use explicit nanoseconds because duration<double> can round to 0
            // on Linux kernels with coarse tick resolution.
            double frameDuration = videoPlayer.frameDuration();
            if (frameDuration > 0.001) {
                int64_t ns = static_cast<int64_t>(frameDuration * 0.9 * 1e9);
                if (ns < 1'000'000) ns = 1'000'000; // min 1 ms
                std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } else {
            // EOF or error
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
}

void VideoRenderer::update(float deltaTime, uint32_t frameIndex) {
    if (!videoPlayer.isReady() || !videoTexture.isReady()) {
        return;
    }

    accumulatedTime += deltaTime;

    double frameDuration = videoPlayer.frameDuration();
    bool forceUpload = firstFrame;
    if (!forceUpload && accumulatedTime < frameDuration) {
        return;
    }

    if (!forceUpload) {
        accumulatedTime -= frameDuration;
    }
    firstFrame = false;

    // Pull the latest decoded frame from the ring buffer
    size_t wIdx = ringWriteIdx.load(std::memory_order_acquire);
    size_t rIdx = ringReadIdx.load(std::memory_order_relaxed);
    if (wIdx <= rIdx) {
        return; // nothing new decoded yet
    }

    // Take the newest frame, skipping any intermediate ones
    size_t newestIdx = (wIdx - 1) % RING_SIZE;
    RingSlot& slot = ringBuffer[newestIdx];
    if (!slot.ready.load(std::memory_order_acquire)) {
        return;
    }

    size_t pixelDataSize = static_cast<size_t>(videoPlayer.width()) * static_cast<size_t>(videoPlayer.height()) * 4;
    if (pixelDataSize > slot.data.size()) {
        return; // buffer not ready (resize in progress)
    }

    videoTexture.uploadFrame(frameIndex, slot.data.data(), pixelDataSize);
    ringReadIdx.store(wIdx, std::memory_order_release);
}

void VideoRenderer::reset() {
    accumulatedTime = 0.0f;
    firstFrame = true;
    // Clear ring buffer readiness to avoid stale frames after video swap
    for (auto& slot : ringBuffer) {
        slot.ready.store(false, std::memory_order_relaxed);
    }
    ringWriteIdx.store(0, std::memory_order_relaxed);
    ringReadIdx.store(0, std::memory_order_relaxed);
}

void VideoRenderer::resize(uint32_t width, uint32_t height) {
    stopDecoder();
    // sws_scale produces compact layout, so stride = width * 4
    cpuFramePool.resize(width, height, width * 4);

    size_t frameSize = cpuFramePool.getFrame(0).data.size();
    for (auto& slot : ringBuffer) {
        slot.data.resize(frameSize);
        slot.ready.store(false, std::memory_order_relaxed);
    }
    ringWriteIdx.store(0, std::memory_order_relaxed);
    ringReadIdx.store(0, std::memory_order_relaxed);

    startDecoder();
}
