#pragma once

// Device display registry — one entry per device that has a custom
// display in the detail panel. Previously setupInstrumentDisplay /
// setupAudioEffectDisplay / setupMidiEffectDisplay in
// DetailPanelWidget.h were ~740 lines of if-chains keyed on device
// name strings, living in a header included by half the UI — every
// new device meant editing it and recompiling everything. The
// per-device knowledge now lives in DetailDisplayRegistry.cpp; the
// dispatchers in DetailPanelWidget are thin lookups.
//
// To add a device's display: write its builder in
// DetailDisplayRegistry.cpp and add one row to the matching table.

#include "ui/framework/v2/GroupedKnobBody.h"
#include <functional>
#include <string>

namespace yawn {
namespace instruments { class Instrument; }
namespace effects { class AudioEffect; }
namespace midi { class MidiEffect; }
namespace ui {
namespace fw2 {

class DetailPanelWidget;

// Per-device display wiring produced by a registry builder. Three
// body styles exist in the panel; the dispatchers consume whichever
// the builder set (priority: customBody → customPanel → grouped
// config).
struct DeviceDisplaySetup {
    GroupedKnobBody::Config config;          // grouped-knob body path
    std::function<void()> updater;           // optional per-frame updater
    CustomDeviceBody* customBody = nullptr;  // full-body replacement path
    Widget* customPanel = nullptr;           // setCustomPanel path
    float customPanelHeight = 0.0f;
    float customPanelMinW   = 0.0f;
};

// What a builder needs beyond the device pointer: a param
// write-through to the engine, and the device's slot in its chain
// (used by the LFO target picker).
struct DisplayBuildArgs {
    DetailPanelWidget& panel;
    std::function<void(int, float)> setParam;
    int chainIndex = -1;
};

using InstrumentDisplayBuilder =
    DeviceDisplaySetup (*)(const DisplayBuildArgs&, instruments::Instrument*);
using AudioFxDisplayBuilder =
    DeviceDisplaySetup (*)(const DisplayBuildArgs&, effects::AudioEffect*);
using MidiFxDisplayBuilder =
    DeviceDisplaySetup (*)(const DisplayBuildArgs&, midi::MidiEffect*);

// Lookup by device display name (instruments, from name()) or id
// (effects, from id()). Null when the device has no custom display —
// the panel then falls back to the generic knob layout.
InstrumentDisplayBuilder findInstrumentDisplayBuilder(const std::string& name);
AudioFxDisplayBuilder    findAudioFxDisplayBuilder(const std::string& id);
MidiFxDisplayBuilder     findMidiFxDisplayBuilder(const std::string& id);

} // namespace fw2
} // namespace ui
} // namespace yawn
