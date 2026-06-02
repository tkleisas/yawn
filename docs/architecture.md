# Architecture

*Designed by an AI that has read every audio programming tutorial on the internet but has never actually heard a sound.*

```
┌─────────────────────────────────────────────────────────────────┐
│          UI Layer — fw2 only (SDL3 + OpenGL 3.3)                │
│  ┌─────────────┐ ┌─────────────┐ ┌──────────┐ ┌─────────────┐  │
│  │  Session    │ │ Arrangement │ │  Detail  │ │   Piano     │  │
│  │   Panel     │ │   Panel     │ │  Panel   │ │    Roll     │  │
│  └─────────────┘ └─────────────┘ └──────────┘ └─────────────┘  │
│  ┌─────────────┐ ┌─────────────┐ ┌──────────┐ ┌─────────────┐  │
│  │   Mixer     │ │   Browser   │ │ Visual   │ │  Transport  │  │
│  │   Panel     │ │    Panel    │ │  Params  │ │    Panel    │  │
│  └─────────────┘ └─────────────┘ └──────────┘ └─────────────┘  │
│  ┌─────────────┐ ┌─────────────┐ ┌──────────┐ ┌─────────────┐  │
│  │  FlexBox /  │ │ LayerStack  │ │  fw2     │ │  MIDI Learn │  │
│  │ ContentGrid │ │ (overlays)  │ │ Widgets  │ │   manager   │  │
│  └─────────────┘ └─────────────┘ └──────────┘ └─────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                    Application Core                             │
│  ┌──────────┐ ┌───────────┐ ┌──────────┐ ┌──────────────────┐  │
│  │ Project  │ │ Transport │ │  Undo    │ │  Message Queue   │  │
│  │  Model   │ │  & Loop   │ │ Manager  │ │   (lock-free)    │  │
│  └──────────┘ └───────────┘ └──────────┘ └──────────────────┘  │
│  ┌──────────┐ ┌───────────┐ ┌──────────┐ ┌──────────────────┐  │
│  │ Project  │ │   MIDI    │ │  MIDI    │ │      Crash       │  │
│  │ Serial.  │ │  Mapping  │ │ Monitor  │ │     Handler      │  │
│  └──────────┘ └───────────┘ └──────────┘ └──────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│         Controller Scripting (Lua 5.4) ─── 4 controllers        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────────────────┐  │
│  │Controller│ │   Lua    │ │Controller│ │    yawn.* API     │  │
│  │ Manager  │ │  Engine  │ │ MidiPort │ │ (~50 functions)   │  │
│  └──────────┘ └──────────┘ └──────────┘ └───────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│      Visual Engine ─── GPU shaders, video, 3D, automation       │
│  ┌──────────┐ ┌───────────┐ ┌──────────────┐ ┌──────────────┐  │
│  │  Layer   │ │ Compositor│ │ Video / Live │ │ Lua Scene    │  │
│  │ Manager  │ │  + PostFX │ │ FFmpeg pipe  │ │  Scripts     │  │
│  └──────────┘ └───────────┘ └──────────────┘ └──────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│     Audio Engine — real-time thread, lock-free SPSC ringbufs    │
│  ┌──────────┐ ┌───────────┐ ┌────────────┐ ┌─────────────────┐ │
│  │PortAudio │ │   Clip    │ │Arrangement │ │   Metronome     │ │
│  │ Callback │ │  Engine   │ │ Playback   │ │                 │ │
│  └──────────┘ └───────────┘ └────────────┘ └─────────────────┘ │
│  ┌──────────┐ ┌───────────┐ ┌────────────┐ ┌─────────────────┐ │
│  │  Mixer   │ │  Effects  │ │Instruments │ │   Automation    │ │
│  │ /Router  │ │  Chains   │ │  (Synths)  │ │  Engine + LFO   │ │
│  └──────────┘ └───────────┘ └────────────┘ └─────────────────┘ │
│  ┌──────────┐ ┌───────────┐ ┌────────────┐ ┌─────────────────┐ │
│  │  MIDI    │ │   Time    │ │ Transient  │ │  Ableton Link   │ │
│  │  Engine  │ │ Stretcher │ │ Detector   │ │  (LAN sync)     │ │
│  └──────────┘ └───────────┘ └────────────┘ └─────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

**Thread model:** UI thread (SDL main loop) + Audio thread (PortAudio callback). Communication is entirely via lock-free SPSC ring buffers — no mutexes or allocations on the audio thread. We asked the AI to explain lock-free programming and it wrote a 200-line ring buffer. We asked it again and it wrote a different 200-line ring buffer. Both passed tests. We don't ask questions anymore.

**Audio signal flow:**
```
                    ┌─────────────┐
 Audio Input ──────→│  Recording  │──→ Recorded Audio/MIDI Data
                    └─────────────┘
                          │
 MIDI Input ──────────────────→ MIDI Effect Chain → Instrument → Track Buffer
 Controller (Lua) ─── notes ──→↑         params ──→ Device Parameters
                                                    ↓
 Clip Engine (session) ──────────────────→ Track Buffer (summed)
          or                                        ↓
 Arrangement Playback (timeline) ────────→ Track Buffer (per-track S/A)
                                                    ↓
           Time Stretcher (WSOLA/PhaseVocoder) ────→↓
                                                    ↓
 Track Fader/Pan/Mute/Solo → Sends → Return Buses → Master Output
                                                        ↓
 Automation Engine (envelopes + LFOs) ────────→ Parameter modulation
                                                        ↓
                                               Metronome (added)
```

See also: [UI framework architecture](ui-v2-architecture.md) · [Project structure](project-structure.md).
