#pragma once
#include <atomic>
#include <string>

// Render mode - preview vs export quality
enum class RenderMode {
    PREVIEW,    // Fast preview: simple fps duplication, veryfast preset
    EXPORT      // Full quality: minterpolate, slow preset
};

// Video source for NLE editor
enum class NLEVideoSource {
    VIDEO_1,    // Use video 1 (videoSourcePath)
    VIDEO_2     // Use video 2 (videoSourcePath2)
};

// ProjectState - centro de verdad del sistema
struct ProjectState {
    float speed = 1.0f;
    int fps = 0;  // 0 = auto-detect from input
    int width = 0;  // 0 = auto-detect from input
    int height = 0;  // 0 = auto-detect from input
    
    // Scale settings
    std::string scale_flags = "lanczos";
    
    // Unsharp filter settings
    bool enable_unsharp = false;
    float unsharp_amount = 0.7f;
    float unsharp_radius = 5.0f;
    
    // Output file
    std::string output_file = "output.mp4";

    // Video source for NLE
    NLEVideoSource nleVideoSource = NLEVideoSource::VIDEO_1;

    // Render mode
    RenderMode render_mode = RenderMode::PREVIEW;
    
    // Versioning system
    std::atomic<uint64_t> version{0};
    std::atomic<bool> dirty{false};
    std::atomic<bool> do_swap{true};  // Whether to swap output with original file
    
    std::string active_file = "video.mp4";
    
    // Increment version on state change
    void increment_version() {
        version++;
        dirty = true;
    }
    
    // Get current version (thread-safe)
    uint64_t get_version() const {
        return version.load();
    }
    
    // Reset dirty flag
    void mark_clean() {
        dirty = false;
    }
    
    // Check if needs render
    bool is_dirty() const {
        return dirty.load();
    }
};

// Global project state instance
inline ProjectState g_project_state;
