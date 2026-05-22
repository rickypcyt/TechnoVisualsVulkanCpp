#pragma once

#include <string>
#include <map>
#include <string>

struct VisualControls;
struct VideoRandomizerState;
struct VideoRandomizerState2;
class MidiSystem;
class OscSystem;

// Lee y escribe controls_state.cfg.
// No sabe nada de Vulkan, ImGui ni SDL.
// Uso:
//   ControlState::load("controls_state.cfg", controls, randomizer, randomizer2, allowDimChange, midiSystem, oscSystem);
//   ControlState::save("controls_state.cfg", controls, randomizer, randomizer2, allowDimChange, midiSystem, oscSystem);
namespace ControlState {

void load(
    const std::string&  path,
    VisualControls&     controls,
    VideoRandomizerState& randomizer,
    VideoRandomizerState2& randomizer2,
    bool&               allowDimensionChangeRecreation,
    MidiSystem&         midiSystem,
    OscSystem&          oscSystem,
    int&                selectedVideoAsset,
    int&                selectedVideoAsset2
);

void save(
    const std::string&        path,
    const VisualControls&     controls,
    const VideoRandomizerState& randomizer,
    const VideoRandomizerState2& randomizer2,
    bool                      allowDimensionChangeRecreation,
    const MidiSystem&         midiSystem,
    const OscSystem&          oscSystem,
    int                       selectedVideoAsset,
    int                       selectedVideoAsset2
);

} // namespace ControlState
