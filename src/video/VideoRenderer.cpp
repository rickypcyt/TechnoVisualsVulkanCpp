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
    if (accumulatedTime < frameDuration) {
        return;
    }

    accumulatedTime -= frameDuration;

    // Get CPU frame from pool
    CpuFrame& cpuFrame = cpuFramePool.acquireWriteFrame();
    
    // Decode next frame directly into CPU frame pool
    int outWidth, outHeight;
    if (videoPlayer.grabFrameInto(cpuFrame.data.data(), cpuFrame.data.capacity(), outWidth, outHeight)) {
        // Store previous frame data for interpolation
        videoTexture.getPreviousFrameData() = cpuFrame.data;
        
        // Upload to staging buffer with safety check
        size_t pixelDataSize = static_cast<size_t>(cpuFrame.stride) * cpuFrame.height;
        videoTexture.uploadFrame(frameIndex, cpuFrame.data.data(), pixelDataSize);
        
        // Mark previous frame for upload
        videoTexture.getPendingUploadsPrev()[frameIndex] = true;
    }
}

void VideoRenderer::reset() {
    accumulatedTime = 0.0f;
}

void VideoRenderer::resize(uint32_t width, uint32_t height) {
    // sws_scale produces compact layout, so stride = width * 4
    cpuFramePool.resize(width, height, width * 4);
}
