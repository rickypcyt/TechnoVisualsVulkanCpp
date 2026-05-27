#include "VideoRenderer.h"
#include <iostream>

VideoRenderer::VideoRenderer(VideoPlayer& player, VideoTexture& texture, CpuFramePool& cpuPool)
    : videoPlayer(player)
    , videoTexture(texture)
    , cpuFramePool(cpuPool)
{
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

    // Get CPU frame from pool
    CpuFrame& cpuFrame = cpuFramePool.acquireWriteFrame();

    // Decode next frame directly into CPU frame pool
    int outWidth, outHeight;
    if (videoPlayer.grabFrameInto(cpuFrame.data.data(), cpuFrame.data.capacity(), outWidth, outHeight)) {
        // Upload to staging buffer with safety check
        size_t pixelDataSize = static_cast<size_t>(cpuFrame.stride) * cpuFrame.height;
        videoTexture.uploadFrame(frameIndex, cpuFrame.data.data(), pixelDataSize);
    }
}

void VideoRenderer::reset() {
    accumulatedTime = 0.0f;
    firstFrame = true;
}

void VideoRenderer::resize(uint32_t width, uint32_t height) {
    // sws_scale produces compact layout, so stride = width * 4
    cpuFramePool.resize(width, height, width * 4);
}
