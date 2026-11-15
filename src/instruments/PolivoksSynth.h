#pragma once

// Polivoks 1982 — YAWN-native duophonic Polivoks emulation.
//
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 bokontep
//
// This is the YAWN-native port of the Polivoks 1982 DSP. The standalone VST
// source is GPL-3.0-or-later; the copyright holder licenses this YAWN
// integration under MIT. The original is duophonic: two free-running VCOs
// share one VCF, one VCA and one pair of ADSRs. In 2-voice mode VCO I tracks
// the highest held key and VCO II the lowest; in 1-voice mode both follow the
// lowest. Glide always acts on VCO I.

#include "instruments/Instrument.h"
#include "instruments/PolivoksDsp.h"
#include "effects/Biquad.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace yawn {
namespace instruments {

class PolivoksSynth : public Instrument
{
public:
    enum Params
    {
        kProgram = 0,
        kVolume, kTune, kGlide, kVoiceMode,
        kVco1Wave, kVco1Oct, kVco1Level, kLfoToVco1,
        kVco2Wave, kVco2Oct, kVco2Detune, kVco2Level, kLfoToVco2,
        kNoiseLevel, kXMod,
        kCutoff, kResonance, kEnvAmount, kLfoToVcf, kFilterMode,
        kFAttack, kFDecay, kFSustain, kFRelease, kFLoop,
        kAAttack, kADecay, kASustain, kARelease, kALoop, kGateHold,
        kLfoShape, kLfoRate, kLfoToVca,
        kKeyTrack, kVelocity, kBendRange, kPedal,
        kNumParams
    };

    PolivoksSynth();

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    const char* name() const override { return "Polivoks 1982"; }
    const char* id()   const override { return "polivoks"; }

    int parameterCount() const override { return kNumParams; }
    const InstrumentParameterInfo& parameterInfo(int index) const override;
    float getParameter(int index) const override;
    void setParameter(int index, float value) override;

    static float cutoffNormToHz(float norm);
    static float cutoffHzToNorm(float hz);
    static float timeNormToMs(float norm);
    static float timeMsToNorm(float ms);
    static float lfoNormToHz(float norm);
    static float lfoHzToNorm(float hz);
    static float glideNormToMs(float norm);
    static float glideMsToNorm(float ms);
    static const char* programName(int index);

private:
    struct Program
    {
        std::string name;
        std::vector<float> values;
    };

    struct Settings
    {
        int vco1Wave = 1, vco2Wave = 1, vco1Oct = 0, vco2Oct = 2;
        int filterMode = 0, lfoShape = 0, voiceMode = 1, bendRange = 0;
        float cutoff = 10000.0f, resonance = 0.0f, envAmount = 0.0f, lfoToVcf = 0.0f;
        float vco2Detune = 0.0f, vco1Level = 0.5f, vco2Level = 0.0f;
        float noiseLevel = 0.0f, xmod = 0.0f;
        float lfoToVco1 = 0.0f, lfoToVco2 = 0.0f, lfoRate = 5.0f, lfoToVca = 0.0f;
        float fAttack = 4.0f, fDecay = 300.0f, fSustain = 1.0f, fRelease = 4.0f;
        float aAttack = 4.0f, aDecay = 300.0f, aSustain = 1.0f, aRelease = 4.0f;
        bool fLoop = false, aLoop = false, gateHold = false;
        float glide = 0.0f, tune = 0.0f, keyTrack = 0.62f, velocityAmt = 0.0f;
        float volumeDb = -10.0f, pedal = 0.0f;

        float vco1OctSemi = -24.0f, vco2OctSemi = 0.0f;
        float xmodOct = 0.0f, lfoVco1Semis = 0.0f, lfoVco2Semis = 0.0f;
        float lfoVcfOct = 0.0f, envAmountOct = 0.0f, cutoffOctBase = 6.643856f;
    };

    void initParams();
    void buildPrograms();
    void applyProgram(int program);
    void syncSettings();
    void applySettings();

    void prepareDsp(double sampleRate);
    void handleMidi(const midi::MidiMessage& msg);
    void updateTargets();
    void triggerEnvelopes();
    void releaseEnvelopes();
    float renderSample();

    float m_params[kNumParams] = {};
    int   m_program = 0;
    std::vector<Program> m_programs;

    float m_baseSr = 44100.0f;
    float m_osr = 0.0f;

    plv::PolivoksVco m_vco1, m_vco2;
    plv::PolivoksFilter m_filter;
    plv::PolivoksLfo m_lfo;
    plv::PolivoksEnvelope m_filtEnv, m_ampEnv;
    effects::Biquad m_dec1, m_dec2;
    plv::DcBlocker m_dc;
    plv::Xorshift32 m_noise{0x1234567u};
    plv::Xorshift32 m_filterSeed{0x9e3779b9u};

    Settings m_settings;

    bool  m_held[128] = {};
    int   m_heldCount = 0;
    int   m_lastHigh = 60, m_lastLow = 60;
    float m_target1 = 60.0f, m_target2 = 60.0f;
    float m_note1 = 60.0f, m_note2 = 60.0f;
    float m_vco1BaseHz = 261.6256f, m_vco2BaseHz = 261.6256f;
    bool  m_hasPlayed = false;
    bool  m_gate = false;
    bool  m_lastGateHold = false;
    int   m_lastVoiceMode = 1;
    float m_lastVelNorm = 1.0f;
    float m_velGain = 1.0f;
    float m_bend = 0.0f, m_bendTarget = 0.0f;

    float m_glideCoeff = 1.0f, m_note2Coeff = 1.0f;
    float m_bendCoeff = 0.01f;
    float m_cutoffCoeff = 0.006f;
    float m_cutoffSmoothed = 3000.0f;
    float m_vol = 0.316f, m_volTarget = 0.316f, m_volCoeff = 0.001f;
    int   m_ctrl = 0;

    static constexpr int kCtrlInterval = 16;
};

} // namespace instruments
} // namespace yawn
