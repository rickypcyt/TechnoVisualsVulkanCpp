#ifndef AUDIOSYSTEM_H
#define AUDIOSYSTEM_H

#include <portaudio.h>
#include <vector>
#include <atomic>
#include <memory>
#include <complex>

class AudioSystem {
public:
    static constexpr int FFT_SIZE = 512;
    static constexpr int SAMPLE_RATE = 44100;

    AudioSystem();
    ~AudioSystem();

    bool initialize();
    void shutdown();
    
    bool startStream();
    void stopStream();
    
    float getRMS() const { return rmsLevel.load(); }
    bool isRunning() const { return streamRunning.load(); }
    
    void listDevices();
    
    // Device selection
    std::vector<std::string> getInputDeviceNames();
    int getInputDeviceIndex() const { return inputDeviceIndex; }
    bool setInputDevice(int deviceIndex);

    // FFT data access
    const std::vector<float>& getFFTMagnitudes() const { return fftMagnitudes; }
    float getBass() const { return bassLevel.load(); }
    float getMid() const { return midLevel.load(); }
    float getHigh() const { return highLevel.load(); }
    
    // Smoothed band levels (for visuals)
    float getSmoothedBass() const { return smoothedBass.load(); }
    float getSmoothedMid() const { return smoothedMid.load(); }
    float getSmoothedHigh() const { return smoothedHigh.load(); }

private:
    static int audioCallback(const void* inputBuffer, void* outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void* userData);

    void processFFT(const float* samples, size_t count);
    void calculateBands();
    void performFFT(std::vector<std::complex<float>>& data);

    PaStream* stream = nullptr;
    std::atomic<float> rmsLevel{0.0f};
    std::atomic<bool> streamRunning{false};
    int inputDeviceIndex = -1;

    // FFT processing
    std::vector<float> audioBuffer;
    std::vector<float> fftMagnitudes;
    std::vector<std::complex<float>> fftComplex;
    
    // Band levels
    std::atomic<float> bassLevel{0.0f};
    std::atomic<float> midLevel{0.0f};
    std::atomic<float> highLevel{0.0f};
    
    // Smoothed band levels (exponential smoothing)
    std::atomic<float> smoothedBass{0.0f};
    std::atomic<float> smoothedMid{0.0f};
    std::atomic<float> smoothedHigh{0.0f};
    
    // Smoothing factor (0.0-1.0, higher = more smoothing)
    static constexpr float SMOOTHING_FACTOR = 0.9f;
};

#endif // AUDIOSYSTEM_H
