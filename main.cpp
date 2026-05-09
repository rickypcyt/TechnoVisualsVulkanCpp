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

const int WIDTH = 800;
const int HEIGHT = 600;
const int MAX_FRAMES_IN_FLIGHT = 2;

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

        videoWidth = codecCtx->width;
        videoHeight = codecCtx->height;
        frameDurationSeconds = computeFrameDuration(stream);
        ready = true;
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

                const size_t needed = static_cast<size_t>(frame->width) * frame->height * 4;
                if (needed > destCapacity) {
                    outWidth = frame->width;
                    outHeight = frame->height;
                    av_frame_unref(frame);
                    return false;
                }

                if (!swsCtx || cachedWidth != frame->width || cachedHeight != frame->height || cachedFormat != frame->format) {
                    if (swsCtx) {
                        sws_freeContext(swsCtx);
                    }
                    swsCtx = sws_getContext(
                        frame->width,
                        frame->height,
                        static_cast<AVPixelFormat>(frame->format),
                        frame->width,
                        frame->height,
                        AV_PIX_FMT_RGBA,
                        SWS_BILINEAR,
                        nullptr,
                        nullptr,
                        nullptr);
                    cachedWidth = frame->width;
                    cachedHeight = frame->height;
                    cachedFormat = frame->format;
                }

                if (!swsCtx) {
                    av_frame_unref(frame);
                    return false;
                }

                uint8_t* destPlanes[4] = {dest, nullptr, nullptr, nullptr};
                int destStrides[4] = {frame->width * 4, 0, 0, 0};
                sws_scale(
                    swsCtx,
                    frame->data,
                    frame->linesize,
                    0,
                    frame->height,
                    destPlanes,
                    destStrides);

                outWidth = frame->width;
                outHeight = frame->height;
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
        ready = false;
        videoStreamIndex = -1;
        videoWidth = 0;
        videoHeight = 0;
        frameDurationSeconds = 0.0;
    }

    bool isReady() const { return ready; }
    int width() const { return videoWidth; }
    int height() const { return videoHeight; }
    double frameDuration() const { return frameDurationSeconds; }

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
        if (!swsCtx || cachedWidth != src->width || cachedHeight != src->height || cachedFormat != src->format) {
            if (swsCtx) {
                sws_freeContext(swsCtx);
            }
            swsCtx = sws_getContext(
                src->width,
                src->height,
                static_cast<AVPixelFormat>(src->format),
                src->width,
                src->height,
                AV_PIX_FMT_RGBA,
                SWS_BILINEAR,
                nullptr,
                nullptr,
                nullptr);
            cachedWidth = src->width;
            cachedHeight = src->height;
            cachedFormat = src->format;
        }

        if (!swsCtx) {
            return false;
        }

        out.resize(static_cast<size_t>(src->width) * src->height * 4);
        uint8_t* destData[4] = {out.data(), nullptr, nullptr, nullptr};
        int destLinesize[4] = {src->width * 4, 0, 0, 0};

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
    int cachedFormat = AV_PIX_FMT_NONE;
    std::string currentSourcePath;
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
        createDescriptorPool();
        createDescriptorSets();
        createUiWindow();
        initImGui();

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
    } visualControls;
    ImGuiContext* imguiContext = nullptr;
    bool imguiInitialized = false;
    bool showSecondaryWindow = true;
    bool showDemoWindow = false;
    uint32_t lastFrameImageIndex = 0;
    uint32_t lastFrameFrameIndex = 0;
    VideoPlayer videoPlayer;
    VideoTextureResources videoTexture;
    double videoFrameTimer = 0.0;
    std::chrono::steady_clock::time_point lastFrameTimestamp;
    bool videoSubsystemInitialized = false;
    std::string videoSourcePath = "media/sample.mp4";
    VideoRegistry videoRegistry;
    std::string videoAssetsRoot = "mp4s";
    int selectedVideoAsset = -1;
    VideoStaging videoStaging;
    uint64_t currentEpoch = 1;

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

        uiRenderer = SDL_CreateRenderer(uiWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!uiRenderer) {
            uiRenderer = SDL_CreateRenderer(uiWindow, -1, 0);
        }

        if (!uiRenderer) {
            SDL_DestroyWindow(uiWindow);
            uiWindow = nullptr;
            throw std::runtime_error(std::string("failed to create SDL renderer: ") + SDL_GetError());
        }

        SDL_SetRenderDrawBlendMode(uiRenderer, SDL_BLENDMODE_BLEND);
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

        createVideoTextureResources(static_cast<uint32_t>(std::max(1, videoPlayer.width())),
                                    static_cast<uint32_t>(std::max(1, videoPlayer.height())));

        videoSubsystemInitialized = videoTexture.ready;
        if (!videoSubsystemInitialized) {
            return;
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT && i < descriptorSets.size(); ++i) {
            updateDescriptorSet(i);
        }

        std::cout << "[Video] Initialized " << videoSourcePath << " (" << videoPlayer.width() << "x" << videoPlayer.height() << ")" << std::endl;
    }

    void createVideoTextureResources(uint32_t width, uint32_t height) {
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

    void updateVideoTexture(float deltaTime, FrameContext& frame) {
        if (!videoSubsystemInitialized) {
            return;
        }

        const uint32_t writeSlot = frame.frameIndex % MAX_FRAMES_IN_FLIGHT;
        std::cout << "[VIDEO] decode start frameIndex=" << frame.frameIndex << std::endl;
        std::cout << "[VIDEO] snapshot/write slot=" << writeSlot << std::endl;

        if (writeSlot >= videoStaging.getSlotCount()) {
            std::cerr << "[STAGING] invalid write slot " << writeSlot
                      << " (slotCount=" << videoStaging.getSlotCount() << ")" << std::endl;
            return;
        }

        vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

        videoFrameTimer += deltaTime;
        const double frameDuration = std::max(1e-6, videoPlayer.frameDuration());
        if (videoFrameTimer < frameDuration) {
            return;
        }
        videoFrameTimer = std::fmod(videoFrameTimer, frameDuration);

        const auto& slot = videoStaging.getSlot(writeSlot);
        if (!slot.mapped) {
            std::cerr << "[STAGING] slot " << writeSlot << " is not mapped" << std::endl;
            return;
        }

        // Wait for fence of the write slot to ensure GPU is done with it
        VkFence slotFence = frameSystem.getFence(writeSlot);
        if (slotFence != VK_NULL_HANDLE) {
            vkWaitForFences(device, 1, &slotFence, VK_TRUE, UINT64_MAX);
        }

        int decodedWidth = 0;
        int decodedHeight = 0;
        if (!videoPlayer.grabFrameInto(static_cast<uint8_t*>(slot.mapped), slot.capacity, decodedWidth, decodedHeight)) {
            return;
        }

        std::cout << "[VIDEO] frame decoded size=" << decodedWidth << "x" << decodedHeight << std::endl;

        if (decodedWidth != static_cast<int>(videoTexture.width) || decodedHeight != static_cast<int>(videoTexture.height)) {
            createVideoTextureResources(decodedWidth, decodedHeight);
            if (!videoTexture.ready) {
                return;
            }
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT && i < descriptorSets.size(); ++i) {
                updateDescriptorSet(i);
            }
            return;
        }

        const size_t copySize = static_cast<size_t>(decodedWidth) * decodedHeight * 4;
        if (copySize > slot.capacity) {
            throw std::runtime_error("decoded frame larger than staging buffer");
        }

        std::cout << "[STAGING] writing slot=" << writeSlot << " size=" << copySize << " fenceState=SIGNALLED" << std::endl;
        videoTexture.pendingUploads[writeSlot] = true;
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
        } else {
            selectedVideoAsset = -1;
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
        }

        if (ImGui::BeginCombo("Video Asset", assets[selectedVideoAsset].metadata.filename.c_str())) {
            for (int i = 0; i < static_cast<int>(assets.size()); ++i) {
                bool isSelected = (i == selectedVideoAsset);
                if (ImGui::Selectable(assets[i].metadata.filename.c_str(), isSelected)) {
                    if (selectedVideoAsset != i) {
                        selectedVideoAsset = i;
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

    void reloadVideoSource(const std::string& path) {
        videoSourcePath = path;
        videoPlayer.shutdown();
        videoSubsystemInitialized = false;
        videoFrameTimer = 0.0;
        lastFrameTimestamp = std::chrono::steady_clock::now();
        initVideoSystem();
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

    void initImGui() {
        if (imguiInitialized) {
            return;
        }

        IMGUI_CHECKVERSION();
        imguiContext = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

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
        ImGui::SetNextWindowSize(ImVec2(360.0f, 260.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Procedural Controls")) {
            ImGui::Text("Animation");
            ImGui::SliderFloat("Speed", &visualControls.animationSpeed, 0.05f, 1.0f, "%.2f");

            ImGui::Separator();
            ImGui::Text("Layers");
            ImGui::Combo("Active Layer", &visualControls.activeMode, "Layer 0\0Layer 1\0");

            ImGui::Separator();
            ImGui::Text("Color Palette");
            ImGui::ColorEdit4("Primary", glm::value_ptr(visualControls.primaryColor));
            ImGui::ColorEdit4("Secondary", glm::value_ptr(visualControls.secondaryColor));
            ImGui::SliderFloat("Blend", &visualControls.colorBlend, 0.0f, 1.0f);

            ImGui::Separator();
            ImGui::Text("Audio-inspired inputs");
            ImGui::SliderFloat("Tempo", &visualControls.tempo, 0.25f, 4.0f);
            ImGui::SliderFloat("Energy", &visualControls.energy, 0.0f, 1.0f);
            ImGui::SliderFloat("Bass", &visualControls.bass, 0.0f, 1.0f);
            ImGui::SliderFloat("Mid", &visualControls.mid, 0.0f, 1.0f);
            ImGui::SliderFloat("High", &visualControls.high, 0.0f, 1.0f);

            ImGui::Separator();
            ImGui::Text("Video");
            ImGui::SliderFloat("Video Mix", &visualControls.videoMix, 0.0f, 1.0f);
            ImGui::TextWrapped("Video %s", (videoSubsystemInitialized && videoTexture.ready) ? "online" : "unavailable");
            drawVideoAssetSelector();

            ImGui::Separator();
            ImGui::Checkbox("Show diagnostics window", &showSecondaryWindow);
            ImGui::Checkbox("Show ImGui demo", &showDemoWindow);
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
                }
            }
            ImGui::End();
        }

        if (showDemoWindow) {
            ImGui::ShowDemoWindow(&showDemoWindow);
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

        std::cout << "[UPLOAD] frameIndex=" << frameIndex << " stagingSlot=" << frameIndex << " submitted layout=OK" << std::endl;

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
            std::cout << "[FRAME START] cpuFrameIndex=" << frame.frameIndex << " imageIndex=" << imageIndex << std::endl;
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
            updateVideoTexture(deltaTime, frame);
            updateUniformBuffer(frame.frameIndex);
            updateImGuiFrame();

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

    void cleanup() {
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
    App app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Application error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
