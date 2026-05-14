#include "OscSystem.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

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
        if (eventCallback) {
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
}

void OscSystem::removeMapping(const std::string& address) {
    mappings.erase(address);
}

void OscSystem::clearMappings() {
    mappings.clear();
}

const std::map<std::string, OscMapping>& OscSystem::getMappings() const {
    return mappings;
}

void OscSystem::addTriggerMapping(const std::string& address, const std::string& actionName) {
    OscTriggerMapping mapping;
    mapping.address = address;
    mapping.actionName = actionName;
    triggerMappings[address] = mapping;
}

void OscSystem::removeTriggerMapping(const std::string& address) {
    triggerMappings.erase(address);
}

void OscSystem::clearTriggerMappings() {
    triggerMappings.clear();
}

const std::map<std::string, OscTriggerMapping>& OscSystem::getTriggerMappings() const {
    return triggerMappings;
}

void OscSystem::applyToVisualControls(const OscMessage& msg, VisualControls& controls) {
    if (msg.type == OscMessageType::FLOAT) {
        applyMapping(msg.address, msg.floatValue, controls);
    } else if (msg.type == OscMessageType::INT) {
        applyMapping(msg.address, static_cast<float>(msg.intValue), controls);
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
            // Call trigger callback directly for immediate response
            auto it = oscSystem->triggerMappings.find(oscMsg.address);
            if (it != oscSystem->triggerMappings.end()) {
                oscSystem->triggerCallback(it->second.actionName);
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

void OscSystem::applyMapping(const std::string& address, float value, VisualControls& controls) {
    auto it = mappings.find(address);
    if (it == mappings.end()) {
        return;
    }

    const OscMapping& mapping = it->second;
    float normalizedValue = value; // OSC typically sends 0.0-1.0 already

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
