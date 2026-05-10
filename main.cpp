#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <chrono>

#include <iostream>
#include <stdexcept>
#include <vector>
#include <set>
#include <array>
#include <algorithm>
#include <limits>
#include <fstream>
#include <string>
#include <cstring>
#include <cstddef>
#include <functional>
#include <utility>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <mutex>
#include <optional>
#include <filesystem>
#include <system_error>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <unordered_map>
#include <random>
#include <deque>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace fs = std::filesystem;

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "VideoStaging.h"
#include "EffectChain.h"
#include "Timeline.h"
#include "Export.h"
#include "PlaybackClock.h"
#include "ProjectState.h"
#include "RenderJob.h"

const int WIDTH = 800;
const int HEIGHT = 600;
const int MAX_FRAMES_IN_FLIGHT = 2;
const size_t MAX_VIDEO_FRAME_BUFFER = 48;
const int MAX_VIDEO_OUTPUT_HEIGHT = 2160;  // Support 4K resolution
const std::array<int, 5> FORCED_FPS_OPTIONS = {0, 15, 24, 30, 60};
const std::string imguiIniFilename = "imgui.ini";
constexpr bool kEnableVideoTrace = true;

// Signal handler for crashes
#include <csignal>
#include <execinfo.h>

void crash_handler(int sig) {
    void* array[10];
    size_t size = backtrace(array, 10);

    std::cerr << "\n[CRASH] Signal " << sig << " caught!" << std::endl;
    std::cerr << "[CRASH] Backtrace:" << std::endl;
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    std::cerr << std::endl;

    exit(1);
}

template<typename... Args>
inline void videoTrace(Args&&... args) {
    if constexpr (kEnableVideoTrace) {
        (std::cout << ... << std::forward<Args>(args));
        std::cout << std::endl;
    }
}

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

namespace {
void ensureFFmpegInitialized() {
    static std::once_flag ffmpegInitFlag;
    std::call_once(ffmpegInitFlag, []() {
        av_log_set_level(AV_LOG_ERROR);
        avformat_network_init();
    });
}
}

class VideoPlayer {
public:
    bool initialize(const std::string& path) {
        ::ensureFFmpegInitialized();
        shutdown();

        currentSourcePath = path;

        if (avformat_open_input(&formatCtx, path.c_str(), nullptr, nullptr) < 0) {
            std::cerr << "[Video] Failed to open source: " << path << std::endl;
            return false;
        }

        if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
            std::cerr << "[Video] Failed to read stream info" << std::endl;
            return false;
        }

        videoStreamIndex = av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoStreamIndex < 0) {
            std::cerr << "[Video] No video stream found" << std::endl;
            return false;
        }

        AVStream* stream = formatCtx->streams[videoStreamIndex];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            std::cerr << "[Video] Unsupported codec" << std::endl;
            return false;
        }

        codecCtx = avcodec_alloc_context3(codec);
        if (!codecCtx) {
            std::cerr << "[Video] Failed to allocate codec context" << std::endl;
            return false;
        }

        if (avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0) {
            std::cerr << "[Video] Failed to copy codec parameters" << std::endl;
            return false;
        }

        if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
            std::cerr << "[Video] Failed to open codec" << std::endl;
            return false;
        }

        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (!packet || !frame) {
            std::cerr << "[Video] Failed to allocate packet/frame" << std::endl;
            return false;
        }

        int outputWidth = codecCtx->width;
        int outputHeight = codecCtx->height;
        int originalWidth = outputWidth;
        int originalHeight = outputHeight;
        // No screen size available during VideoPlayer initialization
        computeOutputDimensions(outputWidth, outputHeight, outputWidth, outputHeight, 0, 0);

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

    bool reinitialize() {
        if (currentSourcePath.empty()) {
            return false;
        }
        return initialize(currentSourcePath);
    }

    bool grabFrame(std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight) {
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

    bool grabFrameInto(uint8_t* dest, size_t destCapacity, int& outWidth, int& outHeight) {
        if (!ready || !dest || destCapacity == 0) {
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

                if (!ensureScalingContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format))) {
                    av_frame_unref(frame);
                    return false;
                }

                const size_t needed = static_cast<size_t>(videoWidth) * videoHeight * 4;
                if (needed > destCapacity) {
                    outWidth = videoWidth;
                    outHeight = videoHeight;
                    av_frame_unref(frame);
                    return false;
                }

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

    void shutdown() {
        if (packet) {
            av_packet_free(&packet);
            packet = nullptr;
        }
        if (frame) {
            av_frame_free(&frame);
            frame = nullptr;
        }
        if (codecCtx) {
            avcodec_free_context(&codecCtx);
            codecCtx = nullptr;
        }
        if (formatCtx) {
            avformat_close_input(&formatCtx);
            formatCtx = nullptr;
        }
        if (swsCtx) {
            sws_freeContext(swsCtx);
            swsCtx = nullptr;
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
    }

    bool isReady() const { return ready; }
    int width() const { return videoWidth; }
    int height() const { return videoHeight; }
    double frameDuration() const { return frameDurationSeconds; }
    double durationSeconds() const { return videoDurationSeconds; }
    bool seekSeconds(double seconds) {
        if (!ready || !formatCtx || videoStreamIndex < 0 || seconds < 0.0) {
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
        if (av_seek_frame(formatCtx, videoStreamIndex, target, AVSEEK_FLAG_BACKWARD) < 0) {
            return false;
        }
        avcodec_flush_buffers(codecCtx);
        return true;
    }

    // PTS-based frame retrieval for NLE playback
    bool getFrameAtPTS(double targetSeconds, std::vector<uint8_t>& outRGBA, int& outWidth, int& outHeight) {
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

    double getCurrentFramePTS() const {
        if (!ready || !frame || frame->pts == AV_NOPTS_VALUE) {
            return 0.0;
        }
        AVStream* stream = formatCtx->streams[videoStreamIndex];
        if (!stream || stream->time_base.den == 0) {
            return 0.0;
        }
        return frame->pts * av_q2d(stream->time_base);
    }

private:
    double computeFrameDuration(const AVStream* stream) const {
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

    bool convertFrameToRGBA(AVFrame* src, std::vector<uint8_t>& out) {
        if (!src) {
            return false;
        }
        if (!ensureScalingContext(src->width, src->height, static_cast<AVPixelFormat>(src->format))) {
            return false;
        }

        out.resize(static_cast<size_t>(videoWidth) * videoHeight * 4);
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

    void computeOutputDimensions(int srcWidth, int srcHeight, int& outWidth, int& outHeight, int screenWidth = 0, int screenHeight = 0) const {
        int clampedSrcWidth = std::max(1, srcWidth);
        int clampedSrcHeight = std::max(1, srcHeight);

        outWidth = clampedSrcWidth;
        outHeight = clampedSrcHeight;

        // Fit to screen size while maintaining aspect ratio
        // This prevents performance issues by not decoding at full 1080p when screen is smaller
        if (screenWidth > 0 && screenHeight > 0) {
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
    }

    bool ensureScalingContext(int srcWidth, int srcHeight, AVPixelFormat srcFormat) {
        int clampedSrcWidth = std::max(1, srcWidth);
        int clampedSrcHeight = std::max(1, srcHeight);
        int targetWidth = clampedSrcWidth;
        int targetHeight = clampedSrcHeight;
        // No screen size available during scaling context setup
        computeOutputDimensions(clampedSrcWidth, clampedSrcHeight, targetWidth, targetHeight, 0, 0);

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
                SWS_BILINEAR,
                nullptr,
                nullptr,
                nullptr);
            cachedWidth = clampedSrcWidth;
            cachedHeight = clampedSrcHeight;
            cachedFormat = srcFormat;
            cachedDstWidth = targetWidth;
            cachedDstHeight = targetHeight;
        }

        if (!swsCtx) {
            return false;
        }

        videoWidth = targetWidth;
        videoHeight = targetHeight;
        return true;
    }

    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* swsCtx = nullptr;
    int videoStreamIndex = -1;
    int videoWidth = 0;
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
};

struct VideoMetadata {
    std::string path;
    std::string filename;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double duration = 0.0;
    int64_t bitrate = 0;
    bool hasAudio = false;
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
};

struct VideoAsset {
    VideoMetadata metadata;
};

class VideoRegistry {
public:
    void scan(const std::string& rootPath) {
        assets.clear();

        fs::path root(rootPath);
        if (!fs::exists(root) || !fs::is_directory(root)) {
            return;
        }

        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string filename = entry.path().filename().string();

            // Skip output files (they are temporary render outputs, not source videos)
            if (filename.find("output.") == 0) {
                continue;
            }

            std::string ext = entry.path().extension().string();
            if (!isVideoExtension(ext)) {
                continue;
            }

            VideoAsset asset;
            asset.metadata.path = entry.path().string();
            asset.metadata.filename = entry.path().filename().string();
            probeMetadata(asset);
            assets.push_back(std::move(asset));
        }

        std::sort(assets.begin(), assets.end(), [](const VideoAsset& a, const VideoAsset& b) {
            return a.metadata.filename < b.metadata.filename;
        });
    }

    const std::vector<VideoAsset>& getAssets() const {
        return assets;
    }

private:
    static bool isVideoExtension(std::string ext) {
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        static const std::array<const char*, 5> allowed = {".mp4", ".mov", ".mkv", ".avi", ".webm"};
        return std::any_of(allowed.begin(), allowed.end(), [&](const char* allowedExt) {
            return ext == allowedExt;
        });
    }

    static double extractFrameRate(const AVStream* stream) {
        if (stream == nullptr) {
            return 0.0;
        }
        if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
            return av_q2d(stream->avg_frame_rate);
        }
        if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0) {
            return av_q2d(stream->r_frame_rate);
        }
        return 0.0;
    }

    static double extractDuration(const AVStream* stream, const AVFormatContext* ctx) {
        if (stream && stream->duration > 0) {
            return stream->duration * av_q2d(stream->time_base);
        }
        if (ctx && ctx->duration > 0) {
            return static_cast<double>(ctx->duration) / AV_TIME_BASE;
        }
        return 0.0;
    }

    static int64_t extractBitrate(const AVStream* stream, const AVFormatContext* ctx) {
        if (stream && stream->codecpar && stream->codecpar->bit_rate > 0) {
            return stream->codecpar->bit_rate;
        }
        if (ctx && ctx->bit_rate > 0) {
            return ctx->bit_rate;
        }
        return 0;
    }

    static void probeMetadata(VideoAsset& asset) {
        ::ensureFFmpegInitialized();
        AVFormatContext* context = nullptr;
        if (avformat_open_input(&context, asset.metadata.path.c_str(), nullptr, nullptr) != 0) {
            if (context) {
                avformat_close_input(&context);
            }
            return;
        }

        if (avformat_find_stream_info(context, nullptr) < 0) {
            avformat_close_input(&context);
            return;
        }

        for (unsigned int i = 0; i < context->nb_streams; ++i) {
            AVStream* stream = context->streams[i];
            if (!stream || !stream->codecpar) {
                continue;
            }

            if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && asset.metadata.width == 0) {
                asset.metadata.width = stream->codecpar->width;
                asset.metadata.height = stream->codecpar->height;
                asset.metadata.fps = extractFrameRate(stream);
                asset.metadata.duration = extractDuration(stream, context);
                asset.metadata.bitrate = extractBitrate(stream, context);
                asset.metadata.pixelFormat = static_cast<AVPixelFormat>(stream->codecpar->format);
            }

            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                asset.metadata.hasAudio = true;
            }
        }

        avformat_close_input(&context);
    }

    std::vector<VideoAsset> assets;
};
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData) {

    std::cerr << "[VULKAN] " << callbackData->pMessage << std::endl;
    return VK_FALSE;
}

bool checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool found = false;

        for (const auto& layerProps : availableLayers) {
            if (strcmp(layerName, layerProps.layerName) == 0) {
                found = true;
                break;
            }
        }

        if (!found) return false;
    }

    return true;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger) {

    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator) {

    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

struct Vertex {
    float pos[2];
    float color[3];
};

const std::vector<Vertex> vertices = {
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
};

struct GlobalUBO {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec2 resolution;
    alignas(16) glm::vec2 videoResolution;
    alignas(4) float time;
    alignas(4) float tempo;
    alignas(4) float energy;
    alignas(4) float bass;
    alignas(4) float mid;
    alignas(4) float high;
    alignas(16) glm::vec4 primaryColor;
    alignas(16) glm::vec4 secondaryColor;
    alignas(4) float colorBlend;
    alignas(4) int mode;
    alignas(4) float videoMix;
    alignas(4) float videoAvailable;
    alignas(4) float grayscaleAmount;
    alignas(4) float sharpenAmount;
    alignas(4) float upscaleEnabled;
    alignas(4) float crtCurvature;
    alignas(4) float crtHorizontalCurvature;
    alignas(4) float crtScanlineIntensity;
    alignas(4) float crtMaskIntensity;
    alignas(4) float crtVignette;
    alignas(4) float crtFishEye;
    alignas(4) float bloomIntensity;
    alignas(4) float bloomThreshold;
    alignas(4) float aberrationAmount;
    alignas(4) float grainStrength;
    alignas(4) float bendAmount;
    alignas(4) float glitchAmount;
    alignas(16) glm::vec3 colorBalance;
    alignas(4) float gradeBrightness;
    alignas(4) float gradeContrast;
    alignas(4) float gradeSaturation;
    alignas(4) float gradeHueShift;
    alignas(4) float gradeGamma;
    alignas(4) int colorLUTIndex;
    alignas(4) float splitToneBalance;
    alignas(16) glm::vec3 splitToneShadows;
    alignas(16) glm::vec3 splitToneHighlights;
    alignas(4) float feedbackAmount;
    alignas(4) float trailStrength;
    alignas(4) float temporalAccumulation;
    alignas(4) float feedbackDecay;
    alignas(4) float recursiveBlend;
    alignas(4) float uvWarpStrength;
    alignas(4) float rippleStrength;
    alignas(4) float rippleFrequency;
    alignas(4) float swirlStrength;
    alignas(4) float displacementAmount;
    alignas(4) float kaleidoSegments;
    alignas(4) float tunnelDepth;
    alignas(4) float tunnelCurvature;
    alignas(4) float gaussianBlur;
    alignas(4) float directionalBlur;
    alignas(4) float directionalBlurAngle;
    alignas(4) float zoomBlur;
    alignas(4) float motionBlur;
    alignas(4) float temporalBlur;
    alignas(4) float unsharpMask;
    alignas(4) float casAmount;
    alignas(4) float localContrast;
    alignas(4) float glitchDatamosh;
    alignas(4) float glitchRGBSplit;
    alignas(4) float glitchScanlineBreak;
    alignas(4) float glitchJitter;
    alignas(4) float glitchTearing;
    alignas(4) float glitchPixelSort;
    alignas(4) float glitchBufferCorruption;
    alignas(4) int blendModeProcedural;
    alignas(4) int blendModeVideo;
    alignas(4) int blendModeFeedback;
    alignas(4) float blendProceduralMix;
    alignas(4) float blendVideoMix;
    alignas(4) float blendFeedbackMix;
    alignas(4) float analogScanlineFocus;
    alignas(4) float analogMaskBalance;
    alignas(4) float analogNoise;
    alignas(4) float analogBloom;
    alignas(4) float vhsDistortion;
    alignas(4) float analogChromaticAberration;
    alignas(4) float audioWarpResponse;
    alignas(4) float audioFeedbackResponse;
    alignas(4) float audioBlurResponse;
    alignas(4) float audioColorResponse;
    alignas(4) float audioGlitchResponse;
    alignas(4) float audioBeatSync;
    alignas(4) float audioLfoRate;
    alignas(4) float temporalInterpolation;
    alignas(4) float temporalBlendStrength;
    alignas(4) float slowMotionFactor;
    alignas(4) float frameAccumulation;
    // NLE Effect Chain parameters
    alignas(4) int nleOutputWidth;
    alignas(4) int nleOutputHeight;
    alignas(4) float nleGrayscale;
    alignas(4) float nleBrightness;
    alignas(4) float nleContrast;
    alignas(4) float nleSaturation;
};

struct FrameContext {
    uint32_t frameIndex;
    VkCommandBuffer commandBuffer;
    VkFence inFlightFence;
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    uint32_t swapchainImageIndex;
};

class FrameSystem {
public:
    void init(VkDevice deviceHandle, uint32_t frameCount) {
        device = deviceHandle;
        maxFrames = frameCount;
        currentFrame = 0;
        frameContexts.resize(maxFrames);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < maxFrames; ++i) {
            FrameContext ctx{};
            ctx.frameIndex = i;

            if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &ctx.imageAvailableSemaphore) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphoreInfo, nullptr, &ctx.renderFinishedSemaphore) != VK_SUCCESS ||
                vkCreateFence(device, &fenceInfo, nullptr, &ctx.inFlightFence) != VK_SUCCESS) {
                throw std::runtime_error("failed to create synchronization objects for frame system");
            }

            frameContexts[i] = ctx;
        }

        std::cout << "[FrameSystem] Initialized with " << maxFrames << " frames" << std::endl;
    }

    FrameContext& beginFrame(VkSwapchainKHR swapchain, uint32_t& imageIndex, VkResult& result) {
        FrameContext& frame = frameContexts[currentFrame];

        vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

        result = vkAcquireNextImageKHR(
            device,
            swapchain,
            UINT64_MAX,
            frame.imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex
        );

        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
            if (imageIndex >= imagesInFlight.size()) {
                imagesInFlight.resize(imageIndex + 1, VK_NULL_HANDLE);
            }

            if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
                vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
            }

            imagesInFlight[imageIndex] = frame.inFlightFence;
            frame.swapchainImageIndex = imageIndex;
        }

        return frame;
    }

    void endFrame() {
        if (maxFrames == 0) {
            return;
        }
        currentFrame = (currentFrame + 1) % maxFrames;
    }

    void resizeSwapchainImages(size_t count) {
        imagesInFlight.assign(count, VK_NULL_HANDLE);
    }

    void clearImageTracking() {
        imagesInFlight.clear();
    }

    void resetCurrentFrame() {
        currentFrame = 0;
    }

    VkFence getFence(uint32_t frameIndex) {
        if (frameIndex >= frameContexts.size()) {
            return VK_NULL_HANDLE;
        }
        return frameContexts[frameIndex].inFlightFence;
    }

    void cleanup() {
        for (auto& frame : frameContexts) {
            if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, frame.imageAvailableSemaphore, nullptr);
                frame.imageAvailableSemaphore = VK_NULL_HANDLE;
            }
            if (frame.renderFinishedSemaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, frame.renderFinishedSemaphore, nullptr);
                frame.renderFinishedSemaphore = VK_NULL_HANDLE;
            }
            if (frame.inFlightFence != VK_NULL_HANDLE) {
                vkDestroyFence(device, frame.inFlightFence, nullptr);
                frame.inFlightFence = VK_NULL_HANDLE;
            }
        }

        frameContexts.clear();
        imagesInFlight.clear();
        device = VK_NULL_HANDLE;
        maxFrames = 0;
        currentFrame = 0;
    }

private:
    VkDevice device = VK_NULL_HANDLE;
    uint32_t maxFrames = 0;
    uint32_t currentFrame = 0;
    std::vector<FrameContext> frameContexts;
    std::vector<VkFence> imagesInFlight;
};

struct ResourceHandle {
    enum class Type {
        Unknown,
        Buffer,
        Image
    } type = Type::Unknown;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
};

struct RenderPass {
    virtual void execute(VkCommandBuffer commandBuffer, FrameContext& frame) = 0;
    virtual void onResize() {}
    virtual ~RenderPass() = default;
};

struct RenderNode {
    std::string name;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::function<void(VkCommandBuffer, FrameContext&)> execute;
};

class RenderGraph {
public:
    void addNode(const RenderNode& node) {
        nodes.push_back(node);
    }

    void execute(VkCommandBuffer commandBuffer, FrameContext& frame) {
        for (auto& node : nodes) {
            node.execute(commandBuffer, frame);
        }
    }

private:
    std::vector<RenderNode> nodes;
};

class Renderer {
public:
    void record(VkCommandBuffer commandBuffer, FrameContext& frame) {
        graph.execute(commandBuffer, frame);
    }

    void addNode(const RenderNode& node) {
        graph.addNode(node);
    }

private:
    RenderGraph graph;
};

class TrianglePass : public RenderPass {
public:
    TrianglePass() = default;

    void setup(VkRenderPass* renderPass,
               std::vector<VkFramebuffer>* framebuffers,
               VkExtent2D* extent,
               VkPipeline* pipeline,
               VkPipelineLayout* pipelineLayout,
               ResourceHandle* vertexBuffer,
               std::vector<VkDescriptorSet>* descriptorSets) {
        this->renderPass = renderPass;
        this->framebuffers = framebuffers;
        this->extent = extent;
        this->pipeline = pipeline;
        this->pipelineLayout = pipelineLayout;
        this->vertexBuffer = vertexBuffer;
        this->descriptorSets = descriptorSets;
        initialized = true;
    }

    void execute(VkCommandBuffer commandBuffer, FrameContext& frame) override {
        if (!initialized) {
            return;
        }

        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = *renderPass;
        renderPassInfo.framebuffer = (*framebuffers)[frame.swapchainImageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = *extent;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent->width);
        viewport.height = static_cast<float>(extent->height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = *extent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        VkBuffer vertexBuffers[] = {vertexBuffer->buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            *pipelineLayout,
            0,
            1,
            &((*descriptorSets)[frame.frameIndex]),
            0,
            nullptr
        );

        vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    }

private:
    bool initialized = false;
    VkRenderPass* renderPass = nullptr;
    std::vector<VkFramebuffer>* framebuffers = nullptr;
    VkExtent2D* extent = nullptr;
    VkPipeline* pipeline = nullptr;
    VkPipelineLayout* pipelineLayout = nullptr;
    ResourceHandle* vertexBuffer = nullptr;
    std::vector<VkDescriptorSet>* descriptorSets = nullptr;
};

class FullscreenPass : public RenderPass {
public:
    FullscreenPass() = default;

    void setup(VkRenderPass* renderPass,
               std::vector<VkFramebuffer>* framebuffers,
               VkExtent2D* extent,
               VkPipeline* pipeline,
               VkPipelineLayout* pipelineLayout,
               std::vector<VkDescriptorSet>* descriptorSets) {
        this->renderPass = renderPass;
        this->framebuffers = framebuffers;
        this->extent = extent;
        this->pipeline = pipeline;
        this->pipelineLayout = pipelineLayout;
        this->descriptorSets = descriptorSets;
        initialized = true;
    }

    void setPostDrawCallback(std::function<void(VkCommandBuffer, FrameContext&)> callback) {
        postDrawCallback = std::move(callback);
    }

    void execute(VkCommandBuffer commandBuffer, FrameContext& frame) override {
        if (!initialized) {
            return;
        }

        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = *renderPass;
        renderPassInfo.framebuffer = (*framebuffers)[frame.swapchainImageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = *extent;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            *pipelineLayout,
            0,
            1,
            &((*descriptorSets)[frame.frameIndex]),
            0,
            nullptr
        );

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent->width);
        viewport.height = static_cast<float>(extent->height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = *extent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        if (postDrawCallback) {
            postDrawCallback(commandBuffer, frame);
        }
        vkCmdEndRenderPass(commandBuffer);
    }

private:
    bool initialized = false;
    VkRenderPass* renderPass = nullptr;
    std::vector<VkFramebuffer>* framebuffers = nullptr;
    VkExtent2D* extent = nullptr;
    VkPipeline* pipeline = nullptr;
    VkPipelineLayout* pipelineLayout = nullptr;
    std::vector<VkDescriptorSet>* descriptorSets = nullptr;
    std::function<void(VkCommandBuffer, FrameContext&)> postDrawCallback;
};

class ResourceSystem {
public:
    void init(VkDevice deviceHandle, VkPhysicalDevice physicalDeviceHandle) {
        device = deviceHandle;
        physicalDevice = physicalDeviceHandle;
    }

    ResourceHandle createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
        ResourceHandle handle;
        handle.type = ResourceHandle::Type::Buffer;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &handle.buffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create buffer");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, handle.buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &handle.memory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate buffer memory");
        }

        vkBindBufferMemory(device, handle.buffer, handle.memory, 0);
        return handle;
    }

    ResourceHandle createImage(uint32_t width, uint32_t height, VkFormat format,
                               VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties) {
        ResourceHandle handle;
        handle.type = ResourceHandle::Type::Image;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device, &imageInfo, nullptr, &handle.image) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, handle.image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &handle.memory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate image memory");
        }

        vkBindImageMemory(device, handle.image, handle.memory, 0);
        return handle;
    }

    void destroy(ResourceHandle& handle) {
        if (handle.type == ResourceHandle::Type::Buffer) {
            if (handle.buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, handle.buffer, nullptr);
                handle.buffer = VK_NULL_HANDLE;
            }
            if (handle.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, handle.memory, nullptr);
                handle.memory = VK_NULL_HANDLE;
            }
        } else if (handle.type == ResourceHandle::Type::Image) {
            if (handle.image != VK_NULL_HANDLE) {
                vkDestroyImage(device, handle.image, nullptr);
                handle.image = VK_NULL_HANDLE;
            }
            if (handle.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, handle.memory, nullptr);
                handle.memory = VK_NULL_HANDLE;
            }
        }
        handle.type = ResourceHandle::Type::Unknown;
    }

    void cleanup() {
        device = VK_NULL_HANDLE;
        physicalDevice = VK_NULL_HANDLE;
    }

private:
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("failed to find suitable memory type");
    }

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
};

struct VideoTextureResources {
    struct StagingSlot {
        ResourceHandle buffer;
        void* mapped = nullptr;
        size_t capacity = 0;
    };

    ResourceHandle imageHandle;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorImageInfo descriptorInfo{};
    uint32_t width = 1;
    uint32_t height = 1;
    size_t frameSize = 4;
    bool ready = false;
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::array<StagingSlot, MAX_FRAMES_IN_FLIGHT> stagingSlots{};
    std::array<double, MAX_FRAMES_IN_FLIGHT> stagingTimestamps{};
    std::array<bool, MAX_FRAMES_IN_FLIGHT> pendingUploads{};
};

class App {
public:
    void run() {
        initSDL();
        createWindow();
        createVulkanInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        resourceSystem.init(device, physicalDevice);
        createSwapchain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createPipelineLayout();
        createGraphicsPipeline();
        createFullscreenPipeline();
        createSwapchainFramebuffers();
        createSwapchainSemaphores();
        createCommandPool();
        createVertexBuffer();
        createUniformBuffers();
        initializeVideoAssets();
        initVideoSystem();
        loadControlState();
        createDescriptorPool();
        createDescriptorSets();
        createUiWindow();
        initImGui();
        
        // Initialize formal NLE architecture
        renderWorker = std::make_unique<RenderWorker>();
        renderWorker->on_render_complete = [this](const std::string& source_file) {
            std::cout << "[Render] Completed, new version at: " << source_file << std::endl;
            // Auto-reload disabled to prevent crash with high-resolution videos
            // User can manually reload if needed
        };

        g_project_state.active_file = videoSourcePath;
        // Reset all to auto-detect (0 = auto)
        g_project_state.width = 0;
        g_project_state.height = 0;
        g_project_state.fps = 0;

        fullscreenPass.setup(&renderPass,
                            &swapchainFramebuffers,
                            &swapchainExtent,
                            &fullscreenPipeline,
                            &pipelineLayout,
                            &descriptorSets);

        renderer.addNode({
            "FullscreenProcedural",
            {},
            {},
            [&](VkCommandBuffer cmd, FrameContext& frame) {
                fullscreenPass.execute(cmd, frame);
            }
        });

        createCommandBuffers();
        frameSystem.init(device, MAX_FRAMES_IN_FLIGHT);
        startTime = std::chrono::steady_clock::now();
        lastControlSaveTime = startTime;
        lastFrameTimestamp = startTime;
        initializationComplete = true;
        mainLoop();
        cleanup();
    }

private:
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct QueueFamilyIndices {
        uint32_t graphicsFamily;
        uint32_t presentFamily;
        bool graphicsFamilyFound;
        bool presentFamilyFound;
    };

    SDL_Window* window = nullptr;
    SDL_Window* uiWindow = nullptr;
    SDL_Renderer* uiRenderer = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    QueueFamilyIndices queueFamilyIndices{};
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkImageView> swapchainImageViews;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    std::vector<VkSemaphore> swapchainRenderSemaphores;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    FrameSystem frameSystem;
    ResourceSystem resourceSystem;
    Renderer renderer;
    TrianglePass trianglePass;
    FullscreenPass fullscreenPass;
    bool running = true;
    bool framebufferResized = false;
    bool resizePending = false;
    bool initializationComplete = false;
    VkExtent2D lastDrawableExtent{0, 0};
    uint32_t resizeStableFrames = 0;
    static constexpr uint32_t RESIZE_STABILITY_THRESHOLD = 2;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkPipeline fullscreenPipeline = VK_NULL_HANDLE;
    VkPipeline tonemapPipeline = VK_NULL_HANDLE;
    ResourceHandle vertexBufferHandle;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;
    std::vector<ResourceHandle> uniformBufferHandles;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    std::chrono::steady_clock::time_point startTime;
    std::vector<FrameContext*> imagesInFlight;
    int currentMode = 0;
    struct VisualControls {
        float animationSpeed = 0.3f;
        float tempo = 1.0f;
        float energy = 0.5f;
        float bass = 0.3f;
        float mid = 0.3f;
        float high = 0.3f;
        float colorBlend = 0.5f;
        glm::vec4 primaryColor = glm::vec4(0.9f, 0.4f, 0.1f, 1.0f);
        glm::vec4 secondaryColor = glm::vec4(0.1f, 0.5f, 0.8f, 1.0f);
        int activeMode = -1;
        float videoMix = 1.0f;
        float videoPlaybackRate = 1.0f;
        float videoDecodeOversample = 1.0f;
        float grayscaleAmount = 0.0f;
        float sharpenAmount = 0.35f;
        bool upscaleEnabled = true;
        float loopBlendSeconds = 0.5f;
        int forcedFpsIndex = 0;
        float crtCurvature = 0.15f;
        float crtHorizontalCurvature = 0.15f;
        float crtScanlineIntensity = 0.35f;
        float crtMaskIntensity = 0.35f;
        float crtVignette = 0.55f;
        float crtFishEye = 0.0f;
        float bloomIntensity = 0.45f;
        float bloomThreshold = 0.7f;
        float aberrationAmount = 0.02f;
        float grainStrength = 0.15f;
        float bendAmount = 0.0f;
        float glitchAmount = 0.0f;
        bool randomVideoStart = false;
        glm::vec3 colorBalance = glm::vec3(1.0f);
        bool enablePostCrtCurvature = true;
        bool enablePostScanMask = true;
        bool enablePostVignette = true;
        bool enablePostFishEye = true;
        bool enablePostBloom = true;
        bool enablePostAberration = true;
        bool enablePostGrain = true;
        bool enablePostBend = true;
        bool enablePostGlitch = true;
        bool enablePostColorBalance = true;
        // Color grading core
        float gradeBrightness = 0.0f;
        float gradeContrast = 1.0f;
        float gradeSaturation = 1.0f;
        float gradeHueShift = 0.0f;
        float gradeGamma = 1.0f;
        int colorLUTIndex = 0;
        float splitToneBalance = 0.5f;
        glm::vec3 splitToneShadows = glm::vec3(0.85f, 0.4f, 0.3f);
        glm::vec3 splitToneHighlights = glm::vec3(1.1f, 1.0f, 0.85f);
        // Temporal feedback
        float feedbackAmount = 0.4f;
        float trailStrength = 0.35f;
        float temporalAccumulation = 0.5f;
        float feedbackDecay = 0.25f;
        float recursiveBlend = 0.4f;
        // Spatial distortion
        float uvWarpStrength = 0.0f;
        float rippleStrength = 0.0f;
        float rippleFrequency = 1.0f;
        float swirlStrength = 0.0f;
        float displacementAmount = 0.0f;
        float kaleidoSegments = 6.0f;
        float tunnelDepth = 0.0f;
        float tunnelCurvature = 0.0f;
        // Blur / motion
        float gaussianBlur = 0.0f;
        float directionalBlur = 0.0f;
        float directionalBlurAngle = 0.0f;
        float zoomBlur = 0.0f;
        float motionBlur = 0.0f;
        float temporalBlur = 0.0f;
        // Sharpening / detail
        float unsharpMask = 0.0f;
        float casAmount = 0.0f;
        float localContrast = 0.0f;
        // Glitch / corruption
        float glitchDatamosh = 0.0f;
        float glitchRGBSplit = 0.0f;
        float glitchScanlineBreak = 0.0f;
        float glitchJitter = 0.0f;
        float glitchTearing = 0.0f;
        float glitchPixelSort = 0.0f;
        float glitchBufferCorruption = 0.0f;
        // Compositing / blending
        int blendModeProcedural = 0;
        int blendModeVideo = 1;
        int blendModeFeedback = 2;
        float blendProceduralMix = 1.0f;
        float blendVideoMix = 1.0f;
        float blendFeedbackMix = 0.5f;
        // Analog / CRT booster
        float analogScanlineFocus = 0.5f;
        float analogMaskBalance = 0.5f;
        float analogNoise = 0.2f;
        float analogBloom = 0.3f;
        float vhsDistortion = 0.0f;
        float analogChromaticAberration = 0.02f;
        bool enableColorGrading = true;
        bool enableFeedback = true;
        bool enableDistortion = true;
        bool enableBlurMotion = true;
        bool enableSharpen = true;
        bool enableGlitch = true;
        bool enableBlending = true;
        bool enableAnalog = true;
        bool enableAudioReactive = true;
        bool enableTemporal = true;
        // Audio reactivity
        float audioWarpResponse = 0.8f;
        float audioFeedbackResponse = 0.8f;
        float audioBlurResponse = 0.4f;
        float audioColorResponse = 0.6f;
        float audioGlitchResponse = 0.5f;
        float audioBeatSync = 1.0f;
        float audioLfoRate = 0.5f;
        // Temporal processing
        float temporalInterpolation = 0.0f;
        float temporalBlendStrength = 0.0f;
        float slowMotionFactor = 1.0f;
        float frameAccumulation = 0.0f;
    } visualControls;
    ImGuiContext* imguiContext = nullptr;
    bool imguiInitialized = false;
    bool showSecondaryWindow = true;
    bool showDemoWindow = false;
    uint32_t lastFrameImageIndex = 0;
    uint32_t lastFrameFrameIndex = 0;
    VideoPlayer videoPlayer;
    VideoTextureResources videoTexture;
    struct CachedVideoFrame {
        std::vector<uint8_t> pixels;
        double timestamp = 0.0;
        int width = 0;
        int height = 0;
        bool keyframe = false;
    };
    std::deque<CachedVideoFrame> videoFrameBuffer;
    std::vector<uint8_t> loopBlendScratch;
    std::vector<uint8_t> transitionFrame;  // Last frame from previous video for crossfade
    double loopBlendDuration = 0.5;
    float transitionDuration = 0.5f;  // Crossfade duration during video change
    double transitionProgress = 0.0;  // 0.0 to 1.0, 1.0 = transition complete
    bool inTransition = false;
    double videoPlaybackCursor = 0.0;
    double videoDecodeCursor = 0.0;
    double videoDisplayTimer = 0.0;
    double videoDecodeTimer = 0.0;
    std::chrono::steady_clock::time_point lastFrameTimestamp;
    bool videoSubsystemInitialized = false;
    std::string videoSourcePath = "media/sample.mp4";
    std::string pendingVideoReload;  // Thread-safe video reload request
    std::mutex videoReloadMutex;
    VideoRegistry videoRegistry;
    std::string videoAssetsRoot = "mp4s";
    int selectedVideoAsset = -1;
    VideoStaging videoStaging;
    uint64_t currentEpoch = 1;
    struct VideoRandomizerState {
        bool autoRandomize = false;
        bool useVideoDuration = false;
        float intervalSeconds = 30.0f;
        float elapsedSeconds = 0.0f;
        float currentVideoDuration = 0.0f;
        int historyWindow = 3;
        std::deque<int> recentHistory;
    } videoRandomizer;
    std::mt19937 randomEngine{std::random_device{}()};
    std::string controlStatePath = "controls_state.cfg";
    static constexpr const char* CONTROL_STATE_VERSION = "VJAY_STATE_V6";
    static constexpr const char* CONTROL_STATE_VERSION_PREV = "VJAY_STATE_V5";
    static constexpr const char* CONTROL_STATE_VERSION_PREV2 = "VJAY_STATE_V4";
    static constexpr const char* CONTROL_STATE_VERSION_PREV3 = "VJAY_STATE_V3";
    static constexpr const char* CONTROL_STATE_VERSION_PREV4 = "VJAY_STATE_V2";
    bool controlsDirty = false;
    std::chrono::steady_clock::time_point lastControlSaveTime;
    std::chrono::steady_clock::time_point lastReloadTime;
    
    // NLE Architecture Components
    EffectChain currentEffectChain;
    Timeline timeline;
    PlaybackClock playbackClock;
    bool useNLEPlayback = false;  // Toggle between old sequential and new PTS-based playback
    bool showNLEWindow = true;
    
    // Formal NLE Architecture (ProjectState system)
    std::unique_ptr<RenderWorker> renderWorker;
    uint64_t last_loaded_version = 0;

    void initSDL() {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            throw std::runtime_error(std::string("failed to initialize SDL: ") + SDL_GetError());
        }
    }

    void createWindow() {
        window = SDL_CreateWindow(
            "Vulkan",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            WIDTH,
            HEIGHT,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
        );

        if (!window) {
            throw std::runtime_error(std::string("failed to create SDL window: ") + SDL_GetError());
        }
    }

    void createUiWindow() {
        uiWindow = SDL_CreateWindow(
            "Controls",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            420,
            420,
            SDL_WINDOW_RESIZABLE
        );

        if (!uiWindow) {
            throw std::runtime_error(std::string("failed to create ImGui SDL window: ") + SDL_GetError());
        }

        uiRenderer = SDL_CreateRenderer(uiWindow, -1, SDL_RENDERER_ACCELERATED);
        if (!uiRenderer) {
            uiRenderer = SDL_CreateRenderer(uiWindow, -1, 0);
        }

        if (!uiRenderer) {
            SDL_DestroyWindow(uiWindow);
            uiWindow = nullptr;
            throw std::runtime_error(std::string("failed to create SDL renderer: ") + SDL_GetError());
        }

        SDL_SetRenderDrawBlendMode(uiRenderer, SDL_BLENDMODE_BLEND);
        if (SDL_RenderSetVSync(uiRenderer, 0) != 0) {
            std::cerr << "[ImGui] warning: failed to disable renderer vsync: " << SDL_GetError() << std::endl;
        }
    }

    void destroyUiWindow() {
        if (uiRenderer != nullptr) {
            SDL_DestroyRenderer(uiRenderer);
            uiRenderer = nullptr;
        }
        if (uiWindow != nullptr) {
            SDL_DestroyWindow(uiWindow);
            uiWindow = nullptr;
        }
    }

    void createVulkanInstance() {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Minimal Vulkan";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        if (enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("validation layers not available");
        }

        std::vector<const char*> extensions = getRequiredExtensions();

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledLayerCount = enableValidationLayers ?
            static_cast<uint32_t>(validationLayers.size()) : 0;
        createInfo.ppEnabledLayerNames = enableValidationLayers ?
            validationLayers.data() : nullptr;

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("failed to create instance!");
        }
    }

    std::vector<const char*> getRequiredExtensions() {
        uint32_t extensionCount = 0;
        SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr);

        std::vector<const char*> extensions(extensionCount);
        SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data());

        if (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    void setupDebugMessenger() {
        if (!enableValidationLayers) {
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;

        if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("failed to set up debug messenger");
        }
    }

    void destroyDebugMessenger() {
        if (!enableValidationLayers || debugMessenger == VK_NULL_HANDLE) {
            return;
        }

        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }

    void createSurface() {
        if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
            throw std::runtime_error(std::string("failed to create surface: ") + SDL_GetError());
        }
    }

    void pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        std::cout << "[GPU] Found " << deviceCount << " physical devices" << std::endl;

        if (deviceCount == 0) {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        for (const auto& device : devices) {
            if (isDeviceSuitable(device)) {
                physicalDevice = device;
                queueFamilyIndices = findQueueFamilies(device);

                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(device, &props);
                std::cout << "[GPU] Selected: " << props.deviceName << std::endl;
                std::cout << "[GPU] Graphics queue family: " << queueFamilyIndices.graphicsFamily << std::endl;
                std::cout << "[GPU] Present queue family: " << queueFamilyIndices.presentFamily << std::endl;
                break;
            }
        }

        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
        QueueFamilyIndices indices{};

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
                indices.graphicsFamilyFound = true;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

            if (presentSupport) {
                indices.presentFamily = i;
                indices.presentFamilyFound = true;
            }

            if (indices.graphicsFamilyFound && indices.presentFamilyFound) {
                break;
            }

            i++;
        }

        return indices;
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> available(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

        std::set<std::string> required = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        for (const auto& ext : available) {
            required.erase(ext.extensionName);
        }

        return required.empty();
    }

    bool isDeviceSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = findQueueFamilies(device);

        if (!indices.graphicsFamilyFound || !indices.presentFamilyFound) {
            return false;
        }

        if (!checkDeviceExtensionSupport(device)) {
            return false;
        }

        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        VkPhysicalDeviceFeatures supportedFeatures;
        vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && supportedFeatures.geometryShader) {
            return true;
        }

        return false;
    }

    void createLogicalDevice() {
        float priority = 1.0f;

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {
            queueFamilyIndices.graphicsFamily,
            queueFamilyIndices.presentFamily
        };

        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &priority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
            throw std::runtime_error("failed to create logical device!");
        }

        vkGetDeviceQueue(device, queueFamilyIndices.graphicsFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, queueFamilyIndices.presentFamily, 0, &presentQueue);
    }

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());

        return details;
    }

    VkExtent2D chooseSwapchainExtent(const SwapChainSupportDetails& support) const {
        if (support.capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return support.capabilities.currentExtent;
        }

        uint32_t width = WIDTH;
        uint32_t height = HEIGHT;
        if (window) {
            int drawableWidth = 0;
            int drawableHeight = 0;
            SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
            if (drawableWidth > 0 && drawableHeight > 0) {
                width = static_cast<uint32_t>(drawableWidth);
                height = static_cast<uint32_t>(drawableHeight);
            }
        }

        VkExtent2D extent{};
        extent.width = std::clamp(width, support.capabilities.minImageExtent.width, support.capabilities.maxImageExtent.width);
        extent.height = std::clamp(height, support.capabilities.minImageExtent.height, support.capabilities.maxImageExtent.height);
        return extent;
    }

    void handleResizeHint() {
        if (!resizePending) {
            return;
        }

        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);

        if (drawableWidth == 0 || drawableHeight == 0) {
            framebufferResized = false;
            return;
        }

        VkExtent2D currentExtent{static_cast<uint32_t>(drawableWidth), static_cast<uint32_t>(drawableHeight)};
        if (currentExtent.width == lastDrawableExtent.width && currentExtent.height == lastDrawableExtent.height) {
            if (++resizeStableFrames >= RESIZE_STABILITY_THRESHOLD) {
                framebufferResized = true;
                resizePending = false;
            }
        } else {
            resizeStableFrames = 0;
            lastDrawableExtent = currentExtent;
        }
    }

    void createSwapchain() {
        auto support = querySwapChainSupport(physicalDevice);

        VkSurfaceFormatKHR surfaceFormat = support.formats[0];
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

        VkExtent2D extent = chooseSwapchainExtent(support);

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        std::cout << "[Swapchain] Extent: " << extent.width << "x" << extent.height << std::endl;
        std::cout << "[Swapchain] Image count: " << imageCount << std::endl;
        std::cout << "[Swapchain] Format: " << surfaceFormat.format << std::endl;

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swapchain");
        }

        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

        swapchainImageFormat = surfaceFormat.format;
        swapchainExtent = extent;

        frameSystem.resizeSwapchainImages(swapchainImages.size());
        imagesInFlight.assign(swapchainImages.size(), nullptr);

        std::cout << "[Swapchain] Created successfully" << std::endl;
    }

    void createImageViews() {
        swapchainImageViews.resize(swapchainImages.size());

        std::cout << "[ImageViews] Creating " << swapchainImages.size() << " image views" << std::endl;

        for (size_t i = 0; i < swapchainImages.size(); ++i) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = swapchainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = swapchainImageFormat;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create image views");
            }
        }

        std::cout << "[ImageViews] Created successfully" << std::endl;
    }

    void createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create geometry render pass");
        }

        std::cout << "[GeometryPass] Render pass created" << std::endl;
    }

    void createSwapchainFramebuffers() {
        swapchainFramebuffers.resize(swapchainImageViews.size());

        for (size_t i = 0; i < swapchainImageViews.size(); ++i) {
            VkImageView attachments[] = {swapchainImageViews[i]};

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = swapchainExtent.width;
            framebufferInfo.height = swapchainExtent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer");
            }
        }

        std::cout << "[Swapchain] Framebuffers created" << std::endl;
    }

    void createSwapchainSemaphores() {
        destroySwapchainSemaphores();

        swapchainRenderSemaphores.resize(swapchainImages.size(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for (size_t i = 0; i < swapchainRenderSemaphores.size(); ++i) {
            if (vkCreateSemaphore(device, &info, nullptr, &swapchainRenderSemaphores[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create swapchain render semaphore");
            }
        }
    }

    void destroySwapchainSemaphores() {
        for (auto& semaphore : swapchainRenderSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, semaphore, nullptr);
                semaphore = VK_NULL_HANDLE;
            }
        }
        swapchainRenderSemaphores.clear();
    }

    void cleanupSwapchain() {
        destroySwapchainSemaphores();
        for (auto framebuffer : swapchainFramebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
        }
        swapchainFramebuffers.clear();

        if (!swapchainImageViews.empty()) {
            for (auto imageView : swapchainImageViews) {
                if (imageView != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, imageView, nullptr);
                }
            }
            swapchainImageViews.clear();
        }

        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }

        swapchainImages.clear();
    }

    void recreateSwapchain() {
        if (!initializationComplete) {
            std::cout << "[Swapchain] recreateSwapchain blocked during initialization" << std::endl;
            framebufferResized = true;
            return;
        }
        std::cout << "[SWAPCHAIN] RECREATE START" << std::endl;
        vkDeviceWaitIdle(device);

        cleanupSwapchain();
        std::cout << "[SWAPCHAIN] DESTROY old resources" << std::endl;

        // Reset command buffers to clear references to old swapchain resources
        for (auto cmdBuffer : commandBuffers) {
            vkResetCommandBuffer(cmdBuffer, 0);
        }

        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);

        if (drawableWidth == 0 || drawableHeight == 0) {
            framebufferResized = true;
            return;
        }

        createSwapchain();
        createImageViews();
        createSwapchainFramebuffers();
        createSwapchainSemaphores();
        frameSystem.resizeSwapchainImages(swapchainImages.size());
        std::cout << "[SWAPCHAIN] CREATE new extent=" << drawableWidth << "," << drawableHeight << std::endl;

        std::cout << "[RESET] frameIndex=0 fences reset staging reset" << std::endl;
        frameSystem.resetCurrentFrame();
        videoTexture.pendingUploads.fill(false);

        uint32_t targetVideoWidth = std::max<uint32_t>(1u, videoTexture.width);
        uint32_t targetVideoHeight = std::max<uint32_t>(1u, videoTexture.height);
        if (videoSubsystemInitialized && videoPlayer.isReady()) {
            std::cout << "[VIDEO] Reinitializing decoder after swapchain resize" << std::endl;
            if (!videoPlayer.reinitialize()) {
                std::cerr << "[VIDEO] Decoder reinit failed after swapchain resize" << std::endl;
                videoSubsystemInitialized = false;
            } else {
                targetVideoWidth = static_cast<uint32_t>(std::max(1, videoPlayer.width()));
                targetVideoHeight = static_cast<uint32_t>(std::max(1, videoPlayer.height()));
            }
        }

        createVideoTextureResources(targetVideoWidth, targetVideoHeight);

        // Update descriptor sets to ensure they point to valid resources
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT && i < descriptorSets.size(); ++i) {
            updateDescriptorSet(i);
        }

        std::cout << "[SWAPCHAIN] RECREATE END" << std::endl;
    }

    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding samplerBinding{};
        samplerBinding.binding = 1;
        samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBinding.descriptorCount = 1;
        samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        samplerBinding.pImmutableSamplers = nullptr;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerBinding};
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout");
        }
    }

    void createPipelineLayout() {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout");
        }

        std::cout << "[Pipeline] Layout created" << std::endl;
    }

    void createGraphicsPipeline() {
        auto vertShaderCode = readFile("shaders/triangle.vert.spv");
        auto fragShaderCode = readFile("shaders/triangle.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShaderModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShaderModule;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &binding;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create geometry pipeline");
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        std::cout << "[Pipeline] Geometry pipeline created" << std::endl;
    }

    void createFullscreenPipeline() {
        auto vertShaderCode = readFile("shaders/fullscreen.vert.spv");
        auto fragShaderCode = readFile("shaders/fullscreen.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShaderModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShaderModule;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &fullscreenPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create fullscreen pipeline");
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        std::cout << "[Pipeline] Fullscreen pipeline created" << std::endl;
    }

    void createUniformBuffers() {
        VkDeviceSize bufferSize = sizeof(GlobalUBO);
        uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBufferHandles.resize(MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            ResourceHandle handle = resourceSystem.createBuffer(
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            uniformBufferHandles[i] = handle;
            uniformBuffers[i] = handle.buffer;
            uniformBuffersMemory[i] = handle.memory;

            vkMapMemory(device, uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
        }
    }

    void initVideoSystem() {
        if (videoSubsystemInitialized) {
            return;
        }

        if (!fs::exists(videoSourcePath)) {
            std::cerr << "[Video] Source not found: " << videoSourcePath << std::endl;
            return;
        }

        if (!videoPlayer.initialize(videoSourcePath)) {
            std::cerr << "[Video] Failed to initialize player for " << videoSourcePath << std::endl;
            return;
        }

        std::cout << "[Video] Player initialized: " << videoSourcePath << " (" << videoPlayer.width() << "x" << videoPlayer.height() << ")" << std::endl;

        // Reset lastFrameTimestamp to prevent large deltaTime on first frame
        lastFrameTimestamp = std::chrono::steady_clock::now();

        if (visualControls.randomVideoStart && videoPlayer.durationSeconds() > 0.1) {
            std::uniform_real_distribution<double> dist(0.0, std::max(0.0, videoPlayer.durationSeconds() - videoPlayer.frameDuration()));
            double target = dist(randomEngine);
            if (videoPlayer.seekSeconds(target)) {
                resetVideoPlaybackState(target);
            } else {
                resetVideoPlaybackState(0.0);
            }
        } else {
            resetVideoPlaybackState(0.0);
        }

        createVideoTextureResources(static_cast<uint32_t>(std::max(1, videoPlayer.width())),
                                    static_cast<uint32_t>(std::max(1, videoPlayer.height())));

        videoSubsystemInitialized = videoTexture.ready;
        if (!videoSubsystemInitialized) {
            return;
        }

        videoRandomizer.currentVideoDuration = static_cast<float>(expectedPlaybackDurationSeconds());

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT && i < descriptorSets.size(); ++i) {
            updateDescriptorSet(i);
        }

        std::cout << "[Video] Initialized " << videoSourcePath << " (" << videoPlayer.width() << "x" << videoPlayer.height() << ")" << std::endl;
    }

    void createVideoTextureResources(uint32_t width, uint32_t height) {
        // Check if recreation is necessary
        if (videoTexture.ready && videoTexture.width == width && videoTexture.height == height) {
            std::cout << "[STAGING] Skipping recreation (dimensions unchanged: " << width << "x" << height << ")" << std::endl;
            return;
        }

        std::cout << "[STAGING] Creating video texture resources: " << width << "x" << height << std::endl;
        destroyVideoTexture();

        videoTexture.width = width;
        videoTexture.height = height;
        videoTexture.frameSize = static_cast<size_t>(width) * height * 4;

        videoTexture.imageHandle = resourceSystem.createImage(
            width,
            height,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = videoTexture.imageHandle.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &videoTexture.imageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create video image view");
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        if (vkCreateSampler(device, &samplerInfo, nullptr, &videoTexture.sampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create video sampler");
        }

        initializeVideoImage(videoTexture.imageHandle.image);

        videoTexture.descriptorInfo.sampler = videoTexture.sampler;
        videoTexture.descriptorInfo.imageView = videoTexture.imageView;
        videoTexture.descriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        videoTexture.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        videoTexture.pendingUploads.fill(false);

        if (!videoStaging.init(MAX_FRAMES_IN_FLIGHT, videoTexture.frameSize)) {
            throw std::runtime_error("failed to initialize VideoStaging");
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (videoTexture.frameSize == 0) {
                videoStaging.setSlot(i, VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, 0);
                continue;
            }

            auto buffer = resourceSystem.createBuffer(
                static_cast<VkDeviceSize>(videoTexture.frameSize),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            std::cout << "[STAGING] slot=" << i << " allocated capacity=" << videoTexture.frameSize << std::endl;

            void* mapped = nullptr;
            if (vkMapMemory(device, buffer.memory, 0, static_cast<VkDeviceSize>(videoTexture.frameSize), 0, &mapped) != VK_SUCCESS) {
                resourceSystem.destroy(buffer);
                throw std::runtime_error("failed to map video staging buffer");
            }

            std::cout << "[STAGING] slot=" << i << " mapped ptr=" << mapped << std::endl;

            videoStaging.setSlot(i, buffer.buffer, buffer.memory, mapped, videoTexture.frameSize);
        }

        videoTexture.ready = true;
    }

    void initializeVideoImage(VkImage image) {
        if (image == VK_NULL_HANDLE) {
            return;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffer for video image init");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            throw std::runtime_error("failed to begin command buffer for video image init");
        }

        auto transition = [&](VkImageLayout oldLayout, VkImageLayout newLayout,
                              VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkAccessFlags srcAccessMask = 0;
            VkAccessFlags dstAccessMask = 0;

            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            }

            barrier.srcAccessMask = srcAccessMask;
            barrier.dstAccessMask = dstAccessMask;

            vkCmdPipelineBarrier(
                commandBuffer,
                srcStage,
                dstStage,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier);
        };

        transition(VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkImageSubresourceRange clearRange{};
        clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearRange.baseMipLevel = 0;
        clearRange.levelCount = 1;
        clearRange.baseArrayLayer = 0;
        clearRange.layerCount = 1;

        vkCmdClearColorImage(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clearColor,
            1,
            &clearRange);

        transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            throw std::runtime_error("failed to record command buffer for video image init");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            throw std::runtime_error("failed to submit command buffer for video image init");
        }

        vkQueueWaitIdle(graphicsQueue);
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void resetVideoPlaybackState(double startTime = 0.0) {
        videoPlaybackCursor = startTime;
        videoDecodeCursor = startTime;
        videoDisplayTimer = 0.0;
        videoDecodeTimer = 0.0;
        inTransition = false;
        transitionProgress = 0.0;
        transitionFrame.clear();
        videoFrameBuffer.clear();
    }

    bool decodeFrameIntoBuffer(double frameDuration) {
        if (!videoSubsystemInitialized || !videoPlayer.isReady() || videoTexture.frameSize == 0) {
            return false;
        }

        // Cooldown after reload to prevent FFmpeg crashes
        auto now = std::chrono::steady_clock::now();
        auto timeSinceReload = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReloadTime).count();
        if (timeSinceReload < 500) {
            return false;
        }

        double duration = videoPlayer.durationSeconds();
        bool didLoop = false;
        if (duration > 0.1 && videoDecodeCursor >= duration) {
            // Soft loop: just reset cursors, don't clear buffer to prevent FPS drops
            videoDecodeCursor = 0.0;
            videoPlaybackCursor = 0.0;
            // Keep last few frames for smooth transition
            while (videoFrameBuffer.size() > 2) {
                videoFrameBuffer.pop_front();
            }
            didLoop = true;
        }

        try {
            CachedVideoFrame frame;
            frame.timestamp = videoDecodeCursor;
            frame.width = static_cast<int>(videoTexture.width);
            frame.height = static_cast<int>(videoTexture.height);
            frame.pixels.resize(videoTexture.frameSize);

            int decodedWidth = 0;
            int decodedHeight = 0;
            if (!videoPlayer.grabFrameInto(frame.pixels.data(), frame.pixels.size(), decodedWidth, decodedHeight)) {
                if (didLoop) {
                    return false;
                }
                return false;
            }

            if (decodedWidth != static_cast<int>(videoTexture.width) || decodedHeight != static_cast<int>(videoTexture.height)) {
                std::cout << "[VIDEO] Dimension change detected: " << videoTexture.width << "x" << videoTexture.height
                          << " -> " << decodedWidth << "x" << decodedHeight << std::endl;
                createVideoTextureResources(decodedWidth, decodedHeight);
                frame.width = decodedWidth;
                frame.height = decodedHeight;
                videoFrameBuffer.clear();
                videoFrameBuffer.push_back(frame);
                videoDecodeCursor = frame.timestamp + frameDuration;
                return true;
            }

            frame.width = decodedWidth;
            frame.height = decodedHeight;
            videoFrameBuffer.push_back(std::move(frame));
            // Limit buffer size to prevent memory growth
            while (videoFrameBuffer.size() > 4) {
                videoFrameBuffer.pop_front();
            }
            videoDecodeCursor += frameDuration;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[VIDEO] Exception in decodeFrameIntoBuffer: " << e.what() << std::endl;
            return false;
        } catch (...) {
            std::cerr << "[VIDEO] Unknown exception in decodeFrameIntoBuffer" << std::endl;
            return false;
        }
    }

    bool crossfadeFrames(const CachedVideoFrame& a, const CachedVideoFrame& b, float mix, uint8_t* dst, size_t capacity) {
        mix = std::clamp(mix, 0.0f, 1.0f);
        size_t copySize = static_cast<size_t>(a.width) * a.height * 4;
        if (copySize == 0 || copySize > capacity || a.pixels.empty() || b.pixels.empty() || a.width != b.width || a.height != b.height) {
            return false;
        }
        if (loopBlendScratch.size() < copySize) {
            loopBlendScratch.resize(copySize);
        }
        const uint8_t* pa = a.pixels.data();
        const uint8_t* pb = b.pixels.data();
        uint8_t* out = dst;
        for (size_t i = 0; i < copySize; ++i) {
            float value = pa[i] * (1.0f - mix) + pb[i] * mix;
            out[i] = static_cast<uint8_t>(std::round(std::clamp(value, 0.0f, 255.0f)));
        }
        return true;
    }

    void pruneVideoFrameBuffer(double minTimestamp) {
        // Keep buffer small to prevent accumulation and lag
        while (videoFrameBuffer.size() > 4) {
            videoFrameBuffer.pop_front();
        }
    }

    double clipFramesPerSecond() const {
        if (!videoPlayer.isReady()) {
            return 0.0;
        }
        double frameDuration = videoPlayer.frameDuration();
        if (frameDuration <= 1e-6) {
            return 0.0;
        }
        return 1.0 / frameDuration;
    }

    double effectivePlaybackRate(double clipFps) const {
        double baseRate = std::clamp(static_cast<double>(visualControls.videoPlaybackRate), 0.05, 8.0);
        if (clipFps <= 0.0) {
            return baseRate;
        }
        int idx = std::clamp(visualControls.forcedFpsIndex, 0, static_cast<int>(FORCED_FPS_OPTIONS.size()) - 1);
        int forcedFps = FORCED_FPS_OPTIONS[idx];
        if (forcedFps <= 0) {
            return baseRate;
        }
        double forcedRate = forcedFps / clipFps;
        return std::clamp(forcedRate, 0.05, 8.0);
    }

    bool sampleFrameForTime(double targetTime, uint8_t* dst, size_t capacity, size_t& outCopySize) {
        outCopySize = 0;
        if (videoFrameBuffer.empty() || dst == nullptr) {
            return false;
        }

        const CachedVideoFrame* frameA = &videoFrameBuffer.front();
        const CachedVideoFrame* frameB = frameA;

        double minDelta = std::numeric_limits<double>::max();
        for (const auto& cached : videoFrameBuffer) {
            double delta = std::abs(cached.timestamp - targetTime);
            if (delta < minDelta) {
                minDelta = delta;
                frameA = &cached;
            }
        }
        frameB = frameA;

        loopBlendDuration = std::clamp(static_cast<double>(visualControls.loopBlendSeconds), 0.0, 5.0);
        const double duration = videoPlayer.durationSeconds();
        const bool loopingClip = videoPlayer.isReady() && duration > 0.1 && videoFrameBuffer.size() >= 2;

        size_t copySize = static_cast<size_t>(frameA->width) * frameA->height * 4;
        if (copySize == 0 || copySize > capacity || frameA->pixels.empty()) {
            return false;
        }

        bool didCrossfade = false;
        if (loopingClip && loopBlendDuration > 0.01) {
            const double epsilon = 1e-3;
            const double nearEndStart = std::max(0.0, duration - loopBlendDuration);
            const bool nearStart = targetTime <= loopBlendDuration;
            const bool nearEnd = targetTime >= nearEndStart - epsilon;
            if (nearStart || nearEnd) {
                const CachedVideoFrame* earliest = &videoFrameBuffer.front();
                const CachedVideoFrame* latest = &videoFrameBuffer.back();
                if (earliest && latest && earliest != latest && earliest->width == latest->width && earliest->height == latest->height) {
                    float mix = 0.0f;
                    if (nearStart) {
                        mix = static_cast<float>(targetTime / std::max(loopBlendDuration, epsilon));
                    } else {
                        double timeIntoBlend = duration - targetTime;
                        mix = static_cast<float>(std::clamp(timeIntoBlend / std::max(loopBlendDuration, epsilon), 0.0, 1.0));
                    }
                    if (crossfadeFrames(*latest, *earliest, mix, dst, capacity)) {
                        outCopySize = copySize;
                        didCrossfade = true;
                    }
                }
            }
        }

        if (!didCrossfade) {
            std::memcpy(dst, frameA->pixels.data(), copySize);
            outCopySize = copySize;
        }
        return true;
    }

    double expectedPlaybackDurationSeconds() const {
        double duration = videoPlayer.durationSeconds();
        if (duration <= 0.1) {
            return 0.0;
        }
        double clipFps = clipFramesPerSecond();
        double rate = effectivePlaybackRate(clipFps);
        double oversample = std::max(1.0, static_cast<double>(visualControls.videoDecodeOversample));
        double effective = std::max(rate, oversample);
        return duration * (rate / effective);
    }

    void updateVideoTexture(float deltaTime, FrameContext& frame) {
        if (!videoSubsystemInitialized) {
            return;
        }

        try {
            // Version-based reload check
            uint64_t current_version = g_project_state.get_version();
            if (current_version != last_loaded_version) {
                // Reload video source if version changed
                std::cout << "[Video] Version changed from " << last_loaded_version << " to " << current_version << ", reloading" << std::endl;
                reloadVideoSource(g_project_state.active_file);
                last_loaded_version = current_version;
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Video] Exception in version check: " << e.what() << std::endl;
            return;
        }

        const uint32_t writeSlot = frame.frameIndex % MAX_FRAMES_IN_FLIGHT;
        videoTrace("[VIDEO] decode start frameIndex=", frame.frameIndex);
        videoTrace("[VIDEO] snapshot/write slot=", writeSlot);

        // Debug: log frame info every 60 frames
        static int debugFrameCounter = 0;
        static auto lastDebugTime = std::chrono::steady_clock::now();
        if (++debugFrameCounter % 60 == 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDebugTime).count();
            double actualFps = elapsed > 0 ? (60000.0 / elapsed) : 0.0;
            double targetFps = videoPlayer.frameDuration() > 0 ? (1.0 / videoPlayer.frameDuration()) : 0.0;
            lastDebugTime = now;

            std::cout << "[FRAME DEBUG] frameIndex=" << frame.frameIndex
                      << " writeSlot=" << writeSlot
                      << " videoSize=" << videoTexture.width << "x" << videoTexture.height
                      << " screenSize=" << swapchainExtent.width << "x" << swapchainExtent.height
                      << " targetFps=" << targetFps
                      << " actualFps=" << actualFps
                      << " elapsed=" << elapsed << "ms" << std::endl;
        }

        if (writeSlot >= videoStaging.getSlotCount()) {
            std::cerr << "[STAGING] invalid write slot " << writeSlot
                      << " (slotCount=" << videoStaging.getSlotCount() << ")" << std::endl;
            return;
        }

        try {
            auto& slot = videoStaging.getSlot(writeSlot);
            if (slot.mapped == nullptr || slot.capacity == 0) {
                std::cerr << "[STAGING] slot " << writeSlot << " is not mapped" << std::endl;
                return;
            }

            // Wait for fence of the write slot to ensure GPU is done with it
            VkFence slotFence = frameSystem.getFence(writeSlot);
            if (slotFence != VK_NULL_HANDLE) {
                vkWaitForFences(device, 1, &slotFence, VK_TRUE, UINT64_MAX);
            }

        const double frameDuration = std::max(1e-6, videoPlayer.frameDuration());

        // NLE Playback Mode: Use PTS-based clock
        if (useNLEPlayback) {
            try {
                std::cout << "[NLE] NLE mode activated" << std::endl;
                // Apply speed factor from EffectChain to the clock
                if (currentEffectChain.slow_factor != playbackClock.getSpeedFactor()) {
                    playbackClock.setSpeed(currentEffectChain.slow_factor);
                }

                // The clock auto-advances based on wall-clock time
                // No manual delta update needed - getCurrentTime() handles it

                double targetTime = playbackClock.getCurrentTime();

                // Handle loop
                double duration = videoPlayer.durationSeconds();
                if (duration > 0.1 && targetTime >= duration) {
                    playbackClock.seek(0.0);
                    targetTime = 0.0;
                }

                // Get frame at PTS
                std::vector<uint8_t> frameData;
                int frameWidth, frameHeight;
                std::cout << "[NLE] Getting frame at PTS=" << targetTime << std::endl;
                if (videoPlayer.getFrameAtPTS(targetTime, frameData, frameWidth, frameHeight)) {
                    size_t copySize = std::min(frameData.size(), slot.capacity);
                    std::cout << "[NLE] Frame data size=" << frameData.size() << " copySize=" << copySize << std::endl;
                    std::memcpy(slot.mapped, frameData.data(), copySize);

                    videoTexture.stagingTimestamps[writeSlot] = targetTime;
                    videoTexture.pendingUploads[writeSlot] = true;

                    videoTrace("[NLE] PTS-based frame at t=", targetTime, " size=", copySize);
                } else {
                    std::cerr << "[NLE] Failed to get frame at PTS=" << targetTime << std::endl;
                }
                return;
            } catch (const std::exception& e) {
                std::cerr << "[NLE] Exception: " << e.what() << std::endl;
                return;
            }
        }

        // Original sequential playback mode
        const double clipFps = (frameDuration > 1e-6) ? (1.0 / frameDuration) : 0.0;
        const double playbackRate = effectivePlaybackRate(clipFps);
        const double decodeOversample = std::max(1.0, static_cast<double>(visualControls.videoDecodeOversample));
        const double decodeSpeed = std::max(playbackRate, decodeOversample);
        const double decodeInterval = frameDuration / decodeSpeed;
        const double displayInterval = frameDuration / playbackRate;

        videoDecodeTimer += deltaTime;
        int decodeIterations = 0;
        while (videoDecodeTimer >= decodeInterval) {
            if (!decodeFrameIntoBuffer(frameDuration)) {
                break;
            }
            videoDecodeTimer -= decodeInterval;
            if (++decodeIterations > 16) {
                break;
            }
        }

        if (videoFrameBuffer.empty()) {
            if (!decodeFrameIntoBuffer(frameDuration)) {
                return;
            }
        }

        videoDisplayTimer += deltaTime;
        while (videoDisplayTimer >= displayInterval) {
            videoDisplayTimer -= displayInterval;
            videoPlaybackCursor += frameDuration;
        }

        double fraction = displayInterval > 1e-6 ? (videoDisplayTimer / displayInterval) : 0.0;
        double targetTime = videoPlaybackCursor + fraction * frameDuration;

        size_t copySize = 0;
        if (!sampleFrameForTime(targetTime, static_cast<uint8_t*>(slot.mapped), slot.capacity, copySize)) {
            return;
        }

        // Apply crossfade transition if in transition
        if (inTransition && !transitionFrame.empty()) {
            transitionProgress += deltaTime / transitionDuration;
            if (transitionProgress >= 1.0) {
                transitionProgress = 1.0;
                inTransition = false;
                transitionFrame.clear();  // Free memory after transition
            }

            float mix = static_cast<float>(transitionProgress);
            size_t transitionSize = static_cast<size_t>(videoTexture.width) * videoTexture.height * 4;
            if (transitionSize > 0 && transitionSize <= slot.capacity && transitionFrame.size() >= transitionSize) {
                if (loopBlendScratch.size() < transitionSize) {
                    loopBlendScratch.resize(transitionSize);
                }
                const uint8_t* prevFrame = transitionFrame.data();
                const uint8_t* currFrame = static_cast<const uint8_t*>(slot.mapped);
                uint8_t* out = loopBlendScratch.data();
                for (size_t i = 0; i < transitionSize; ++i) {
                    float value = prevFrame[i] * (1.0f - mix) + currFrame[i] * mix;
                    out[i] = static_cast<uint8_t>(std::round(std::clamp(value, 0.0f, 255.0f)));
                }
                std::memcpy(slot.mapped, loopBlendScratch.data(), transitionSize);
            }
        }

        pruneVideoFrameBuffer(videoPlaybackCursor - frameDuration * 2.0);

        videoTexture.stagingTimestamps[writeSlot] = targetTime;

        videoTrace("[STAGING] writing slot=", writeSlot, " size=", copySize, " fenceState=SIGNALLED t=", targetTime);
        videoTexture.pendingUploads[writeSlot] = true;
        } catch (const std::exception& e) {
            std::cerr << "[VIDEO] Exception in video decode: " << e.what() << std::endl;
            return;
        }
    }

    void destroyVideoTexture() {
        std::cout << "[STAGING] destroyVideoTexture called" << std::endl;
        if (videoTexture.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, videoTexture.imageView, nullptr);
            videoTexture.imageView = VK_NULL_HANDLE;
        }
        if (videoTexture.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, videoTexture.sampler, nullptr);
            videoTexture.sampler = VK_NULL_HANDLE;
        }

        for (size_t i = 0; i < videoStaging.getSlotCount(); ++i) {
            const auto& slot = videoStaging.getSlot(i);
            std::cout << "[STAGING] destroy slot=" << i << " mapped=" << slot.mapped << " capacity=" << slot.capacity << std::endl;
            if (slot.mapped) {
                vkUnmapMemory(device, slot.memory);
            }
            if (slot.buffer != VK_NULL_HANDLE) {
                ResourceHandle handle;
                handle.type = ResourceHandle::Type::Buffer;
                handle.buffer = slot.buffer;
                handle.memory = slot.memory;
                resourceSystem.destroy(handle);
            }
            videoStaging.clearSlot(i);
        }
        if (videoTexture.imageHandle.type != ResourceHandle::Type::Unknown) {
            resourceSystem.destroy(videoTexture.imageHandle);
            videoTexture.imageHandle = {};
        }
        videoTexture.ready = false;
        videoStaging.destroy();
        std::cout << "[STAGING] destroyVideoTexture completed" << std::endl;
        videoTexture.descriptorInfo = {};
        videoTexture.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        videoTexture.pendingUploads.fill(false);
    }

    void initializeVideoAssets() {
        videoRegistry.scan(videoAssetsRoot);
        const auto& assets = videoRegistry.getAssets();
        if (!assets.empty()) {
            selectedVideoAsset = 0;
            videoSourcePath = assets[0].metadata.path;
            updateRandomizerForSelection(selectedVideoAsset, true);
            if (!assets.empty()) {
                videoRandomizer.currentVideoDuration = static_cast<float>(assets[0].metadata.duration);
            }
        } else {
            selectedVideoAsset = -1;
            videoRandomizer.recentHistory.clear();
            videoRandomizer.currentVideoDuration = 0.0f;
        }
    }

    void drawVideoAssetSelector() {
        const auto& assets = videoRegistry.getAssets();
        if (assets.empty()) {
            ImGui::TextDisabled("No videos found in %s", videoAssetsRoot.c_str());
            return;
        }

        if (selectedVideoAsset < 0 || selectedVideoAsset >= static_cast<int>(assets.size())) {
            selectedVideoAsset = 0;
            updateRandomizerForSelection(selectedVideoAsset, true);
        }

        if (ImGui::BeginCombo("Video Asset", assets[selectedVideoAsset].metadata.filename.c_str())) {
            for (int i = 0; i < static_cast<int>(assets.size()); ++i) {
                bool isSelected = (i == selectedVideoAsset);
                if (ImGui::Selectable(assets[i].metadata.filename.c_str(), isSelected)) {
                    if (selectedVideoAsset != i) {
                        selectedVideoAsset = i;
                        updateRandomizerForSelection(i, true);
                        reloadVideoSource(assets[i].metadata.path);
                    }
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    void updateRandomizerForSelection(int index, bool recordHistory) {
        const auto& assets = videoRegistry.getAssets();
        if (index >= 0 && index < static_cast<int>(assets.size())) {
            float duration = static_cast<float>(assets[index].metadata.duration);
            if (!std::isfinite(duration) || duration <= 0.1f) {
                duration = videoRandomizer.intervalSeconds;
            }
            videoRandomizer.currentVideoDuration = duration;
            if (recordHistory) {
                auto& history = videoRandomizer.recentHistory;
                history.erase(std::remove(history.begin(), history.end(), index), history.end());
                history.push_back(index);
                const int window = std::max(1, videoRandomizer.historyWindow);
                while (static_cast<int>(history.size()) > window) {
                    history.pop_front();
                }
            }
        }
        videoRandomizer.elapsedSeconds = 0.0f;
    }

    int chooseRandomVideoIndexAvoidingHistory(const std::vector<VideoAsset>& assets) {
        if (assets.empty()) {
            return -1;
        }
        if (assets.size() == 1) {
            return 0;
        }

        std::vector<int> pool;
        const auto& history = videoRandomizer.recentHistory;
        for (int i = 0; i < static_cast<int>(assets.size()); ++i) {
            if (selectedVideoAsset >= 0 && i == selectedVideoAsset) {
                continue;
            }
            if (std::find(history.begin(), history.end(), i) == history.end()) {
                pool.push_back(i);
            }
        }

        if (pool.empty()) {
            for (int i = 0; i < static_cast<int>(assets.size()); ++i) {
                if (selectedVideoAsset >= 0 && i == selectedVideoAsset) {
                    continue;
                }
                pool.push_back(i);
            }
        }

        if (pool.empty()) {
            pool.push_back(selectedVideoAsset >= 0 ? selectedVideoAsset : 0);
        }

        std::uniform_int_distribution<int> dist(0, static_cast<int>(pool.size()) - 1);
        return pool[dist(randomEngine)];
    }

    void randomizeVideoAsset(bool recordHistory = true) {
        const auto& assets = videoRegistry.getAssets();
        if (assets.empty()) {
            return;
        }

        int newIndex = chooseRandomVideoIndexAvoidingHistory(assets);
        if (newIndex < 0) {
            return;
        }

        bool selectionChanged = (selectedVideoAsset != newIndex);
        selectedVideoAsset = newIndex;
        updateRandomizerForSelection(newIndex, recordHistory);
        if (selectionChanged || !videoSubsystemInitialized) {
            // Capture last frame for crossfade transition
            if (!videoFrameBuffer.empty() && videoTexture.width > 0 && videoTexture.height > 0) {
                const auto& lastFrame = videoFrameBuffer.back();
                size_t frameSize = static_cast<size_t>(lastFrame.width) * lastFrame.height * 4;
                if (transitionFrame.size() < frameSize) {
                    transitionFrame.resize(frameSize);
                }
                std::memcpy(transitionFrame.data(), lastFrame.pixels.data(), std::min(frameSize, lastFrame.pixels.size()));
                inTransition = true;
                transitionProgress = 0.0;
            }
            reloadVideoSource(assets[newIndex].metadata.path);
        }
        videoRandomizer.currentVideoDuration = static_cast<float>(expectedPlaybackDurationSeconds());
    }

    void updateVideoRandomizer(float deltaTime) {
        if (!videoRandomizer.autoRandomize) {
            videoRandomizer.elapsedSeconds = 0.0f;
            return;
        }

        const auto& assets = videoRegistry.getAssets();
        if (assets.size() <= 1) {
            videoRandomizer.elapsedSeconds = 0.0f;
            return;
        }

        videoRandomizer.intervalSeconds = std::max(1.0f, videoRandomizer.intervalSeconds);
        float durationInterval = std::max(1.0f, videoRandomizer.currentVideoDuration);
        float targetInterval = (videoRandomizer.useVideoDuration && videoRandomizer.currentVideoDuration > 0.0f)
                                   ? durationInterval
                                   : videoRandomizer.intervalSeconds;
        videoRandomizer.elapsedSeconds += deltaTime;
        if (videoRandomizer.elapsedSeconds >= targetInterval) {
            videoRandomizer.elapsedSeconds = 0.0f;
            randomizeVideoAsset();
        }
    }

    void reloadVideoSource(const std::string& path) {
        if (videoSourcePath == path && videoSubsystemInitialized) {
            std::cout << "[Reload] Already playing this file, but forcing reload for new version" << std::endl;
            // Don't return - force reload even if path is same (file content may have changed)
        }

        std::cout << "[Reload] Starting reload for: " << path << std::endl;

        videoSourcePath = path;
        g_project_state.active_file = path;  // Update ProjectState to keep it in sync

        // Wait for GPU to finish using current video resources before destroying
        std::cout << "[Reload] Waiting for GPU idle..." << std::endl;
        vkDeviceWaitIdle(device);
        std::cout << "[Reload] GPU idle, destroying video player..." << std::endl;

        videoPlayer.shutdown();
        videoSubsystemInitialized = false;
        resetVideoPlaybackState(0.0);
        lastFrameTimestamp = std::chrono::steady_clock::now();

        std::cout << "[Reload] Initializing video system..." << std::endl;
        initVideoSystem();
        lastReloadTime = std::chrono::steady_clock::now();
        std::cout << "[Reload] Video system initialized" << std::endl;
    }

    void createDescriptorPool() {
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool");
        }
    }

    void createDescriptorSets() {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        descriptorSets.resize(layouts.size());
        if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor sets");
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            updateDescriptorSet(i);
        }
    }

    void saveImGuiLayout() {
        // ImGui guarda automáticamente cuando io.IniFilename está establecido
        // Esta función ya no es necesaria pero se mantiene para compatibilidad
    }

    void initImGui() {
        if (imguiInitialized) {
            return;
        }

        IMGUI_CHECKVERSION();
        imguiContext = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = imguiIniFilename.c_str();

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.FrameRounding = 4.0f;

        if (uiWindow == nullptr || uiRenderer == nullptr) {
            throw std::runtime_error("ImGui UI window is not available");
        }

        if (!ImGui_ImplSDL2_InitForSDLRenderer(uiWindow, uiRenderer)) {
            throw std::runtime_error("failed to initialize ImGui SDL2 backend");
        }

        if (!ImGui_ImplSDLRenderer2_Init(uiRenderer)) {
            ImGui_ImplSDL2_Shutdown();
            throw std::runtime_error("failed to initialize ImGui SDL renderer backend");
        }

        imguiInitialized = true;
    }

    void updateImGuiFrame() {
        if (!imguiInitialized) {
            return;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        buildImGuiWindows();

        ImGui::Render();
        renderImGuiWindow();
    }

    void buildImGuiWindows() {
        bool changed = false;
        bool vjayChanged = false;

        ImGui::SetNextWindowSize(ImVec2(360.0f, 260.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Procedural Controls")) {
            ImGui::Text("Animation");
            changed |= ImGui::SliderFloat("Speed", &visualControls.animationSpeed, 0.05f, 1.0f, "%.2f");

            ImGui::Separator();
            ImGui::Text("Layers");
            changed |= ImGui::Combo("Active Layer", &visualControls.activeMode, "Layer 0\0Layer 1\0");

            ImGui::Separator();
            ImGui::Text("Color Palette");
            changed |= ImGui::ColorEdit4("Primary", glm::value_ptr(visualControls.primaryColor));
            changed |= ImGui::ColorEdit4("Secondary", glm::value_ptr(visualControls.secondaryColor));
            changed |= ImGui::SliderFloat("Blend", &visualControls.colorBlend, 0.0f, 1.0f);

            ImGui::Separator();
            ImGui::Text("Audio-inspired inputs");
            changed |= ImGui::SliderFloat("Tempo", &visualControls.tempo, 0.25f, 4.0f);
            changed |= ImGui::SliderFloat("Energy", &visualControls.energy, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Bass", &visualControls.bass, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Mid", &visualControls.mid, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("High", &visualControls.high, 0.0f, 1.0f);

            ImGui::Separator();
            ImGui::Text("Video");
            changed |= ImGui::SliderFloat("Video Mix", &visualControls.videoMix, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Video speed", &visualControls.videoPlaybackRate, 0.1f, 5.0f, "%.2fx");
            if (ImGui::SliderFloat("Decode oversample", &visualControls.videoDecodeOversample, 1.0f, 8.0f, "%.1fx")) {
                visualControls.videoDecodeOversample = std::clamp(visualControls.videoDecodeOversample, 1.0f, 8.0f);
                changed = true;
            }
            static const char* forceFpsLabels[] = {"Off", "15 fps", "24 fps", "30 fps", "60 fps"};
            int forceIdx = std::clamp(visualControls.forcedFpsIndex, 0, static_cast<int>(FORCED_FPS_OPTIONS.size()) - 1);
            if (forceIdx != visualControls.forcedFpsIndex) {
                visualControls.forcedFpsIndex = forceIdx;
                changed = true;
            }
            if (ImGui::Combo("Force FPS", &forceIdx, forceFpsLabels, IM_ARRAYSIZE(forceFpsLabels))) {
                visualControls.forcedFpsIndex = forceIdx;
                changed = true;
            }
            changed |= ImGui::SliderFloat("Grayscale", &visualControls.grayscaleAmount, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Sharpen", &visualControls.sharpenAmount, 0.0f, 1.0f);
            changed |= ImGui::Checkbox("Bicubic Upscale", &visualControls.upscaleEnabled);
            changed |= ImGui::Checkbox("Random start", &visualControls.randomVideoStart);
            changed |= ImGui::SliderFloat("Loop crossfade (s)", &visualControls.loopBlendSeconds, 0.0f, 2.0f, "%.2f s");
            ImGui::Separator();
            ImGui::Text("Post FX");
            auto setPostFxEnabled = [&](bool enabled) {
                visualControls.enablePostCrtCurvature = enabled;
                visualControls.enablePostScanMask = enabled;
                visualControls.enablePostVignette = enabled;
                visualControls.enablePostFishEye = enabled;
                visualControls.enablePostBloom = enabled;
                visualControls.enablePostAberration = enabled;
                visualControls.enablePostGrain = enabled;
                visualControls.enablePostBend = enabled;
                visualControls.enablePostGlitch = enabled;
                visualControls.enablePostColorBalance = enabled;
            };
            if (ImGui::Button("Randomize Post FX")) {
                std::uniform_real_distribution<float> zeroOne(0.0f, 1.0f);
                auto randSym = [&](float minVal, float maxVal) {
                    std::uniform_real_distribution<float> dist(minVal, maxVal);
                    return dist(randomEngine);
                };
                auto randIfEnabled = [&](bool enabled, auto&& fn) {
                    if (enabled) {
                        fn();
                    }
                };

                randIfEnabled(visualControls.enablePostCrtCurvature, [&] {
                    visualControls.crtCurvature = randSym(0.0f, 0.6f);
                    visualControls.crtHorizontalCurvature = randSym(0.0f, 0.6f);
                });
                randIfEnabled(visualControls.enablePostScanMask, [&] {
                    visualControls.crtScanlineIntensity = zeroOne(randomEngine);
                    visualControls.crtMaskIntensity = zeroOne(randomEngine);
                });
                randIfEnabled(visualControls.enablePostVignette, [&] {
                    visualControls.crtVignette = zeroOne(randomEngine);
                });
                randIfEnabled(visualControls.enablePostFishEye, [&] {
                    visualControls.crtFishEye = randSym(-3.0f, 3.0f);
                });
                randIfEnabled(visualControls.enablePostBloom, [&] {
                    visualControls.bloomIntensity = randSym(0.0f, 2.0f);
                    visualControls.bloomThreshold = zeroOne(randomEngine);
                });
                randIfEnabled(visualControls.enablePostAberration, [&] {
                    visualControls.aberrationAmount = randSym(-0.05f, 0.05f);
                });
                randIfEnabled(visualControls.enablePostGrain, [&] {
                    visualControls.grainStrength = randSym(0.0f, 0.5f);
                });
                randIfEnabled(visualControls.enablePostBend, [&] {
                    visualControls.bendAmount = zeroOne(randomEngine);
                });
                randIfEnabled(visualControls.enablePostGlitch, [&] {
                    visualControls.glitchAmount = zeroOne(randomEngine);
                });
                randIfEnabled(visualControls.enablePostColorBalance, [&] {
                    visualControls.colorBalance = glm::vec3(
                        randSym(0.0f, 2.0f),
                        randSym(0.0f, 2.0f),
                        randSym(0.0f, 2.0f));
                });
                controlsDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Post FX")) {
                setPostFxEnabled(false);
                visualControls.crtCurvature = 0.15f;
                visualControls.crtHorizontalCurvature = 0.15f;
                visualControls.crtScanlineIntensity = 0.35f;
                visualControls.crtMaskIntensity = 0.35f;
                visualControls.crtVignette = 0.55f;
                visualControls.crtFishEye = 0.0f;
                visualControls.bloomIntensity = 0.45f;
                visualControls.bloomThreshold = 0.7f;
                visualControls.aberrationAmount = 0.02f;
                visualControls.grainStrength = 0.15f;
                visualControls.bendAmount = 0.0f;
                visualControls.glitchAmount = 0.0f;
                visualControls.colorBalance = glm::vec3(1.0f);
                changed = true;
                controlsDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Enable All")) {
                setPostFxEnabled(true);
                changed = true;
                controlsDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Disable All")) {
                setPostFxEnabled(false);
                changed = true;
                controlsDirty = true;
            }

            ImGui::Separator();
            changed |= ImGui::Checkbox("CRT Curvature", &visualControls.enablePostCrtCurvature);
            ImGui::BeginDisabled(!visualControls.enablePostCrtCurvature);
            changed |= ImGui::SliderFloat("CRT Curvature V", &visualControls.crtCurvature, 0.0f, 0.6f, "%.2f");
            changed |= ImGui::SliderFloat("CRT Curvature H", &visualControls.crtHorizontalCurvature, 0.0f, 0.6f, "%.2f");
            ImGui::EndDisabled();

            changed |= ImGui::Checkbox("Scanlines / Mask", &visualControls.enablePostScanMask);
            ImGui::BeginDisabled(!visualControls.enablePostScanMask);
            changed |= ImGui::SliderFloat("CRT Scanlines", &visualControls.crtScanlineIntensity, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("CRT Mask", &visualControls.crtMaskIntensity, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            changed |= ImGui::Checkbox("Vignette", &visualControls.enablePostVignette);
            ImGui::BeginDisabled(!visualControls.enablePostVignette);
            changed |= ImGui::SliderFloat("CRT Black Bars", &visualControls.crtVignette, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            changed |= ImGui::Checkbox("Fish-eye", &visualControls.enablePostFishEye);
            ImGui::BeginDisabled(!visualControls.enablePostFishEye);
            changed |= ImGui::SliderFloat("CRT Fish-eye", &visualControls.crtFishEye, -3.0f, 3.0f, "%.2f");
            ImGui::EndDisabled();

            changed |= ImGui::Checkbox("Bloom", &visualControls.enablePostBloom);
            ImGui::BeginDisabled(!visualControls.enablePostBloom);
            changed |= ImGui::SliderFloat("Bloom Intensity", &visualControls.bloomIntensity, 0.0f, 2.0f, "%.2f");
            changed |= ImGui::SliderFloat("Bloom Threshold", &visualControls.bloomThreshold, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            changed |= ImGui::Checkbox("Aberration##Toggle", &visualControls.enablePostAberration);
            ImGui::BeginDisabled(!visualControls.enablePostAberration);
            changed |= ImGui::SliderFloat("Aberration", &visualControls.aberrationAmount, -0.05f, 0.05f, "%.3f");
            ImGui::EndDisabled();

            changed |= ImGui::Checkbox("Film Grain##Toggle", &visualControls.enablePostGrain);
            ImGui::BeginDisabled(!visualControls.enablePostGrain);
            changed |= ImGui::SliderFloat("Film Grain", &visualControls.grainStrength, 0.0f, 0.5f, "%.2f");
            ImGui::EndDisabled();

            changed |= ImGui::Checkbox("Screen Bend", &visualControls.enablePostBend);
            ImGui::BeginDisabled(!visualControls.enablePostBend);
            changed |= ImGui::SliderFloat("Bend Amount", &visualControls.bendAmount, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            changed |= ImGui::Checkbox("Glitch wrapper", &visualControls.enablePostGlitch);
            ImGui::BeginDisabled(!visualControls.enablePostGlitch);
            changed |= ImGui::SliderFloat("Glitch Intensity", &visualControls.glitchAmount, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            changed |= ImGui::Checkbox("RGB Mix##Toggle", &visualControls.enablePostColorBalance);
            ImGui::BeginDisabled(!visualControls.enablePostColorBalance);
            changed |= ImGui::SliderFloat3("RGB Mix", glm::value_ptr(visualControls.colorBalance), 0.0f, 2.0f);
            ImGui::EndDisabled();
            ImGui::TextWrapped("Video %s", (videoSubsystemInitialized && videoTexture.ready) ? "online" : "unavailable");
            drawVideoAssetSelector();

            const bool hasRandomChoices = videoRegistry.getAssets().size() > 1;
            ImGui::BeginDisabled(videoRandomizer.useVideoDuration);
            if (ImGui::SliderFloat("Random interval (s)", &videoRandomizer.intervalSeconds, 1.0f, 300.0f, "%.0f s")) {
                videoRandomizer.intervalSeconds = std::clamp(videoRandomizer.intervalSeconds, 1.0f, 600.0f);
                videoRandomizer.elapsedSeconds = 0.0f;
                changed = true;
            }
            ImGui::EndDisabled();
            bool syncToggle = ImGui::Checkbox("Sync shuffle to clip duration", &videoRandomizer.useVideoDuration);
            if (syncToggle) {
                videoRandomizer.elapsedSeconds = 0.0f;
            }
            changed |= syncToggle;
            if (videoRandomizer.useVideoDuration) {
                float clipSeconds = std::max(0.0f, videoRandomizer.currentVideoDuration);
                ImGui::Text("Current clip: %.1f s", clipSeconds);
            }
            changed |= ImGui::SliderFloat("Transition duration (s)", &transitionDuration, 0.1f, 2.0f, "%.2f s");
            ImGui::BeginDisabled(!hasRandomChoices);
            if (ImGui::Button("Randomize video online")) {
                randomizeVideoAsset();
                changed = true;
            }
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Auto randomize", &videoRandomizer.autoRandomize);
            if (!hasRandomChoices) {
                ImGui::SameLine();
                ImGui::TextDisabled("Need at least 2 videos");
            } else if (videoRandomizer.autoRandomize) {
                float targetInterval = videoRandomizer.useVideoDuration && videoRandomizer.currentVideoDuration > 0.0f
                                           ? videoRandomizer.currentVideoDuration
                                           : videoRandomizer.intervalSeconds;
                float remaining = std::max(0.0f, targetInterval - videoRandomizer.elapsedSeconds);
                ImGui::Text("Next shuffle in %.1f s", remaining);
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::Checkbox("Show diagnostics window", &showSecondaryWindow);
            ImGui::Checkbox("Show ImGui demo", &showDemoWindow);
            ImGui::Checkbox("Show NLE Editor", &showNLEWindow);
        }
        ImGui::End();

        // NLE Editor Window
        if (showNLEWindow) {
            ImGui::SetNextWindowSize(ImVec2(400.0f, 500.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("NLE Editor", &showNLEWindow)) {
                ImGui::Text("Playback Mode");
                ImGui::Checkbox("Use NLE Playback (PTS-based)", &useNLEPlayback);
                
                if (useNLEPlayback) {
                    ImGui::Separator();
                    ImGui::Text("Playback Clock");
                    ImGui::Text("Current time: %.3f s", playbackClock.getCurrentTime());
                    ImGui::Text("Speed: %.2fx", playbackClock.getSpeedFactor());
                    
                    if (ImGui::Button("Reset Clock")) {
                        playbackClock.reset();
                    }
                    ImGui::SameLine();
                    if (playbackClock.isPaused()) {
                        if (ImGui::Button("Resume")) {
                            playbackClock.resume();
                        }
                    } else {
                        if (ImGui::Button("Pause")) {
                            playbackClock.pause();
                        }
                    }
                    
                    ImGui::Separator();
                    ImGui::Text("Edit Parameters");
                    
                    // Quality presets
                    ImGui::Text("Quality Presets:");
                    if (ImGui::Button("Auto (Match Input)")) {
                        g_project_state.width = 0;
                        g_project_state.height = 0;
                        g_project_state.fps = 0;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("240p")) {
                        g_project_state.width = 426;
                        g_project_state.height = 240;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("720p")) {
                        g_project_state.width = 1280;
                        g_project_state.height = 720;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("1080p")) {
                        g_project_state.width = 1920;
                        g_project_state.height = 1080;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("1080p 4:3")) {
                        g_project_state.width = 1440;
                        g_project_state.height = 1080;
                    }
                    
                    ImGui::Separator();
                    
                    // Speed control
                    ImGui::SliderFloat("Speed", &g_project_state.speed, 0.25f, 4.0f, "%.2fx");

                    // FPS (0 = auto-detect from input)
                    ImGui::SliderInt("FPS", &g_project_state.fps, 0, 120);

                    // Scale (0 = auto-detect from input)
                    ImGui::SliderInt("Width", &g_project_state.width, 0, 3840);
                    ImGui::SliderInt("Height", &g_project_state.height, 0, 2160);
                    
                    // Scale flags
                    static char scale_flags_buf[64] = "lanczos";
                    ImGui::InputText("Scale Flags", scale_flags_buf, sizeof(scale_flags_buf));
                    g_project_state.scale_flags = scale_flags_buf;
                    
                    // Unsharp filter
                    ImGui::Checkbox("Enable Unsharp", &g_project_state.enable_unsharp);
                    if (g_project_state.enable_unsharp) {
                        ImGui::SliderFloat("Unsharp Amount", &g_project_state.unsharp_amount, 0.0f, 2.0f);
                        ImGui::SliderFloat("Unsharp Radius", &g_project_state.unsharp_radius, 1.0f, 10.0f);
                    }
                    
                    ImGui::Separator();
                    
                    // Output file
                    static char output_file_buf[256] = "output.mp4";
                    ImGui::InputText("Output File", output_file_buf, sizeof(output_file_buf));
                    g_project_state.output_file = output_file_buf;
                    
                    ImGui::Separator();

                    if (ImGui::Button("Apply Changes")) {
                        g_project_state.increment_version();
                    }

                    ImGui::Separator();

                    // Render status display
                    if (renderWorker) {
                        auto status = renderWorker->get_status();
                        ImGui::Text("Render Status:");
                        ImGui::Text("  Active Jobs: %zu", status.active_job_count);
                        ImGui::Text("  Pending Jobs: %zu", status.pending_job_count);
                        ImGui::Text("  Current Version: %lu", status.current_version);

                        if (status.active_job_count > 0) {
                            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  Status: RENDERING");
                        } else if (status.pending_job_count > 0) {
                            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "  Status: QUEUED");
                        } else {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  Status: IDLE");
                        }

                        if (!status.last_error.empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "  Last Error: %s", status.last_error.c_str());
                        }
                    }

                    ImGui::Separator();

                    ImGui::Text("Current File: %s", g_project_state.active_file.c_str());
                    ImGui::Text("Version: %lu", g_project_state.get_version());
                    ImGui::Text("Dirty: %s", g_project_state.is_dirty() ? "Yes" : "No");
                }
            }
            ImGui::End();
        }

        ImGui::SetNextWindowSize(ImVec2(430.0f, 640.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("VJAY BASICS")) {
            static const char* LUT_ITEMS = "Off\0Filmic\0Neon\0Noir\0Heatmap\0Analog CRT\0";
            static const char* BLEND_ITEMS = "Add\0Screen\0Multiply\0Overlay\0Difference\0Soft Light\0";

            if (ImGui::Button("Randomize VJAY basics")) {
                std::uniform_real_distribution<float> zeroOne(0.0f, 1.0f);
                auto randRange = [&](float minVal, float maxVal) {
                    std::uniform_real_distribution<float> dist(minVal, maxVal);
                    return dist(randomEngine);
                };
                auto randInt = [&](int minVal, int maxVal) {
                    std::uniform_int_distribution<int> dist(minVal, maxVal);
                    return dist(randomEngine);
                };
                auto randIfEnabled = [&](bool enabled, auto&& fn) {
                    if (enabled) {
                        fn();
                    }
                };

                randIfEnabled(visualControls.enableColorGrading, [&] {
                    visualControls.gradeBrightness = randRange(-0.5f, 0.5f);
                    visualControls.gradeContrast = randRange(0.5f, 1.8f);
                    visualControls.gradeSaturation = randRange(0.4f, 2.0f);
                    visualControls.gradeHueShift = randRange(-180.0f, 180.0f);
                    visualControls.gradeGamma = randRange(0.6f, 2.2f);
                    visualControls.colorLUTIndex = randInt(0, 5);
                    visualControls.splitToneBalance = zeroOne(randomEngine);
                    visualControls.splitToneShadows = glm::vec3(randRange(0.0f, 1.2f), randRange(0.0f, 1.2f), randRange(0.0f, 1.2f));
                    visualControls.splitToneHighlights = glm::vec3(randRange(0.8f, 1.4f), randRange(0.8f, 1.4f), randRange(0.8f, 1.4f));
                });

                randIfEnabled(visualControls.enableFeedback, [&] {
                    visualControls.feedbackAmount = randRange(0.0f, 1.0f);
                    visualControls.trailStrength = randRange(0.0f, 1.0f);
                    visualControls.temporalAccumulation = randRange(0.0f, 1.0f);
                    visualControls.feedbackDecay = randRange(0.0f, 1.0f);
                    visualControls.recursiveBlend = randRange(0.0f, 1.0f);
                });

                randIfEnabled(visualControls.enableDistortion, [&] {
                    visualControls.uvWarpStrength = randRange(0.0f, 2.0f);
                    visualControls.rippleStrength = randRange(0.0f, 2.0f);
                    visualControls.rippleFrequency = randRange(0.2f, 12.0f);
                    visualControls.swirlStrength = randRange(-2.0f, 2.0f);
                    visualControls.displacementAmount = randRange(0.0f, 2.0f);
                    visualControls.kaleidoSegments = randRange(3.0f, 24.0f);
                    visualControls.tunnelDepth = randRange(0.0f, 1.0f);
                    visualControls.tunnelCurvature = randRange(-1.0f, 1.0f);
                });

                randIfEnabled(visualControls.enableBlurMotion, [&] {
                    visualControls.gaussianBlur = randRange(0.0f, 1.0f);
                    visualControls.directionalBlur = randRange(0.0f, 1.0f);
                    visualControls.directionalBlurAngle = randRange(0.0f, 360.0f);
                    visualControls.zoomBlur = randRange(0.0f, 1.0f);
                    visualControls.motionBlur = randRange(0.0f, 1.0f);
                    visualControls.temporalBlur = randRange(0.0f, 1.0f);
                });

                randIfEnabled(visualControls.enableSharpen, [&] {
                    visualControls.unsharpMask = randRange(0.0f, 1.0f);
                    visualControls.casAmount = randRange(0.0f, 1.0f);
                    visualControls.localContrast = randRange(0.0f, 1.0f);
                });

                randIfEnabled(visualControls.enableGlitch, [&] {
                    visualControls.glitchDatamosh = randRange(0.0f, 1.0f);
                    visualControls.glitchRGBSplit = randRange(0.0f, 1.0f);
                    visualControls.glitchScanlineBreak = randRange(0.0f, 1.0f);
                    visualControls.glitchJitter = randRange(0.0f, 1.0f);
                    visualControls.glitchTearing = randRange(0.0f, 1.0f);
                    visualControls.glitchPixelSort = randRange(0.0f, 1.0f);
                    visualControls.glitchBufferCorruption = randRange(0.0f, 1.0f);
                });

                randIfEnabled(visualControls.enableBlending, [&] {
                    visualControls.blendModeProcedural = randInt(0, 5);
                    visualControls.blendModeVideo = randInt(0, 5);
                    visualControls.blendModeFeedback = randInt(0, 5);
                    visualControls.blendProceduralMix = randRange(0.0f, 2.0f);
                    visualControls.blendVideoMix = randRange(0.0f, 2.0f);
                    visualControls.blendFeedbackMix = randRange(0.0f, 2.0f);
                });

                randIfEnabled(visualControls.enableAnalog, [&] {
                    visualControls.analogScanlineFocus = zeroOne(randomEngine);
                    visualControls.analogMaskBalance = zeroOne(randomEngine);
                    visualControls.analogNoise = zeroOne(randomEngine);
                    visualControls.analogBloom = randRange(0.0f, 2.0f);
                    visualControls.vhsDistortion = zeroOne(randomEngine);
                    visualControls.analogChromaticAberration = randRange(0.0f, 0.25f);
                });

                randIfEnabled(visualControls.enableAudioReactive, [&] {
                    visualControls.audioWarpResponse = randRange(0.0f, 2.0f);
                    visualControls.audioFeedbackResponse = randRange(0.0f, 2.0f);
                    visualControls.audioBlurResponse = randRange(0.0f, 2.0f);
                    visualControls.audioColorResponse = randRange(0.0f, 2.0f);
                    visualControls.audioGlitchResponse = randRange(0.0f, 2.0f);
                    visualControls.audioBeatSync = randRange(0.0f, 4.0f);
                    visualControls.audioLfoRate = randRange(0.05f, 4.0f);
                });

                randIfEnabled(visualControls.enableTemporal, [&] {
                    visualControls.temporalInterpolation = randRange(0.0f, 1.0f);
                    visualControls.temporalBlendStrength = randRange(0.0f, 1.0f);
                    visualControls.slowMotionFactor = randRange(0.1f, 4.0f);
                    visualControls.frameAccumulation = randRange(0.0f, 1.0f);
                });

                vjayChanged = true;
                controlsDirty = true;
            }
            auto setAllVjayToggles = [&](bool value) {
                visualControls.enableColorGrading = value;
                visualControls.enableFeedback = value;
                visualControls.enableDistortion = value;
                visualControls.enableBlurMotion = value;
                visualControls.enableSharpen = value;
                visualControls.enableGlitch = value;
                visualControls.enableBlending = value;
                visualControls.enableAnalog = value;
                visualControls.enableAudioReactive = value;
                visualControls.enableTemporal = value;
            };
            if (ImGui::Button("Turn all ON")) {
                setAllVjayToggles(true);
                vjayChanged = true;
                controlsDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Turn all OFF")) {
                setAllVjayToggles(false);
                vjayChanged = true;
                controlsDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset VJAY basics")) {
                visualControls.enableColorGrading = false;
                visualControls.enableFeedback = false;
                visualControls.enableDistortion = false;
                visualControls.enableBlurMotion = false;
                visualControls.enableSharpen = false;
                visualControls.enableGlitch = false;
                visualControls.enableBlending = false;
                visualControls.enableAnalog = false;
                visualControls.enableAudioReactive = false;
                visualControls.enableTemporal = false;

                visualControls.gradeBrightness = 0.0f;
                visualControls.gradeContrast = 1.0f;
                visualControls.gradeSaturation = 1.0f;
                visualControls.gradeHueShift = 0.0f;
                visualControls.gradeGamma = 1.0f;
                visualControls.colorLUTIndex = 0;
                visualControls.splitToneBalance = 0.5f;
                visualControls.splitToneShadows = glm::vec3(0.0f);
                visualControls.splitToneHighlights = glm::vec3(1.0f);

                visualControls.feedbackAmount = 0.0f;
                visualControls.trailStrength = 0.0f;
                visualControls.temporalAccumulation = 0.0f;
                visualControls.feedbackDecay = 0.0f;
                visualControls.recursiveBlend = 0.0f;

                visualControls.uvWarpStrength = 0.0f;
                visualControls.rippleStrength = 0.0f;
                visualControls.rippleFrequency = 1.0f;
                visualControls.swirlStrength = 0.0f;
                visualControls.displacementAmount = 0.0f;
                visualControls.kaleidoSegments = 6.0f;
                visualControls.tunnelDepth = 0.0f;
                visualControls.tunnelCurvature = 0.0f;

                visualControls.gaussianBlur = 0.0f;
                visualControls.directionalBlur = 0.0f;
                visualControls.directionalBlurAngle = 0.0f;
                visualControls.zoomBlur = 0.0f;
                visualControls.motionBlur = 0.0f;
                visualControls.temporalBlur = 0.0f;

                visualControls.unsharpMask = 0.0f;
                visualControls.casAmount = 0.0f;
                visualControls.localContrast = 0.0f;

                visualControls.glitchDatamosh = 0.0f;
                visualControls.glitchRGBSplit = 0.0f;
                visualControls.glitchScanlineBreak = 0.0f;
                visualControls.glitchJitter = 0.0f;
                visualControls.glitchTearing = 0.0f;
                visualControls.glitchPixelSort = 0.0f;
                visualControls.glitchBufferCorruption = 0.0f;

                visualControls.blendModeProcedural = 0;
                visualControls.blendModeVideo = 1;
                visualControls.blendModeFeedback = 2;
                visualControls.blendProceduralMix = 1.0f;
                visualControls.blendVideoMix = 1.0f;
                visualControls.blendFeedbackMix = 0.5f;

                visualControls.analogScanlineFocus = 0.5f;
                visualControls.analogMaskBalance = 0.5f;
                visualControls.analogNoise = 0.2f;
                visualControls.analogBloom = 0.3f;
                visualControls.vhsDistortion = 0.0f;
                visualControls.analogChromaticAberration = 0.02f;

                visualControls.audioWarpResponse = 0.0f;
                visualControls.audioFeedbackResponse = 0.0f;
                visualControls.audioBlurResponse = 0.0f;
                visualControls.audioColorResponse = 0.0f;
                visualControls.audioGlitchResponse = 0.0f;
                visualControls.audioBeatSync = 0.0f;
                visualControls.audioLfoRate = 0.5f;

                visualControls.temporalInterpolation = 0.0f;
                visualControls.temporalBlendStrength = 0.0f;
                visualControls.slowMotionFactor = 1.0f;
                visualControls.frameAccumulation = 0.0f;

                vjayChanged = true;
                controlsDirty = true;
            }

            ImGui::Text("1. Color grading dinamico");
            ImGui::SameLine();
            vjayChanged |= ImGui::Checkbox("On##ColorGrade", &visualControls.enableColorGrading);
            ImGui::Separator();
            ImGui::BeginDisabled(!visualControls.enableColorGrading);
            vjayChanged |= ImGui::SliderFloat("Brightness", &visualControls.gradeBrightness, -1.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Contrast", &visualControls.gradeContrast, 0.1f, 2.5f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Saturation", &visualControls.gradeSaturation, 0.0f, 2.5f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Hue shift", &visualControls.gradeHueShift, -180.0f, 180.0f, "%.1f°");
            vjayChanged |= ImGui::SliderFloat("Gamma", &visualControls.gradeGamma, 0.4f, 3.0f, "%.2f");
            vjayChanged |= ImGui::Combo("LUT", &visualControls.colorLUTIndex, LUT_ITEMS);
            vjayChanged |= ImGui::SliderFloat("Split tone balance", &visualControls.splitToneBalance, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::ColorEdit3("Split tone shadows", glm::value_ptr(visualControls.splitToneShadows));
            vjayChanged |= ImGui::ColorEdit3("Split tone highlights", glm::value_ptr(visualControls.splitToneHighlights));
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Text("2. Feedback temporal");
            ImGui::SameLine();
            vjayChanged |= ImGui::Checkbox("On##Feedback", &visualControls.enableFeedback);
            ImGui::Separator();
            ImGui::BeginDisabled(!visualControls.enableFeedback);
            vjayChanged |= ImGui::SliderFloat("Feedback", &visualControls.feedbackAmount, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Trails", &visualControls.trailStrength, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Temporal accumulation", &visualControls.temporalAccumulation, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Decay", &visualControls.feedbackDecay, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Recursive blend", &visualControls.recursiveBlend, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Text("3. Distorsion espacial");
            ImGui::SameLine();
            vjayChanged |= ImGui::Checkbox("On##Distortion", &visualControls.enableDistortion);
            ImGui::Separator();
            ImGui::BeginDisabled(!visualControls.enableDistortion);
            vjayChanged |= ImGui::SliderFloat("UV warp", &visualControls.uvWarpStrength, 0.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Ripple", &visualControls.rippleStrength, 0.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Ripple freq", &visualControls.rippleFrequency, 0.2f, 12.0f, "%.1f");
            vjayChanged |= ImGui::SliderFloat("Swirl", &visualControls.swirlStrength, -2.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Displacement", &visualControls.displacementAmount, 0.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Kaleido segments", &visualControls.kaleidoSegments, 3.0f, 24.0f, "%.0f");
            vjayChanged |= ImGui::SliderFloat("Tunnel depth", &visualControls.tunnelDepth, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Tunnel curvature", &visualControls.tunnelCurvature, -1.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Text("4. Blur & motion");
            ImGui::SameLine();
            vjayChanged |= ImGui::Checkbox("On##Blur", &visualControls.enableBlurMotion);
            ImGui::Separator();
            ImGui::BeginDisabled(!visualControls.enableBlurMotion);
            vjayChanged |= ImGui::SliderFloat("Gaussian blur", &visualControls.gaussianBlur, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Directional blur", &visualControls.directionalBlur, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Directional angle", &visualControls.directionalBlurAngle, 0.0f, 360.0f, "%.0f°");
            vjayChanged |= ImGui::SliderFloat("Zoom blur", &visualControls.zoomBlur, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Motion blur", &visualControls.motionBlur, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Temporal blur", &visualControls.temporalBlur, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Text("5. Sharpen / detalle");
            ImGui::SameLine();
            vjayChanged |= ImGui::Checkbox("On##Sharpen", &visualControls.enableSharpen);
            ImGui::Separator();
            ImGui::BeginDisabled(!visualControls.enableSharpen);
            vjayChanged |= ImGui::SliderFloat("Unsharp mask", &visualControls.unsharpMask, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("CAS", &visualControls.casAmount, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Local contrast", &visualControls.localContrast, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Text("6. Glitch / corruption");
            ImGui::SameLine();
            vjayChanged |= ImGui::Checkbox("On##Glitch", &visualControls.enableGlitch);
            ImGui::Separator();
            ImGui::BeginDisabled(!visualControls.enableGlitch);
            vjayChanged |= ImGui::SliderFloat("Datamosh", &visualControls.glitchDatamosh, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("RGB split", &visualControls.glitchRGBSplit, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Scanline break", &visualControls.glitchScanlineBreak, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Jitter", &visualControls.glitchJitter, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Tearing", &visualControls.glitchTearing, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Pixel sorting", &visualControls.glitchPixelSort, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Buffer corruption", &visualControls.glitchBufferCorruption, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Text("7. Compositing & blending");
            ImGui::SameLine();
            vjayChanged |= ImGui::Checkbox("On##Blend", &visualControls.enableBlending);
            ImGui::Separator();
            ImGui::BeginDisabled(!visualControls.enableBlending);
            vjayChanged |= ImGui::Combo("Procedural blend", &visualControls.blendModeProcedural, BLEND_ITEMS);
            vjayChanged |= ImGui::Combo("Video blend", &visualControls.blendModeVideo, BLEND_ITEMS);
            vjayChanged |= ImGui::Combo("Feedback blend", &visualControls.blendModeFeedback, BLEND_ITEMS);
            vjayChanged |= ImGui::SliderFloat("Procedural mix", &visualControls.blendProceduralMix, 0.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Video mix", &visualControls.blendVideoMix, 0.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Feedback mix", &visualControls.blendFeedbackMix, 0.0f, 2.0f, "%.2f");
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Text("8. CRT / analog simulation");
            ImGui::SameLine();
            vjayChanged |= ImGui::Checkbox("On##Analog", &visualControls.enableAnalog);
            ImGui::Separator();
            ImGui::BeginDisabled(!visualControls.enableAnalog);
            vjayChanged |= ImGui::SliderFloat("Scanline focus", &visualControls.analogScanlineFocus, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Mask balance", &visualControls.analogMaskBalance, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Analog noise", &visualControls.analogNoise, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Analog bloom", &visualControls.analogBloom, 0.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("VHS distortion", &visualControls.vhsDistortion, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Analog chroma", &visualControls.analogChromaticAberration, 0.0f, 0.25f, "%.3f");
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Text("9. Audio reactivity");
            ImGui::SameLine();
            vjayChanged |= ImGui::Checkbox("On##Audio", &visualControls.enableAudioReactive);
            ImGui::Separator();
            ImGui::BeginDisabled(!visualControls.enableAudioReactive);
            vjayChanged |= ImGui::SliderFloat("Warp response", &visualControls.audioWarpResponse, 0.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Feedback response", &visualControls.audioFeedbackResponse, 0.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Blur response", &visualControls.audioBlurResponse, 0.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Color response", &visualControls.audioColorResponse, 0.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Glitch response", &visualControls.audioGlitchResponse, 0.0f, 2.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Beat sync", &visualControls.audioBeatSync, 0.0f, 4.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("LFO rate", &visualControls.audioLfoRate, 0.05f, 4.0f, "%.2f Hz");
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Text("10. Temporal speed processing");
            ImGui::SameLine();
            vjayChanged |= ImGui::Checkbox("On##Temporal", &visualControls.enableTemporal);
            ImGui::Separator();
            ImGui::BeginDisabled(!visualControls.enableTemporal);
            vjayChanged |= ImGui::SliderFloat("Frame interpolation", &visualControls.temporalInterpolation, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Temporal blend", &visualControls.temporalBlendStrength, 0.0f, 1.0f, "%.2f");
            vjayChanged |= ImGui::SliderFloat("Slow-motion", &visualControls.slowMotionFactor, 0.1f, 4.0f, "%.2fx");
            vjayChanged |= ImGui::SliderFloat("Frame accumulation", &visualControls.frameAccumulation, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();
        }
        ImGui::End();

        if (showSecondaryWindow) {
            ImGui::SetNextWindowSize(ImVec2(320.0f, 220.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Diagnostics", &showSecondaryWindow)) {
                ImGui::Text("Frame %u | Image %u", lastFrameFrameIndex, lastFrameImageIndex);
                ImGui::Text("Swapchain: %ux%u", swapchainExtent.width, swapchainExtent.height);
                ImGui::Text("Current mode: %d", currentMode);
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                if (videoSubsystemInitialized && videoTexture.ready) {
                    ImGui::Text("Video: %ux%u", videoTexture.width, videoTexture.height);
                    if (videoPlayer.isReady()) {
                        double frameDuration = std::max(1e-6, videoPlayer.frameDuration());
                        double sourceFps = 1.0 / frameDuration;
                        double playbackRate = effectivePlaybackRate(sourceFps);
                        double decodeOversample = std::max(1.0, static_cast<double>(visualControls.videoDecodeOversample));
                        double decodeRate = std::max(playbackRate, decodeOversample);
                        double displayFps = sourceFps * playbackRate;
                        double decodeFps = sourceFps * decodeRate;
                        ImGui::Text("Clip FPS: %.2f", sourceFps);
                        ImGui::Text("Display FPS (rate): %.2f", displayFps);
                        ImGui::Text("Decode FPS (max(rate, oversample)): %.2f", decodeFps);
                        if (visualControls.forcedFpsIndex > 0) {
                            ImGui::Text("Forced FPS target: %d", FORCED_FPS_OPTIONS[visualControls.forcedFpsIndex]);
                        }
                    }
                } else {
                    ImGui::Text("Video offline");
                }
                if (ImGui::Button("Reset Palette")) {
                    visualControls.primaryColor = glm::vec4(0.9f, 0.4f, 0.1f, 1.0f);
                    visualControls.secondaryColor = glm::vec4(0.1f, 0.5f, 0.8f, 1.0f);
                }
                if (selectedVideoAsset >= 0 && selectedVideoAsset < static_cast<int>(videoRegistry.getAssets().size())) {
                    const auto& meta = videoRegistry.getAssets()[selectedVideoAsset].metadata;
                    ImGui::Separator();
                    ImGui::Text("Video asset: %s", meta.filename.c_str());
                    ImGui::Text("Resolution: %dx%d", meta.width, meta.height);
                    ImGui::Text("FPS: %.2f", meta.fps);
                    ImGui::Text("Duration: %.2f s", meta.duration);
                    ImGui::Text("Bitrate: %.0f kbps", meta.bitrate / 1000.0);
                    ImGui::Text("Audio: %s", meta.hasAudio ? "yes" : "no");
                    ImGui::Separator();
                    if (ImGui::Button("Delete Selected Video")) {
                        try {
                            std::string videoPath = meta.path;
                            std::cout << "[Delete] Deleting video: " << videoPath << std::endl;
                            std::filesystem::remove(videoPath);
                            videoRegistry.scan(videoAssetsRoot);
                            if (selectedVideoAsset >= static_cast<int>(videoRegistry.getAssets().size())) {
                                selectedVideoAsset = std::max(0, static_cast<int>(videoRegistry.getAssets().size()) - 1);
                            }
                            if (!videoRegistry.getAssets().empty()) {
                                reloadVideoSource(videoRegistry.getAssets()[selectedVideoAsset].metadata.path);
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "[Delete] Error: " << e.what() << std::endl;
                        }
                    }
                }
            }
            ImGui::End();
        }

        if (showDemoWindow) {
            ImGui::ShowDemoWindow(&showDemoWindow);
        }

        if (changed || vjayChanged) {
            controlsDirty = true;
        }
    }

    void renderImGuiWindow() {
        if (!imguiInitialized || uiRenderer == nullptr) {
            return;
        }

        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData == nullptr || drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) {
            SDL_SetRenderDrawColor(uiRenderer, 12, 12, 12, 255);
            SDL_RenderClear(uiRenderer);
            SDL_RenderPresent(uiRenderer);
            return;
        }

        SDL_SetRenderDrawColor(uiRenderer, 12, 12, 12, 255);
        SDL_RenderClear(uiRenderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(drawData, uiRenderer);
        SDL_RenderPresent(uiRenderer);
    }

    void cleanupImGui() {
        if (!imguiInitialized) {
            return;
        }

        saveImGuiLayout();
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        if (imguiContext != nullptr) {
            ImGui::DestroyContext(imguiContext);
            imguiContext = nullptr;
        }
        imguiInitialized = false;
    }

    void updateDescriptorSet(uint32_t frameIndex) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[frameIndex];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GlobalUBO);

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[frameIndex];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[frameIndex];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &videoTexture.descriptorInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    void createCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create command pool");
        }

        std::cout << "[CommandPool] Created successfully with queue family " << queueFamilyIndices.graphicsFamily << std::endl;
    }

    void createCommandBuffers() {
        commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        std::cout << "[CommandBuffers] Allocating " << commandBuffers.size() << " command buffers" << std::endl;

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

        if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers");
        }
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer, FrameContext& frame) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer");
        }

        recordPendingVideoUpload(commandBuffer, frame.frameIndex);

        renderer.record(commandBuffer, frame);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer");
        }
    }

    void recordPendingVideoUpload(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
        if (!videoTexture.ready) {
            return;
        }
        if (frameIndex >= MAX_FRAMES_IN_FLIGHT) {
            return;
        }
        if (!videoTexture.pendingUploads[frameIndex]) {
            return;
        }

        videoTrace("[UPLOAD] frameIndex=", frameIndex, " stagingSlot=", frameIndex, " submitted layout=OK");

        auto transition = [&](VkImageLayout oldLayout, VkImageLayout newLayout,
                              VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = videoTexture.imageHandle.image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkAccessFlags srcAccessMask = 0;
            VkAccessFlags dstAccessMask = 0;

            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            }

            barrier.srcAccessMask = srcAccessMask;
            barrier.dstAccessMask = dstAccessMask;

            vkCmdPipelineBarrier(
                commandBuffer,
                srcStage,
                dstStage,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier);
        };

        const auto& slot = videoStaging.getSlot(frameIndex);
        VkImageLayout startLayout = videoTexture.currentLayout;
        VkPipelineStageFlags srcStage = (startLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        transition(startLayout,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   srcStage,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {videoTexture.width, videoTexture.height, 1};

        vkCmdCopyBufferToImage(
            commandBuffer,
            slot.buffer,
            videoTexture.imageHandle.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region);

        transition(
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        videoTexture.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        videoTexture.pendingUploads[frameIndex] = false;
    }

    void updateUniformBuffer(uint32_t frameIndex) {
        GlobalUBO ubo{};
        auto currentTime = std::chrono::steady_clock::now();
        float time = std::chrono::duration<float>(currentTime - startTime).count();
        float proceduralTime = time * visualControls.animationSpeed;
        visualControls.activeMode = std::clamp(visualControls.activeMode, 0, 1);
        currentMode = visualControls.activeMode;

        float rotation = glm::radians(90.0f) * time;
        ubo.model = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 2.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f),
                                    swapchainExtent.width / static_cast<float>(swapchainExtent.height),
                                    0.1f,
                                    10.0f);
        ubo.proj[1][1] *= -1.0f;
        ubo.resolution = glm::vec2(static_cast<float>(swapchainExtent.width),
                                   static_cast<float>(swapchainExtent.height));
        ubo.videoResolution = glm::vec2(static_cast<float>(videoTexture.width),
                                       static_cast<float>(videoTexture.height));

        // Debug: log video resolution periodically
        static int frameCounter = 0;
        if (++frameCounter % 300 == 0) {
            std::cout << "[UBO] Video resolution: " << videoTexture.width << "x" << videoTexture.height
                      << " Screen: " << swapchainExtent.width << "x" << swapchainExtent.height << std::endl;
        }

        ubo.time = proceduralTime;
        ubo.tempo = visualControls.tempo;
        ubo.energy = visualControls.energy;
        ubo.bass = visualControls.bass;
        ubo.mid = visualControls.mid;
        ubo.high = visualControls.high;
        ubo.primaryColor = visualControls.primaryColor;
        ubo.secondaryColor = visualControls.secondaryColor;
        ubo.colorBlend = visualControls.colorBlend;
        ubo.mode = currentMode;
        ubo.videoMix = visualControls.videoMix;
        ubo.videoAvailable = (videoSubsystemInitialized && videoTexture.ready) ? 1.0f : 0.0f;
        ubo.grayscaleAmount = visualControls.grayscaleAmount;
        ubo.sharpenAmount = visualControls.sharpenAmount;
        ubo.upscaleEnabled = visualControls.upscaleEnabled ? 1.0f : 0.0f;

        auto postCrtCurvatureEnabled = visualControls.enablePostCrtCurvature;
        ubo.crtCurvature = postCrtCurvatureEnabled ? visualControls.crtCurvature : 0.0f;
        ubo.crtHorizontalCurvature = postCrtCurvatureEnabled ? visualControls.crtHorizontalCurvature : 0.0f;

        auto postScanMaskEnabled = visualControls.enablePostScanMask;
        ubo.crtScanlineIntensity = postScanMaskEnabled ? visualControls.crtScanlineIntensity : 0.0f;
        ubo.crtMaskIntensity = postScanMaskEnabled ? visualControls.crtMaskIntensity : 0.0f;

        auto postVignetteEnabled = visualControls.enablePostVignette;
        ubo.crtVignette = postVignetteEnabled ? visualControls.crtVignette : 0.0f;

        auto postFishEyeEnabled = visualControls.enablePostFishEye;
        ubo.crtFishEye = postFishEyeEnabled ? visualControls.crtFishEye : 0.0f;

        auto postBloomEnabled = visualControls.enablePostBloom;
        ubo.bloomIntensity = postBloomEnabled ? visualControls.bloomIntensity : 0.0f;
        ubo.bloomThreshold = postBloomEnabled ? visualControls.bloomThreshold : 1.0f;

        auto postAberrationEnabled = visualControls.enablePostAberration;
        ubo.aberrationAmount = postAberrationEnabled ? visualControls.aberrationAmount : 0.0f;

        auto postGrainEnabled = visualControls.enablePostGrain;
        ubo.grainStrength = postGrainEnabled ? visualControls.grainStrength : 0.0f;

        auto postBendEnabled = visualControls.enablePostBend;
        ubo.bendAmount = postBendEnabled ? visualControls.bendAmount : 0.0f;

        auto postGlitchEnabled = visualControls.enablePostGlitch;
        ubo.glitchAmount = postGlitchEnabled ? visualControls.glitchAmount : 0.0f;

        auto postColorBalanceEnabled = visualControls.enablePostColorBalance;
        ubo.colorBalance = postColorBalanceEnabled ? visualControls.colorBalance : glm::vec3(1.0f);

        auto colorEnabled = visualControls.enableColorGrading;
        ubo.gradeBrightness = colorEnabled ? visualControls.gradeBrightness : 0.0f;
        ubo.gradeContrast = colorEnabled ? visualControls.gradeContrast : 1.0f;
        ubo.gradeSaturation = colorEnabled ? visualControls.gradeSaturation : 1.0f;
        ubo.gradeHueShift = colorEnabled ? visualControls.gradeHueShift : 0.0f;
        ubo.gradeGamma = colorEnabled ? visualControls.gradeGamma : 1.0f;
        ubo.colorLUTIndex = colorEnabled ? visualControls.colorLUTIndex : 0;
        ubo.splitToneBalance = colorEnabled ? visualControls.splitToneBalance : 0.0f;
        ubo.splitToneShadows = colorEnabled ? visualControls.splitToneShadows : glm::vec3(1.0f);
        ubo.splitToneHighlights = colorEnabled ? visualControls.splitToneHighlights : glm::vec3(1.0f);

        auto feedbackEnabled = visualControls.enableFeedback;
        ubo.feedbackAmount = feedbackEnabled ? visualControls.feedbackAmount : 0.0f;
        ubo.trailStrength = feedbackEnabled ? visualControls.trailStrength : 0.0f;
        ubo.temporalAccumulation = feedbackEnabled ? visualControls.temporalAccumulation : 0.0f;
        ubo.feedbackDecay = feedbackEnabled ? visualControls.feedbackDecay : 0.0f;
        ubo.recursiveBlend = feedbackEnabled ? visualControls.recursiveBlend : 0.0f;

        auto distEnabled = visualControls.enableDistortion;
        ubo.uvWarpStrength = distEnabled ? visualControls.uvWarpStrength : 0.0f;
        ubo.rippleStrength = distEnabled ? visualControls.rippleStrength : 0.0f;
        ubo.rippleFrequency = distEnabled ? visualControls.rippleFrequency : 1.0f;
        ubo.swirlStrength = distEnabled ? visualControls.swirlStrength : 0.0f;
        ubo.displacementAmount = distEnabled ? visualControls.displacementAmount : 0.0f;
        ubo.kaleidoSegments = distEnabled ? visualControls.kaleidoSegments : 0.0f;
        ubo.tunnelDepth = distEnabled ? visualControls.tunnelDepth : 0.0f;
        ubo.tunnelCurvature = distEnabled ? visualControls.tunnelCurvature : 0.0f;

        auto blurEnabled = visualControls.enableBlurMotion;
        ubo.gaussianBlur = blurEnabled ? visualControls.gaussianBlur : 0.0f;
        ubo.directionalBlur = blurEnabled ? visualControls.directionalBlur : 0.0f;
        ubo.directionalBlurAngle = blurEnabled ? visualControls.directionalBlurAngle : 0.0f;
        ubo.zoomBlur = blurEnabled ? visualControls.zoomBlur : 0.0f;
        ubo.motionBlur = blurEnabled ? visualControls.motionBlur : 0.0f;
        ubo.temporalBlur = blurEnabled ? visualControls.temporalBlur : 0.0f;

        auto sharpenEnabled = visualControls.enableSharpen;
        ubo.unsharpMask = sharpenEnabled ? visualControls.unsharpMask : 0.0f;
        ubo.casAmount = sharpenEnabled ? visualControls.casAmount : 0.0f;
        ubo.localContrast = sharpenEnabled ? visualControls.localContrast : 0.0f;

        auto glitchEnabled = visualControls.enableGlitch;
        ubo.glitchDatamosh = glitchEnabled ? visualControls.glitchDatamosh : 0.0f;
        ubo.glitchRGBSplit = glitchEnabled ? visualControls.glitchRGBSplit : 0.0f;
        ubo.glitchScanlineBreak = glitchEnabled ? visualControls.glitchScanlineBreak : 0.0f;
        ubo.glitchJitter = glitchEnabled ? visualControls.glitchJitter : 0.0f;
        ubo.glitchTearing = glitchEnabled ? visualControls.glitchTearing : 0.0f;
        ubo.glitchPixelSort = glitchEnabled ? visualControls.glitchPixelSort : 0.0f;
        ubo.glitchBufferCorruption = glitchEnabled ? visualControls.glitchBufferCorruption : 0.0f;

        auto blendEnabled = visualControls.enableBlending;
        ubo.blendModeProcedural = blendEnabled ? visualControls.blendModeProcedural : 0;
        ubo.blendModeVideo = blendEnabled ? visualControls.blendModeVideo : 0;
        ubo.blendModeFeedback = blendEnabled ? visualControls.blendModeFeedback : 0;
        ubo.blendProceduralMix = blendEnabled ? visualControls.blendProceduralMix : 0.0f;
        ubo.blendVideoMix = blendEnabled ? visualControls.blendVideoMix : 0.0f;
        ubo.blendFeedbackMix = blendEnabled ? visualControls.blendFeedbackMix : 0.0f;

        auto analogEnabled = visualControls.enableAnalog;
        ubo.analogScanlineFocus = analogEnabled ? visualControls.analogScanlineFocus : 0.0f;
        ubo.analogMaskBalance = analogEnabled ? visualControls.analogMaskBalance : 0.0f;
        ubo.analogNoise = analogEnabled ? visualControls.analogNoise : 0.0f;
        ubo.analogBloom = analogEnabled ? visualControls.analogBloom : 0.0f;
        ubo.vhsDistortion = analogEnabled ? visualControls.vhsDistortion : 0.0f;
        ubo.analogChromaticAberration = analogEnabled ? visualControls.analogChromaticAberration : 0.0f;

        auto audioEnabled = visualControls.enableAudioReactive;
        ubo.audioWarpResponse = audioEnabled ? visualControls.audioWarpResponse : 0.0f;
        ubo.audioFeedbackResponse = audioEnabled ? visualControls.audioFeedbackResponse : 0.0f;
        ubo.audioBlurResponse = audioEnabled ? visualControls.audioBlurResponse : 0.0f;
        ubo.audioColorResponse = audioEnabled ? visualControls.audioColorResponse : 0.0f;
        ubo.audioGlitchResponse = audioEnabled ? visualControls.audioGlitchResponse : 0.0f;
        ubo.audioBeatSync = audioEnabled ? visualControls.audioBeatSync : 0.0f;
        ubo.audioLfoRate = audioEnabled ? visualControls.audioLfoRate : 0.0f;

        auto temporalEnabled = visualControls.enableTemporal;
        ubo.temporalInterpolation = temporalEnabled ? visualControls.temporalInterpolation : 0.0f;
        ubo.temporalBlendStrength = temporalEnabled ? visualControls.temporalBlendStrength : 0.0f;
        ubo.slowMotionFactor = temporalEnabled ? visualControls.slowMotionFactor : 1.0f;
        ubo.frameAccumulation = temporalEnabled ? visualControls.frameAccumulation : 0.0f;

        // NLE Effect Chain parameters - only apply when NLE mode is enabled
        if (useNLEPlayback) {
            ubo.nleOutputWidth = 0;  // Always use original resolution
            ubo.nleOutputHeight = 0; // Always use original resolution
            ubo.nleGrayscale = currentEffectChain.grayscale ? 1.0f : 0.0f;
            ubo.nleBrightness = currentEffectChain.brightness;
            ubo.nleContrast = currentEffectChain.contrast;
            ubo.nleSaturation = currentEffectChain.saturation;
        } else {
            ubo.nleOutputWidth = 0;
            ubo.nleOutputHeight = 0;
            ubo.nleGrayscale = 0.0f;
            ubo.nleBrightness = 0.0f;
            ubo.nleContrast = 1.0f;
            ubo.nleSaturation = 1.0f;
        }

        memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
    }

    static std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file: " + filename);
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
        file.close();

        return buffer;
    }

    static std::string resolveIncludes(const std::string& source, const std::string& basePath) {
        std::istringstream stream(source);
        std::string line;
        std::string result;

        while (std::getline(stream, line)) {
            size_t includePos = line.find("#include");
            if (includePos != std::string::npos) {
                size_t start = line.find('"', includePos);
                size_t end = line.find('"', start + 1);
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    std::string includeFile = line.substr(start + 1, end - start - 1);
                    std::string includePath = basePath + "/" + includeFile;
                    auto includedRaw = readFile(includePath);
                    std::string includedSource(includedRaw.begin(), includedRaw.end());
                    result += resolveIncludes(includedSource, basePath);
                    continue;
                }
            }

            result += line + "\n";
        }

        return result;
    }

    static std::vector<char> loadShaderCode(const std::string& path) {
        auto raw = readFile(path);
        std::string source(raw.begin(), raw.end());
        std::string basePath = ".";
        size_t slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            basePath = path.substr(0, slash);
        }
        std::string resolved = resolveIncludes(source, basePath);
        return std::vector<char>(resolved.begin(), resolved.end());
    }

    VkShaderModule createShaderModule(const std::vector<char>& code) {
        if (code.empty()) {
            throw std::runtime_error("shader code is empty");
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shader module");
        }

        return shaderModule;
    }

    void createVertexBuffer() {
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
        ResourceHandle handle = resourceSystem.createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        vertexBufferHandle = handle;

        void* data;
        vkMapMemory(device, handle.memory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
        vkUnmapMemory(device, handle.memory);
    }

    void mainLoop() {
        while (running) {
            // Check for pending video reload from render completion callback
            {
                std::lock_guard<std::mutex> lock(videoReloadMutex);
                if (!pendingVideoReload.empty()) {
                    std::cout << "[Main] Reloading video: " << pendingVideoReload << std::endl;
                    reloadVideoSource(pendingVideoReload);
                    pendingVideoReload.clear();
                }
            }

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (imguiInitialized) {
                    ImGui_ImplSDL2_ProcessEvent(&event);
                }
                if (event.type == SDL_QUIT) {
                    running = false;
                }
                if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                        running = false;
                    }
                }
                if (event.type == SDL_WINDOWEVENT) {
                    SDL_Window* sourceWindow = SDL_GetWindowFromID(event.window.windowID);
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                        if (sourceWindow == window || sourceWindow == uiWindow) {
                            running = false;
                        }
                    }
                    if (sourceWindow == window &&
                        (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                        resizePending = true;
                        resizeStableFrames = 0;
                    }
                }
            }

            if (!running) {
                break;
            }

            handleResizeHint();

            if (framebufferResized && initializationComplete) {
                recreateSwapchain();
                framebufferResized = false;
            }

            uint32_t imageIndex = 0;
            VkResult result = VK_SUCCESS;
            FrameContext& frame = frameSystem.beginFrame(swapchain, imageIndex, result);
            videoTrace("[FRAME START] cpuFrameIndex=", frame.frameIndex, " imageIndex=", imageIndex);
            lastFrameFrameIndex = frame.frameIndex;
            lastFrameImageIndex = imageIndex;

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                std::cerr << "swapchain out of date" << std::endl;
                framebufferResized = true;
                continue;
            } else if (result != VK_SUCCESS) {
                throw std::runtime_error("failed to acquire swapchain image");
            }

            if (imagesInFlight[imageIndex] != nullptr && imagesInFlight[imageIndex] != &frame) {
                vkWaitForFences(device, 1, &imagesInFlight[imageIndex]->inFlightFence, VK_TRUE, UINT64_MAX);
            }
            imagesInFlight[imageIndex] = &frame;

            auto now = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(now - lastFrameTimestamp).count();
            lastFrameTimestamp = now;
            updateVideoRandomizer(deltaTime);
            updateVideoTexture(deltaTime, frame);
            updateUniformBuffer(frame.frameIndex);
            updateImGuiFrame();

            if (controlsDirty) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration<float>(now - lastControlSaveTime).count() > 1.0f) {
                    saveControlState();
                    controlsDirty = false;
                    lastControlSaveTime = now;
                }
            }

            if (vkResetFences(device, 1, &frame.inFlightFence) != VK_SUCCESS) {
                throw std::runtime_error("failed to reset in-flight fence");
            }

            if (vkResetCommandBuffer(commandBuffers[frame.frameIndex], 0) != VK_SUCCESS) {
                throw std::runtime_error("failed to reset command buffer");
            }

            recordCommandBuffer(commandBuffers[frame.frameIndex], frame);

            VkSemaphore waitSemaphores[] = {frame.imageAvailableSemaphore};
            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            VkSemaphore signalSemaphores[] = {swapchainRenderSemaphores[imageIndex]};

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffers[frame.frameIndex];
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;

            if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, frame.inFlightFence) != VK_SUCCESS) {
                throw std::runtime_error("failed to submit draw command buffer");
            }

            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &swapchainRenderSemaphores[imageIndex];
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &swapchain;
            presentInfo.pImageIndices = &imageIndex;

            result = vkQueuePresentKHR(presentQueue, &presentInfo);

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                std::cerr << "swapchain out of date during present" << std::endl;
                framebufferResized = true;
                continue;
            } else if (result != VK_SUCCESS) {
                throw std::runtime_error("failed to present swapchain image");
            }

            frameSystem.endFrame();
        }
    }

    void loadControlState() {
        std::ifstream file(controlStatePath);
        if (!file.is_open()) {
            return;
        }

        auto loadLegacy = [&](std::istream& legacy) {
            VisualControls loaded = visualControls;
            int activeMode = loaded.activeMode;
            int upscaleFlag = loaded.upscaleEnabled ? 1 : 0;
            int autoRandomFlag = videoRandomizer.autoRandomize ? 1 : 0;

            if (!(legacy >> loaded.animationSpeed
                      >> loaded.tempo
                      >> loaded.energy
                      >> loaded.bass
                      >> loaded.mid
                      >> loaded.high
                      >> loaded.colorBlend
                      >> loaded.primaryColor.r >> loaded.primaryColor.g >> loaded.primaryColor.b >> loaded.primaryColor.a
                      >> loaded.secondaryColor.r >> loaded.secondaryColor.g >> loaded.secondaryColor.b >> loaded.secondaryColor.a
                      >> activeMode
                      >> loaded.videoMix
                      >> loaded.videoPlaybackRate
                      >> loaded.grayscaleAmount
                      >> loaded.sharpenAmount
                      >> upscaleFlag
                      >> loaded.crtCurvature
                      >> loaded.crtHorizontalCurvature
                      >> loaded.crtScanlineIntensity
                      >> loaded.crtMaskIntensity
                      >> loaded.crtVignette
                      >> loaded.crtFishEye
                      >> loaded.bloomIntensity
                      >> loaded.bloomThreshold
                      >> loaded.aberrationAmount
                      >> loaded.grainStrength
                      >> loaded.bendAmount
                      >> loaded.glitchAmount
                      >> loaded.randomVideoStart
                      >> loaded.colorBalance.r >> loaded.colorBalance.g >> loaded.colorBalance.b
                      >> autoRandomFlag
                      >> videoRandomizer.intervalSeconds)) {
                return false;
            }

            loaded.activeMode = activeMode;
            loaded.upscaleEnabled = (upscaleFlag != 0);
            videoRandomizer.autoRandomize = (autoRandomFlag != 0);
            videoRandomizer.useVideoDuration = false;
            videoRandomizer.currentVideoDuration = 0.0f;
            videoRandomizer.recentHistory.clear();
            visualControls = loaded;
            return true;
        };

        auto parseModernState = [&](std::istream& modern,
                                    bool readVjayToggles,
                                    bool readPostFxLocks,
                                    bool readRandomizerExtras,
                                    bool readDecodeOversample,
                                    bool readForcedFpsAndLoop) {
            VisualControls loaded = visualControls;
            VideoRandomizerState randomizer = videoRandomizer;
            int activeMode = loaded.activeMode;
            int upscaleFlag = loaded.upscaleEnabled ? 1 : 0;
            int randomStartFlag = loaded.randomVideoStart ? 1 : 0;
            int autoRandomFlag = randomizer.autoRandomize ? 1 : 0;
            int durationToggle = randomizer.useVideoDuration ? 1 : 0;
            int colorToggle = loaded.enableColorGrading ? 1 : 0;
            int feedbackToggle = loaded.enableFeedback ? 1 : 0;
            int distortionToggle = loaded.enableDistortion ? 1 : 0;
            int blurToggle = loaded.enableBlurMotion ? 1 : 0;
            int sharpenToggle = loaded.enableSharpen ? 1 : 0;
            int glitchToggle = loaded.enableGlitch ? 1 : 0;
            int blendToggle = loaded.enableBlending ? 1 : 0;
            int analogToggle = loaded.enableAnalog ? 1 : 0;
            int audioToggle = loaded.enableAudioReactive ? 1 : 0;
            int temporalToggle = loaded.enableTemporal ? 1 : 0;
            int postCrtToggle = loaded.enablePostCrtCurvature ? 1 : 0;
            int postScanMaskToggle = loaded.enablePostScanMask ? 1 : 0;
            int postVignetteToggle = loaded.enablePostVignette ? 1 : 0;
            int postFishToggle = loaded.enablePostFishEye ? 1 : 0;
            int postBloomToggle = loaded.enablePostBloom ? 1 : 0;
            int postAberToggle = loaded.enablePostAberration ? 1 : 0;
            int postGrainToggle = loaded.enablePostGrain ? 1 : 0;
            int postBendToggle = loaded.enablePostBend ? 1 : 0;
            int postGlitchToggle = loaded.enablePostGlitch ? 1 : 0;
            int postColorToggle = loaded.enablePostColorBalance ? 1 : 0;

            if (!(modern >> loaded.animationSpeed
                  >> loaded.tempo
                  >> loaded.energy
                  >> loaded.bass
                  >> loaded.mid
                  >> loaded.high
                  >> loaded.colorBlend
                  >> loaded.primaryColor.r >> loaded.primaryColor.g >> loaded.primaryColor.b >> loaded.primaryColor.a
                  >> loaded.secondaryColor.r >> loaded.secondaryColor.g >> loaded.secondaryColor.b >> loaded.secondaryColor.a
                  >> activeMode
                  >> loaded.videoMix
                  >> loaded.videoPlaybackRate
                  >> loaded.grayscaleAmount
                  >> loaded.sharpenAmount
                  >> upscaleFlag
                  >> loaded.crtCurvature
                  >> loaded.crtHorizontalCurvature
                  >> loaded.crtScanlineIntensity
                  >> loaded.crtMaskIntensity
                  >> loaded.crtVignette
                  >> loaded.crtFishEye
                  >> loaded.bloomIntensity
                  >> loaded.bloomThreshold
                  >> loaded.aberrationAmount
                  >> loaded.grainStrength
                  >> loaded.bendAmount
                  >> loaded.glitchAmount
                  >> randomStartFlag
                  >> loaded.colorBalance.r >> loaded.colorBalance.g >> loaded.colorBalance.b
                  >> loaded.gradeBrightness
                  >> loaded.gradeContrast
                  >> loaded.gradeSaturation
                  >> loaded.gradeHueShift
                  >> loaded.gradeGamma
                  >> loaded.colorLUTIndex
                  >> loaded.splitToneBalance
                  >> loaded.splitToneShadows.r >> loaded.splitToneShadows.g >> loaded.splitToneShadows.b
                  >> loaded.splitToneHighlights.r >> loaded.splitToneHighlights.g >> loaded.splitToneHighlights.b
                  >> loaded.feedbackAmount
                  >> loaded.trailStrength
                  >> loaded.temporalAccumulation
                  >> loaded.feedbackDecay
                  >> loaded.recursiveBlend
                  >> loaded.uvWarpStrength
                  >> loaded.rippleStrength
                  >> loaded.rippleFrequency
                  >> loaded.swirlStrength
                  >> loaded.displacementAmount
                  >> loaded.kaleidoSegments
                  >> loaded.tunnelDepth
                  >> loaded.tunnelCurvature
                  >> loaded.gaussianBlur
                  >> loaded.directionalBlur
                  >> loaded.directionalBlurAngle
                  >> loaded.zoomBlur
                  >> loaded.motionBlur
                  >> loaded.temporalBlur
                  >> loaded.unsharpMask
                  >> loaded.casAmount
                  >> loaded.localContrast
                  >> loaded.glitchDatamosh
                  >> loaded.glitchRGBSplit
                  >> loaded.glitchScanlineBreak
                  >> loaded.glitchJitter
                  >> loaded.glitchTearing
                  >> loaded.glitchPixelSort
                  >> loaded.glitchBufferCorruption
                  >> loaded.blendModeProcedural
                  >> loaded.blendModeVideo
                  >> loaded.blendModeFeedback
                  >> loaded.blendProceduralMix
                  >> loaded.blendVideoMix
                  >> loaded.blendFeedbackMix
                  >> loaded.analogScanlineFocus
                  >> loaded.analogMaskBalance
                  >> loaded.analogNoise
                  >> loaded.analogBloom
                  >> loaded.vhsDistortion
                  >> loaded.analogChromaticAberration)) {
                return false;
            }

            if (readDecodeOversample) {
                if (!(modern >> loaded.videoDecodeOversample)) {
                    return false;
                }
            } else {
                loaded.videoDecodeOversample = 1.0f;
            }

            if (readForcedFpsAndLoop) {
                if (!(modern >> loaded.forcedFpsIndex >> loaded.loopBlendSeconds)) {
                    return false;
                }
            } else {
                loaded.forcedFpsIndex = 0;
                loaded.loopBlendSeconds = 0.5f;
            }

            if (readVjayToggles) {
                if (!(modern >> colorToggle
                              >> feedbackToggle
                              >> distortionToggle
                              >> blurToggle
                              >> sharpenToggle
                              >> glitchToggle
                              >> blendToggle
                              >> analogToggle
                              >> audioToggle
                              >> temporalToggle)) {
                    return false;
                }
            } else {
                colorToggle = feedbackToggle = distortionToggle = blurToggle = sharpenToggle = glitchToggle = blendToggle = analogToggle = audioToggle = temporalToggle = 1;
            }

            if (readPostFxLocks) {
                if (!(modern >> postCrtToggle
                              >> postScanMaskToggle
                              >> postVignetteToggle
                              >> postFishToggle
                              >> postBloomToggle
                              >> postAberToggle
                              >> postGrainToggle
                              >> postBendToggle
                              >> postGlitchToggle
                              >> postColorToggle)) {
                    return false;
                }
            } else {
                postCrtToggle = postScanMaskToggle = postVignetteToggle = postFishToggle = postBloomToggle = postAberToggle = postGrainToggle = postBendToggle = postGlitchToggle = postColorToggle = 1;
            }

            if (!(modern
                  >> loaded.audioWarpResponse
                  >> loaded.audioFeedbackResponse
                  >> loaded.audioBlurResponse
                  >> loaded.audioColorResponse
                  >> loaded.audioGlitchResponse
                  >> loaded.audioBeatSync
                  >> loaded.audioLfoRate
                  >> loaded.temporalInterpolation
                  >> loaded.temporalBlendStrength
                  >> loaded.slowMotionFactor
                  >> loaded.frameAccumulation
                  >> autoRandomFlag
                  >> randomizer.intervalSeconds)) {
                return false;
            }

            if (readRandomizerExtras) {
                if (!(modern >> durationToggle)) {
                    return false;
                }
            } else {
                durationToggle = 0;
            }

            loaded.activeMode = std::clamp(activeMode, 0, 1);
            loaded.upscaleEnabled = (upscaleFlag != 0);
            loaded.randomVideoStart = (randomStartFlag != 0);
            loaded.enableColorGrading = (colorToggle != 0);
            loaded.enableFeedback = (feedbackToggle != 0);
            loaded.enableDistortion = (distortionToggle != 0);
            loaded.enableBlurMotion = (blurToggle != 0);
            loaded.enableSharpen = (sharpenToggle != 0);
            loaded.enableGlitch = (glitchToggle != 0);
            loaded.enableBlending = (blendToggle != 0);
            loaded.enableAnalog = (analogToggle != 0);
            loaded.enableAudioReactive = (audioToggle != 0);
            loaded.enableTemporal = (temporalToggle != 0);
            loaded.enablePostCrtCurvature = (postCrtToggle != 0);
            loaded.enablePostScanMask = (postScanMaskToggle != 0);
            loaded.enablePostVignette = (postVignetteToggle != 0);
            loaded.enablePostFishEye = (postFishToggle != 0);
            loaded.enablePostBloom = (postBloomToggle != 0);
            loaded.enablePostAberration = (postAberToggle != 0);
            loaded.enablePostGrain = (postGrainToggle != 0);
            loaded.enablePostBend = (postBendToggle != 0);
            loaded.enablePostGlitch = (postGlitchToggle != 0);
            loaded.enablePostColorBalance = (postColorToggle != 0);
            loaded.forcedFpsIndex = std::clamp(loaded.forcedFpsIndex, 0, static_cast<int>(FORCED_FPS_OPTIONS.size()) - 1);
            loaded.loopBlendSeconds = std::clamp(loaded.loopBlendSeconds, 0.0f, 5.0f);
            randomizer.autoRandomize = (autoRandomFlag != 0);
            randomizer.useVideoDuration = (durationToggle != 0);
            visualControls = loaded;
            videoRandomizer = randomizer;
            videoRandomizer.currentVideoDuration = 0.0f;
            videoRandomizer.recentHistory.clear();
            videoRandomizer.elapsedSeconds = 0.0f;
            return true;
        };

        std::string versionToken;
        file >> versionToken;
        if (!file) {
            return;
        }

        if (versionToken == CONTROL_STATE_VERSION) {
            if (!parseModernState(file, true, true, true, true, true)) {
                std::cerr << "[Controls] failed to parse control state file" << std::endl;
            }
            return;
        }

        if (versionToken == CONTROL_STATE_VERSION_PREV) {
            if (!parseModernState(file, true, true, true, true, false)) {
                std::cerr << "[Controls] failed to parse control state file (v5)" << std::endl;
            }
            return;
        }

        if (versionToken == CONTROL_STATE_VERSION_PREV2) {
            if (!parseModernState(file, true, false, false, false, false)) {
                std::cerr << "[Controls] failed to parse control state file (v4)" << std::endl;
            }
            return;
        }

        if (versionToken == CONTROL_STATE_VERSION_PREV3) {
            if (!parseModernState(file, false, false, false, false, false)) {
                std::cerr << "[Controls] failed to parse control state file (v3)" << std::endl;
            }
            return;
        }

        if (versionToken == CONTROL_STATE_VERSION_PREV4) {
            if (!parseModernState(file, false, false, false, false, false)) {
                std::cerr << "[Controls] failed to parse control state file (v2)" << std::endl;
            }
            return;
        }

        file.clear();
        file.seekg(0);
        if (!loadLegacy(file)) {
            std::cerr << "[Controls] failed to parse legacy control state file" << std::endl;
        }
    }

    void saveControlState() {
        std::ofstream file(controlStatePath);
        if (!file.is_open()) {
            std::cerr << "[Controls] failed to save state: cannot open " << controlStatePath << std::endl;
            return;
        }

        file << CONTROL_STATE_VERSION << '\n'
             << visualControls.animationSpeed << ' '
             << visualControls.tempo << ' '
             << visualControls.energy << ' '
             << visualControls.bass << ' '
             << visualControls.mid << ' '
             << visualControls.high << ' '
             << visualControls.colorBlend << ' '
             << visualControls.primaryColor.r << ' ' << visualControls.primaryColor.g << ' ' << visualControls.primaryColor.b << ' ' << visualControls.primaryColor.a << ' '
             << visualControls.secondaryColor.r << ' ' << visualControls.secondaryColor.g << ' ' << visualControls.secondaryColor.b << ' ' << visualControls.secondaryColor.a << ' '
             << visualControls.activeMode << ' '
             << visualControls.videoMix << ' '
             << visualControls.videoPlaybackRate << ' '
             << visualControls.videoDecodeOversample << ' '
             << visualControls.forcedFpsIndex << ' '
             << visualControls.loopBlendSeconds << ' '
             << visualControls.grayscaleAmount << ' '
             << visualControls.sharpenAmount << ' '
             << (visualControls.upscaleEnabled ? 1 : 0) << ' '
             << visualControls.crtCurvature << ' '
             << visualControls.crtHorizontalCurvature << ' '
             << visualControls.crtScanlineIntensity << ' '
             << visualControls.crtMaskIntensity << ' '
             << visualControls.crtVignette << ' '
             << visualControls.crtFishEye << ' '
             << visualControls.bloomIntensity << ' '
             << visualControls.bloomThreshold << ' '
             << visualControls.aberrationAmount << ' '
             << visualControls.grainStrength << ' '
             << visualControls.bendAmount << ' '
             << visualControls.glitchAmount << ' '
             << (visualControls.randomVideoStart ? 1 : 0) << ' '
             << visualControls.colorBalance.r << ' ' << visualControls.colorBalance.g << ' ' << visualControls.colorBalance.b << ' '
             << visualControls.gradeBrightness << ' '
             << visualControls.gradeContrast << ' '
             << visualControls.gradeSaturation << ' '
             << visualControls.gradeHueShift << ' '
             << visualControls.gradeGamma << ' '
             << visualControls.colorLUTIndex << ' '
             << visualControls.splitToneBalance << ' '
             << visualControls.splitToneShadows.r << ' ' << visualControls.splitToneShadows.g << ' ' << visualControls.splitToneShadows.b << ' '
             << visualControls.splitToneHighlights.r << ' ' << visualControls.splitToneHighlights.g << ' ' << visualControls.splitToneHighlights.b << ' '
             << visualControls.feedbackAmount << ' '
             << visualControls.trailStrength << ' '
             << visualControls.temporalAccumulation << ' '
             << visualControls.feedbackDecay << ' '
             << visualControls.recursiveBlend << ' '
             << visualControls.uvWarpStrength << ' '
             << visualControls.rippleStrength << ' '
             << visualControls.rippleFrequency << ' '
             << visualControls.swirlStrength << ' '
             << visualControls.displacementAmount << ' '
             << visualControls.kaleidoSegments << ' '
             << visualControls.tunnelDepth << ' '
             << visualControls.tunnelCurvature << ' '
             << visualControls.gaussianBlur << ' '
             << visualControls.directionalBlur << ' '
             << visualControls.directionalBlurAngle << ' '
             << visualControls.zoomBlur << ' '
             << visualControls.motionBlur << ' '
             << visualControls.temporalBlur << ' '
             << visualControls.unsharpMask << ' '
             << visualControls.casAmount << ' '
             << visualControls.localContrast << ' '
             << visualControls.glitchDatamosh << ' '
             << visualControls.glitchRGBSplit << ' '
             << visualControls.glitchScanlineBreak << ' '
             << visualControls.glitchJitter << ' '
             << visualControls.glitchTearing << ' '
             << visualControls.glitchPixelSort << ' '
             << visualControls.glitchBufferCorruption << ' '
             << visualControls.blendModeProcedural << ' '
             << visualControls.blendModeVideo << ' '
             << visualControls.blendModeFeedback << ' '
             << visualControls.blendProceduralMix << ' '
             << visualControls.blendVideoMix << ' '
             << visualControls.blendFeedbackMix << ' '
             << visualControls.analogScanlineFocus << ' '
             << visualControls.analogMaskBalance << ' '
             << visualControls.analogNoise << ' '
             << visualControls.analogBloom << ' '
             << visualControls.vhsDistortion << ' '
             << visualControls.analogChromaticAberration << ' '
             << (visualControls.enableColorGrading ? 1 : 0) << ' '
             << (visualControls.enableFeedback ? 1 : 0) << ' '
             << (visualControls.enableDistortion ? 1 : 0) << ' '
             << (visualControls.enableBlurMotion ? 1 : 0) << ' '
             << (visualControls.enableSharpen ? 1 : 0) << ' '
             << (visualControls.enableGlitch ? 1 : 0) << ' '
             << (visualControls.enableBlending ? 1 : 0) << ' '
             << (visualControls.enableAnalog ? 1 : 0) << ' '
             << (visualControls.enableAudioReactive ? 1 : 0) << ' '
             << (visualControls.enableTemporal ? 1 : 0) << ' '
             << (visualControls.enablePostCrtCurvature ? 1 : 0) << ' '
             << (visualControls.enablePostScanMask ? 1 : 0) << ' '
             << (visualControls.enablePostVignette ? 1 : 0) << ' '
             << (visualControls.enablePostFishEye ? 1 : 0) << ' '
             << (visualControls.enablePostBloom ? 1 : 0) << ' '
             << (visualControls.enablePostAberration ? 1 : 0) << ' '
             << (visualControls.enablePostGrain ? 1 : 0) << ' '
             << (visualControls.enablePostBend ? 1 : 0) << ' '
             << (visualControls.enablePostGlitch ? 1 : 0) << ' '
             << (visualControls.enablePostColorBalance ? 1 : 0) << ' '
             << visualControls.audioWarpResponse << ' '
             << visualControls.audioFeedbackResponse << ' '
             << visualControls.audioBlurResponse << ' '
             << visualControls.audioColorResponse << ' '
             << visualControls.audioGlitchResponse << ' '
             << visualControls.audioBeatSync << ' '
             << visualControls.audioLfoRate << ' '
             << visualControls.temporalInterpolation << ' '
             << visualControls.temporalBlendStrength << ' '
             << visualControls.slowMotionFactor << ' '
             << visualControls.frameAccumulation << ' '
             << (videoRandomizer.autoRandomize ? 1 : 0) << ' '
             << videoRandomizer.intervalSeconds << ' '
             << (videoRandomizer.useVideoDuration ? 1 : 0);

        saveImGuiLayout();
    }

    void cleanup() {
        saveControlState();
        vkDeviceWaitIdle(device);

        cleanupImGui();
        destroyUiWindow();

        videoPlayer.shutdown();
        destroyVideoTexture();
        destroySwapchainSemaphores();

        for (size_t i = 0; i < uniformBuffersMemory.size(); ++i) {
            if (uniformBuffersMapped[i]) {
                vkUnmapMemory(device, uniformBuffersMemory[i]);
                uniformBuffersMapped[i] = nullptr;
            }
        }

        for (auto& handle : uniformBufferHandles) {
            if (handle.type != ResourceHandle::Type::Unknown) {
                resourceSystem.destroy(handle);
            }
        }
        uniformBufferHandles.clear();
        uniformBuffers.clear();
        uniformBuffersMemory.clear();
        uniformBuffersMapped.clear();

        if (vertexBufferHandle.type != ResourceHandle::Type::Unknown) {
            resourceSystem.destroy(vertexBufferHandle);
            vertexBufferHandle = {};
        }

        cleanupSwapchain();

        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
        }

        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
        }

        if (graphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, graphicsPipeline, nullptr);
            graphicsPipeline = VK_NULL_HANDLE;
        }
        if (fullscreenPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, fullscreenPipeline, nullptr);
            fullscreenPipeline = VK_NULL_HANDLE;
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }

        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }

        if (!swapchainImageViews.empty()) {
            for (auto imageView : swapchainImageViews) {
                if (imageView != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, imageView, nullptr);
                }
            }
            swapchainImageViews.clear();
        }

        if (descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
            descriptorSetLayout = VK_NULL_HANDLE;
        }

        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }

        frameSystem.cleanup();
        resourceSystem.cleanup();

        destroyDebugMessenger();

        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }

        if (device != VK_NULL_HANDLE) {
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }

        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }

        if (window != nullptr) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        SDL_Quit();
    }
};

int main() {
    // Register signal handlers for crashes
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGILL, crash_handler);

    App app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Application error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
