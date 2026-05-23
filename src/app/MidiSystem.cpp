#include "MidiSystem.h"
#include <iostream>
#include <cstring>
#include <utility>

MidiSystem::MidiSystem()
    : midiIn(nullptr)
    , initialized(false)
    , enabled(true)
    , learnMode(false)
    , hasLearned(false)
{
}

MidiSystem::~MidiSystem() {
    shutdown();
}

bool MidiSystem::initialize() {
    try {
        midiIn = new RtMidiIn(RtMidi::UNSPECIFIED, "VulkanApp MIDI");
        initialized = true;

        // No default mappings - start clean

        std::cout << "[MidiSystem] Initialized successfully" << std::endl;
        return true;
    } catch (RtMidiError& error) {
        std::cerr << "[MidiSystem] Failed to initialize: " << error.getMessage() << std::endl;
        initialized = false;
        return false;
    }
}

void MidiSystem::shutdown() {
    if (midiIn) {
        closePort();
        delete midiIn;
        midiIn = nullptr;
    }
    initialized = false;
}

bool MidiSystem::openPort(unsigned int portIndex) {
    if (!initialized || !midiIn) {
        std::cerr << "[MidiSystem] Not initialized" << std::endl;
        return false;
    }

    try {
        midiIn->openPort(portIndex);
        std::cout << "[MidiSystem] Opened port " << portIndex << std::endl;
        return true;
    } catch (RtMidiError& error) {
        std::cerr << "[MidiSystem] Failed to open port: " << error.getMessage() << std::endl;
        return false;
    }
}

void MidiSystem::closePort() {
    if (midiIn && midiIn->isPortOpen()) {
        midiIn->closePort();
        std::cout << "[MidiSystem] Port closed" << std::endl;
    }
}

std::vector<std::string> MidiSystem::getAvailablePorts() const {
    std::vector<std::string> ports;
    if (!initialized || !midiIn) {
        return ports;
    }

    try {
        unsigned int portCount = midiIn->getPortCount();
        for (unsigned int i = 0; i < portCount; ++i) {
            ports.push_back(midiIn->getPortName(i));
        }
    } catch (RtMidiError& error) {
        std::cerr << "[MidiSystem] Error getting ports: " << error.getMessage() << std::endl;
    }

    return ports;
}

unsigned int MidiSystem::getPortCount() const {
    if (!initialized || !midiIn) {
        return 0;
    }

    try {
        return midiIn->getPortCount();
    } catch (RtMidiError& error) {
        std::cerr << "[MidiSystem] Error getting port count: " << error.getMessage() << std::endl;
        return 0;
    }
}

void MidiSystem::update() {
    if (!enabled || !initialized || !midiIn || !midiIn->isPortOpen()) {
        return;
    }

    std::vector<unsigned char> message;
    try {
        while (midiIn->getMessage(&message) > 0) {
            if (!message.empty()) {
                MidiMessage msg = parseMessage(&message);

                // Learn mode: capture the first message
                if (learnMode && !hasLearned) {
                    lastLearnedMessage = msg;
                    hasLearned = true;
                    learnMode = false; // Exit learn mode after capturing
                    std::cout << "[MidiSystem] Learned MIDI message: ";
                    if (msg.type == MidiEventType::CONTROL_CHANGE) {
                        std::cout << "CC " << msg.controller << " value " << msg.value << std::endl;
                    } else if (msg.type == MidiEventType::NOTE_ON) {
                        std::cout << "Note " << msg.note << " velocity " << msg.velocity << std::endl;
                    }
                    continue; // Don't process the learned message yet
                }

                // Call event callback if set
                if (eventCallback) {
                    eventCallback(msg);
                }
            }
        }
    } catch (RtMidiError& error) {
        std::cerr << "[MidiSystem] Error reading MIDI message: " << error.getMessage() << std::endl;
    }
}

void MidiSystem::setEventCallback(std::function<void(const MidiMessage&)> callback) {
    eventCallback = callback;
}

void MidiSystem::addMapping(int ccNumber, const std::string& parameterName, float minVal, float maxVal, bool invert) {
    MidiMapping mapping;
    mapping.ccNumber = ccNumber;
    mapping.parameterName = parameterName;
    mapping.minValue = minVal;
    mapping.maxValue = maxVal;
    mapping.invert = invert;
    mappings[ccNumber] = mapping;
}

void MidiSystem::removeMapping(int ccNumber) {
    mappings.erase(ccNumber);
}

void MidiSystem::clearMappings() {
    mappings.clear();
}

const std::map<int, MidiMapping>& MidiSystem::getMappings() const {
    return mappings;
}

void MidiSystem::addTriggerMapping(int note, const std::string& actionName) {
    MidiTriggerMapping mapping;
    mapping.note = note;
    mapping.actionName = actionName;
    triggerMappings[note] = mapping;
}

void MidiSystem::removeTriggerMapping(int note) {
    triggerMappings.erase(note);
}

void MidiSystem::clearTriggerMappings() {
    triggerMappings.clear();
}

const std::map<int, MidiTriggerMapping>& MidiSystem::getTriggerMappings() const {
    return triggerMappings;
}

void MidiSystem::setTriggerCallback(std::function<void(const std::string&)> callback) {
    triggerCallback = std::move(callback);
}

void MidiSystem::applyToVisualControls(const MidiMessage& msg, VisualControls& controls) {
    if (msg.type == MidiEventType::CONTROL_CHANGE) {
        applyCCMapping(msg.controller, msg.value, controls);
    } else if (msg.type == MidiEventType::NOTE_ON) {
        applyNoteMapping(msg.note, msg.velocity, controls);
    }
}

void MidiSystem::setEnabled(bool enabled) {
    this->enabled = enabled;
}

bool MidiSystem::isEnabled() const {
    return enabled;
}

void MidiSystem::setLearnMode(bool enabled) {
    learnMode = enabled;
    if (enabled) {
        hasLearned = false; // Reset when entering learn mode
    }
}

bool MidiSystem::isLearnMode() const {
    return learnMode;
}

MidiMessage MidiSystem::getLastLearnedMessage() const {
    return lastLearnedMessage;
}

bool MidiSystem::hasLearnedMessage() const {
    return hasLearned;
}

void MidiSystem::clearLearnedMessage() {
    hasLearned = false;
}

MidiMessage MidiSystem::parseMessage(std::vector<unsigned char>* message) {
    MidiMessage msg;
    msg.channel = static_cast<int>((*message)[0] & 0x0F);
    unsigned char status = (*message)[0] & 0xF0;

    switch (status) {
        case 0x80: // Note Off
            msg.type = MidiEventType::NOTE_OFF;
            msg.note = static_cast<int>((*message)[1]);
            msg.velocity = static_cast<int>((*message)[2]);
            break;
        case 0x90: // Note On
            msg.type = MidiEventType::NOTE_ON;
            msg.note = static_cast<int>((*message)[1]);
            msg.velocity = static_cast<int>((*message)[2]);
            break;
        case 0xB0: // Control Change
            msg.type = MidiEventType::CONTROL_CHANGE;
            msg.controller = static_cast<int>((*message)[1]);
            msg.value = static_cast<int>((*message)[2]);
            break;
        case 0xC0: // Program Change
            msg.type = MidiEventType::PROGRAM_CHANGE;
            msg.value = static_cast<int>((*message)[1]);
            break;
        case 0xE0: // Pitch Bend
            msg.type = MidiEventType::PITCH_BEND;
            int bend = (*message)[1] + ((*message)[2] << 7);
            msg.pitch = (static_cast<double>(bend) - 8192.0) / 8192.0;
            break;
    }

    return msg;
}

void MidiSystem::applyCCMapping(int ccNumber, int value, VisualControls& controls) {
    auto it = mappings.find(ccNumber);
    if (it == mappings.end()) {
        return;
    }

    const MidiMapping& mapping = it->second;
    float normalizedValue = static_cast<float>(value) / 127.0f;

    if (mapping.invert) {
        normalizedValue = 1.0f - normalizedValue;
    }

    float mappedValue = mapping.minValue + normalizedValue * (mapping.maxValue - mapping.minValue);

    // Map to VisualControls parameters - all parameters from the wizard
    const std::string& name = mapping.parameterName;

    // Procedural
    if (name == "animationSpeed") controls.animationSpeed = mappedValue;
    else if (name == "tempo") controls.tempo = mappedValue;
    else if (name == "energy") controls.energy = mappedValue;
    else if (name == "bass") controls.bass = mappedValue;
    else if (name == "mid") controls.mid = mappedValue;
    else if (name == "high") controls.high = mappedValue;
    else if (name == "colorBlend") controls.colorBlend = mappedValue;
    else if (name == "uvWarpStrength") controls.uvWarpStrength = mappedValue;
    else if (name == "rippleStrength") controls.rippleStrength = mappedValue;
    else if (name == "rippleFrequency") controls.rippleFrequency = mappedValue;
    else if (name == "swirlStrength") controls.swirlStrength = mappedValue;
    else if (name == "displacementAmount") controls.displacementAmount = mappedValue;
    else if (name == "kaleidoSegments") controls.kaleidoSegments = mappedValue;
    else if (name == "tunnelDepth") controls.tunnelDepth = mappedValue;
    else if (name == "tunnelCurvature") controls.tunnelCurvature = mappedValue;
    // Post FX
    else if (name == "bloomIntensity") controls.bloomIntensity = mappedValue;
    else if (name == "bloomThreshold") controls.bloomThreshold = mappedValue;
    else if (name == "aberrationAmount") controls.aberrationAmount = mappedValue;
    else if (name == "grainStrength") controls.grainStrength = mappedValue;
    else if (name == "crtCurvature") controls.crtCurvature = mappedValue;
    else if (name == "crtScanlineIntensity") controls.crtScanlineIntensity = mappedValue;
    else if (name == "crtMaskIntensity") controls.crtMaskIntensity = mappedValue;
    else if (name == "crtVignette") controls.crtVignette = mappedValue;
    else if (name == "crtFishEye") controls.crtFishEye = mappedValue;
    else if (name == "gaussianBlur") controls.gaussianBlur = mappedValue;
    else if (name == "directionalBlur") controls.directionalBlur = mappedValue;
    else if (name == "directionalBlurAngle") controls.directionalBlurAngle = mappedValue;
    else if (name == "zoomBlur") controls.zoomBlur = mappedValue;
    else if (name == "motionBlur") controls.motionBlur = mappedValue;
    else if (name == "temporalBlur") controls.temporalBlur = mappedValue;
    else if (name == "unsharpMask") controls.unsharpMask = mappedValue;
    else if (name == "casAmount") controls.casAmount = mappedValue;
    else if (name == "localContrast") controls.localContrast = mappedValue;
    else if (name == "pixelateAmount") controls.pixelateAmount = mappedValue;
    else if (name == "strobeSpeed") controls.strobeSpeed = mappedValue;
    else if (name == "thresholdLevel") controls.thresholdLevel = mappedValue;
    else if (name == "slowZoomAmount") controls.slowZoomAmount = mappedValue;
    else if (name == "edgeStrength") controls.edgeStrength = mappedValue;
    else if (name == "edgeThreshold") controls.edgeThreshold = mappedValue;
    else if (name == "edgeBlend") controls.edgeBlend = mappedValue;
    else if (name == "fxaaQualitySubpix") controls.fxaaQualitySubpix = mappedValue;
    else if (name == "fxaaQualityEdgeThreshold") controls.fxaaQualityEdgeThreshold = mappedValue;
    else if (name == "fxaaQualityEdgeThresholdMin") controls.fxaaQualityEdgeThresholdMin = mappedValue;
    // VJay Basics
    else if (name == "videoPlaybackRate") controls.videoPlaybackRate = mappedValue;
    else if (name == "videoDecodeOversample") controls.videoDecodeOversample = mappedValue;
    else if (name == "videoMix") controls.videoMix = mappedValue;
    else if (name == "video2Mix") controls.video2Mix = mappedValue;
    else if (name == "video2PlaybackRate") controls.video2PlaybackRate = mappedValue;
    else if (name == "grayscaleAmount") controls.grayscaleAmount = mappedValue;
    else if (name == "sharpenAmount") controls.sharpenAmount = mappedValue;
    else if (name == "gradeBrightness") controls.gradeBrightness = mappedValue;
    else if (name == "gradeContrast") controls.gradeContrast = mappedValue;
    else if (name == "gradeSaturation") controls.gradeSaturation = mappedValue;
    else if (name == "gradeHueShift") controls.gradeHueShift = mappedValue;
    else if (name == "gradeGamma") controls.gradeGamma = mappedValue;
    else if (name == "colorLUTIndex") controls.colorLUTIndex = static_cast<int>(mappedValue);
    else if (name == "splitToneBalance") controls.splitToneBalance = mappedValue;
    else if (name == "blendProceduralMix") controls.blendProceduralMix = mappedValue;
    else if (name == "blendVideoMix") controls.blendVideoMix = mappedValue;
    else if (name == "blendFeedbackMix") controls.blendFeedbackMix = mappedValue;
    // VJay Extra
    else if (name == "feedbackAmount") controls.feedbackAmount = mappedValue;
    else if (name == "trailStrength") controls.trailStrength = mappedValue;
    else if (name == "temporalAccumulation") controls.temporalAccumulation = mappedValue;
    else if (name == "feedbackDecay") controls.feedbackDecay = mappedValue;
    else if (name == "recursiveBlend") controls.recursiveBlend = mappedValue;
    else if (name == "glitchAmount") controls.glitchAmount = mappedValue;
    else if (name == "glitchDatamosh") controls.glitchDatamosh = mappedValue;
    else if (name == "glitchRGBSplit") controls.glitchRGBSplit = mappedValue;
    else if (name == "glitchScanlineBreak") controls.glitchScanlineBreak = mappedValue;
    else if (name == "glitchJitter") controls.glitchJitter = mappedValue;
    else if (name == "glitchTearing") controls.glitchTearing = mappedValue;
    else if (name == "glitchPixelSort") controls.glitchPixelSort = mappedValue;
    else if (name == "glitchBufferCorruption") controls.glitchBufferCorruption = mappedValue;
    else if (name == "analogScanlineFocus") controls.analogScanlineFocus = mappedValue;
    else if (name == "analogMaskBalance") controls.analogMaskBalance = mappedValue;
    else if (name == "analogNoise") controls.analogNoise = mappedValue;
    else if (name == "analogBloom") controls.analogBloom = mappedValue;
    else if (name == "vhsDistortion") controls.vhsDistortion = mappedValue;
    else if (name == "analogChromaticAberration") controls.analogChromaticAberration = mappedValue;
    else if (name == "mirrorAmount") controls.mirrorAmount = mappedValue;
    else if (name == "posterizeLevels") controls.posterizeLevels = mappedValue;
    else if (name == "zoomPulseAmount") controls.zoomPulseAmount = mappedValue;
    else if (name == "rgbShiftAmount") controls.rgbShiftAmount = mappedValue;
    else if (name == "audioWarpResponse") controls.audioWarpResponse = mappedValue;
    else if (name == "audioFeedbackResponse") controls.audioFeedbackResponse = mappedValue;
    else if (name == "audioBlurResponse") controls.audioBlurResponse = mappedValue;
    else if (name == "audioColorResponse") controls.audioColorResponse = mappedValue;
    else if (name == "audioGlitchResponse") controls.audioGlitchResponse = mappedValue;
    else if (name == "audioBeatSync") controls.audioBeatSync = mappedValue;
    else if (name == "audioLfoRate") controls.audioLfoRate = mappedValue;
    else if (name == "temporalInterpolation") controls.temporalInterpolation = mappedValue;
    else if (name == "temporalBlendStrength") controls.temporalBlendStrength = mappedValue;
    else if (name == "slowMotionFactor") controls.slowMotionFactor = mappedValue;
    else if (name == "frameAccumulation") controls.frameAccumulation = mappedValue;
}

void MidiSystem::applyNoteMapping(int note, int velocity, VisualControls& controls) {
    auto triggerIt = triggerMappings.find(note);
    if (triggerIt != triggerMappings.end() && velocity > 0) {
        if (triggerCallback) {
            triggerCallback(triggerIt->second.actionName);
        }
        return;
    }

    // Note-based triggers
    // Low notes (C2-C3) for mode switching
    if (note >= 36 && note <= 48) {
        int mode = note - 36;
        controls.activeMode = mode;
    }
    // High notes for triggering effects
    else if (note >= 60 && velocity > 0) {
        // Trigger random video change on C4 (note 60)
        if (note == 60) {
            // This would need to be connected to Application's video randomizer
            // For now, just a placeholder
        }
        // Toggle effects on other notes
        else if (note == 62) controls.enablePostBloom = !controls.enablePostBloom;
        else if (note == 64) controls.enablePostGlitch = !controls.enablePostGlitch;
        else if (note == 65) controls.enablePostBend = !controls.enablePostBend;
        else if (note == 67) controls.enableFeedback = !controls.enableFeedback;
    }
}
