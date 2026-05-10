#pragma once
#include <string>
#include <sstream>

// Effect node structure representing FFmpeg filters as C++ objects
struct EffectChain {
    // Playback speed (setpts equivalent)
    float slow_factor = 1.0f;
    
    // Frame rate interpolation (minterpolate equivalent)
    int target_fps = 0;  // 0 = original, otherwise target fps
    bool interpolate = false;
    
    // Scale flags (not used, always original resolution)
    std::string scale_flags = "lanczos";
    
    // Additional effects can be added here
    bool grayscale = false;
    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    
    // Build FFmpeg filtergraph string from this effect chain
    std::string buildFFmpegFilter() const {
        std::stringstream filter;
        bool first = true;
        
        // Frame rate interpolation
        if (interpolate && target_fps > 0) {
            if (!first) filter << ",";
            filter << "minterpolate=fps=" << target_fps;
            first = false;
        }
        
        // Speed adjustment (setpts)
        if (slow_factor != 1.0f) {
            if (!first) filter << ",";
            // Invert slow_factor for setpts: lower value = slower video = higher setpts value
            // setpts formula: 1.0/slow_factor * PTS (e.g., 0.5x speed = 2.0*PTS, 2.0x speed = 0.5*PTS)
            float setpts_value = 1.0f / slow_factor;
            filter << "setpts=" << setpts_value << "*PTS";
            first = false;
        }
        
        // Scale - always use original resolution
        
        // Color adjustments
        if (grayscale) {
            if (!first) filter << ",";
            filter << "format=gray";
            first = false;
        }
        
        if (brightness != 0.0f || contrast != 1.0f || saturation != 1.0f) {
            if (!first) filter << ",";
            filter << "eq=brightness=" << brightness 
                   << ":contrast=" << contrast 
                   << ":saturation=" << saturation;
            first = false;
        }
        
        return filter.str();
    }
    
    // Check if any effects are active
    bool hasEffects() const {
        return slow_factor != 1.0f || 
               (interpolate && target_fps > 0) ||
               grayscale ||
               brightness != 0.0f ||
               contrast != 1.0f ||
               saturation != 1.0f;
    }
    
    void reset() {
        slow_factor = 1.0f;
        target_fps = 0;
        interpolate = false;
        scale_flags = "lanczos";
        grayscale = false;
        brightness = 0.0f;
        contrast = 1.0f;
        saturation = 1.0f;
    }
};
