// AudioSystem.h
#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <complex>
#include <mutex>
#include <condition_variable>
#include <portaudio.h>
#include <fftw3.h>

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    bool initialize();
    bool start();
    void stop();
    void shutdown();

    float getRMS() const {
        // Convert stored dB back to linear amplitude (0..1)
        float db = rmsDb.load();
        return std::pow(10.0f, db / 20.0f);
    }

    float getSubBass() const { return smoothedSubBass.load(); }
    float getKick() const { return smoothedKick.load(); }
    float getBass() const { return smoothedBass.load(); }
    float getMid() const { return smoothedMid.load(); }
    float getHigh() const { return smoothedHigh.load(); }

    bool kickDetected() const { return kickPeak.load(); }

    // Compatibility methods
    bool startStream() { return start(); }
    void stopStream() { stop(); }
    bool isRunning() const { return running.load(); }

    const std::vector<float>& getFFTMagnitudes() const { return fftMagnitudes; }

    float getSmoothedSubBass() const { return smoothedSubBass.load(); }
    float getSmoothedKick() const { return smoothedKick.load(); }
    float getSmoothedBass() const { return smoothedBass.load(); }
    float getSmoothedMid() const { return smoothedMid.load(); }
    float getSmoothedHigh() const { return smoothedHigh.load(); }

    std::vector<std::string> getInputDeviceNames();
    int getInputDeviceIndex() const { return pulseSourceIndex; }
    bool setInputDevice(int deviceIndex);
    void refreshPulseAudioSources();
    bool restartStream();

private:
    static constexpr int FFT_SIZE = 2048;
    static constexpr int HOP_SIZE = FFT_SIZE / 2;
    static constexpr int CHANNELS = 1;

    static constexpr float ATTACK  = 0.20f;
    static constexpr float RELEASE = 0.92f;

    static constexpr float KICK_THRESHOLD = 0.005f;

    PaStream* stream = nullptr;
    int inputDevice = paNoDevice;

    double sampleRate = 44100.0;

    // PulseAudio source listing (monitors, virtual mics, etc.)
    std::vector<std::string> pulseSourceNames;
    std::vector<std::string> pulseSourceIds;
    int pulseSourceIndex = -1;

    std::atomic<bool> running = false;

    // Ring buffer
    std::vector<float> ringBuffer;
    size_t writeIndex = 0;

    std::mutex ringMutex;
    std::condition_variable cv;

    // DSP thread
    std::thread dspThread;

    // FFT
    fftwf_complex* fftOut = nullptr;
    float* fftIn = nullptr;
    fftwf_plan fftPlan = nullptr;

    std::vector<float> window;
    std::vector<float> fftMagnitudes;
    std::vector<float> fftFrame;

    // Metrics
    std::atomic<float> rmsDb = -90.0f;

    std::atomic<float> subBass{0};
    std::atomic<float> kick{0};
    std::atomic<float> bass{0};
    std::atomic<float> mid{0};
    std::atomic<float> high{0};

    std::atomic<float> smoothedSubBass{0};
    std::atomic<float> smoothedKick{0};
    std::atomic<float> smoothedBass{0};
    std::atomic<float> smoothedMid{0};
    std::atomic<float> smoothedHigh{0};

    std::atomic<bool> kickPeak{false};

    float previousKick = 0.0f;

private:
    bool openStream();

    static int audioCallback(
        const void* input,
        void* output,
        unsigned long frameCount,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData
    );

    void dspLoop();

    void processFFT();
    void calculateBands();

    void applyEnvelope(
        float input,
        std::atomic<float>& output
    );

    int hzToBin(float hz) const;
};