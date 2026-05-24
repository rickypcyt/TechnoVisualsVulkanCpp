#pragma once
#include <chrono>

// High-precision playback clock (PTS-style)
// Handles pause/resume, speed scaling, and seeking without time drift.
class PlaybackClock {
public:
    PlaybackClock() {
        reset();
    }

    void reset() {
        base_time = std::chrono::steady_clock::now();
        accumulated = 0.0;
        paused = false;
        speed = 1.0f;
        pause_start = {};
    }

    void pause() {
        if (paused) return;

        paused = true;
        pause_start = std::chrono::steady_clock::now();
    }

    void resume() {
        if (!paused) return;

        auto now = std::chrono::steady_clock::now();
        accumulated += elapsedSeconds(pause_start, now);
        paused = false;

        // Shift base_time forward so continuity is preserved
        base_time = now;
    }

    void setSpeed(float newSpeed) {
        if (newSpeed <= 0.0f) return;

        // commit current time before changing scale
        double current = getCurrentTime();

        speed = newSpeed;

        base_time = std::chrono::steady_clock::now();
        accumulated = current / speed;
    }

    void seek(double timeSeconds) {
        accumulated = timeSeconds;
        base_time = std::chrono::steady_clock::now();
        paused = false;
    }

    double getCurrentTime() const {
        if (paused) {
            return accumulated * speed;
        }

        auto now = std::chrono::steady_clock::now();
        double elapsed = elapsedSeconds(base_time, now);

        return (accumulated + elapsed) * speed;
    }

    bool isPaused() const {
        return paused;
    }

    float getSpeed() const {
        return speed;
    }

private:
    static double elapsedSeconds(
        std::chrono::steady_clock::time_point a,
        std::chrono::steady_clock::time_point b
    ) {
        return std::chrono::duration<double>(b - a).count();
    }

private:
    std::chrono::steady_clock::time_point base_time;
    std::chrono::steady_clock::time_point pause_start;

    double accumulated = 0.0;
    bool paused = false;
    float speed = 1.0f;
};