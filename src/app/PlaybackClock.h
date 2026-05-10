#pragma once
#include <chrono>

// PTS-based clock for accurate video playback timing
class PlaybackClock {
public:
    PlaybackClock() : paused(false), speed_factor(1.0f) {
        reset();
    }
    
    void reset() {
        start_time = std::chrono::steady_clock::now();
        accumulated_time = 0.0;
        last_pause_time = std::chrono::steady_clock::time_point();
        paused = false;
        speed_factor = 1.0f;
    }
    
    void pause() {
        if (!paused) {
            paused = true;
            last_pause_time = std::chrono::steady_clock::now();
        }
    }
    
    void resume() {
        if (paused) {
            auto now = std::chrono::steady_clock::now();
            auto pause_duration = std::chrono::duration<double>(now - last_pause_time).count();
            accumulated_time += pause_duration;
            paused = false;
            // Adjust start_time to account for the pause
            start_time += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(pause_duration));
        }
    }
    
    void setSpeed(float factor) {
        // When changing speed, we need to adjust the accumulated time
        double current = getCurrentTime();
        speed_factor = factor;
        reset();
        accumulated_time = current / speed_factor;
    }
    
    double getCurrentTime() const {
        if (paused) {
            auto pause_duration = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - last_pause_time).count();
            return (accumulated_time + pause_duration) * speed_factor;
        } else {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double>(now - start_time).count();
            return (accumulated_time + elapsed) * speed_factor;
        }
    }
    
    void seek(double time) {
        reset();
        accumulated_time = time / speed_factor;
    }
    
    bool isPaused() const { return paused; }
    float getSpeedFactor() const { return speed_factor; }
    
private:
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_pause_time;
    double accumulated_time;
    bool paused;
    float speed_factor;
};
