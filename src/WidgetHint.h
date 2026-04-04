#pragma once
#include <cstdint>

namespace yawn {

// Hint for the UI layer to select the right widget for a parameter.
// Used by InstrumentParameterInfo, effects::ParameterInfo, and
// MidiEffectParameterInfo so that DeviceWidget::buildKnobs() can
// create DentedKnob, StepSelector, Toggle, etc. instead of always
// using a plain FwKnob.
enum class WidgetHint : uint8_t {
    Knob = 0,       // Standard continuous knob (default)
    DentedKnob,     // Knob with snap detent at default value
    StepSelector,   // Integer step selector with left/right arrows
    Toggle,         // On/off (or A/B) toggle switch
    Knob360,        // Full 360° rotation knob (phase, cyclic)
};

} // namespace yawn
