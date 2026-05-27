// AudioSystem.cpp
#include "AudioSystem.h"

#include <iostream>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

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

    refreshPulseAudioSources();
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

void AudioSystem::refreshPulseAudioSources() {
    pulseSourceNames.clear();
    pulseSourceIds.clear();

    auto parseTabLine = [](const char* line, std::string& outName) -> bool {
        // Format: <number>\t<name>\t<driver>\t<spec>\t<state>
        char buf[512];
        strncpy(buf, line, sizeof(buf)-1);
        buf[sizeof(buf)-1] = '\0';
        char* tok = strtok(buf, "\t");
        if (!tok) return false; // number
        tok = strtok(nullptr, "\t");
        if (!tok) return false; // name
        outName = tok;
        while (!outName.empty() && (outName.back() == '\n' || outName.back() == '\r'))
            outName.pop_back();
        return true;
    };

    // ── 1. Real input sources ─────────────────────────────────────────
    FILE* fp = popen("pactl list short sources", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            std::string name;
            if (parseTabLine(line, name)) {
                pulseSourceIds.push_back(name);
                pulseSourceNames.push_back(name);
            }
        }
        pclose(fp);
    } else {
        std::cerr << "[AudioSystem] Failed to query PulseAudio sources\n";
    }

    // ── 2. Sink monitors (always add these, they are the key for system audio) ──
    FILE* fsinks = popen("pactl list short sinks", "r");
    if (fsinks) {
        char line[512];
        while (fgets(line, sizeof(line), fsinks)) {
            std::string sinkName;
            if (parseTabLine(line, sinkName)) {
                std::string monitorId = sinkName + ".monitor";
                // Only add if not already present
                bool already = false;
                for (const auto& id : pulseSourceIds) {
                    if (id == monitorId) { already = true; break; }
                }
                if (!already) {
                    pulseSourceIds.push_back(monitorId);
                    pulseSourceNames.push_back("[Monitor] " + sinkName);
                }
            }
        }
        pclose(fsinks);
    }

    // ── 3. Determine current default source ─────────────────────────────
    FILE* def = popen("pactl info | grep 'Default Source'", "r");
    if (def) {
        char defLine[256];
        if (fgets(defLine, sizeof(defLine), def)) {
            std::string defaultName;
            char* colon = strchr(defLine, ':');
            if (colon) {
                defaultName = colon + 1;
                while (!defaultName.empty() && (defaultName.front() == ' ' || defaultName.front() == '\t'))
                    defaultName.erase(0, 1);
                while (!defaultName.empty() && (defaultName.back() == '\n' || defaultName.back() == '\r'))
                    defaultName.pop_back();
            }
            for (int i = 0; i < (int)pulseSourceIds.size(); ++i) {
                if (pulseSourceIds[i] == defaultName) {
                    pulseSourceIndex = i;
                    break;
                }
            }
        }
        pclose(def);
    }

    std::cout << "[AudioSystem] Found " << pulseSourceNames.size()
              << " PulseAudio sources (default idx=" << pulseSourceIndex << ")\n";
}

std::vector<std::string> AudioSystem::getInputDeviceNames() {
    return pulseSourceNames;
}

bool AudioSystem::setInputDevice(int deviceIndex) {
    if (deviceIndex < 0 || deviceIndex >= (int)pulseSourceIds.size()) {
        return false;
    }

    bool wasRunning = running.load();
    if (wasRunning) {
        stop();
    }

    // Set the selected source as PulseAudio default
    std::string cmd = "pactl set-default-source " + pulseSourceIds[deviceIndex];
    int ret = system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "[AudioSystem] Failed to set default source: " << pulseSourceIds[deviceIndex] << "\n";
    } else {
        std::cout << "[AudioSystem] Default source set to: " << pulseSourceIds[deviceIndex] << "\n";
    }

    pulseSourceIndex = deviceIndex;

    // Give PulseAudio time to propagate the new default
    usleep(100000); // 100ms

    // Re-query PortAudio default — with PulseAudio backend this now captures
    // the newly selected default source.
    inputDevice = Pa_GetDefaultInputDevice();
    if (inputDevice != paNoDevice) {
        const PaDeviceInfo* dev = Pa_GetDeviceInfo(inputDevice);
        if (dev) {
            sampleRate = dev->defaultSampleRate;
            std::cout << "[AudioSystem] PortAudio default input: " << dev->name
                      << " idx=" << inputDevice << " sr=" << sampleRate << "\n";
        }
    } else {
        std::cerr << "[AudioSystem] Pa_GetDefaultInputDevice returned paNoDevice\n";
    }

    if (wasRunning) {
        bool ok = start();
        std::cout << "[AudioSystem] Stream restart after device change: " << (ok ? "OK" : "FAILED") << "\n";
        return ok;
    }
    return true;
}

bool AudioSystem::restartStream() {
    bool wasRunning = running.load();
    if (wasRunning) {
        stop();
    }
    // Re-query default in case it changed externally
    inputDevice = Pa_GetDefaultInputDevice();
    if (inputDevice != paNoDevice) {
        const PaDeviceInfo* dev = Pa_GetDeviceInfo(inputDevice);
        if (dev) {
            sampleRate = dev->defaultSampleRate;
            std::cout << "[AudioSystem] Restart — using device: " << dev->name
                      << " idx=" << inputDevice << " sr=" << sampleRate << "\n";
        }
    }
    bool ok = start();
    std::cout << "[AudioSystem] Manual restart: " << (ok ? "OK" : "FAILED") << "\n";
    return ok;
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
    float db = 20.0f * std::log10(rms + 1e-6f);
    rmsDb.store(db);

    fftwf_execute(fftPlan);

    // Magnitude spectrum — linear scale with Hanning-window compensation.
    // Hanning attenuates by ~50% so we multiply by 2 to recover amplitude.
    const float windowCompensation = 2.0f / FFT_SIZE;
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float re = fftOut[i][0];
        float im = fftOut[i][1];
        float mag = std::sqrt(re * re + im * im) * windowCompensation;
        // Soft compression: preserves dynamic range better than log10
        // Maps typical audio magnitudes (0..1) to a usable 0..1 range
        float compressed = std::sqrt(mag);
        fftMagnitudes[i] = compressed;
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
    // Energy-based (RMS) per band with logarithmic frequency spacing.
    // This is closer to how real spectrum analysers / graphic EQs work.
    auto rmsBand = [&](float startHz, float endHz) -> float {
        int start = std::max(1, hzToBin(startHz));
        int end   = std::min((int)fftMagnitudes.size() - 1, hzToBin(endHz));
        if (end <= start) end = start + 1;

        float sumSq = 0.0f;
        int count = 0;
        for (int i = start; i <= end; i++) {
            sumSq += fftMagnitudes[i] * fftMagnitudes[i];
            count++;
        }
        return std::sqrt(sumSq / count);
    };

    // Logarithmic-spaced bands (roughly 1-2 octaves each)
    float sb = rmsBand(20,    80);
    float k  = rmsBand(80,    250);
    float b  = rmsBand(250,   500);
    float m  = rmsBand(500,   2000);
    float h  = rmsBand(2000, 20000);

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