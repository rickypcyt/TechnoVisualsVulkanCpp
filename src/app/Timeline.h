#pragma once
#include "../render/EffectChain.h"
#include <vector>
#include <string>
#include <algorithm>

// Timeline clip structure
struct Clip {
    std::string source_path;
    double start_time;        // Position on timeline (seconds)
    double duration;          // Duration on timeline (seconds)
    double source_start;      // Where to start in source video (seconds)
    EffectChain effects;
    
    Clip() : start_time(0.0), duration(0.0), source_start(0.0) {}
    
    Clip(const std::string& path, double start, double dur, double src_start = 0.0)
        : source_path(path), start_time(start), duration(dur), source_start(src_start) {}
    
    double end_time() const { return start_time + duration; }
    
    bool contains(double timeline_time) const {
        return timeline_time >= start_time && timeline_time < end_time();
    }
    
    double timeline_to_source(double timeline_time) const {
        return source_start + (timeline_time - start_time);
    }
};

// Timeline structure
class Timeline {
public:
    void addClip(const Clip& clip) {
        clips.push_back(clip);
        sort_clips();
    }
    
    void removeClip(size_t index) {
        if (index < clips.size()) {
            clips.erase(clips.begin() + index);
        }
    }
    
    void clear() {
        clips.clear();
    }
    
    const std::vector<Clip>& getClips() const {
        return clips;
    }
    
    Clip* findClipAt(double timeline_time) {
        for (auto& clip : clips) {
            if (clip.contains(timeline_time)) {
                return &clip;
            }
        }
        return nullptr;
    }
    
    const Clip* findClipAt(double timeline_time) const {
        for (const auto& clip : clips) {
            if (clip.contains(timeline_time)) {
                return &clip;
            }
        }
        return nullptr;
    }
    
    double getDuration() const {
        if (clips.empty()) return 0.0;
        double max_end = 0.0;
        for (const auto& clip : clips) {
            max_end = std::max(max_end, clip.end_time());
        }
        return max_end;
    }
    
    size_t getClipCount() const {
        return clips.size();
    }
    
private:
    std::vector<Clip> clips;
    
    void sort_clips() {
        std::sort(clips.begin(), clips.end(), 
            [](const Clip& a, const Clip& b) {
                return a.start_time < b.start_time;
            });
    }
};
