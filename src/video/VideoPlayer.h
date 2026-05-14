#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../app/ProjectState.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// VideoPlayer - FFmpeg video decoder
// Handles video file decoding, frame extraction, and format conversion to RGBA
class VideoPlayer {
public:
    bool initialize(const std::string& path, int screenW = 0, int screenH = 0);
    bool reinitialize();
    bool isReady() const;
    int width() const;
    int height() const;
    double durationSeconds() const;
    double frameDuration() const;
    void setPlaybackRate(double rate);
    double getPlaybackRate() const;
    void setAutoScale(bool autoScale);
    bool getAutoScale() const;

    bool grabFrame(std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight);
    bool grabFrameInto(uint8_t* dest, size_t destCapacity, int& outWidth, int& outHeight);
    bool seekSeconds(double seconds);
    bool getFrameAtPTS(double targetSeconds, std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight);
    double getCurrentFramePTS() const;
    const std::string& sourcePath() const;
    
    void shutdown();

private:
    double computeFrameDuration(const AVStream* stream) const;
    bool convertFrameToRGBA(AVFrame* src, std::vector<uint8_t>& out);
    void computeOutputDimensions(int srcWidth, int srcHeight, int& outWidth, int& outHeight, int screenWidth = 0, int screenHeight = 0, bool autoScale = true) const;
    bool ensureScalingContext(int srcWidth, int srcHeight, AVPixelFormat srcFormat);

    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* swsCtx = nullptr;
    int videoStreamIndex = -1;
    int videoWidth = 0;
    double playbackRate = 1.0;
    int videoHeight = 0;
    double frameDurationSeconds = 0.0;
    bool ready = false;
    bool loopEnabled = true;
    int cachedWidth = 0;
    int cachedHeight = 0;
    int cachedDstWidth = 0;
    int cachedDstHeight = 0;
    int cachedFormat = AV_PIX_FMT_NONE;
    std::string currentSourcePath;
    double videoDurationSeconds = 0.0;
    int targetScreenWidth = 0;
    int targetScreenHeight = 0;
    bool autoScaleEnabled = true;
};
