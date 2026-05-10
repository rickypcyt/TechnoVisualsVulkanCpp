#pragma once
#include "EffectChain.h"
#include <string>
#include <vector>

// Export module - handles FFmpeg encode operations
class Exporter {
public:
    struct ExportConfig {
        std::string input_path;
        std::string output_path;
        EffectChain effects;
        std::string codec = "libx264";
        int crf = 18;
        std::string preset = "slow";
        int bitrate = 0;  // 0 = use CRF
    };
    
    // Build complete FFmpeg command for export
    static std::string buildFFmpegCommand(const ExportConfig& config) {
        std::string cmd = "ffmpeg -i \"" + config.input_path + "\"";
        
        // Add video filter if effects are active
        std::string filter = config.effects.buildFFmpegFilter();
        if (!filter.empty()) {
            cmd += " -vf \"" + filter + "\"";
        }
        
        // Add codec settings
        cmd += " -c:v " + config.codec;
        
        if (config.bitrate > 0) {
            cmd += " -b:v " + std::to_string(config.bitrate) + "k";
        } else {
            cmd += " -crf " + std::to_string(config.crf);
        }
        
        cmd += " -preset " + config.preset;
        cmd += " \"" + config.output_path + "\"";
        
        return cmd;
    }
    
    // Build filtergraph-only command (for testing/debugging)
    static std::string buildFilterCommand(const ExportConfig& config) {
        std::string cmd = "ffmpeg -i \"" + config.input_path + "\"";
        
        std::string filter = config.effects.buildFFmpegFilter();
        if (!filter.empty()) {
            cmd += " -vf \"" + filter + "\"";
        }
        
        cmd += " -c:v rawvideo -f null -";
        return cmd;
    }
    
    // Validate export configuration
    static bool validateConfig(const ExportConfig& config) {
        if (config.input_path.empty()) return false;
        if (config.output_path.empty()) return false;
        if (config.codec.empty()) return false;
        if (config.crf < 0 || config.crf > 51) return false;
        
        // Validate preset
        static const std::vector<std::string> valid_presets = {
            "ultrafast", "superfast", "veryfast", "faster", "fast", 
            "medium", "slow", "slower", "veryslow"
        };
        bool preset_valid = std::find(valid_presets.begin(), valid_presets.end(), 
                                     config.preset) != valid_presets.end();
        if (!preset_valid) return false;
        
        return true;
    }
};
