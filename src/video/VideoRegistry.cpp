#include "video/VideoRegistry.h"
#include "../app/ProjectState.h"
#include <iostream>
#include <fstream>
#include <cctype>
#include <mutex>
#include <filesystem>
#include <chrono>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

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
    scan(rootPath, "");
}

void VideoRegistry::scan(const std::string& rootPath, const std::string& /*subfolderFilter*/) {
    // subfolderFilter is ignored: we always scan the whole tree once and
    // let getFilteredAssets() do the filtering from the in-memory cache.
    assets.clear();
    filteredCache.clear();

    fs::path root(rootPath);
    if (!fs::exists(root) || !fs::is_directory(root)) {
        std::cout << "[VideoRegistry] scan aborted: root path invalid '" << rootPath << "'\n";
        return;
    }

    std::cout << "[VideoRegistry] scan starting: '" << rootPath << "'\n";
    int scanned = 0;
    int probed = 0;
    int cacheHits = 0;

    loadCache(root);

    // Scan the entire rootPath so that getFilteredAssets works for any subfolder
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

        if (scanned % 10 == 0) {
            std::cout << "[VideoRegistry] scanning progress: " << scanned
                      << " files checked, " << probed << " video probes done\n";
        }
        ++scanned;

        VideoAsset asset;
        asset.metadata.path = entry.path().string();
        asset.metadata.filename = entry.path().filename().string();

        if (tryUseCache(entry.path(), asset)) {
            ++probed;
            ++cacheHits;
        } else {
            probeMetadata(asset);
            if (asset.metadata.width > 0 || asset.metadata.duration > 0 || asset.metadata.fps > 0) {
                ++probed;
                updateCache(asset);
            } else {
                std::cout << "[VideoRegistry] probe failed/skipped: '" << asset.metadata.path << "'\n";
            }
        }
        assets.push_back(std::move(asset));
    }

    saveCache(root);

    std::sort(assets.begin(), assets.end(), [](const VideoAsset& a, const VideoAsset& b) {
        return a.metadata.filename < b.metadata.filename;
    });

    std::cout << "[VideoRegistry] scan complete: " << assets.size()
              << " video assets (scanned " << scanned << " video files, "
              << cacheHits << " cache hits)\n";
}

const std::vector<VideoAsset>& VideoRegistry::getAssets() const {
    return assets;
}

const std::vector<VideoAsset>& VideoRegistry::getFilteredAssets(const std::string& subfolderFilter) const {
    if (subfolderFilter.empty()) {
        return assets;
    }

    auto it = filteredCache.find(subfolderFilter);
    if (it != filteredCache.end()) {
        return it->second;
    }

    auto& filteredAssets = filteredCache[subfolderFilter];
    for (const auto& asset : assets) {
        fs::path assetPath(asset.metadata.path);
        std::string relativePath = assetPath.string();
        if (relativePath.find(subfolderFilter) != std::string::npos) {
            filteredAssets.push_back(asset);
        }
    }

    return filteredAssets;
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
    std::cout << "[VideoRegistry] probe start: '" << asset.metadata.path << "'\n";
    std::cout.flush();

    AVFormatContext* context = nullptr;
    if (avformat_open_input(&context, asset.metadata.path.c_str(), nullptr, nullptr) != 0) {
        std::cout << "[VideoRegistry] probe avformat_open_input failed: '" << asset.metadata.path << "'\n";
        if (context) {
            avformat_close_input(&context);
        }
        return;
    }

    std::cout << "[VideoRegistry] probe open ok, finding stream info: '" << asset.metadata.path << "'\n";
    std::cout.flush();
    if (avformat_find_stream_info(context, nullptr) < 0) {
        std::cout << "[VideoRegistry] probe avformat_find_stream_info failed: '" << asset.metadata.path << "'\n";
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

std::filesystem::path VideoRegistry::getCachePath(const std::filesystem::path& root) const {
    return root / ".video_registry_cache.json";
}

void VideoRegistry::loadCache(const std::filesystem::path& root) {
    cache_.clear();
    auto path = getCachePath(root);
    if (!fs::exists(path)) {
        return;
    }
    try {
        std::ifstream in(path);
        if (!in) {
            return;
        }
        json j;
        in >> j;
        if (j.value("version", 0) != 1) {
            return;
        }
        for (const auto& e : j["entries"]) {
            VideoMetadata m;
            m.path = e.value("path", std::string{});
            m.filename = e.value("filename", std::string{});
            m.width = e.value("width", 0);
            m.height = e.value("height", 0);
            m.fps = e.value("fps", 0.0);
            m.duration = e.value("duration", 0.0);
            m.bitrate = e.value("bitrate", int64_t{0});
            m.hasAudio = e.value("hasAudio", false);
            std::string pixName = e.value("pixelFormat", std::string{"none"});
            m.pixelFormat = (pixName == "none") ? AV_PIX_FMT_NONE : av_get_pix_fmt(pixName.c_str());

            CacheEntry entry;
            entry.metadata = std::move(m);
            entry.mtime_ns = e.value("mtime_ns", uint64_t{0});
            entry.size = e.value("size", uint64_t{0});
            cache_[entry.metadata.path] = std::move(entry);
        }
    } catch (...) {
        cache_.clear();
    }
}

void VideoRegistry::saveCache(const std::filesystem::path& root) {
    auto path = getCachePath(root);
    try {
        // Remove stale entries for files that no longer exist.
        for (auto it = cache_.begin(); it != cache_.end(); ) {
            if (!fs::exists(it->first)) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }

        json j;
        j["version"] = 1;
        json entries = json::array();
        for (auto& kv : cache_) {
            const auto& entry = kv.second;
            json e;
            e["path"] = entry.metadata.path;
            e["filename"] = entry.metadata.filename;
            e["mtime_ns"] = entry.mtime_ns;
            e["size"] = entry.size;
            e["width"] = entry.metadata.width;
            e["height"] = entry.metadata.height;
            e["fps"] = entry.metadata.fps;
            e["duration"] = entry.metadata.duration;
            e["bitrate"] = entry.metadata.bitrate;
            e["hasAudio"] = entry.metadata.hasAudio;
            const char* pixName = av_get_pix_fmt_name(entry.metadata.pixelFormat);
            e["pixelFormat"] = pixName ? std::string(pixName) : std::string("none");
            entries.push_back(std::move(e));
        }
        j["entries"] = std::move(entries);

        std::ofstream out(path);
        if (out) {
            out << j.dump(2);
        }
    } catch (...) {
        // Ignore write failures (e.g. read-only asset directory).
    }
}

bool VideoRegistry::tryUseCache(const std::filesystem::path& filePath, VideoAsset& asset) {
    auto it = cache_.find(filePath.string());
    if (it == cache_.end()) {
        return false;
    }
    try {
        if (!fs::exists(filePath)) {
            return false;
        }
        if (fs::file_size(filePath) != it->second.size) {
            return false;
        }
        auto currentMtime = std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(filePath));
        uint64_t currentMtimeNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(currentMtime.time_since_epoch()).count());
        if (currentMtimeNs != it->second.mtime_ns) {
            return false;
        }
        asset.metadata = it->second.metadata;
        return true;
    } catch (...) {
        return false;
    }
}

void VideoRegistry::updateCache(const VideoAsset& asset) {
    try {
        auto ft = fs::last_write_time(asset.metadata.path);
        auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
        uint64_t mtimeNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(sys.time_since_epoch()).count());
        uint64_t size = fs::file_size(asset.metadata.path);

        CacheEntry entry;
        entry.metadata = asset.metadata;
        entry.mtime_ns = mtimeNs;
        entry.size = size;
        cache_[asset.metadata.path] = std::move(entry);
    } catch (...) {
        // If we can't read file metadata, just skip caching this asset.
    }
}
