#pragma once
#include <string>
#include <map>
#include <vector>
#include <variant>
#include <memory>

// Unified EffectSpec - representación única para Vulkan + FFmpeg
// Esto es lo que convierte tu proyecto en un NLE serio

enum class EffectType {
    SCALE,
    SPEED,
    FPS,
    COLOR,
    POSTFX,
    GRAYSCALE,
    BRIGHTNESS,
    CONTRAST,
    SATURATION,
    HUE_SHIFT,
    GAMMA,
    CROP,
    ROTATE,
    FLIP
};

// Variant type for effect parameters
using EffectParam = std::variant<float, int, bool, std::string>;

// EffectNode - unidad básica de efecto
struct EffectNode {
    EffectType type;
    std::map<std::string, EffectParam> params;
    bool enabled = true;
    
    EffectNode(EffectType t) : type(t) {}
    
    // Helper methods for common parameters
    void set_param(const std::string& key, EffectParam value) {
        params[key] = value;
    }
    
    template<typename T>
    T get_param(const std::string& key, T default_value) const {
        auto it = params.find(key);
        if (it != params.end()) {
            if constexpr (std::is_same_v<T, float>) {
                return std::get<float>(it->second);
            } else if constexpr (std::is_same_v<T, int>) {
                return std::get<int>(it->second);
            } else if constexpr (std::is_same_v<T, bool>) {
                return std::get<bool>(it->second);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::get<std::string>(it->second);
            }
        }
        return default_value;
    }
};

// EffectGraph - grafo de efectos (timeline + effects)
class EffectGraph {
public:
    void add_node(std::shared_ptr<EffectNode> node) {
        nodes.push_back(node);
    }
    
    void remove_node(size_t index) {
        if (index < nodes.size()) {
            nodes.erase(nodes.begin() + index);
        }
    }
    
    const std::vector<std::shared_ptr<EffectNode>>& get_nodes() const {
        return nodes;
    }
    
    void clear() {
        nodes.clear();
    }
    
    size_t size() const {
        return nodes.size();
    }
    
private:
    std::vector<std::shared_ptr<EffectNode>> nodes;
};

// EffectCompiler - convierte EffectGraph a:
// 1. Vulkan shader parameters
// 2. FFmpeg filtergraph string

struct VulkanEffectParams {
    float grayscale = 0.0f;
    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float hue_shift = 0.0f;
    float gamma = 1.0f;
    float speed = 1.0f;
    int target_fps = 0;
};

class EffectCompiler {
public:
    // Compile to FFmpeg filtergraph
    static std::string compile_to_ffmpeg(const EffectGraph& graph) {
        std::string filter;
        bool first = true;
        
        for (const auto& node : graph.get_nodes()) {
            if (!node->enabled) continue;
            
            std::string node_filter = compile_node_to_ffmpeg(node);
            if (!node_filter.empty()) {
                if (!first) filter += ",";
                filter += node_filter;
                first = false;
            }
        }
        
        return filter;
    }
    
    // Compile to Vulkan shader parameters (returns struct with values)
    static VulkanEffectParams compile_to_vulkan(const EffectGraph& graph) {
        VulkanEffectParams params;
        
        for (const auto& node : graph.get_nodes()) {
            if (!node->enabled) continue;
            
            switch (node->type) {
                case EffectType::GRAYSCALE:
                    params.grayscale = node->get_param<float>("amount", 1.0f);
                    break;
                case EffectType::BRIGHTNESS:
                    params.brightness = node->get_param<float>("value", 0.0f);
                    break;
                case EffectType::CONTRAST:
                    params.contrast = node->get_param<float>("value", 1.0f);
                    break;
                case EffectType::SATURATION:
                    params.saturation = node->get_param<float>("value", 1.0f);
                    break;
                case EffectType::HUE_SHIFT:
                    params.hue_shift = node->get_param<float>("degrees", 0.0f);
                    break;
                case EffectType::GAMMA:
                    params.gamma = node->get_param<float>("value", 1.0f);
                    break;
                case EffectType::SCALE:
                    // Scale not used - always original resolution
                    break;
                case EffectType::SPEED:
                    params.speed = node->get_param<float>("factor", 1.0f);
                    break;
                case EffectType::FPS:
                    params.target_fps = node->get_param<int>("fps", 0);
                    break;
                default:
                    break;
            }
        }
        
        return params;
    }
    
private:
    static std::string compile_node_to_ffmpeg(const std::shared_ptr<EffectNode>& node) {
        switch (node->type) {
            case EffectType::SCALE: {
                int w = node->get_param<int>("width", 0);
                int h = node->get_param<int>("height", 0);
                std::string flags = node->get_param<std::string>("flags", "lanczos");
                if (w > 0 && h > 0) {
                    return "scale=" + std::to_string(w) + ":" + std::to_string(h) + 
                           ":flags=" + flags;
                }
                break;
            }
            case EffectType::SPEED: {
                float factor = node->get_param<float>("factor", 1.0f);
                if (factor != 1.0f) {
                    return "setpts=" + std::to_string(factor) + "*PTS";
                }
                break;
            }
            case EffectType::FPS: {
                int fps = node->get_param<int>("fps", 0);
                if (fps > 0) {
                    return "minterpolate=fps=" + std::to_string(fps);
                }
                break;
            }
            case EffectType::GRAYSCALE: {
                bool enabled = node->get_param<bool>("enabled", true);
                if (enabled) {
                    return "format=gray";
                }
                break;
            }
            case EffectType::BRIGHTNESS:
            case EffectType::CONTRAST:
            case EffectType::SATURATION: {
                float b = node->get_param<float>("brightness", 0.0f);
                float c = node->get_param<float>("contrast", 1.0f);
                float s = node->get_param<float>("saturation", 1.0f);
                if (b != 0.0f || c != 1.0f || s != 1.0f) {
                    return "eq=brightness=" + std::to_string(b) +
                           ":contrast=" + std::to_string(c) +
                           ":saturation=" + std::to_string(s);
                }
                break;
            }
            default:
                break;
        }
        return "";
    }
};

// Builder para crear efectos fácilmente
class EffectBuilder {
public:
    static std::shared_ptr<EffectNode> create_scale(int width, int height, const std::string& flags = "lanczos") {
        auto node = std::make_shared<EffectNode>(EffectType::SCALE);
        node->set_param("width", width);
        node->set_param("height", height);
        node->set_param("flags", flags);
        return node;
    }
    
    static std::shared_ptr<EffectNode> create_speed(float factor) {
        auto node = std::make_shared<EffectNode>(EffectType::SPEED);
        node->set_param("factor", factor);
        return node;
    }
    
    static std::shared_ptr<EffectNode> create_fps(int fps) {
        auto node = std::make_shared<EffectNode>(EffectType::FPS);
        node->set_param("fps", fps);
        return node;
    }
    
    static std::shared_ptr<EffectNode> create_grayscale(bool enabled = true) {
        auto node = std::make_shared<EffectNode>(EffectType::GRAYSCALE);
        node->set_param("enabled", enabled);
        return node;
    }
    
    static std::shared_ptr<EffectNode> create_color_grade(float brightness, float contrast, float saturation) {
        auto node = std::make_shared<EffectNode>(EffectType::COLOR);
        node->set_param("brightness", brightness);
        node->set_param("contrast", contrast);
        node->set_param("saturation", saturation);
        return node;
    }
    
    static std::shared_ptr<EffectNode> create_brightness(float value) {
        auto node = std::make_shared<EffectNode>(EffectType::BRIGHTNESS);
        node->set_param("value", value);
        return node;
    }
    
    static std::shared_ptr<EffectNode> create_contrast(float value) {
        auto node = std::make_shared<EffectNode>(EffectType::CONTRAST);
        node->set_param("value", value);
        return node;
    }
    
    static std::shared_ptr<EffectNode> create_saturation(float value) {
        auto node = std::make_shared<EffectNode>(EffectType::SATURATION);
        node->set_param("value", value);
        return node;
    }
};
