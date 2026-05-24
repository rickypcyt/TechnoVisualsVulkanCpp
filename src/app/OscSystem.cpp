#include "OscSystem.h"
#include "parameters/ParameterBindingRegistry.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

namespace {

static const std::pair<const char*, const char*> DEFAULT_TRIGGER_ACTIONS[] = {
    {"/vjay/randomizeVideo", "randomizeVideo"},
    {"/vjay/randomizeVideo2", "randomizeVideo2"},
    {"/vjay/jumpRandom", "jumpRandom"},
    {"/vjay/folderChanged", "folderChanged"},
    {"/vjay/applyChanges", "applyChanges"}
};

} // namespace

OscSystem::OscSystem()
    : serverThread(nullptr)
    , port(9000)
    , initialized(false)
    , enabled(true)
    , learnMode(false)
    , hasLearned(false)
{
}

OscSystem::~OscSystem() {
    shutdown();
}

bool OscSystem::initialize(int port) {
    this->port = port;
    try {
        serverThread = lo_server_thread_new(std::to_string(port).c_str(), nullptr);
        if (!serverThread) {
            std::cerr << "[OscSystem] Failed to create server thread" << std::endl;
            return false;
        }
        
        // Add method handler for all paths
        lo_server_thread_add_method(serverThread, nullptr, nullptr, oscHandler, this);
        
        initialized = true;
        std::cout << "[OscSystem] Initialized on port " << port << std::endl;
        return true;
    } catch (std::exception& e) {
        std::cerr << "[OscSystem] Failed to initialize: " << e.what() << std::endl;
        initialized = false;
        return false;
    }
}

void OscSystem::shutdown() {
    stop();
    if (serverThread) {
        lo_server_thread_free(serverThread);
        serverThread = nullptr;
    }
    initialized = false;
}

bool OscSystem::start() {
    if (!initialized || !serverThread) {
        std::cerr << "[OscSystem] Not initialized" << std::endl;
        return false;
    }

    try {
        lo_server_thread_start(serverThread);
        std::cout << "[OscSystem] Started listening on port " << port << std::endl;
        return true;
    } catch (std::exception& e) {
        std::cerr << "[OscSystem] Failed to start: " << e.what() << std::endl;
        return false;
    }
}

void OscSystem::stop() {
    if (serverThread) {
        lo_server_thread_stop(serverThread);
        std::cout << "[OscSystem] Stopped listening" << std::endl;
    }
}

int OscSystem::getPort() const {
    return port;
}

void OscSystem::setPort(int port) {
    if (initialized) {
        std::cerr << "[OscSystem] Cannot change port while initialized" << std::endl;
        return;
    }
    this->port = port;
}

std::string OscSystem::getLocalIPAddress() {
    struct ifaddrs *ifaddr, *ifa;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        return "127.0.0.1";
    }

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) {
            continue;
        }

        // Skip loopback and non-AF_INET interfaces
        if (ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        // Skip loopback interface
        if (strcmp(ifa->ifa_name, "lo") == 0) {
            continue;
        }

        // Prefer wlan0 or eth0
        if (strcmp(ifa->ifa_name, "wlan0") == 0 || strcmp(ifa->ifa_name, "eth0") == 0) {
            int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                               host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
            if (s != 0) {
                continue;
            }
            freeifaddrs(ifaddr);
            return std::string(host);
        }
    }

    // Fallback: return first non-loopback interface
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) {
            continue;
        }

        if (ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        if (strcmp(ifa->ifa_name, "lo") == 0) {
            continue;
        }

        int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                           host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
        if (s != 0) {
            continue;
        }
        freeifaddrs(ifaddr);
        return std::string(host);
    }

    freeifaddrs(ifaddr);
    return "127.0.0.1";
}

void OscSystem::update() {
    if (!enabled || !initialized) {
        return;
    }

    // Process queued messages (thread-safe)
    std::lock_guard<std::mutex> lock(queueMutex);
    for (const auto& msg : messageQueue) {
        if (msg.type == OscMessageType::TRIGGER && triggerCallback) {
            auto it = triggerMappings.find(msg.address);
            if (it != triggerMappings.end()) {
                triggerCallback(it->second.actionName);
            }
        } else if (eventCallback) {
            eventCallback(msg);
        }
    }
    messageQueue.clear();
}

void OscSystem::setEventCallback(std::function<void(const OscMessage&)> callback) {
    eventCallback = callback;
}

void OscSystem::setTriggerCallback(std::function<void(const std::string&)> callback) {
    triggerCallback = callback;
}

void OscSystem::addMapping(const std::string& address, const std::string& parameterName, float minVal, float maxVal, bool invert) {
    OscMapping mapping;
    mapping.address = address;
    mapping.parameterName = parameterName;
    mapping.minValue = minVal;
    mapping.maxValue = maxVal;
    mapping.invert = invert;
    mappings[address] = mapping;
    if (onMappingsChanged) onMappingsChanged();
}

void OscSystem::removeMapping(const std::string& address) {
    mappings.erase(address);
    if (onMappingsChanged) onMappingsChanged();
}

void OscSystem::clearMappings() {
    mappings.clear();
    if (onMappingsChanged) onMappingsChanged();
}

const std::map<std::string, OscMapping>& OscSystem::getMappings() const {
    return mappings;
}

void OscSystem::addTriggerMapping(const std::string& address, const std::string& actionName) {
    OscTriggerMapping mapping;
    mapping.address = address;
    mapping.actionName = actionName;
    triggerMappings[address] = mapping;
    if (onMappingsChanged) onMappingsChanged();
}

void OscSystem::removeTriggerMapping(const std::string& address) {
    triggerMappings.erase(address);
    if (onMappingsChanged) onMappingsChanged();
}

void OscSystem::clearTriggerMappings() {
    triggerMappings.clear();
    if (onMappingsChanged) onMappingsChanged();
}

const std::map<std::string, OscTriggerMapping>& OscSystem::getTriggerMappings() const {
    return triggerMappings;
}

void OscSystem::ensureDefaultTriggers() {
    for (const auto& [address, action] : DEFAULT_TRIGGER_ACTIONS) {
        if (triggerMappings.find(address) == triggerMappings.end()) {
            OscTriggerMapping mapping;
            mapping.address = address;
            mapping.actionName = action;
            triggerMappings[address] = mapping;
            if (onMappingsChanged) onMappingsChanged();
        }
    }
}

void OscSystem::applyToVisualControls(const OscMessage& msg, VisualControls& controls) {
    if (msg.type == OscMessageType::FLOAT) {
        applyMapping(msg, controls);
    } else if (msg.type == OscMessageType::INT) {
        OscMessage converted = msg;
        converted.type = OscMessageType::FLOAT;
        converted.floatValue = static_cast<float>(msg.intValue);
        converted.floatValues.clear();
        applyMapping(converted, controls);
    }
}

void OscSystem::setEnabled(bool enabled) {
    this->enabled = enabled;
}

bool OscSystem::isEnabled() const {
    return enabled;
}

void OscSystem::setLearnMode(bool enabled) {
    learnMode = enabled;
    if (enabled) {
        hasLearned = false;
    }
}

bool OscSystem::isLearnMode() const {
    return learnMode;
}

OscMessage OscSystem::getLastLearnedMessage() const {
    return lastLearnedMessage;
}

bool OscSystem::hasLearnedMessage() const {
    return hasLearned;
}

void OscSystem::clearLearnedMessage() {
    hasLearned = false;
}

int OscSystem::oscHandler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data) {
    OscSystem* oscSystem = static_cast<OscSystem*>(user_data);
    if (!oscSystem || !oscSystem->enabled) {
        return 1;
    }

    try {
        OscMessage oscMsg = oscSystem->parseMessage(path, types, argv, argc);

        // Check if this is a trigger (no arguments)
        if (oscMsg.type == OscMessageType::TRIGGER && oscSystem->triggerCallback) {
            // Queue trigger for processing in main thread (thread-safe)
            auto it = oscSystem->triggerMappings.find(oscMsg.address);
            if (it != oscSystem->triggerMappings.end()) {
                std::lock_guard<std::mutex> lock(oscSystem->queueMutex);
                oscSystem->messageQueue.push_back(oscMsg);
            }
            return 1;
        }

        // Learn mode: capture the first message
        if (oscSystem->learnMode && !oscSystem->hasLearned) {
            oscSystem->lastLearnedMessage = oscMsg;
            oscSystem->hasLearned = true;
            oscSystem->learnMode = false;
            std::cout << "[OscSystem] Learned OSC address: " << oscMsg.address;
            if (oscMsg.type == OscMessageType::FLOAT) {
                std::cout << " value: " << oscMsg.floatValue << std::endl;
            } else if (oscMsg.type == OscMessageType::INT) {
                std::cout << " value: " << oscMsg.intValue << std::endl;
            } else if (oscMsg.type == OscMessageType::TRIGGER) {
                std::cout << " (trigger)" << std::endl;
            }
            return 1;
        }

        // Queue message for processing in main thread (thread-safe)
        std::lock_guard<std::mutex> lock(oscSystem->queueMutex);
        oscSystem->messageQueue.push_back(oscMsg);
    } catch (std::exception& e) {
        std::cerr << "[OscSystem] Error parsing message: " << e.what() << std::endl;
    }

    return 1;
}

OscMessage OscSystem::parseMessage(const char* path, const char* types, lo_arg** argv, int argc) {
    OscMessage msg;
    msg.address = path;

    // No arguments = trigger
    if (argc == 0) {
        msg.type = OscMessageType::TRIGGER;
        return msg;
    }

    // Support up to 3 float values for RGB payloads
    if (types[0] == 'f' && argc >= 3) {
        msg.type = OscMessageType::FLOAT;
        msg.floatValues.clear();
        for (int i = 0; i < argc; ++i) {
            if (types[i] == 'f') {
                msg.floatValues.push_back(argv[i]->f);
            }
        }
        if (!msg.floatValues.empty()) {
            msg.floatValue = msg.floatValues[0];
        }
        return msg;
    }

    switch (types[0]) {
        case 'f':
            msg.type = OscMessageType::FLOAT;
            msg.floatValue = argv[0]->f;
            break;
        case 'i':
            msg.type = OscMessageType::INT;
            msg.intValue = argv[0]->i;
            break;
        case 's':
            msg.type = OscMessageType::STRING;
            msg.stringValue = &argv[0]->s;
            break;
        case 'T':
            msg.type = OscMessageType::BOOL;
            msg.boolValue = true;
            break;
        case 'F':
            msg.type = OscMessageType::BOOL;
            msg.boolValue = false;
            break;
        default:
            msg.type = OscMessageType::FLOAT;
            msg.floatValue = 0.0f;
            break;
    }

    return msg;
}

void OscSystem::applyMapping(const OscMessage& message, VisualControls& controls) {
    const std::string& address = message.address;
    auto it = mappings.find(address);
    float normalizedValue = message.floatValue;
    std::string paramName;
    float minVal = 0.0f;
    float maxVal = 1.0f;
    bool invert = false;

    if (it != mappings.end()) {
        const OscMapping& mapping = it->second;
        paramName = mapping.parameterName;
        minVal = mapping.minValue;
        maxVal = mapping.maxValue;
        invert = mapping.invert;
    } else {
        const std::string prefix = "/vjay/";
        if (address.rfind(prefix, 0) == 0) {
            paramName = address.substr(prefix.length());
            const auto& registry = ParameterBindingRegistry::get();
            if (registry.find(paramName) == registry.end()) {
                return;
            }
        } else {
            return;
        }
    }

    if (invert) {
        normalizedValue = 1.0f - normalizedValue;
    }

    float mappedValue = minVal + normalizedValue * (maxVal - minVal);

    // OSC-specific: RGB payload handling (before registry dispatch)
    if (message.floatValues.size() >= 3 && paramName == "rgbOverlay") {
        controls.color.rgbOverlay = glm::vec3(
            message.floatValues[0],
            message.floatValues[1],
            message.floatValues[2]
        );
        return;
    }

    // OSC-specific: Individual RGB channels
    if (paramName == "rgbOverlayR" || paramName == "rgbOverlayG" || paramName == "rgbOverlayB") {
        if (paramName == "rgbOverlayR") controls.color.rgbOverlay.r = mappedValue;
        else if (paramName == "rgbOverlayG") controls.color.rgbOverlay.g = mappedValue;
        else if (paramName == "rgbOverlayB") controls.color.rgbOverlay.b = mappedValue;
        return;
    }

    // Registry dispatch for scalar parameters
    const auto& registry = ParameterBindingRegistry::get();
    auto bindingIt = registry.find(paramName);

    if (bindingIt == registry.end()) {
        return;
    }

    const ParamBinding& binding = bindingIt->second;

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
            break;
    }
}
