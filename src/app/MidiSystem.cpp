#include "MidiSystem.h"
#include "parameters/ParameterBindingRegistry.h"
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
            if (message.empty())
                continue;

            MidiMessage msg = parseMessage(message);

            // Learn mode: capture only valid control messages
            if (learnMode && !hasLearned) {
                if (msg.type == MidiEventType::CONTROL_CHANGE ||
                    msg.type == MidiEventType::NOTE_ON) {
                    lastLearnedMessage = msg;
                    hasLearned = true;
                    learnMode = false;
                    std::cout << "[MidiSystem] Learned MIDI message: ";
                    if (msg.type == MidiEventType::CONTROL_CHANGE) {
                        std::cout << "CC " << msg.controller << " value " << msg.value << std::endl;
                    } else if (msg.type == MidiEventType::NOTE_ON) {
                        std::cout << "Note " << msg.note << " velocity " << msg.velocity << std::endl;
                    }
                }
                continue;
            }

            // Call event callback if set
            if (eventCallback) {
                eventCallback(msg);
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

MidiMessage MidiSystem::parseMessage(const std::vector<unsigned char>& message) {
    MidiMessage msg{};

    if (message.empty())
        return msg;

    const uint8_t status = message[0];
    const uint8_t type = status & 0xF0;

    msg.channel = status & 0x0F;

    switch (type) {
        case 0x80: // Note Off
            if (message.size() >= 3) {
                msg.type = MidiEventType::NOTE_OFF;
                msg.note = static_cast<int>(message[1]);
                msg.velocity = static_cast<int>(message[2]);
            }
            break;
        case 0x90: // Note On
            if (message.size() >= 3) {
                if (message[2] == 0)
                    msg.type = MidiEventType::NOTE_OFF;
                else
                    msg.type = MidiEventType::NOTE_ON;
                msg.note = static_cast<int>(message[1]);
                msg.velocity = static_cast<int>(message[2]);
            }
            break;
        case 0xB0: // Control Change
            if (message.size() >= 3) {
                msg.type = MidiEventType::CONTROL_CHANGE;
                msg.controller = static_cast<int>(message[1]);
                msg.value = static_cast<int>(message[2]);
            }
            break;
        case 0xC0: // Program Change
            if (message.size() >= 2) {
                msg.type = MidiEventType::PROGRAM_CHANGE;
                msg.value = static_cast<int>(message[1]);
            }
            break;
        case 0xE0: // Pitch Bend
            if (message.size() >= 3) {
                msg.type = MidiEventType::PITCH_BEND;
                int bend = message[1] | (message[2] << 7);
                msg.pitch = (static_cast<double>(bend) - 8192.0) / 8192.0;
            }
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

    const auto& registry = ParameterBindingRegistry::get();
    auto regIt = registry.find(mapping.parameterName);

    if (regIt == registry.end()) {
        return;
    }

    const ParamBinding& binding = regIt->second;

    switch (binding.type) {
        case ParamBinding::Type::Float:
            binding.set(controls, mappedValue);
            break;
        case ParamBinding::Type::Int:
            binding.set(controls, static_cast<int>(mappedValue));
            break;
        case ParamBinding::Type::Bool:
            binding.set(controls, mappedValue > 0.5f);
            break;
        case ParamBinding::Type::Vec3:
        case ParamBinding::Type::Vec4:
            // Vec3/Vec4 not supported for CC mapping (requires multi-dimensional input)
            break;
    }
}

void MidiSystem::applyNoteMapping(int note, int velocity, VisualControls& controls) {
    if (velocity <= 0)
        return;

    auto triggerIt = triggerMappings.find(note);
    if (triggerIt != triggerMappings.end()) {
        if (triggerCallback) {
            triggerCallback(triggerIt->second.actionName);
        }
        return;
    }

    // Note-based triggers
    // Low notes (C2-C3) for mode switching
    if (note >= 36 && note <= 48) {
        int mode = note - 36;
        controls.playback.activeMode = mode;
    }
    // High notes for triggering effects
    else if (note >= 60) {
        switch (note) {
            case 62: controls.post.enablePostBloom = !controls.post.enablePostBloom; break;
            case 64: controls.post.enablePostGlitch = !controls.post.enablePostGlitch; break;
            case 65: controls.post.enablePostBend = !controls.post.enablePostBend; break;
            case 67: controls.temporal.enableFeedback = !controls.temporal.enableFeedback; break;
            default:
                break;
        }
    }
}
