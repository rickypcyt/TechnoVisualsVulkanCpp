#include "video/VideoPlayer.h"
#include "video/FrameLayout.h"
#include <iostream>
#include <algorithm>
#include <mutex>
#include <cmath>
#include <cstring>
#include <thread>

namespace {
    void ensureFFmpegInitialized() {
        static std::once_flag ffmpegInitFlag;
        std::call_once(ffmpegInitFlag, []() {
            av_log_set_level(AV_LOG_ERROR);
            avformat_network_init();
        });
    }
}

bool VideoPlayer::initialize(const std::string& path, int screenW, int screenH) {
    ::ensureFFmpegInitialized();
    shutdown();

    currentSourcePath = path;
    // Hard cap at 1080p — we never need more than this for display
    targetScreenWidth  = std::min(screenW,  1920);
    targetScreenHeight = std::min(screenH, 1080);

    if (avformat_open_input(&formatCtx, path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[Video] Failed to open source: " << path << std::endl;
        return false;
    }

    if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
        std::cerr << "[Video] Failed to read stream info" << std::endl;
        shutdown();
        return false;
    }

    videoStreamIndex = av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        std::cerr << "[Video] No video stream found" << std::endl;
        shutdown();
        return false;
    }

    AVStream* stream = formatCtx->streams[videoStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "[Video] Unsupported codec" << std::endl;
        shutdown();
        return false;
    }

    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        std::cerr << "[Video] Failed to allocate codec context" << std::endl;
        shutdown();
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0) {
        std::cerr << "[Video] Failed to copy codec parameters" << std::endl;
        shutdown();
        return false;
    }

    // Enable multi-threaded decoding (essential for 4K performance)
    int hwThreads = static_cast<int>(std::thread::hardware_concurrency());
    codecCtx->thread_count = (hwThreads > 0) ? hwThreads : 4;
    codecCtx->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;

    // Skip loop filter for faster decoding (~20-30% speedup, minor quality loss)
    codecCtx->skip_loop_filter = AVDISCARD_ALL;

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        std::cerr << "[Video] Failed to open codec" << std::endl;
        shutdown();
        return false;
    }

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) {
        std::cerr << "[Video] Failed to allocate packet/frame" << std::endl;
        shutdown();
        return false;
    }

    int outputWidth = codecCtx->width;
    int outputHeight = codecCtx->height;
    int originalWidth = outputWidth;
    int originalHeight = outputHeight;
    computeOutputDimensions(outputWidth, outputHeight, outputWidth, outputHeight, targetScreenWidth, targetScreenHeight, autoScaleEnabled);

    if (outputWidth != originalWidth || outputHeight != originalHeight) {
        std::cout << "[Video] Downscaling " << originalWidth << "x" << originalHeight
                  << " to " << outputWidth << "x" << outputHeight << " for playback" << std::endl;
    }

    videoWidth = outputWidth;
    videoHeight = outputHeight;
    frameDurationSeconds = computeFrameDuration(stream);
    videoDurationSeconds = 0.0;
    if (stream->duration > 0) {
        videoDurationSeconds = stream->duration * av_q2d(stream->time_base);
    } else if (formatCtx->duration > 0) {
        videoDurationSeconds = static_cast<double>(formatCtx->duration) / AV_TIME_BASE;
    }
    ready = true;
    std::cout << "[Video] Initialized " << currentSourcePath << " (" << videoWidth << "x" << videoHeight << ")" << std::endl;
    return true;
}

bool VideoPlayer::reinitialize() {
    if (currentSourcePath.empty()) {
        return false;
    }
    return initialize(currentSourcePath, targetScreenWidth, targetScreenHeight);
}

bool VideoPlayer::isReady() const {
    return ready;
}

int VideoPlayer::width() const {
    return videoWidth;
}

int VideoPlayer::height() const {
    return videoHeight;
}

double VideoPlayer::durationSeconds() const {
    return videoDurationSeconds;
}

double VideoPlayer::frameDuration() const {
    return frameDurationSeconds / playbackRate;
}

void VideoPlayer::setPlaybackRate(double rate) {
    playbackRate = std::clamp(rate, 0.05, 8.0);
}

double VideoPlayer::getPlaybackRate() const {
    return playbackRate;
}

void VideoPlayer::setAutoScale(bool autoScale) {
    autoScaleEnabled = autoScale;
}

bool VideoPlayer::getAutoScale() const {
    return autoScaleEnabled;
}

bool VideoPlayer::grabFrame(std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight) {
    if (!ready) {
        return false;
    }

    while (true) {
        int ret = av_read_frame(formatCtx, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                if (!loopEnabled) {
                    return false;
                }
                if (av_seek_frame(formatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD) < 0) {
                    return false;
                }
                avcodec_flush_buffers(codecCtx);
                continue;
            }
            return false;
        }

        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        ret = avcodec_send_packet(codecCtx, packet);
        av_packet_unref(packet);
        if (ret < 0) {
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == AVERROR(EAGAIN)) {
                break;
            }
            if (ret < 0) {
                return false;
            }

            bool converted = convertFrameToRGBA(frame, outRGBA);
            av_frame_unref(frame);
            if (converted) {
                outWidth = videoWidth;
                outHeight = videoHeight;
                return true;
            }
        }
    }
}

bool VideoPlayer::grabFrameInto(uint8_t* dest, size_t destCapacity, int& outWidth, int& outHeight) {
    if (!ready || !dest || destCapacity == 0) {
        std::cerr << "[VideoPlayer] grabFrameInto aborted: ready=" << ready
                  << " dest=" << (dest ? "yes" : "null")
                  << " cap=" << destCapacity << "\n";
        return false;
    }

    while (true) {
        int ret = av_read_frame(formatCtx, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                if (!loopEnabled) {
                    std::cerr << "[VideoPlayer] grabFrameInto EOF (loop disabled)\n";
                    return false;
                }
                if (av_seek_frame(formatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD) < 0) {
                    std::cerr << "[VideoPlayer] grabFrameInto seek to 0 failed\n";
                    return false;
                }
                avcodec_flush_buffers(codecCtx);
                continue;
            }
            std::cerr << "[VideoPlayer] grabFrameInto av_read_frame error: " << ret << "\n";
            return false;
        }

        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        ret = avcodec_send_packet(codecCtx, packet);
        av_packet_unref(packet);
        if (ret < 0) {
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == AVERROR(EAGAIN)) {
                break;
            }
            if (ret < 0) {
                std::cerr << "[VideoPlayer] grabFrameInto avcodec_receive_frame error: " << ret << "\n";
                return false;
            }

            if (!ensureScalingContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format))) {
                std::cerr << "[VideoPlayer] grabFrameInto ensureScalingContext failed for "
                          << frame->width << "x" << frame->height << " fmt=" << frame->format << "\n";
                av_frame_unref(frame);
                return false;
            }

            // sws_scale may write up to 16 bytes past the end of each line
            // for SIMD alignment, so use av_image_get_buffer_size with align=16
            const size_t needed = static_cast<size_t>(av_image_get_buffer_size(
                AV_PIX_FMT_RGBA, videoWidth, videoHeight, 16));
            if (needed > destCapacity) {
                std::cerr << "[VideoPlayer] grabFrameInto buffer too small: needed=" << needed
                          << " cap=" << destCapacity << " for " << videoWidth << "x" << videoHeight << "\n";
                outWidth = videoWidth;
                outHeight = videoHeight;
                av_frame_unref(frame);
                return false;
            }

            // sws_scale converts to compact RGBA layout
            // frame->linesize[0] is for the SOURCE format (e.g., YUV), not destination
            uint8_t* destPlanes[4] = {dest, nullptr, nullptr, nullptr};
            int destStrides[4] = {videoWidth * 4, 0, 0, 0};
            
            sws_scale(
                swsCtx,
                frame->data,
                frame->linesize,
                0,
                frame->height,
                destPlanes,
                destStrides);

            outWidth = videoWidth;
            outHeight = videoHeight;
            av_frame_unref(frame);
            return true;
        }
    }
}

void VideoPlayer::shutdown() {
    std::cout << "[FFmpeg] Shutting down player..." << std::endl;
    std::cout << "[FFmpeg] Before FREE: packet=" << packet << " frame=" << frame << " swsCtx=" << swsCtx << " codecCtx=" << codecCtx << " formatCtx=" << formatCtx << std::endl;

    if (packet) {
        av_packet_free(&packet);
        packet = nullptr;
    }
    if (frame) {
        av_frame_free(&frame);
        frame = nullptr;
    }
    if (swsCtx) {
        sws_freeContext(swsCtx);
        swsCtx = nullptr;
    }
    // Close format context BEFORE freeing codec context to avoid double-free
    if (formatCtx) {
        avformat_close_input(&formatCtx);
        formatCtx = nullptr;
    }
    if (codecCtx) {
        avcodec_free_context(&codecCtx);
        codecCtx = nullptr;
    }
    
    cachedWidth = 0;
    cachedHeight = 0;
    cachedDstWidth = 0;
    cachedDstHeight = 0;
    cachedFormat = AV_PIX_FMT_NONE;
    ready = false;
    videoStreamIndex = -1;
    videoWidth = 0;
    videoHeight = 0;
    frameDurationSeconds = 0.0;
    videoDurationSeconds = 0.0;
    
    std::cout << "[FFmpeg] Player shutdown complete" << std::endl;
}

bool VideoPlayer::seekSeconds(double seconds) {
    if (!formatCtx || videoStreamIndex < 0) {
        return false;
    }
    AVStream* stream = formatCtx->streams[videoStreamIndex];
    if (!stream || stream->time_base.den == 0) {
        return false;
    }
    double clamped = seconds;
    if (videoDurationSeconds > 0.0) {
        clamped = std::clamp(seconds, 0.0, videoDurationSeconds - frameDurationSeconds);
    }
    int64_t target = static_cast<int64_t>(clamped / av_q2d(stream->time_base));

    // Flush packet before seek to avoid double free
    if (packet) {
        av_packet_unref(packet);
    }

    if (av_seek_frame(formatCtx, videoStreamIndex, target, AVSEEK_FLAG_BACKWARD) < 0) {
        return false;
    }
    avcodec_flush_buffers(codecCtx);
    return true;
}

bool VideoPlayer::getFrameAtPTS(double targetSeconds, std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight) {
    if (!ready) {
        return false;
    }

    // Clamp target time to valid range
    double clampedTarget = targetSeconds;
    if (videoDurationSeconds > 0.0) {
        clampedTarget = std::clamp(targetSeconds, 0.0, videoDurationSeconds - frameDurationSeconds);
    }

    // Seek to the target position
    if (!seekSeconds(clampedTarget)) {
        return false;
    }

    // Read and decode frames until we reach the target timestamp
    while (true) {
        int ret = av_read_frame(formatCtx, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                if (!loopEnabled) {
                    return false;
                }
                if (av_seek_frame(formatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD) < 0) {
                    return false;
                }
                avcodec_flush_buffers(codecCtx);
                continue;
            }
            return false;
        }

        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        ret = avcodec_send_packet(codecCtx, packet);
        av_packet_unref(packet);
        if (ret < 0) {
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == AVERROR(EAGAIN)) {
                break;
            }
            if (ret < 0) {
                return false;
            }

            // Check if this frame's PTS matches our target
            AVStream* stream = formatCtx->streams[videoStreamIndex];
            double framePTS = frame->pts * av_q2d(stream->time_base);
            
            // If we've passed the target time, use this frame
            if (framePTS >= clampedTarget - frameDurationSeconds) {
                bool converted = convertFrameToRGBA(frame, outRGBA);
                av_frame_unref(frame);
                if (converted) {
                    outWidth = videoWidth;
                    outHeight = videoHeight;
                    return true;
                }
            }
            
            av_frame_unref(frame);
        }
    }
}

double VideoPlayer::getCurrentFramePTS() const {
    if (!ready || !frame || frame->pts == AV_NOPTS_VALUE) {
        return 0.0;
    }
    AVStream* stream = formatCtx->streams[videoStreamIndex];
    if (!stream || stream->time_base.den == 0) {
        return 0.0;
    }
    return frame->pts * av_q2d(stream->time_base);
}

const std::string& VideoPlayer::sourcePath() const {
    return currentSourcePath;
}

double VideoPlayer::computeFrameDuration(const AVStream* stream) const {
    if (!stream) {
        return 0.0;
    }
    if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
        return static_cast<double>(stream->avg_frame_rate.den) / stream->avg_frame_rate.num;
    }
    if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0) {
        return static_cast<double>(stream->r_frame_rate.den) / stream->r_frame_rate.num;
    }
    return 1.0 / 30.0;
}

bool VideoPlayer::convertFrameToRGBA(AVFrame* src, std::vector<uint8_t>& out) {
    if (!src) {
        return false;
    }
    if (!ensureScalingContext(src->width, src->height, static_cast<AVPixelFormat>(src->format))) {
        return false;
    }

    // sws_scale may write up to 16 bytes past the end of each line for SIMD
    // alignment, so add 64 bytes of padding (same safety margin as CpuFramePool)
    out.resize(static_cast<size_t>(videoWidth) * videoHeight * 4 + 64);
    uint8_t* destData[4] = {out.data(), nullptr, nullptr, nullptr};
    int destLinesize[4] = {videoWidth * 4, 0, 0, 0};

    sws_scale(
        swsCtx,
        src->data,
        src->linesize,
        0,
        src->height,
        destData,
        destLinesize);

    return true;
}

void VideoPlayer::computeOutputDimensions(int srcWidth, int srcHeight, int& outWidth, int& outHeight, int screenWidth, int screenHeight, bool autoScale) const {
    int clampedSrcWidth = std::max(1, srcWidth);
    int clampedSrcHeight = std::max(1, srcHeight);

    outWidth = clampedSrcWidth;
    outHeight = clampedSrcHeight;

    // Fit to screen size while maintaining aspect ratio
    // This prevents performance issues by not decoding at full 1080p when screen is smaller
    if (autoScale && screenWidth > 0 && screenHeight > 0) {
        if (clampedSrcHeight > screenHeight) {
            // Video is taller than screen - scale down to fit
            double scale = static_cast<double>(screenHeight) / clampedSrcHeight;
            outHeight = screenHeight;
            outWidth = std::max(1, static_cast<int>(std::round(clampedSrcWidth * scale)));
        }
        if (clampedSrcWidth > screenWidth) {
            // Video is wider than screen - scale down to fit
            double scale = static_cast<double>(screenWidth) / clampedSrcWidth;
            outWidth = screenWidth;
            outHeight = std::max(1, static_cast<int>(std::round(clampedSrcHeight * scale)));
        }
    }

    if (outWidth % 2 != 0 && outWidth > 1) {
        --outWidth;
    }
    if (outHeight % 2 != 0 && outHeight > 1) {
        --outHeight;
    }
}

bool VideoPlayer::ensureScalingContext(int srcWidth, int srcHeight, AVPixelFormat srcFormat) {
    int clampedSrcWidth = std::max(1, srcWidth);
    int clampedSrcHeight = std::max(1, srcHeight);
    int targetWidth = clampedSrcWidth;
    int targetHeight = clampedSrcHeight;
    computeOutputDimensions(clampedSrcWidth, clampedSrcHeight, targetWidth, targetHeight, targetScreenWidth, targetScreenHeight, autoScaleEnabled);

    bool needsContext = (!swsCtx ||
                         cachedWidth != clampedSrcWidth ||
                         cachedHeight != clampedSrcHeight ||
                         cachedFormat != srcFormat ||
                         cachedDstWidth != targetWidth ||
                         cachedDstHeight != targetHeight);

    if (needsContext) {
        if (swsCtx) {
            sws_freeContext(swsCtx);
        }
        swsCtx = sws_getContext(
            clampedSrcWidth,
            clampedSrcHeight,
            srcFormat,
            targetWidth,
            targetHeight,
            AV_PIX_FMT_RGBA,
            SWS_FAST_BILINEAR,
            nullptr,
            nullptr,
            nullptr);
        cachedWidth = clampedSrcWidth;
        cachedHeight = clampedSrcHeight;
        cachedFormat = srcFormat;
        cachedDstWidth = targetWidth;
        cachedDstHeight = targetHeight;

        if (targetWidth != clampedSrcWidth || targetHeight != clampedSrcHeight) {
            std::cout << "[Video] Scaling decode "
                      << clampedSrcWidth << "x" << clampedSrcHeight
                      << " → " << targetWidth << "x" << targetHeight
                      << " (screen " << targetScreenWidth << "x" << targetScreenHeight << ")"
                      << std::endl;
        }
    }

    if (!swsCtx) {
        return false;
    }

    videoWidth = targetWidth;
    videoHeight = targetHeight;
    return true;
}
