#pragma once

#include <string>
#include <map>

struct VideoRandomizerState;
class OscSystem;

// Lee y escribe controls_state.cfg.
// No sabe nada de Vulkan, ImGui ni SDL.
// Uso:
//   ControlState::load("controls_state.cfg", randomizer, randomizer2, allowDimChange, oscSystem);
//   ControlState::save("controls_state.cfg", randomizer, randomizer2, allowDimChange, oscSystem);
// Nota: MIDI mappings ahora se manejan separadamente por MidiSystem
namespace ControlState {

void load(
    const std::string&  path,
    VideoRandomizerState& randomizer,
    VideoRandomizerState& randomizer2,
    VideoRandomizerState& randomizer3,
    bool&               allowDimensionChangeRecreation,
    OscSystem&          oscSystem,
    int&                selectedVideoAsset,
    int&                selectedVideoAsset2,
    int&                selectedVideoAsset3,
    std::string&        videoSourcePath,
    std::string&        videoSourcePath2,
    std::string&        videoSourcePath3,
    std::string&        videoAssetsRoot
);

void save(
    const std::string&        path,
    const VideoRandomizerState& randomizer,
    const VideoRandomizerState& randomizer2,
    const VideoRandomizerState& randomizer3,
    bool                      allowDimensionChangeRecreation,
    const OscSystem&          oscSystem,
    int                       selectedVideoAsset,
    int                       selectedVideoAsset2,
    int                       selectedVideoAsset3,
    const std::string&        videoSourcePath,
    const std::string&        videoSourcePath2,
    const std::string&        videoSourcePath3,
    const std::string&        videoAssetsRoot
);

} // namespace ControlState
