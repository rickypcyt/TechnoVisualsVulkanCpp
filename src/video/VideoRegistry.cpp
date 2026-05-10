#include "video/VideoRegistry.h"
#include "../app/ProjectState.h"
#include <iostream>
#include <cctype>
#include <mutex>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
    void ensureFFmpegInitialized() {
        static std::once_flag ffmpegInitFlag;
        std::call_once(ffmpegInitFlag, []() {
            av_log_set_level(AV_LOG_ERROR);
            avformat_network_init();
        });
    }
}

void VideoRegistry::scan(const std::string& rootPath) {
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
        fs::path outputPath = root / g_project_state.output_file;
        if (entry.path() == outputPath) {
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

const std::vector<VideoAsset>& VideoRegistry::getAssets() const {
    return assets;
}

bool VideoRegistry::isVideoExtension(std::string ext) {
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    static const std::array<const char*, 5> allowed = {".mp4", ".mov", ".mkv", ".avi", ".webm"};
    return std::any_of(allowed.begin(), allowed.end(), [&](const char* allowedExt) {
        return ext == allowedExt;
    });
}

double VideoRegistry::extractFrameRate(const AVStream* stream) {
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

double VideoRegistry::extractDuration(const AVStream* stream, const AVFormatContext* ctx) {
    if (stream && stream->duration > 0) {
        return stream->duration * av_q2d(stream->time_base);
    }
    if (ctx && ctx->duration > 0) {
        return static_cast<double>(ctx->duration) / AV_TIME_BASE;
    }
    return 0.0;
}

int64_t VideoRegistry::extractBitrate(const AVStream* stream, const AVFormatContext* ctx) {
    if (stream && stream->codecpar && stream->codecpar->bit_rate > 0) {
        return stream->codecpar->bit_rate;
    }
    if (ctx && ctx->bit_rate > 0) {
        return ctx->bit_rate;
    }
    return 0;
}

void VideoRegistry::probeMetadata(VideoAsset& asset) {
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
