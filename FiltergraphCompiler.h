#pragma once
#include "ProjectState.h"
#include "EffectNode.h"
#include <string>
#include <sstream>

// FiltergraphCompiler formal - compila ProjectState a FFmpeg filtergraph
class FiltergraphCompiler {
public:
    // Compila timeline + effects a filtergraph completo
    static std::string compile(const ProjectState& state) {
        std::stringstream filtergraph;
        bool first = true;
        
        // 1. Timeline filters (si hay múltiples clips)
        std::string timeline_filter = build_timeline_filter(state.timeline);
        if (!timeline_filter.empty()) {
            filtergraph << timeline_filter;
            first = false;
        }
        
        // 2. Global effects
        std::string effects_filter = build_effects_filter(state.global_effects);
        if (!effects_filter.empty()) {
            if (!first) filtergraph << ",";
            filtergraph << effects_filter;
            first = false;
        }
        
        // 3. Render settings (output scale)
        std::string render_filter = build_render_filter(state.render);
        if (!render_filter.empty()) {
            if (!first) filtergraph << ",";
            filtergraph << render_filter;
        }
        
        return filtergraph.str();
    }
    
    // Compila solo effects (para preview)
    static std::string compile_effects_only(const EffectChain& effects) {
        return build_effects_filter(effects);
    }
    
    // Compila timeline (para multi-clip)
    static std::string compile_timeline_only(const Timeline& timeline) {
        return build_timeline_filter(timeline);
    }
    
private:
    static std::string build_timeline_filter(const Timeline& timeline) {
        if (timeline.getClipCount() <= 1) {
            return ""; // No timeline filter needed for single clip
        }
        
        // Para múltiples clips, usamos concat filter
        std::stringstream filter;
        filter << "concat=";
        
        const auto& clips = timeline.getClips();
        for (size_t i = 0; i < clips.size(); ++i) {
            filter << "v:" << clips[i].source_path;
            if (i < clips.size() - 1) {
                filter << "|";
            }
        }
        
        filter << "[vout]";
        return filter.str();
    }
    
    static std::string build_effects_filter(const EffectChain& effects) {
        std::stringstream filter;
        bool first = true;
        
        // Frame rate interpolation (minterpolate)
        if (effects.interpolate && effects.target_fps > 0) {
            filter << "minterpolate=fps=" << effects.target_fps;
            first = false;
        }
        
        // Speed adjustment (setpts)
        if (effects.slow_factor != 1.0f) {
            if (!first) filter << ",";
            filter << "setpts=" << effects.slow_factor << "*PTS";
            first = false;
        }
        
        // Scale - always use original resolution
        
        // Color adjustments
        if (effects.grayscale) {
            if (!first) filter << ",";
            filter << "format=gray";
            first = false;
        }
        
        if (effects.brightness != 0.0f || effects.contrast != 1.0f || effects.saturation != 1.0f) {
            if (!first) filter << ",";
            filter << "eq=brightness=" << effects.brightness
                   << ":contrast=" << effects.contrast
                   << ":saturation=" << effects.saturation;
            first = false;
        }
        
        return filter.str();
    }
    
    static std::string build_render_filter(const RenderSettings& render) {
        std::stringstream filter;
        
        // Output scale si es diferente al input
        if (render.output_width > 0 && render.output_height > 0) {
            filter << "scale=" << render.output_width << ":" << render.output_height
                   << ":flags=lanczos";
        }
        
        return filter.str();
    }
};

// FFmpegCommandBuilder - construye comandos FFmpeg completos
class FFmpegCommandBuilder {
public:
    static std::string build_render_command(const ProjectState& state, const std::string& output_file) {
        std::stringstream cmd;
        
        cmd << "ffmpeg";
        
        // Input
        cmd << " -i \"" << state.active_file << "\"";
        
        // Filtergraph
        std::string filter = FiltergraphCompiler::compile(state);
        if (!filter.empty()) {
            cmd << " -vf \"" << filter << "\"";
        }
        
        // Codec
        cmd << " -c:v " << state.render.codec;
        cmd << " -crf " << state.render.crf;
        cmd << " -preset " << state.render.preset;
        cmd << " -r " << state.render.fps;
        
        // Output
        cmd << " \"" << output_file << "\"";
        
        return cmd.str();
    }
    
    static std::string build_preview_command(const ProjectState& state) {
        std::stringstream cmd;
        
        cmd << "ffplay";
        cmd << " -i \"" << state.active_file << "\"";
        
        // Filtergraph
        std::string filter = FiltergraphCompiler::compile(state);
        if (!filter.empty()) {
            cmd << " -vf \"" << filter << "\"";
        }
        
        return cmd.str();
    }
};
