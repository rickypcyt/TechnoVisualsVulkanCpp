#pragma once

#include <cstdint>
#include "../video/VideoPlayer.h"
#include "../video/VideoTexture.h"
#include "../video/CpuFramePool.h"

class VideoRenderer {
public:
    VideoRenderer(VideoPlayer& player, VideoTexture& texture, CpuFramePool& cpuPool);
    
    void update(float deltaTime, uint32_t frameIndex);
    void reset();
    void resize(uint32_t width, uint32_t height);
    
private:
    VideoPlayer& videoPlayer;
    VideoTexture& videoTexture;
    CpuFramePool& cpuFramePool;
    double accumulatedTime = 0.0f;
    bool firstFrame = true;
};
