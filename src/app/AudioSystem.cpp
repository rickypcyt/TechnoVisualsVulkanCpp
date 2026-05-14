#include "AudioSystem.h"
#include <iostream>
#include <cmath>
#include <cstring>

AudioSystem::AudioSystem() {
    // Initialize FFT buffers
    audioBuffer.resize(FFT_SIZE);
    fftMagnitudes.resize(FFT_SIZE / 2);
    fftComplex.resize(FFT_SIZE);
}

AudioSystem::~AudioSystem() {
    shutdown();
}

bool AudioSystem::initialize() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "[AudioSystem] PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    std::cout << "[AudioSystem] PortAudio initialized successfully" << std::endl;
    listDevices();

    // Try to find default input device
    inputDeviceIndex = Pa_GetDefaultInputDevice();
    if (inputDeviceIndex == paNoDevice) {
        std::cerr << "[AudioSystem] No default input device found" << std::endl;
        return false;
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(inputDeviceIndex);
    std::cout << "[AudioSystem] Using input device: " << deviceInfo->name << std::endl;

    return true;
}

void AudioSystem::shutdown() {
    stopStream();
    
    PaError err = Pa_Terminate();
    if (err != paNoError) {
        std::cerr << "[AudioSystem] PortAudio terminate error: " << Pa_GetErrorText(err) << std::endl;
    }
}

bool AudioSystem::startStream() {
    if (inputDeviceIndex == paNoDevice) {
        std::cerr << "[AudioSystem] No input device selected" << std::endl;
        return false;
    }

    PaStreamParameters inputParameters;
    std::memset(&inputParameters, 0, sizeof(inputParameters));
    inputParameters.device = inputDeviceIndex;
    inputParameters.channelCount = 1; // Mono input
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputDeviceIndex)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(
        &stream,
        &inputParameters,
        nullptr, // No output
        44100,   // Sample rate
        512,     // Frames per buffer
        paClipOff, // No clipping
        audioCallback,
        this
    );

    if (err != paNoError) {
        std::cerr << "[AudioSystem] OpenStream error: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "[AudioSystem] StartStream error: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream);
        stream = nullptr;
        return false;
    }

    streamRunning = true;
    std::cout << "[AudioSystem] Stream started successfully" << std::endl;
    return true;
}

void AudioSystem::stopStream() {
    if (stream) {
        PaError err = Pa_StopStream(stream);
        if (err != paNoError) {
            std::cerr << "[AudioSystem] StopStream error: " << Pa_GetErrorText(err) << std::endl;
        }
        
        err = Pa_CloseStream(stream);
        if (err != paNoError) {
            std::cerr << "[AudioSystem] CloseStream error: " << Pa_GetErrorText(err) << std::endl;
        }
        
        stream = nullptr;
        streamRunning = false;
        std::cout << "[AudioSystem] Stream stopped" << std::endl;
    }
}

void AudioSystem::listDevices() {
    int numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) {
        std::cerr << "[AudioSystem] Error getting device count" << std::endl;
        return;
    }

    std::cout << "[AudioSystem] Found " << numDevices << " audio devices:" << std::endl;
    
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        std::cout << "  Device " << i << ": " << deviceInfo->name << std::endl;
        std::cout << "    Input channels: " << deviceInfo->maxInputChannels << std::endl;
        std::cout << "    Output channels: " << deviceInfo->maxOutputChannels << std::endl;
        std::cout << "    Default sample rate: " << deviceInfo->defaultSampleRate << std::endl;
    }
}

int AudioSystem::audioCallback(const void* inputBuffer, void* outputBuffer,
                              unsigned long framesPerBuffer,
                              const PaStreamCallbackTimeInfo* timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void* userData) {
    AudioSystem* audioSystem = static_cast<AudioSystem*>(userData);
    
    if (inputBuffer == nullptr) {
        return paContinue;
    }

    const float* in = static_cast<const float*>(inputBuffer);
    
    // Calculate RMS
    float rms = 0.0f;
    for (unsigned long i = 0; i < framesPerBuffer; i++) {
        rms += in[i] * in[i];
    }
    rms = std::sqrt(rms / framesPerBuffer);
    
    // Update atomic RMS level
    audioSystem->rmsLevel.store(rms);
    
    // Process FFT
    audioSystem->processFFT(in, framesPerBuffer);
    
    // Print debug info periodically
    static int frameCount = 0;
    if (++frameCount % 100 == 0) {
        std::cout << "[AudioSystem] RMS: " << rms 
                  << " Bass: " << audioSystem->bassLevel.load()
                  << " Mid: " << audioSystem->midLevel.load()
                  << " High: " << audioSystem->highLevel.load()
                  << " | Smoothed Bass: " << audioSystem->smoothedBass.load()
                  << " Mid: " << audioSystem->smoothedMid.load()
                  << " High: " << audioSystem->smoothedHigh.load() << std::endl;
    }

    return paContinue;
}

void AudioSystem::processFFT(const float* samples, size_t count) {
    // Copy samples to buffer
    size_t copyCount = std::min(count, audioBuffer.size());
    std::memmove(audioBuffer.data(), audioBuffer.data() + copyCount, audioBuffer.size() - copyCount);
    std::memcpy(audioBuffer.data() + audioBuffer.size() - copyCount, samples, copyCount * sizeof(float));
    
    // Apply window function (Hann window)
    for (int i = 0; i < FFT_SIZE; i++) {
        float window = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (FFT_SIZE - 1)));
        fftComplex[i] = std::complex<float>(audioBuffer[i] * window, 0.0f);
    }
    
    // Perform FFT (manual Cooley-Tukey)
    performFFT(fftComplex);
    
    // Calculate magnitudes
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float magnitude = std::sqrt(fftComplex[i].real() * fftComplex[i].real() + 
                                   fftComplex[i].imag() * fftComplex[i].imag());
        fftMagnitudes[i] = magnitude / FFT_SIZE; // Normalize
    }
    
    // Calculate frequency bands
    calculateBands();
}

void AudioSystem::performFFT(std::vector<std::complex<float>>& data) {
    const int N = data.size();
    
    // Bit-reversal permutation
    for (int i = 0; i < N; i++) {
        int j = 0;
        for (int k = 0; k < std::log2(N); k++) {
            j = (j << 1) | ((i >> k) & 1);
        }
        if (j > i) {
            std::swap(data[i], data[j]);
        }
    }
    
    // Cooley-Tukey FFT
    for (int length = 2; length <= N; length *= 2) {
        float angle = -2.0f * M_PI / length;
        std::complex<float> wlen(std::cos(angle), std::sin(angle));
        
        for (int i = 0; i < N; i += length) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < length / 2; j++) {
                std::complex<float> u = data[i + j];
                std::complex<float> v = data[i + j + length / 2] * w;
                data[i + j] = u + v;
                data[i + j + length / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

void AudioSystem::calculateBands() {
    float bassSum = 0.0f;
    float midSum = 0.0f;
    float highSum = 0.0f;
    
    // Frequency bins: sampleRate / FFT_SIZE = 44100 / 512 ≈ 86 Hz per bin
    // Bass: 20-150 Hz → bins 0-2
    // Mid: 150-2000 Hz → bins 2-24
    // High: 2000+ Hz → bins 24+
    
    int bassEnd = 2;
    int midEnd = 24;
    
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        if (i < bassEnd) {
            bassSum += fftMagnitudes[i];
        } else if (i < midEnd) {
            midSum += fftMagnitudes[i];
        } else {
            highSum += fftMagnitudes[i];
        }
    }
    
    // Normalize bands
    float currentBass = bassSum / bassEnd;
    float currentMid = midSum / (midEnd - bassEnd);
    float currentHigh = highSum / ((FFT_SIZE / 2) - midEnd);
    
    bassLevel.store(currentBass);
    midLevel.store(currentMid);
    highLevel.store(currentHigh);
    
    // Apply exponential smoothing
    float prevSmoothedBass = smoothedBass.load();
    float prevSmoothedMid = smoothedMid.load();
    float prevSmoothedHigh = smoothedHigh.load();
    
    float newSmoothedBass = prevSmoothedBass * SMOOTHING_FACTOR + currentBass * (1.0f - SMOOTHING_FACTOR);
    float newSmoothedMid = prevSmoothedMid * SMOOTHING_FACTOR + currentMid * (1.0f - SMOOTHING_FACTOR);
    float newSmoothedHigh = prevSmoothedHigh * SMOOTHING_FACTOR + currentHigh * (1.0f - SMOOTHING_FACTOR);
    
    smoothedBass.store(newSmoothedBass);
    smoothedMid.store(newSmoothedMid);
    smoothedHigh.store(newSmoothedHigh);
}
