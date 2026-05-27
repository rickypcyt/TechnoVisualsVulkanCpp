// AudioSystem.cpp
#include "AudioSystem.h"

#include <iostream>
#include <cmath>
#include <cstring>

AudioSystem::AudioSystem() {
    ringBuffer.resize(FFT_SIZE * 8, 0.0f);

    fftFrame.resize(FFT_SIZE);
    fftMagnitudes.resize(FFT_SIZE / 2);

    window.resize(FFT_SIZE);

    for (int i = 0; i < FFT_SIZE; i++) {
        window[i] =
            0.5f * (1.0f - std::cos(
                2.0f * M_PI * i / (FFT_SIZE - 1)
            ));
    }

    fftIn = (float*)fftwf_malloc(sizeof(float) * FFT_SIZE);

    fftOut = (fftwf_complex*)fftwf_malloc(
        sizeof(fftwf_complex) * (FFT_SIZE / 2 + 1)
    );

    fftPlan = fftwf_plan_dft_r2c_1d(
        FFT_SIZE,
        fftIn,
        fftOut,
        FFTW_MEASURE
    );
}

AudioSystem::~AudioSystem() {
    shutdown();

    fftwf_destroy_plan(fftPlan);

    fftwf_free(fftIn);
    fftwf_free(fftOut);
}

bool AudioSystem::initialize() {
    PaError err = Pa_Initialize();

    if (err != paNoError) {
        std::cerr << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    inputDevice = Pa_GetDefaultInputDevice();

    if (inputDevice == paNoDevice) {
        std::cerr << "No input device\n";
        return false;
    }

    const PaDeviceInfo* dev =
        Pa_GetDeviceInfo(inputDevice);

    sampleRate = dev->defaultSampleRate;

    std::cout << "[AudioSystem] Input device: " << dev->name
              << " Sample rate: " << sampleRate << std::endl;

    return true;
}

bool AudioSystem::openStream() {
    PaStreamParameters input{};

    input.device = inputDevice;
    input.channelCount = CHANNELS;
    input.sampleFormat = paFloat32;
    input.suggestedLatency =
        Pa_GetDeviceInfo(inputDevice)->defaultLowInputLatency;

    PaError err = Pa_OpenStream(
        &stream,
        &input,
        nullptr,
        sampleRate,
        HOP_SIZE,
        paNoFlag,
        audioCallback,
        this
    );

    if (err != paNoError) {
        std::cerr << "[AudioSystem] Pa_OpenStream failed: " << Pa_GetErrorText(err) << std::endl;
    } else {
        std::cout << "[AudioSystem] Stream opened successfully" << std::endl;
    }

    return err == paNoError;
}

bool AudioSystem::start() {
    if (!openStream())
        return false;

    running = true;

    dspThread = std::thread(
        &AudioSystem::dspLoop,
        this
    );

    PaError err = Pa_StartStream(stream);

    if (err != paNoError) {
        std::cerr << "[AudioSystem] Pa_StartStream failed: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    std::cout << "[AudioSystem] Stream started successfully" << std::endl;
    return true;
}

void AudioSystem::stop() {
    running = false;

    cv.notify_all();

    if (dspThread.joinable())
        dspThread.join();

    if (stream) {
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        stream = nullptr;
    }
}

void AudioSystem::shutdown() {
    stop();
    Pa_Terminate();
}

std::vector<std::string> AudioSystem::getInputDeviceNames() {
    std::vector<std::string> names;
    int numDevices = Pa_GetDeviceCount();

    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo && deviceInfo->maxInputChannels > 0) {
            names.push_back(deviceInfo->name);
        }
    }

    return names;
}

bool AudioSystem::setInputDevice(int deviceIndex) {
    if (deviceIndex < 0 || deviceIndex >= Pa_GetDeviceCount()) {
        return false;
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    if (!deviceInfo || deviceInfo->maxInputChannels == 0) {
        return false;
    }

    bool wasRunning = running.load();

    if (wasRunning) {
        stop();
    }

    inputDevice = deviceIndex;
    sampleRate = deviceInfo->defaultSampleRate;

    if (wasRunning) {
        return start();
    }

    return true;
}

int AudioSystem::audioCallback(
    const void* input,
    void*,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo*,
    PaStreamCallbackFlags,
    void* userData
) {
    auto* self = static_cast<AudioSystem*>(userData);

    if (!input)
        return paContinue;

    const float* in =
        static_cast<const float*>(input);

    {
        std::lock_guard<std::mutex> lock(
            self->ringMutex
        );

        for (unsigned long i = 0; i < frameCount; i++) {
            self->ringBuffer[self->writeIndex] = in[i];

            self->writeIndex =
                (self->writeIndex + 1)
                % self->ringBuffer.size();
        }
    }

    self->cv.notify_one();

    return paContinue;
}

void AudioSystem::dspLoop() {
    size_t readIndex = 0;

    while (running) {
        {
            std::unique_lock<std::mutex> lock(
                ringMutex
            );

            cv.wait_for(
                lock,
                std::chrono::milliseconds(10)
            );

            // Sync readIndex with writeIndex to read latest data
            readIndex = (writeIndex + ringBuffer.size() - FFT_SIZE) % ringBuffer.size();

            for (int i = 0; i < FFT_SIZE; i++) {
                size_t idx =
                    (readIndex + i)
                    % ringBuffer.size();

                fftFrame[i] = ringBuffer[idx];
            }

            readIndex =
                (readIndex + HOP_SIZE)
                % ringBuffer.size();
        }

        processFFT();
    }
}

void AudioSystem::processFFT() {
    float rms = 0.0f;

    for (int i = 0; i < FFT_SIZE; i++) {
        float s = fftFrame[i];

        rms += s * s;

        fftIn[i] = s * window[i];
    }

    rms = std::sqrt(rms / FFT_SIZE);

    float db =
        20.0f * std::log10(rms + 1e-6f);

    rmsDb.store(db);

    fftwf_execute(fftPlan);

    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float re = fftOut[i][0];
        float im = fftOut[i][1];

        float mag =
            std::sqrt(re * re + im * im);

        mag /= FFT_SIZE;

        mag = std::log10(1.0f + mag * 10.0f);

        fftMagnitudes[i] = mag;
    }

    calculateBands();
}

int AudioSystem::hzToBin(float hz) const {
    float binWidth =
        sampleRate / FFT_SIZE;

    return static_cast<int>(hz / binWidth);
}

void AudioSystem::applyEnvelope(
    float input,
    std::atomic<float>& output
) {
    float current = output.load();

    float result;

    if (input > current) {
        result =
            current * ATTACK
            + input * (1.0f - ATTACK);
    }
    else {
        result =
            current * RELEASE
            + input * (1.0f - RELEASE);
    }

    output.store(result);
}

void AudioSystem::calculateBands() {
    auto sumRange =
        [&](float startHz, float endHz)
    {
        int start = hzToBin(startHz);
        int end   = hzToBin(endHz);

        float sum = 0.0f;

        for (int i = start; i <= end; i++) {
            sum += fftMagnitudes[i];
        }

        return sum / (end - start + 1);
    };

    float sb =
        sumRange(20, 60);

    float k =
        sumRange(60, 150);

    float b =
        sumRange(150, 300);

    float m =
        sumRange(300, 4000);

    float h =
        sumRange(4000, 12000);

    subBass.store(sb);
    kick.store(k);
    bass.store(b);
    mid.store(m);
    high.store(h);

    applyEnvelope(
        sb,
        smoothedSubBass
    );

    applyEnvelope(
        k,
        smoothedKick
    );

    applyEnvelope(
        b,
        smoothedBass
    );

    applyEnvelope(
        m,
        smoothedMid
    );

    applyEnvelope(
        h,
        smoothedHigh
    );

    float delta =
        k - previousKick;

    kickPeak.store(
        delta > KICK_THRESHOLD
    );

    previousKick = k;
}