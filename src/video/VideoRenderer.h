#pragma once

#include <cstdint>
#include "../video/VideoPlayer.h"
#include "../video/VideoTexture.h"

class VideoRenderer {
public:
    VideoRenderer(VideoPlayer& player, VideoTexture& texture);
    
    void update(float deltaTime, uint32_t frameIndex);
    void reset();
    
private:
    VideoPlayer& videoPlayer;
    VideoTexture& videoTexture;
    double accumulatedTime = 0.0f;
};
