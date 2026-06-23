#pragma once
#include <string>
#include <vector>
#include <array>
#include <filesystem>
#include <algorithm>
#include <unordered_map>
#include <cstdint>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
}

#include "../app/ProjectState.h"

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

// VideoRegistry - scans and manages video assets in a directory
class VideoRegistry {
public:
    void scan(const std::string& rootPath);
    void scan(const std::string& rootPath, const std::string& subfolderFilter);
    const std::vector<VideoAsset>& getAssets() const;
    const std::vector<VideoAsset>& getFilteredAssets(const std::string& subfolderFilter) const;

private:
    struct CacheEntry {
        VideoMetadata metadata;
        uint64_t mtime_ns = 0;
        uint64_t size = 0;
    };

    static bool isVideoExtension(std::string ext);
    static double extractFrameRate(const AVStream* stream);
    static double extractDuration(const AVStream* stream, const AVFormatContext* ctx);
    static int64_t extractBitrate(const AVStream* stream, const AVFormatContext* ctx);
    static void probeMetadata(VideoAsset& asset);

    std::filesystem::path getCachePath(const std::filesystem::path& root) const;
    void loadCache(const std::filesystem::path& root);
    void saveCache(const std::filesystem::path& root);
    bool tryUseCache(const std::filesystem::path& filePath, VideoAsset& asset);
    void updateCache(const VideoAsset& asset);

    std::vector<VideoAsset> assets;
    mutable std::unordered_map<std::string, std::vector<VideoAsset>> filteredCache;
    std::unordered_map<std::string, CacheEntry> cache_;
};
