// PolivoksSynth.cpp — YAWN-native Polivoks 1982 instrument.
//
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 bokontep
//
// Duophonic: VCO I tracks the highest held key, VCO II the lowest, sharing one
// filter, one VCA and one pair of ADSRs. 1-voice mode makes both oscillators
// follow the lowest key. Glide always acts on VCO I; in mono it acts on both.
// The visible control set and the factory programs mirror the standalone
// Polivoks 1982 plugin. The standalone VST source is GPL-3.0-or-later; the
// copyright holder licenses this YAWN integration under MIT.

#include "instruments/PolivoksSynth.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace yawn {
namespace instruments {
namespace {

const char* const kWaveLabels[] = { "Triangle", "Saw", "Square", "Pulse 1", "Pulse 2" };
const char* const kOctLabels[]  = { "32'", "16'", "8'", "4'", "2'" };
const char* const kFilterLabels[] = { "LP", "BP" };
const char* const kVoiceLabels[] = { "1 Voice", "2 Voice" };
const char* const kLfoLabels[] = { "Triangle", "Square", "S&H", "Noise" };
const char* const kProgramNames[] = {
    "Factory Test", "Monster Bass", "Scream", "Duo Lead",
    "Trill Chord", "Looping Wobble", "Steam Train", "FM Growl"
};
const char* const kBendLabels[] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"
};

void fmtCutoff(float v, char* b, int n)
{
    const float hz = PolivoksSynth::cutoffNormToHz(v);
    if (hz >= 1000.0f) std::snprintf(b, n, "%.1fk", hz / 1000.0f);
    else               std::snprintf(b, n, "%.0f", hz);
}

void fmtTime(float v, char* b, int n)
{
    const float ms = PolivoksSynth::timeNormToMs(v);
    if (ms >= 1000.0f) std::snprintf(b, n, "%.2fs", ms / 1000.0f);
    else               std::snprintf(b, n, "%.0fms", ms);
}

void fmtLfoRate(float v, char* b, int n)
{
    const float hz = PolivoksSynth::lfoNormToHz(v);
    if (hz >= 10.0f) std::snprintf(b, n, "%.1fHz", hz);
    else             std::snprintf(b, n, "%.2fHz", hz);
}

void fmtGlide(float v, char* b, int n)
{
    const float ms = PolivoksSynth::glideNormToMs(v);
    if (ms < 0.5f)         std::snprintf(b, n, "Off");
    else if (ms >= 1000.f) std::snprintf(b, n, "%.2fs", ms / 1000.0f);
    else                   std::snprintf(b, n, "%.0fms", ms);
}

void fmtSemitones(float v, char* b, int n) { std::snprintf(b, n, "%.2f st", v); }

} // namespace

// ── Normalized parameter mappings ────────────────────────────────────
float PolivoksSynth::cutoffNormToHz(float n)
{
    n = std::clamp(n, 0.0f, 1.0f);
    return 100.0f * std::pow(100.0f, n);
}

float PolivoksSynth::cutoffHzToNorm(float hz)
{
    hz = std::clamp(hz, 100.0f, 10000.0f);
    return std::log(hz / 100.0f) / std::log(100.0f);
}

float PolivoksSynth::timeNormToMs(float n)
{
    n = std::clamp(n, 0.0f, 1.0f);
    return 4.0f * std::pow(2500.0f, n);
}

float PolivoksSynth::timeMsToNorm(float ms)
{
    ms = std::clamp(ms, 4.0f, 10000.0f);
    return std::log(ms / 4.0f) / std::log(2500.0f);
}

float PolivoksSynth::lfoNormToHz(float n)
{
    n = std::clamp(n, 0.0f, 1.0f);
    return 0.1f * std::pow(300.0f, n);
}

float PolivoksSynth::lfoHzToNorm(float hz)
{
    hz = std::clamp(hz, 0.1f, 30.0f);
    return std::log(hz / 0.1f) / std::log(300.0f);
}

float PolivoksSynth::glideNormToMs(float n)
{
    n = std::clamp(n, 0.0f, 1.0f);
    return 2000.0f * n * n;
}

float PolivoksSynth::glideMsToNorm(float ms)
{
    ms = std::clamp(ms, 0.0f, 2000.0f);
    return std::sqrt(ms / 2000.0f);
}

const char* PolivoksSynth::programName(int index)
{
    if (index < 0 || index >= 8) return "";
    return kProgramNames[index];
}

// ── Parameter metadata ───────────────────────────────────────────────
const InstrumentParameterInfo& PolivoksSynth::parameterInfo(int index) const
{
    static const InstrumentParameterInfo p[kNumParams] = {
        { "Program",       0, 7, 0, "",  false, false, WidgetHint::StepSelector,
          kProgramNames, 8, nullptr },
        { "Volume",      -40, 6, -10, "dB", false },
        { "Master Tune",  -2, 2, 0, "st", false, false, WidgetHint::Knob, nullptr, 0,
          fmtSemitones },
        { "Glide",         0, 1, 0, "ms", false, false, WidgetHint::Knob, nullptr, 0,
          fmtGlide },
        { "Voice Mode",    0, 1, 1, "", false, false, WidgetHint::StepSelector,
          kVoiceLabels, 2, nullptr },

        { "VCO1 Wave",     0, 4, 1, "", false, false, WidgetHint::StepSelector,
          kWaveLabels, 5, nullptr },
        { "VCO1 Oct",      0, 4, 0, "", false, false, WidgetHint::StepSelector,
          kOctLabels, 5, nullptr },
        { "VCO1 Level",    0, 1, 0.5f, "%", false },
        { "LFO>VCO1",      0, 1, 0, "%", false },

        { "VCO2 Wave",     0, 4, 1, "", false, false, WidgetHint::StepSelector,
          kWaveLabels, 5, nullptr },
        { "VCO2 Oct",      0, 4, 2, "", false, false, WidgetHint::StepSelector,
          kOctLabels, 5, nullptr },
        { "VCO2 Detune",  -4, 4, 0, "st", false, false, WidgetHint::Knob, nullptr, 0,
          fmtSemitones },
        { "VCO2 Level",    0, 1, 0, "%", false },
        { "LFO>VCO2",      0, 1, 0, "%", false },

        { "Noise",         0, 1, 0, "%", false },
        { "X-MOD",         0, 1, 0, "%", false },

        { "Cutoff",        0, 1, 1.0f, "Hz", false, false, WidgetHint::Knob, nullptr, 0,
          fmtCutoff },
        { "Resonance",     0, 1, 0, "%", false },
        { "Env Amount",    0, 1, 0, "%", false },
        { "LFO>VCF",       0, 1, 0, "%", false },
        { "Filter Mode",   0, 1, 0, "", false, false, WidgetHint::StepSelector,
          kFilterLabels, 2, nullptr },

        { "Filt Attack",   0, 1, 0, "ms", false, false, WidgetHint::Knob, nullptr, 0,
          fmtTime },
        { "Filt Decay",    0, 1, timeMsToNorm(300), "ms", false, false,
          WidgetHint::Knob, nullptr, 0, fmtTime },
        { "Filt Sustain",  0, 1, 1, "%", false },
        { "Filt Release",  0, 1, 0, "ms", false, false, WidgetHint::Knob, nullptr, 0,
          fmtTime },
        { "Filt Loop",   0, 1, 0, "", true, false, WidgetHint::Toggle },

        { "Amp Attack",    0, 1, 0, "ms", false, false, WidgetHint::Knob, nullptr, 0,
          fmtTime },
        { "Amp Decay",     0, 1, timeMsToNorm(300), "ms", false, false,
          WidgetHint::Knob, nullptr, 0, fmtTime },
        { "Amp Sustain",   0, 1, 1, "%", false },
        { "Amp Release",   0, 1, 0, "ms", false, false, WidgetHint::Knob, nullptr, 0,
          fmtTime },
        { "Amp Loop",    0, 1, 0, "", true, false, WidgetHint::Toggle },
        { "Gate On",     0, 1, 0, "", true, false, WidgetHint::Toggle },

        { "LFO Shape",     0, 3, 0, "", false, false, WidgetHint::StepSelector,
          kLfoLabels, 4, nullptr },
        { "LFO Rate",      0, 1, lfoHzToNorm(5.0f), "Hz", false, false,
          WidgetHint::Knob, nullptr, 0, fmtLfoRate },
        { "LFO>VCA",       0, 1, 0, "%", false },

        { "Key Track",     0, 1, 0.62f, "%", false },
        { "Velocity",      0, 1, 0, "%", false },
        { "Bend Range",    0, 12, 0, "", false, false, WidgetHint::StepSelector,
          kBendLabels, 13, nullptr },
        { "Pedal",         0, 1, 0, "%", false },
    };
    return p[std::clamp(index, 0, kNumParams - 1)];
}

float PolivoksSynth::getParameter(int index) const
{
    if (index < 0 || index >= kNumParams) return 0.0f;
    if (index == kProgram) return static_cast<float>(m_program);
    return m_params[index];
}

void PolivoksSynth::setParameter(int index, float value)
{
    if (index < 0 || index >= kNumParams) return;
    const auto& info = parameterInfo(index);
    value = std::clamp(value, info.minValue, info.maxValue);

    if (index == kProgram)
    {
        applyProgram(static_cast<int>(std::lround(value)));
        return;
    }

    if (info.isBoolean)
        value = value >= 0.5f ? 1.0f : 0.0f;
    else if (info.widgetHint == WidgetHint::StepSelector)
        value = std::round(value);

    m_params[index] = value;
}

// ── Programs ─────────────────────────────────────────────────────────
void PolivoksSynth::buildPrograms()
{
    m_programs.clear();
    m_programs.reserve(8);

    auto hz  = [](float value) { return cutoffHzToNorm(value); };
    auto ms  = [](float value) { return timeMsToNorm(value); };
    auto lfo = [](float value) { return lfoHzToNorm(value); };
    auto gl  = [](float value) { return glideMsToNorm(value); };

    auto add = [this](const char* name,
                      std::initializer_list<std::pair<int, float>> overrides) {
        Program prog;
        prog.name = name;
        prog.values.resize(kNumParams);
        for (int i = 0; i < kNumParams; ++i)
            prog.values[(size_t)i] = parameterInfo(i).defaultValue;
        prog.values[kProgram] = static_cast<float>(m_programs.size());
        for (const auto& o : overrides)
            prog.values[(size_t)o.first] = o.second;
        m_programs.push_back(std::move(prog));
    };

    add("Factory Test", {});

    add("Monster Bass", {
        { kVco1Oct, 1 }, { kVco1Level, 1.0f },
        { kVco2Oct, 1 }, { kVco2Detune, 0.12f }, { kVco2Level, 0.95f },
        { kCutoff, hz(250) }, { kResonance, 0.45f }, { kEnvAmount, 0.75f },
        { kFAttack, ms(4) }, { kFDecay, ms(180) }, { kFSustain, 0 },
        { kFRelease, ms(150) },
        { kAAttack, ms(4) }, { kADecay, ms(350) }, { kASustain, 0.65f },
        { kARelease, ms(120) }, { kVolume, -12 }
    });

    add("Scream", {
        { kVco1Oct, 2 }, { kVco1Level, 0.8f },
        { kVco2Oct, 2 }, { kVco2Detune, -0.1f }, { kVco2Level, 0.6f },
        { kCutoff, hz(700) }, { kResonance, 1.0f }, { kEnvAmount, 0.25f },
        { kFAttack, ms(10) }, { kFDecay, ms(800) }, { kFSustain, 0.55f },
        { kFRelease, ms(500) },
        { kAAttack, ms(5) }, { kADecay, ms(500) }, { kASustain, 0.8f },
        { kARelease, ms(400) },
        { kLfoShape, 0 }, { kLfoRate, lfo(0.25f) }, { kLfoToVcf, 0.25f },
        { kVolume, -14 }
    });

    add("Duo Lead", {
        { kVco1Oct, 2 }, { kVco1Level, 0.85f },
        { kVco2Oct, 2 }, { kVco2Detune, 0.2f }, { kVco2Level, 0.75f },
        { kCutoff, hz(2200) }, { kResonance, 0.25f }, { kEnvAmount, 0.45f },
        { kFAttack, ms(4) }, { kFDecay, ms(450) }, { kFSustain, 0.45f },
        { kFRelease, ms(350) },
        { kAAttack, ms(8) }, { kADecay, ms(450) }, { kASustain, 0.8f },
        { kARelease, ms(300) },
        { kGlide, gl(280) }, { kVolume, -10 }
    });

    add("Trill Chord", {
        { kVco1Oct, 2 }, { kVco1Level, 0.85f },
        { kVco2Oct, 2 }, { kVco2Detune, 3.0f }, { kVco2Level, 0.75f },
        { kCutoff, hz(3200) }, { kResonance, 0.2f }, { kEnvAmount, 0.2f },
        { kAAttack, ms(4) }, { kADecay, ms(300) }, { kASustain, 1.0f },
        { kARelease, ms(200) },
        { kLfoShape, 1 }, { kLfoRate, lfo(5.7f) }, { kLfoToVco1, 0.2f },
        { kLfoToVco2, 0.2f }, { kVolume, -10 }
    });

    add("Looping Wobble", {
        { kVco1Oct, 1 }, { kVco1Level, 0.65f },
        { kVco2Oct, 1 }, { kVco2Detune, 0.15f }, { kVco2Level, 0.55f },
        { kCutoff, hz(700) }, { kResonance, 0.55f }, { kEnvAmount, 0.55f },
        { kFAttack, ms(180) }, { kFDecay, ms(180) }, { kFSustain, 0 },
        { kFRelease, ms(200) }, { kFLoop, 1 },
        { kAAttack, ms(5) }, { kADecay, ms(500) }, { kASustain, 1.0f },
        { kARelease, ms(300) }, { kVolume, -12 }
    });

    add("Steam Train", {
        { kVco1Level, 0.5f }, { kVco2Level, 0 }, { kNoiseLevel, 0.65f },
        { kCutoff, hz(600) }, { kResonance, 0.8f }, { kEnvAmount, 0.2f },
        { kLfoShape, 2 }, { kLfoRate, lfo(8.0f) }, { kLfoToVcf, 0.6f },
        { kLfoToVco1, 0.25f },
        { kFAttack, ms(120) }, { kFDecay, ms(180) }, { kFSustain, 0 },
        { kFRelease, ms(200) }, { kFLoop, 1 },
        { kAAttack, ms(5) }, { kADecay, ms(500) }, { kASustain, 1.0f },
        { kARelease, ms(300) }, { kVolume, -14 }
    });

    add("FM Growl", {
        { kVco1Oct, 2 }, { kVco1Level, 0.85f },
        { kVco2Wave, 0 }, { kVco2Oct, 1 }, { kVco2Level, 0.5f },
        { kXMod, 0.55f },
        { kCutoff, hz(1400) }, { kResonance, 0.3f }, { kEnvAmount, 0.45f },
        { kFAttack, ms(4) }, { kFDecay, ms(300) }, { kFSustain, 0.3f },
        { kFRelease, ms(250) },
        { kAAttack, ms(4) }, { kADecay, ms(300) }, { kASustain, 0.85f },
        { kARelease, ms(250) }, { kVolume, -12 }
    });
}

void PolivoksSynth::applyProgram(int program)
{
    if (m_programs.empty()) return;
    program = std::clamp(program, 0, static_cast<int>(m_programs.size()) - 1);
    const Program& prog = m_programs[(size_t)program];
    for (int i = 0; i < kNumParams; ++i)
        m_params[i] = prog.values[(size_t)i];
    m_params[kProgram] = static_cast<float>(program);
    m_program = program;
}

void PolivoksSynth::initParams()
{
    for (int i = 0; i < kNumParams; ++i)
        m_params[i] = parameterInfo(i).defaultValue;
    buildPrograms();
    applyProgram(0);
}

PolivoksSynth::PolivoksSynth()
{
    initParams();
}

// ── Lifecycle ────────────────────────────────────────────────────────
void PolivoksSynth::init(double sampleRate, int maxBlockSize)
{
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    prepareDsp(sampleRate);
    syncSettings();
    applySettings();
    reset();
}

void PolivoksSynth::prepareDsp(double sampleRate)
{
    m_baseSr = static_cast<float>(sampleRate);
    m_osr = m_baseSr * static_cast<float>(plv::kOversample);

    m_vco1.prepare(m_osr);
    m_vco2.prepare(m_osr);
    m_filter.prepare(m_osr);
    m_lfo.prepare(m_osr);
    m_filtEnv.prepare(m_osr);
    m_ampEnv.prepare(m_osr);
    m_dc.prepare(m_baseSr);

    m_dec1.compute(effects::Biquad::Type::LowPass, m_osr, 19000.0, 0.0, 0.5411961);
    m_dec2.compute(effects::Biquad::Type::LowPass, m_osr, 19000.0, 0.0, 1.3065630);

    m_cutoffCoeff = 1.0f - std::exp(-1.0f / (0.0010f * m_osr));
    m_bendCoeff   = 1.0f - std::exp(-1.0f / (0.0050f * m_osr));
    m_volCoeff    = 1.0f - std::exp(-1.0f / (0.0200f * m_baseSr));
}

void PolivoksSynth::reset()
{
    m_filtEnv.reset();
    m_ampEnv.reset();
    m_vco1.reset(0.0f);
    m_vco2.reset(0.37f);
    m_filter.reset();
    m_dec1.reset();
    m_dec2.reset();
    m_dc.reset();
    m_lfo.reset();
    m_noise.reseed(0x1234567u);
    m_filterSeed.reseed(0x9e3779b9u);

    std::memset(m_held, 0, sizeof(m_held));
    m_heldCount = 0;
    m_lastHigh = m_lastLow = 60;
    m_target1 = m_target2 = m_note1 = m_note2 = 60.0f;
    m_hasPlayed = false;
    m_gate = false;
    m_lastVelNorm = 1.0f;
    m_velGain = 1.0f;
    m_bend = m_bendTarget = 0.0f;
    m_ctrl = 0;

    m_cutoffSmoothed = std::clamp(cutoffNormToHz(m_params[kCutoff]), 20.0f, 18000.0f);
    m_volTarget = std::pow(10.0f, m_params[kVolume] * 0.05f);
    m_vol = m_volTarget;
    m_filter.setMode(static_cast<float>(std::lround(m_params[kFilterMode])));
    m_filter.set(m_cutoffSmoothed, m_params[kResonance]);

    m_lastGateHold = m_params[kGateHold] >= 0.5f;
    m_lastVoiceMode = static_cast<int>(std::lround(m_params[kVoiceMode]));
}

// ── Settings ─────────────────────────────────────────────────────────
void PolivoksSynth::syncSettings()
{
    auto p = [this](int i) { return m_params[i]; };
    auto i = [this](int idx) { return static_cast<int>(std::lround(m_params[idx])); };

    m_settings.vco1Wave = i(kVco1Wave);
    m_settings.vco1Oct = i(kVco1Oct);
    m_settings.vco2Wave = i(kVco2Wave);
    m_settings.vco2Oct = i(kVco2Oct);
    m_settings.filterMode = i(kFilterMode);
    m_settings.lfoShape = i(kLfoShape);
    m_settings.voiceMode = i(kVoiceMode);
    m_settings.bendRange = i(kBendRange);

    m_settings.cutoff = cutoffNormToHz(p(kCutoff));
    m_settings.cutoffOctBase = std::log2(std::max(m_settings.cutoff, 100.0f) / 100.0f);
    m_settings.resonance = p(kResonance);
    m_settings.envAmount = p(kEnvAmount);
    m_settings.lfoToVcf = p(kLfoToVcf);
    m_settings.vco2Detune = p(kVco2Detune);
    m_settings.vco1Level = p(kVco1Level);
    m_settings.vco2Level = p(kVco2Level);
    m_settings.noiseLevel = p(kNoiseLevel);
    m_settings.xmod = p(kXMod);
    m_settings.lfoToVco1 = p(kLfoToVco1);
    m_settings.lfoToVco2 = p(kLfoToVco2);
    m_settings.lfoRate = lfoNormToHz(p(kLfoRate));
    m_settings.lfoToVca = p(kLfoToVca);

    m_settings.fAttack = timeNormToMs(p(kFAttack));
    m_settings.fDecay = timeNormToMs(p(kFDecay));
    m_settings.fSustain = p(kFSustain);
    m_settings.fRelease = timeNormToMs(p(kFRelease));
    m_settings.fLoop = p(kFLoop) >= 0.5f;
    m_settings.aAttack = timeNormToMs(p(kAAttack));
    m_settings.aDecay = timeNormToMs(p(kADecay));
    m_settings.aSustain = p(kASustain);
    m_settings.aRelease = timeNormToMs(p(kARelease));
    m_settings.aLoop = p(kALoop) >= 0.5f;

    m_settings.gateHold = p(kGateHold) >= 0.5f;
    m_settings.glide = glideNormToMs(p(kGlide));
    m_settings.tune = p(kTune);
    m_settings.keyTrack = p(kKeyTrack);
    m_settings.velocityAmt = p(kVelocity);
    m_settings.volumeDb = p(kVolume);
    m_settings.pedal = p(kPedal);

    m_settings.vco1OctSemi = static_cast<float>(m_settings.vco1Oct - 2) * 12.0f;
    m_settings.vco2OctSemi = static_cast<float>(m_settings.vco2Oct - 2) * 12.0f;
    m_settings.xmodOct = m_settings.xmod * 5.0f;
    m_settings.lfoVco1Semis = m_settings.lfoToVco1 * 12.0f;
    m_settings.lfoVco2Semis = m_settings.lfoToVco2 * 12.0f;
    m_settings.lfoVcfOct = m_settings.lfoToVcf * 5.0f;
    m_settings.envAmountOct = m_settings.envAmount * 6.64f;
}

void PolivoksSynth::applySettings()
{
    m_vco1.setWave(m_settings.vco1Wave);
    m_vco2.setWave(m_settings.vco2Wave);
    m_lfo.setRate(m_settings.lfoRate);
    m_lfo.setShape(m_settings.lfoShape);
    m_filter.setMode(static_cast<float>(m_settings.filterMode));

    m_filtEnv.set(m_settings.fAttack, m_settings.fDecay,
                  m_settings.fSustain, m_settings.fRelease, m_settings.fLoop);
    m_ampEnv.set(m_settings.aAttack, m_settings.aDecay,
                 m_settings.aSustain, m_settings.aRelease, m_settings.aLoop);

    if (m_settings.glide <= 0.5f)
        m_glideCoeff = 1.0f;
    else
    {
        const float tauMs = m_settings.glide * (1.0f / 3.0f);
        m_glideCoeff = 1.0f - std::exp(-1.0f / (tauMs * 0.001f * m_osr));
    }
    m_note2Coeff = (m_settings.voiceMode == 1) ? 1.0f : m_glideCoeff;

    m_volTarget = std::pow(10.0f, m_settings.volumeDb * 0.05f);
    m_velGain = 1.0f + (m_lastVelNorm - 1.0f) * m_settings.velocityAmt;
    if (m_settings.bendRange == 0) m_bendTarget = 0.0f;

    if (m_settings.gateHold != m_lastGateHold)
    {
        m_lastGateHold = m_settings.gateHold;
        if (m_settings.gateHold)
        {
            if (!m_gate) { m_gate = true; triggerEnvelopes(); }
        }
        else if (m_heldCount == 0)
        {
            m_gate = false;
            releaseEnvelopes();
        }
    }

    if (m_settings.voiceMode != m_lastVoiceMode)
    {
        m_lastVoiceMode = m_settings.voiceMode;
        updateTargets();
    }
}

// ── Notes ────────────────────────────────────────────────────────────
void PolivoksSynth::updateTargets()
{
    if (m_heldCount > 0)
    {
        int hi = -1, lo = 128;
        for (int n = 0; n < 128; ++n)
        {
            if (!m_held[n]) continue;
            if (n > hi) hi = n;
            if (n < lo) lo = n;
        }
        if (hi >= 0) { m_lastHigh = hi; m_lastLow = lo; }
    }

    if (m_settings.voiceMode == 0)
    {
        m_target1 = static_cast<float>(m_lastLow);
        m_target2 = static_cast<float>(m_lastLow);
    }
    else
    {
        m_target1 = static_cast<float>(m_lastHigh);
        m_target2 = static_cast<float>(m_lastLow);
    }

    if (!m_hasPlayed)
    {
        m_note1 = m_target1;
        m_note2 = m_target2;
    }
}

void PolivoksSynth::triggerEnvelopes() { m_filtEnv.gateOn(); m_ampEnv.gateOn(); }
void PolivoksSynth::releaseEnvelopes() { m_filtEnv.gateOff(); m_ampEnv.gateOff(); }

void PolivoksSynth::handleMidi(const midi::MidiMessage& msg)
{
    switch (msg.type)
    {
        case midi::MidiMessage::Type::NoteOn:
        {
            const int n = std::clamp(static_cast<int>(msg.note), 0, 127);
            if (!m_held[n]) { m_held[n] = true; ++m_heldCount; }
            m_lastVelNorm = std::clamp(static_cast<float>(msg.velocity) / 65535.0f,
                                       0.0f, 1.0f);
            m_velGain = 1.0f + (m_lastVelNorm - 1.0f) * m_settings.velocityAmt;
            updateTargets();
            m_hasPlayed = true;
            if (!m_gate) { m_gate = true; triggerEnvelopes(); }
            break;
        }
        case midi::MidiMessage::Type::NoteOff:
        {
            const int n = std::clamp(static_cast<int>(msg.note), 0, 127);
            if (m_held[n]) { m_held[n] = false; --m_heldCount; }
            updateTargets();
            if (m_heldCount == 0 && !m_settings.gateHold)
            {
                m_gate = false;
                releaseEnvelopes();
            }
            break;
        }
        case midi::MidiMessage::Type::ControlChange:
            if (msg.ccNumber == 123 || msg.ccNumber == 120)
            {
                std::memset(m_held, 0, sizeof(m_held));
                m_heldCount = 0;
                updateTargets();
                m_gate = false;
                releaseEnvelopes();
            }
            break;
        case midi::MidiMessage::Type::PitchBend:
            m_bendTarget = midi::Convert::pb32toFloat(msg.value)
                         * static_cast<float>(m_settings.bendRange);
            break;
        default:
            break;
    }
}

// ── Audio ────────────────────────────────────────────────────────────
void PolivoksSynth::process(float* buffer, int numFrames, int numChannels,
                            const midi::MidiBuffer& midi)
{
    if (!buffer || numFrames <= 0 || numChannels <= 0 || m_osr <= 0.0f) return;

    for (int i = 0; i < midi.count(); ++i)
        handleMidi(midi[i]);

    syncSettings();
    applySettings();

    for (int i = 0; i < numFrames; ++i)
    {
        const float sample = renderSample();
        float* frame = buffer + static_cast<size_t>(i) * static_cast<size_t>(numChannels);
        frame[0] += sample;
        if (numChannels > 1) frame[1] += sample;
    }
}

float PolivoksSynth::renderSample()
{
    float decimated = 0.0f;

    for (int k = 0; k < plv::kOversample; ++k)
    {
        float lfoSigned = 0.0f, lfoUnipolar = 0.0f;
        m_lfo.process(lfoSigned, lfoUnipolar);

        const float fenv = m_filtEnv.process();
        const float aenv = m_ampEnv.process();

        m_note1 += m_glideCoeff * (m_target1 - m_note1);
        m_note2 += m_note2Coeff * (m_target2 - m_note2);
        m_bend += m_bendCoeff * (m_bendTarget - m_bend);

        if (m_ctrl == 0)
        {
            const float pitch1 = m_note1 + m_bend + m_settings.tune
                               + m_settings.vco1OctSemi
                               + lfoSigned * m_settings.lfoVco1Semis;
            const float pitch2 = m_note2 + m_bend + m_settings.tune
                               + m_settings.vco2OctSemi + m_settings.vco2Detune
                               + lfoSigned * m_settings.lfoVco2Semis;
            m_vco1BaseHz = plv::midiToHzFast(pitch1);
            m_vco2BaseHz = plv::midiToHzFast(pitch2);

            const float keyOct = m_settings.keyTrack * (m_note1 - 60.0f) * (1.0f / 12.0f);
            const float modOct = keyOct + m_settings.envAmountOct * fenv
                               + m_settings.lfoVcfOct * lfoSigned
                               + m_settings.pedal * 5.0f;
            const float cutoffHz = std::clamp(
                100.0f * plv::fastExp2(m_settings.cutoffOctBase + modOct),
                20.0f, 18000.0f);
            m_cutoffSmoothed += m_cutoffCoeff * (cutoffHz - m_cutoffSmoothed);
            m_filter.set(m_cutoffSmoothed, m_settings.resonance);
        }
        m_ctrl++;
        if (m_ctrl >= kCtrlInterval) m_ctrl = 0;

        m_vco2.setFrequency(m_vco2BaseHz);
        const float v2 = m_vco2.process();

        float f1 = m_vco1BaseHz;
        if (m_settings.xmodOct > 0.0f)
            f1 *= plv::fastExp2(m_settings.xmodOct * v2);
        m_vco1.setFrequency(f1);
        const float v1 = m_vco1.process();

        float mix = v1 * m_settings.vco1Level
                  + v2 * m_settings.vco2Level
                  + m_noise.nextBipolar() * m_settings.noiseLevel;
        mix += m_filterSeed.nextBipolar() * 2.0e-5f;

        const float filtered = m_filter.process(mix);
        const float lfoGain = 1.0f - m_settings.lfoToVca * (1.0f - lfoUnipolar);
        float vca = filtered * (aenv * lfoGain * m_velGain);
        vca = plv::softClip(vca * 1.1f);

        decimated = m_dec2.process(m_dec1.process(vca));
    }

    float y = m_dc.process(decimated);
    m_vol += m_volCoeff * (m_volTarget - m_vol);
    y *= m_vol;
    return plv::flushDenormal(plv::softClip(y));
}

} // namespace instruments
} // namespace yawn
