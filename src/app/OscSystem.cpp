#include "OscSystem.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

namespace {

struct ParameterRange {
    const char* name;
    float minVal;
    float maxVal;
};

// Default ranges for /vjay/<paramName> auto-mapping
static const ParameterRange DEFAULT_PARAMETER_RANGES[] = {
    // Procedural
    {"animationSpeed", 0.0f, 3.0f},
    {"tempo", 0.0f, 2.0f},
    {"energy", 0.0f, 1.0f},
    {"bass", 0.0f, 1.0f},
    {"mid", 0.0f, 1.0f},
    {"high", 0.0f, 1.0f},
    {"colorBlend", 0.0f, 1.0f},
    {"rgbOverlay", 0.0f, 2.0f},
    {"uvWarpStrength", 0.0f, 1.0f},
    {"rippleStrength", 0.0f, 1.0f},
    {"rippleFrequency", 0.0f, 5.0f},
    {"swirlStrength", 0.0f, 1.0f},
    {"displacementAmount", 0.0f, 1.0f},
    {"kaleidoSegments", 1.0f, 12.0f},
    {"tunnelDepth", 0.0f, 1.0f},
    {"tunnelCurvature", 0.0f, 1.0f},
    // Post FX
    {"bloomIntensity", 0.0f, 1.0f},
    {"bloomThreshold", 0.0f, 1.0f},
    {"aberrationAmount", 0.0f, 0.1f},
    {"grainStrength", 0.0f, 1.0f},
    {"crtCurvature", 0.0f, 0.5f},
    {"crtScanlineIntensity", 0.0f, 1.0f},
    {"crtMaskIntensity", 0.0f, 1.0f},
    {"crtVignette", 0.0f, 1.0f},
    {"crtFishEye", 0.0f, 0.5f},
    {"gaussianBlur", 0.0f, 1.0f},
    {"directionalBlur", 0.0f, 1.0f},
    {"directionalBlurAngle", 0.0f, 360.0f},
    {"zoomBlur", 0.0f, 1.0f},
    {"motionBlur", 0.0f, 1.0f},
    {"temporalBlur", 0.0f, 1.0f},
    {"unsharpMask", 0.0f, 1.0f},
    {"casAmount", 0.0f, 1.0f},
    {"localContrast", 0.0f, 1.0f},
    {"pixelateAmount", 0.0f, 1.0f},
    {"strobeSpeed", 0.0f, 10.0f},
    {"thresholdLevel", 0.0f, 1.0f},
    {"slowZoomAmount", 0.0f, 1.0f},
    {"edgeStrength", 0.0f, 2.0f},
    {"edgeThreshold", 0.0f, 1.0f},
    {"edgeBlend", 0.0f, 1.0f},
    {"fxaaQualitySubpix", 0.0f, 1.0f},
    {"fxaaQualityEdgeThreshold", 0.0f, 0.5f},
    {"fxaaQualityEdgeThresholdMin", 0.0f, 0.2f},
    // VJay Basics
    {"videoPlaybackRate", 0.1f, 3.0f},
    {"videoDecodeOversample", 1.0f, 4.0f},
    {"videoMix", 0.0f, 1.0f},
    {"grayscaleAmount", 0.0f, 1.0f},
    {"sharpenAmount", 0.0f, 1.0f},
    {"gradeBrightness", -1.0f, 1.0f},
    {"gradeContrast", 0.0f, 2.0f},
    {"gradeSaturation", 0.0f, 2.0f},
    {"gradeHueShift", 0.0f, 360.0f},
    {"gradeGamma", 0.1f, 3.0f},
    {"colorLUTIndex", 0.0f, 10.0f},
    {"splitToneBalance", 0.0f, 1.0f},
    {"blendProceduralMix", 0.0f, 1.0f},
    {"blendVideoMix", 0.0f, 1.0f},
    {"blendFeedbackMix", 0.0f, 1.0f},
    // VJay Extra
    {"feedbackAmount", 0.0f, 1.0f},
    {"trailStrength", 0.0f, 1.0f},
    {"temporalAccumulation", 0.0f, 1.0f},
    {"feedbackDecay", 0.0f, 1.0f},
    {"recursiveBlend", 0.0f, 1.0f},
    {"glitchAmount", 0.0f, 1.0f},
    {"glitchDatamosh", 0.0f, 1.0f},
    {"glitchRGBSplit", 0.0f, 1.0f},
    {"glitchScanlineBreak", 0.0f, 1.0f},
    {"glitchJitter", 0.0f, 1.0f},
    {"glitchTearing", 0.0f, 1.0f},
    {"glitchPixelSort", 0.0f, 1.0f},
    {"glitchBufferCorruption", 0.0f, 1.0f},
    {"analogScanlineFocus", 0.0f, 1.0f},
    {"analogMaskBalance", 0.0f, 1.0f},
    {"analogNoise", 0.0f, 1.0f},
    {"analogBloom", 0.0f, 1.0f},
    {"vhsDistortion", 0.0f, 1.0f},
    {"analogChromaticAberration", 0.0f, 0.1f},
    {"mirrorAmount", 0.0f, 1.0f},
    {"posterizeLevels", 2.0f, 16.0f},
    {"zoomPulseAmount", 0.0f, 1.0f},
    {"rgbShiftAmount", 0.0f, 0.1f},
    {"audioWarpResponse", 0.0f, 1.0f},
    {"audioFeedbackResponse", 0.0f, 1.0f},
    {"audioBlurResponse", 0.0f, 1.0f},
    {"audioColorResponse", 0.0f, 1.0f},
    {"audioGlitchResponse", 0.0f, 1.0f},
    {"audioBeatSync", 0.0f, 2.0f},
    {"audioLfoRate", 0.0f, 2.0f},
    {"temporalInterpolation", 0.0f, 1.0f},
    {"temporalBlendStrength", 0.0f, 1.0f},
    {"slowMotionFactor", 0.1f, 2.0f},
    {"frameAccumulation", 0.0f, 1.0f},
    {"cameraZoom", 0.5f, 2.0f},
    {"rgbOverlayR", 0.0f, 2.0f},
    {"rgbOverlayG", 0.0f, 2.0f},
    {"rgbOverlayB", 0.0f, 2.0f},
    // Grid / Mirroring
    {"enableGrid", 0.0f, 1.0f},
    {"gridMode", 0.0f, 2.0f},
    {"gridCount", 1.0f, 8.0f},
    {"gridRows", 1.0f, 8.0f},
    {"gridColumns", 1.0f, 8.0f},
    {"gridMirrorCells", 0.0f, 1.0f}
};

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
    float normalizedValue = message.floatValue; // OSC typically sends 0.0-1.0 already
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
        // Auto-detect /vjay/<parameterName> addresses
        const std::string prefix = "/vjay/";
        if (address.rfind(prefix, 0) == 0) {
            paramName = address.substr(prefix.length());
            bool found = false;
            for (const auto& range : DEFAULT_PARAMETER_RANGES) {
                if (paramName == range.name) {
                    minVal = range.minVal;
                    maxVal = range.maxVal;
                    found = true;
                    break;
                }
            }
            if (!found) return;
        } else {
            return;
        }
    }

    if (invert) {
        normalizedValue = 1.0f - normalizedValue;
    }

    float mappedValue = minVal + normalizedValue * (maxVal - minVal);
    // RGB payload handling: allow /vjay/rgbOverlay to send 3 floats at once
    if (message.floatValues.size() >= 3 && paramName == "rgbOverlay") {
        float r = glm::clamp(message.floatValues[0], minVal, maxVal);
        float g = glm::clamp(message.floatValues[1], minVal, maxVal);
        float b = glm::clamp(message.floatValues[2], minVal, maxVal);
        controls.rgbOverlay = glm::vec3(r, g, b);
        return;
    }

    // Map to VisualControls parameters - all parameters from the wizard
    const std::string& name = paramName;

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
    else if (name == "enableThreshold") controls.enableThreshold = (mappedValue > 0.5f);
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
    else if (name == "video2BlendMode") {
        int mode = static_cast<int>(mappedValue + 0.5f);
        mode = std::max(0, std::min(4, mode));
        controls.video2BlendMode = mode;
    }
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
    else if (name == "cameraZoom") controls.cameraZoom = mappedValue;
    else if (name == "rgbOverlayR") controls.rgbOverlay.r = mappedValue;
    else if (name == "rgbOverlayG") controls.rgbOverlay.g = mappedValue;
    else if (name == "rgbOverlayB") controls.rgbOverlay.b = mappedValue;
    else if (name == "enableRgbOverlay") controls.enableRgbOverlay = (mappedValue > 0.5f);
    // Grid / Mirroring
    else if (name == "enableGrid") controls.enableGrid = (mappedValue > 0.5f);
    else if (name == "gridMode") {
        int mode = static_cast<int>(mappedValue + 0.5f);
        controls.gridMode = std::clamp(mode, 0, 2);
    }
    else if (name == "gridCount") {
        int count = static_cast<int>(mappedValue + 0.5f);
        controls.gridCount = std::clamp(count, 1, 8);
    }
    else if (name == "gridRows") {
        int rows = static_cast<int>(mappedValue + 0.5f);
        controls.gridRows = std::clamp(rows, 1, 8);
    }
    else if (name == "gridColumns") {
        int cols = static_cast<int>(mappedValue + 0.5f);
        controls.gridColumns = std::clamp(cols, 1, 8);
    }
    else if (name == "gridMirrorCells") controls.gridMirrorCells = (mappedValue > 0.5f);
}
