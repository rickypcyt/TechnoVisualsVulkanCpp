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
    // Try to use PulseAudio backend first
    const char* pulseHostApi = "PulseAudio";
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "[AudioSystem] PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    std::cout << "[AudioSystem] PortAudio initialized successfully" << std::endl;
    std::cout << "[AudioSystem] Available Host APIs:" << std::endl;
    
    int numHostApis = Pa_GetHostApiCount();
    for (int i = 0; i < numHostApis; i++) {
        const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(i);
        std::cout << "  " << i << ": " << hostApiInfo->name << std::endl;
    }
    
    listDevices();

    // Try to find PulseAudio input device
    int pulseDeviceIndex = -1;
    int numDevices = Pa_GetDeviceCount();
    
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
        
        // Look for PulseAudio devices with input channels
        if (deviceInfo->maxInputChannels > 0 && 
            std::string(hostApiInfo->name) == pulseHostApi) {
            pulseDeviceIndex = i;
            std::cout << "[AudioSystem] Found PulseAudio device: " << deviceInfo->name << std::endl;
            break;
        }
    }
    
    // Use PulseAudio device if found, otherwise fall back to default
    if (pulseDeviceIndex != paNoDevice) {
        inputDeviceIndex = pulseDeviceIndex;
    } else {
        std::cout << "[AudioSystem] WARNING: PulseAudio not found in PortAudio" << std::endl;
        std::cout << "[AudioSystem] PortAudio may have been compiled without PulseAudio support" << std::endl;
        std::cout << "[AudioSystem] Using default input device" << std::endl;
        inputDeviceIndex = Pa_GetDefaultInputDevice();
        if (inputDeviceIndex == paNoDevice) {
            std::cerr << "[AudioSystem] No default input device found" << std::endl;
            return false;
        }
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(inputDeviceIndex);
    const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
    std::cout << "[AudioSystem] Using input device: " << deviceInfo->name << std::endl;
    std::cout << "[AudioSystem] Backend: " << hostApiInfo->name << std::endl;
    
    if (std::string(hostApiInfo->name) != pulseHostApi) {
        std::cout << "[AudioSystem] WARNING: Not using PulseAudio backend" << std::endl;
        std::cout << "[AudioSystem] The application may not appear in pavucontrol" << std::endl;
        std::cout << "[AudioSystem] To fix this, rebuild PortAudio with PulseAudio support" << std::endl;
    }

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

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(inputDeviceIndex);
    const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
    
    std::cout << "[AudioSystem] Opening stream with device: " << deviceInfo->name << std::endl;
    std::cout << "[AudioSystem] Backend: " << hostApiInfo->name << std::endl;

    PaStreamParameters inputParameters;
    std::memset(&inputParameters, 0, sizeof(inputParameters));
    inputParameters.device = inputDeviceIndex;
    inputParameters.channelCount = 1; // Mono input
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = deviceInfo->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    // Use device's default sample rate instead of forcing 44100
    // JACK typically uses 48000Hz, ALSA varies
    double sampleRate = deviceInfo->defaultSampleRate;
    std::cout << "[AudioSystem] Using sample rate: " << sampleRate << " Hz" << std::endl;

    PaError err = Pa_OpenStream(
        &stream,
        &inputParameters,
        nullptr, // No output
        sampleRate,
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

    std::cout << "[AudioSystem] Available audio devices:" << std::endl;
    
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        std::cout << "  Device " << i << ": " << deviceInfo->name << std::endl;
        std::cout << "    Input channels: " << deviceInfo->maxInputChannels << std::endl;
        std::cout << "    Output channels: " << deviceInfo->maxOutputChannels << std::endl;
    }
}

std::vector<std::string> AudioSystem::getInputDeviceNames() {
    std::vector<std::string> deviceNames;
    int numDevices = Pa_GetDeviceCount();
    
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
        
        // Include all input devices, including monitor devices
        if (deviceInfo->maxInputChannels > 0) {
            std::string deviceName = deviceInfo->name;
            // Add backend info for clarity
            deviceName += " [";
            deviceName += hostApiInfo->name;
            deviceName += "]";
            deviceNames.push_back(deviceName);
        } else {
            deviceNames.push_back(""); // No input channels
        }
    }
    
    return deviceNames;
}

bool AudioSystem::setInputDevice(int deviceIndex) {
    int numDevices = Pa_GetDeviceCount();
    if (deviceIndex < 0 || deviceIndex >= numDevices) {
        std::cerr << "[AudioSystem] Invalid device index: " << deviceIndex << std::endl;
        return false;
    }
    
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    if (deviceInfo->maxInputChannels == 0) {
        std::cerr << "[AudioSystem] Device has no input channels: " << deviceIndex << std::endl;
        return false;
    }
    
    // Stop and close current stream if running
    if (stream) {
        PaError err = Pa_StopStream(stream);
        if (err != paNoError && err != paStreamIsNotStopped) {
            std::cerr << "[AudioSystem] StopStream error: " << Pa_GetErrorText(err) << std::endl;
        }
        
        err = Pa_CloseStream(stream);
        if (err != paNoError) {
            std::cerr << "[AudioSystem] CloseStream error: " << Pa_GetErrorText(err) << std::endl;
        }
        
        stream = nullptr;
        streamRunning = false;
    }
    
    // Set new device
    inputDeviceIndex = deviceIndex;
    std::cout << "[AudioSystem] Input device changed to: " << deviceInfo->name << std::endl;
    
    // Always start the stream with the new device
    return startStream();
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
                  << " SubBass: " << audioSystem->subBassLevel.load()
                  << " Kick: " << audioSystem->kickLevel.load()
                  << " Bass: " << audioSystem->bassLevel.load()
                  << " Mid: " << audioSystem->midLevel.load()
                  << " High: " << audioSystem->highLevel.load()
                  << " | Smoothed Kick: " << audioSystem->smoothedKick.load() << std::endl;
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
    float subBassSum = 0.0f;
    float kickSum = 0.0f;
    float bassSum = 0.0f;
    float midSum = 0.0f;
    float highSum = 0.0f;
    
    // Frequency bins: sampleRate / FFT_SIZE = 44100 / 512 ≈ 86 Hz per bin
    // Optimized for techno (Oscar Mulero style):
    // SubBass: 30-60 Hz → bin 0 (0-86 Hz) - sub-bass rumble
    // Kick: 60-150 Hz → bin 1 (86-172 Hz) - kick drum fundamental
    // Bass: 150-300 Hz → bins 2-3 (172-344 Hz) - bass body
    // Mid: 300-4000 Hz → bins 4-46 (344-4000 Hz) - percussion/mids
    // High: 4000+ Hz → bins 47+ (4000+ Hz) - hi-hats/snares
    
    int subBassEnd = 1;
    int kickEnd = 2;
    int bassEnd = 4;
    int midEnd = 47;
    
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        if (i < subBassEnd) {
            subBassSum += fftMagnitudes[i];
        } else if (i < kickEnd) {
            kickSum += fftMagnitudes[i];
        } else if (i < bassEnd) {
            bassSum += fftMagnitudes[i];
        } else if (i < midEnd) {
            midSum += fftMagnitudes[i];
        } else {
            highSum += fftMagnitudes[i];
        }
    }
    
    // Normalize bands
    float currentSubBass = subBassSum / subBassEnd;
    float currentKick = kickSum / (kickEnd - subBassEnd);
    float currentBass = bassSum / (bassEnd - kickEnd);
    float currentMid = midSum / (midEnd - bassEnd);
    float currentHigh = highSum / ((FFT_SIZE / 2) - midEnd);
    
    subBassLevel.store(currentSubBass);
    kickLevel.store(currentKick);
    bassLevel.store(currentBass);
    midLevel.store(currentMid);
    highLevel.store(currentHigh);
    
    // Apply exponential smoothing (faster for techno transients)
    float prevSmoothedSubBass = smoothedSubBass.load();
    float prevSmoothedKick = smoothedKick.load();
    float prevSmoothedBass = smoothedBass.load();
    float prevSmoothedMid = smoothedMid.load();
    float prevSmoothedHigh = smoothedHigh.load();
    
    float newSmoothedSubBass = prevSmoothedSubBass * SMOOTHING_FACTOR + currentSubBass * (1.0f - SMOOTHING_FACTOR);
    float newSmoothedKick = prevSmoothedKick * SMOOTHING_FACTOR + currentKick * (1.0f - SMOOTHING_FACTOR);
    float newSmoothedBass = prevSmoothedBass * SMOOTHING_FACTOR + currentBass * (1.0f - SMOOTHING_FACTOR);
    float newSmoothedMid = prevSmoothedMid * SMOOTHING_FACTOR + currentMid * (1.0f - SMOOTHING_FACTOR);
    float newSmoothedHigh = prevSmoothedHigh * SMOOTHING_FACTOR + currentHigh * (1.0f - SMOOTHING_FACTOR);
    
    smoothedSubBass.store(newSmoothedSubBass);
    smoothedKick.store(newSmoothedKick);
    smoothedBass.store(newSmoothedBass);
    smoothedMid.store(newSmoothedMid);
    smoothedHigh.store(newSmoothedHigh);
}
