#pragma once

#include <string>
#include <map>
#include <string>

struct VisualControls;
struct VideoRandomizerState;
class MidiSystem;

// Lee y escribe controls_state.cfg.
// No sabe nada de Vulkan, ImGui ni SDL.
// Uso:
//   ControlState::load("controls_state.cfg", controls, randomizer, allowDimChange);
//   ControlState::save("controls_state.cfg", controls, randomizer, allowDimChange);
namespace ControlState {

void load(
    const std::string&  path,
    VisualControls&     controls,
    VideoRandomizerState& randomizer,
    bool&               allowDimensionChangeRecreation,
    MidiSystem&         midiSystem
);

void save(
    const std::string&        path,
    const VisualControls&     controls,
    const VideoRandomizerState& randomizer,
    bool                      allowDimensionChangeRecreation,
    const MidiSystem&         midiSystem
);

} // namespace ControlState
