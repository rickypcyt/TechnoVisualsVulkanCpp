#pragma once
#include <string>
#include <sstream>
#include <cmath>

// Effect node structure representing FFmpeg filters as C++ objects
struct EffectChain {
    // Playback speed (setpts equivalent)
    float slow_factor = 1.0f;
    
    // Frame rate interpolation (minterpolate equivalent)
    int target_fps = 0;  // 0 = original, otherwise target fps
    bool interpolate = false;
    
    
    // Additional effects can be added here
    bool grayscale = false;
    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    
    // Build FFmpeg filtergraph string from this effect chain
    std::string buildFFmpegFilter() const {
        std::stringstream filter;
        bool first = true;
        
        // Speed adjustment (setpts) - must come before minterpolate
        // setpts modifies timestamps, interpolation should operate on adjusted timing
        if (slow_factor != 1.0f) {
            // Canonical form: setpts=PTS/factor (e.g., 0.5x speed = PTS/0.5 = 2.0*PTS)
            filter << "setpts=PTS/" << slow_factor;
            first = false;
        }
        
        // Frame rate interpolation (minterpolate) - operates on adjusted timestamps
        if (interpolate && target_fps > 0) {
            if (!first) filter << ",";
            filter << "minterpolate=fps=" << target_fps;
            first = false;
        }
        
        // Color adjustments (eq) - must come before format=gray
        // eq operates on color channels, format=gray removes chroma
        if (brightness != 0.0f || contrast != 1.0f || saturation != 1.0f) {
            if (!first) filter << ",";
            filter << "eq=brightness=" << brightness 
                   << ":contrast=" << contrast 
                   << ":saturation=" << saturation;
            first = false;
        }
        
        // Grayscale conversion (format=gray) - final color transformation
        if (grayscale) {
            if (!first) filter << ",";
            filter << "format=gray";
            first = false;
        }
        
        return filter.str();
    }
    
    // Check if any effects are active
    bool hasEffects() const {
        constexpr float EPSILON = 1e-6f;
        return std::abs(slow_factor - 1.0f) > EPSILON || 
               (interpolate && target_fps > 0) ||
               grayscale ||
               std::abs(brightness - 0.0f) > EPSILON ||
               std::abs(contrast - 1.0f) > EPSILON ||
               std::abs(saturation - 1.0f) > EPSILON;
    }
    
    void reset() {
        slow_factor = 1.0f;
        target_fps = 0;
        interpolate = false;
        grayscale = false;
        brightness = 0.0f;
        contrast = 1.0f;
        saturation = 1.0f;
    }
};
