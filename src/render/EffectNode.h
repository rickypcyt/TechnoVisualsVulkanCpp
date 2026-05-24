#pragma once
#include <string>
#include <map>
#include <vector>
#include <variant>
#include <memory>
#include <cmath>
#include <algorithm>

// Unified EffectSpec - representación única para Vulkan + FFmpeg
// Esto es lo que convierte tu proyecto en un NLE serio

enum class EffectType {
    SCALE,
    SPEED,
    FPS,
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
                if (!std::holds_alternative<float>(it->second)) {
                    return default_value;
                }
                return std::get<float>(it->second);
            } else if constexpr (std::is_same_v<T, int>) {
                if (!std::holds_alternative<int>(it->second)) {
                    return default_value;
                }
                return std::get<int>(it->second);
            } else if constexpr (std::is_same_v<T, bool>) {
                if (!std::holds_alternative<bool>(it->second)) {
                    return default_value;
                }
                return std::get<bool>(it->second);
            } else if constexpr (std::is_same_v<T, std::string>) {
                if (!std::holds_alternative<std::string>(it->second)) {
                    return default_value;
                }
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
    // Temporal effects
    bool flip_horizontal = false;
    bool flip_vertical = false;
    int rotation_degrees = 0;
    int crop_x = 0;
    int crop_y = 0;
    int crop_width = 0;
    int crop_height = 0;
};

class EffectCompiler {
public:
    // Compile to FFmpeg filtergraph
    static std::string compile_to_ffmpeg(const EffectGraph& graph) {
        // Collect all enabled filters with their priority for correct ordering
        struct FilterEntry {
            std::string filter;
            int priority;  // Lower = earlier in pipeline
        };
        std::vector<FilterEntry> filters;
        
        // Aggregate color correction params (brightness, contrast, saturation, hue, gamma)
        float brightness = 0.0f, contrast = 1.0f, saturation = 1.0f, hue = 0.0f, gamma = 1.0f;
        bool has_color_correction = false;
        constexpr float EPSILON = 1e-6f;
        
        for (const auto& node : graph.get_nodes()) {
            if (!node->enabled) continue;
            
            // Collect color correction params
            switch (node->type) {
                case EffectType::BRIGHTNESS:
                    brightness = node->get_param<float>("value", 0.0f);
                    has_color_correction = true;
                    continue;
                case EffectType::CONTRAST:
                    contrast = node->get_param<float>("value", 1.0f);
                    has_color_correction = true;
                    continue;
                case EffectType::SATURATION:
                    saturation = node->get_param<float>("value", 1.0f);
                    has_color_correction = true;
                    continue;
                case EffectType::HUE_SHIFT:
                    hue = node->get_param<float>("degrees", 0.0f);
                    has_color_correction = true;
                    continue;
                case EffectType::GAMMA:
                    gamma = node->get_param<float>("value", 1.0f);
                    has_color_correction = true;
                    continue;
                default:
                    break;
            }
            
            std::string node_filter = compile_node_to_ffmpeg(node);
            if (!node_filter.empty()) {
                int priority = 100;
                // Determine filter priority for correct ordering
                switch (node->type) {
                    case EffectType::SPEED:
                        priority = 0;  // setpts must come first
                        break;
                    case EffectType::FPS:
                        priority = 1;  // minterpolate after setpts
                        break;
                    case EffectType::GRAYSCALE:
                        priority = 3;  // format=gray last
                        break;
                    case EffectType::SCALE:
                        priority = 4;
                        break;
                    case EffectType::FLIP:
                    case EffectType::ROTATE:
                    case EffectType::CROP:
                        priority = 5;
                        break;
                    default:
                        priority = 50;
                        break;
                }
                filters.push_back({node_filter, priority});
            }
        }
        
        // Add aggregated color correction filter
        if (has_color_correction) {
            std::string eq_filter = "eq=";
            bool first_param = true;
            if (std::abs(brightness - 0.0f) > EPSILON) {
                eq_filter += "brightness=" + std::to_string(brightness);
                first_param = false;
            }
            if (std::abs(contrast - 1.0f) > EPSILON) {
                if (!first_param) eq_filter += ":";
                eq_filter += "contrast=" + std::to_string(contrast);
                first_param = false;
            }
            if (std::abs(saturation - 1.0f) > EPSILON) {
                if (!first_param) eq_filter += ":";
                eq_filter += "saturation=" + std::to_string(saturation);
                first_param = false;
            }
            if (std::abs(hue - 0.0f) > EPSILON) {
                if (!first_param) eq_filter += ":";
                eq_filter += "hue=" + std::to_string(hue);
                first_param = false;
            }
            if (std::abs(gamma - 1.0f) > EPSILON) {
                if (!first_param) eq_filter += ":";
                eq_filter += "gamma=" + std::to_string(gamma);
            }
            filters.push_back({eq_filter, 2});
        }
        
        // Sort by priority
        std::sort(filters.begin(), filters.end(), 
            [](const FilterEntry& a, const FilterEntry& b) {
                return a.priority < b.priority;
            });
        
        // Build filter string
        std::string result;
        bool first = true;
        for (const auto& entry : filters) {
            if (!first) result += ",";
            result += entry.filter;
            first = false;
        }
        
        return result;
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
                case EffectType::FLIP:
                    params.flip_horizontal = node->get_param<bool>("horizontal", false);
                    params.flip_vertical = node->get_param<bool>("vertical", false);
                    break;
                case EffectType::ROTATE:
                    params.rotation_degrees = node->get_param<int>("degrees", 0);
                    break;
                case EffectType::CROP:
                    params.crop_x = node->get_param<int>("x", 0);
                    params.crop_y = node->get_param<int>("y", 0);
                    params.crop_width = node->get_param<int>("width", 0);
                    params.crop_height = node->get_param<int>("height", 0);
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
                constexpr float EPSILON = 1e-6f;
                if (std::abs(factor - 1.0f) > EPSILON) {
                    // Canonical form: setpts=PTS/factor (e.g., 0.5x speed = PTS/0.5 = 2.0*PTS)
                    return "setpts=PTS/" + std::to_string(factor);
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
            case EffectType::SATURATION:
            case EffectType::HUE_SHIFT:
            case EffectType::GAMMA: {
                // Aggregate all color correction params from individual nodes
                // This requires a two-pass approach: first collect all color nodes, then merge
                // For now, return empty and handle aggregation at graph level
                break;
            }
            case EffectType::FLIP: {
                bool h = node->get_param<bool>("horizontal", false);
                bool v = node->get_param<bool>("vertical", false);
                if (h && v) {
                    return "hflip,vflip";
                } else if (h) {
                    return "hflip";
                } else if (v) {
                    return "vflip";
                }
                break;
            }
            case EffectType::ROTATE: {
                int degrees = node->get_param<int>("degrees", 0);
                if (degrees == 90) {
                    return "transpose=clock";
                } else if (degrees == 180) {
                    return "transpose=clock,transpose=clock";
                } else if (degrees == 270) {
                    return "transpose=cclock";
                }
                break;
            }
            case EffectType::CROP: {
                int x = node->get_param<int>("x", 0);
                int y = node->get_param<int>("y", 0);
                int w = node->get_param<int>("width", 0);
                int h = node->get_param<int>("height", 0);
                if (w > 0 && h > 0) {
                    return "crop=" + std::to_string(w) + ":" + std::to_string(h) + 
                           ":" + std::to_string(x) + ":" + std::to_string(y);
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
    
    // Create combined color grade (creates 3 separate nodes)
    static std::vector<std::shared_ptr<EffectNode>> create_color_grade(float brightness, float contrast, float saturation) {
        std::vector<std::shared_ptr<EffectNode>> nodes;
        if (brightness != 0.0f) {
            nodes.push_back(create_brightness(brightness));
        }
        if (contrast != 1.0f) {
            nodes.push_back(create_contrast(contrast));
        }
        if (saturation != 1.0f) {
            nodes.push_back(create_saturation(saturation));
        }
        return nodes;
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
    
    static std::shared_ptr<EffectNode> create_hue_shift(float degrees) {
        auto node = std::make_shared<EffectNode>(EffectType::HUE_SHIFT);
        node->set_param("degrees", degrees);
        return node;
    }
    
    static std::shared_ptr<EffectNode> create_gamma(float value) {
        auto node = std::make_shared<EffectNode>(EffectType::GAMMA);
        node->set_param("value", value);
        return node;
    }
    
    static std::shared_ptr<EffectNode> create_flip(bool horizontal = false, bool vertical = false) {
        auto node = std::make_shared<EffectNode>(EffectType::FLIP);
        node->set_param("horizontal", horizontal);
        node->set_param("vertical", vertical);
        return node;
    }
    
    static std::shared_ptr<EffectNode> create_rotate(int degrees) {
        auto node = std::make_shared<EffectNode>(EffectType::ROTATE);
        node->set_param("degrees", degrees);
        return node;
    }
    
    static std::shared_ptr<EffectNode> create_crop(int x, int y, int width, int height) {
        auto node = std::make_shared<EffectNode>(EffectType::CROP);
        node->set_param("x", x);
        node->set_param("y", y);
        node->set_param("width", width);
        node->set_param("height", height);
        return node;
    }
};
