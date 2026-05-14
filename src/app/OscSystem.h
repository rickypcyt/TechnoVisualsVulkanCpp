#pragma once

#include <lo/lo.h>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include "VisualControls.h"

// OSC message types
enum class OscMessageType {
    FLOAT,
    INT,
    STRING,
    BOOL,
    BUNDLE,
    TRIGGER  // No arguments, just a trigger
};

// OSC message structure
struct OscMessage {
    std::string address;
    OscMessageType type;
    float floatValue;
    int intValue;
    std::string stringValue;
    bool boolValue;
};

// OSC mapping: maps OSC addresses to VisualControls parameters
struct OscMapping {
    std::string address;
    std::string parameterName;
    float minValue;
    float maxValue;
    bool invert;
};

// OSC trigger mapping: maps OSC addresses to callbacks (buttons)
struct OscTriggerMapping {
    std::string address;
    std::string actionName;  // "randomizeVideo", "jumpRandom", etc.
};

class OscSystem {
public:
    OscSystem();
    ~OscSystem();

    // Initialize OSC system
    bool initialize(int port = 9000);
    void shutdown();

    // Start/stop OSC listener
    bool start();
    void stop();

    // Get current port
    int getPort() const;
    void setPort(int port);

    // Get local IP address
    static std::string getLocalIPAddress();

    // Process incoming OSC messages (call this in main loop)
    void update();

    // Set callback for OSC events
    void setEventCallback(std::function<void(const OscMessage&)> callback);

    // Set callback for OSC triggers (button presses)
    void setTriggerCallback(std::function<void(const std::string&)> callback);

    // OSC mapping functions
    void addMapping(const std::string& address, const std::string& parameterName, float minVal, float maxVal, bool invert = false);
    void removeMapping(const std::string& address);
    void clearMappings();
    const std::map<std::string, OscMapping>& getMappings() const;

    // OSC trigger mapping functions (for buttons)
    void addTriggerMapping(const std::string& address, const std::string& actionName);
    void removeTriggerMapping(const std::string& address);
    void clearTriggerMappings();
    const std::map<std::string, OscTriggerMapping>& getTriggerMappings() const;

    // Apply OSC message to VisualControls
    void applyToVisualControls(const OscMessage& msg, VisualControls& controls);

    // Enable/disable OSC processing
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Learn mode for OSC mapping
    void setLearnMode(bool enabled);
    bool isLearnMode() const;
    OscMessage getLastLearnedMessage() const;
    bool hasLearnedMessage() const;
    void clearLearnedMessage();

    // liblo callback handler
    static int oscHandler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);

private:
    lo_server_thread serverThread;
    int port;
    bool initialized;
    bool enabled;
    bool learnMode;
    bool hasLearned;
    OscMessage lastLearnedMessage;
    std::function<void(const OscMessage&)> eventCallback;
    std::function<void(const std::string&)> triggerCallback;
    std::map<std::string, OscMapping> mappings;
    std::map<std::string, OscTriggerMapping> triggerMappings;
    std::vector<OscMessage> messageQueue;
    std::mutex queueMutex;

    // Helper functions
    OscMessage parseMessage(const char* path, const char* types, lo_arg** argv, int argc);
    void applyMapping(const std::string& address, float value, VisualControls& controls);
};
