#pragma once

#include <RtMidi.h>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include "VisualControls.h"

// MIDI event types
enum class MidiEventType {
    NOTE_ON,
    NOTE_OFF,
    CONTROL_CHANGE,
    PITCH_BEND,
    PROGRAM_CHANGE
};

// MIDI message structure
struct MidiMessage {
    MidiEventType type;
    int channel;
    int note;        // For note events
    int velocity;    // For note events
    int controller;  // For CC events
    int value;       // For CC events (0-127)
    double pitch;    // For pitch bend (normalized -1.0 to 1.0)
};

// MIDI mapping: maps MIDI CC numbers to VisualControls parameters
struct MidiMapping {
    int ccNumber;
    std::string parameterName;
    float minValue;
    float maxValue;
    bool invert;
};

struct MidiTriggerMapping {
    int note;
    std::string actionName;
};

class MidiSystem {
public:
    MidiSystem();
    ~MidiSystem();

    // Initialize MIDI system
    bool initialize();
    void shutdown();

    // Open a specific MIDI input port
    bool openPort(unsigned int portIndex);
    void closePort();

    // Get available MIDI ports
    std::vector<std::string> getAvailablePorts() const;
    unsigned int getPortCount() const;

    // Process incoming MIDI messages (call this in main loop)
    void update();

    // Set callback for MIDI events
    void setEventCallback(std::function<void(const MidiMessage&)> callback);

    // MIDI mapping functions
    void addMapping(int ccNumber, const std::string& parameterName, float minVal, float maxVal, bool invert = false);
    void removeMapping(int ccNumber);
    void clearMappings();
    const std::map<int, MidiMapping>& getMappings() const;

    // MIDI trigger mappings (note buttons)
    void addTriggerMapping(int note, const std::string& actionName);
    void removeTriggerMapping(int note);
    void clearTriggerMappings();
    const std::map<int, MidiTriggerMapping>& getTriggerMappings() const;

    void setTriggerCallback(std::function<void(const std::string&)> callback);

    // Apply MIDI message to VisualControls
    void applyToVisualControls(const MidiMessage& msg, VisualControls& controls);

    // Enable/disable MIDI processing
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Learn mode for MIDI mapping
    void setLearnMode(bool enabled);
    bool isLearnMode() const;
    MidiMessage getLastLearnedMessage() const;
    bool hasLearnedMessage() const;
    void clearLearnedMessage();

private:
    RtMidiIn* midiIn;
    bool initialized;
    bool enabled;
    bool learnMode;
    bool hasLearned;
    MidiMessage lastLearnedMessage;
    std::function<void(const MidiMessage&)> eventCallback;
    std::function<void(const std::string&)> triggerCallback;
    std::map<int, MidiMapping> mappings;
    std::map<int, MidiTriggerMapping> triggerMappings;

    // Helper functions
    MidiMessage parseMessage(std::vector<unsigned char>* message);
    void applyCCMapping(int ccNumber, int value, VisualControls& controls);
    void applyNoteMapping(int note, int velocity, VisualControls& controls);
};
