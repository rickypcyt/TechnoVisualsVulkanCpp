#include "VideoRenderer.h"
#include <iostream>

VideoRenderer::VideoRenderer(VideoPlayer& player, VideoTexture& texture)
    : videoPlayer(player)
    , videoTexture(texture)
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

    // Decode next frame
    int outWidth, outHeight;
    std::vector<uint8_t> frameData;
    
    if (videoPlayer.grabFrame(frameData, outWidth, outHeight)) {
        // Store previous frame data for interpolation
        videoTexture.getPreviousFrameData() = frameData;
        
        // Upload to staging buffer
        videoTexture.uploadFrame(frameIndex, frameData.data(), frameData.size());
        
        // Mark previous frame for upload
        videoTexture.getPendingUploadsPrev()[frameIndex] = true;
    }
}

void VideoRenderer::reset() {
    accumulatedTime = 0.0f;
}
