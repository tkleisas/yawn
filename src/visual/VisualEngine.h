#pragma once

// VisualEngine — manages the secondary "output" SDL3 window and the visual
// render pipeline that feeds it.
//
// Architecture (Phase D.0, per-track layers):
//   • Creates a second SDL window at app startup, hidden by default.
//   • Its GL context shares textures/shaders/FBOs with the main UI context.
//   • Each visual track owns a Layer: its own 640×360 FBO, shader program,
//     per-layer iTime/iFrame, and audio source for bands/kick/level.
//   • tick() renders each active layer into its own FBO, then composites
//     them back-to-front into the output window using mixer track volume
//     as layer opacity. Two visual tracks = cross-fader; N tracks = stack.
//
// Thread model: everything runs on the main (UI) thread. The audio thread
// never touches GL. Video decode (later) will push frames via an SPSC queue.

#include <SDL3/SDL.h>
#include <glad/gl.h>

#include "visual/VisualLFO.h"
#include "visual/TextRasterizer.h"
#include "visual/VideoDecoder.h"
#include "visual/LiveVideoSource.h"
#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D
#include "visual/gltf/M3DRenderer.h"
#include "visual/gltf/M3DSceneScript.h"
#endif

#include <memory>

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <chrono>

namespace yawn { namespace audio { class AudioEngine; } }

namespace yawn {
namespace visual {

class VisualEngine {
public:
    // Blend mode used when this layer composites onto the accumulator.
    // Mirrors Track::VisualBlendMode; kept as a separate type so visual/
    // doesn't depend on app/Project.h.
    enum class BlendMode : int { Normal = 0, Add = 1, Multiply = 2, Screen = 3 };

    VisualEngine() = default;
    ~VisualEngine();

    VisualEngine(const VisualEngine&) = delete;
    VisualEngine& operator=(const VisualEngine&) = delete;

    bool init();
    void shutdown();

    void setOutputVisible(bool visible);
    bool isOutputVisible() const { return m_outputVisible; }

    void setFullscreen(bool fullscreen);
    bool isFullscreen() const { return m_fullscreen; }

    // Render one visual frame. Safe to call when the window is hidden.
    void tick(double transportSeconds, double transportBeats, bool playing);

    SDL_Window* outputWindow() const { return m_outputWindow; }

    // Borrowed pointer to the audio engine; VisualEngine reads mixer state
    // each frame (peakL/peakR, band peaks, sample tap). Safe under the same
    // torn-read convention as YAWN's existing meter display.
    void setAudioEngine(const audio::AudioEngine* eng);

    // Per-track layer API. loadLayer() ensures a layer exists for that
    // track, loads the shader, and sets the audio source used by
    // iAudioLevel/iAudioLow/Mid/High/iKick for this layer. Returns false
    // if the shader fails to compile — any pre-existing layer keeps its
    // last-good program so live reloads don't break the show.
    bool loadLayer(int track, const std::string& shaderPath, int audioSource);

    // Replace the layer's chain of *additional* shader passes (passes
    // 1..N — pass 0 is whatever loadLayer() compiled). Each entry's
    // shaderPath is compiled + cached; saved paramValues are applied
    // by name. Pass an empty vector to clear the chain (single-pass
    // mode). Returns false if any pass fails to compile — the layer
    // keeps its previous chain to avoid breaking a running show.
    struct ChainPassSpec {
        std::string shaderPath;
        std::vector<std::pair<std::string, float>> paramValues;
        bool bypassed = false;
    };
    bool setLayerAdditionalPasses(int track,
                                   const std::vector<ChainPassSpec>& passes);

    // Cheap per-pass param setter — updates the cached value the next
    // render reads, no recompile. Use this on every knob delta during
    // a chain-knob drag; falling back to setLayerAdditionalPasses() on
    // a knob turn forces a full relink/program-rebuild and recompiles
    // the source pass too, which murders the frame budget. No-op if
    // the track / pass / param name doesn't resolve.
    void setLayerChainPassParam(int track, int passIdx,
                                  const std::string& name, float value);

    // Cheap bypass toggle — flips the per-pass bypassed flag without
    // touching GL state. The render loop skips bypassed passes and
    // threads the previous output through, so toggling on a chain
    // pass is instant whether on or off (no recompile). No-op on
    // bad track / pass index.
    void setLayerChainPassBypass(int track, int passIdx, bool bypassed);

    void setLayerAudioSource(int track, int audioSource);
    void setLayerBlendMode(int track, BlendMode mode);
    void setLayerText(int track, const std::string& text);
    // Attach or detach a video file to the layer. Empty path → no video
    // (iChannel2 reverts to the dummy black texture). Returns false if
    // the file couldn't be opened.
    bool setLayerVideo(int track, const std::string& path);
    // Attach or detach a live video source (webcam, RTSP, …). Empty
    // url → no live input. Live and file video are mutually exclusive;
    // setting one tears down the other.
    bool setLayerLiveInput(int track, const std::string& url);
    // Returns LiveVideoSource::State::Stopped if no layer exists or the
    // layer has no live source currently attached. Used by the session
    // panel to paint a connection-status pip on live clips.
    LiveVideoSource::State getLayerLiveState(int track) const;
    // Last-seen error message for the layer's live source, or empty
    // if no live source is attached / nothing failed yet.
    std::string getLayerLiveError(int track) const;

    // Attach or detach a 3D model to the layer. Mutually exclusive with
    // file video and live video — setting one tears down the others.
    // Empty path clears the model.
    bool setLayerModel(int track, const std::string& path);

    // Pick the clock driving iTime / animTime / video frame advance on
    // a layer. Session-grid clips default to wall-clock (iTime starts
    // at 0 on launch, advances with real time). Arrangement-placed
    // clips should use the transport clock so scrubbing the playhead
    // actually seeks the visuals — iTime = transportBeats*60/bpm -
    // clipStartBeat*60/bpm, which follows the playhead both forward
    // and backward.
    void setLayerWallClock(int track);
    void setLayerTransportClock(int track, double clipStartBeat);

    // Attach or detach a Lua scene script. Only meaningful once a
    // model has been set — the script supplies transforms for the
    // model each frame. Empty path clears any existing script.
    bool setLayerSceneScript(int track, const std::string& path);
    // Per-clip video timing: loopBars=0 → native-rate free-run (loops when
    // source ends). loopBars>0 → stretch playback so the clip loops at
    // exactly N bars of transport time. rate is an additional multiplier
    // (1.0 = no change); applied in both modes.
    void setLayerVideoTiming(int track, int loopBars, float rate);
    // In/out fractions (0..1) restricting the playable range of the clip.
    void setLayerVideoTrim(int track, float inFrac, float outFrac);
    void clearLayer(int track);

    // True if a visual layer has at some point had a shader loaded on it.
    bool hasLayer(int track) const;

    // Shader parameter introspection — populated on each loadLayer() by
    // scanning the source for `uniform float NAME;` declarations and their
    // optional `// @range min..max default=N` annotations.
    struct LayerParamInfo {
        std::string name;
        float value;
        float min;
        float max;
        float defaultValue;
    };

    // Parse a shader file from disk and return its @range uniforms
    // without loading a layer / GL program. Lets the Detail panel show
    // the params before the clip has ever been launched.
    // Returns value = defaultValue for each entry.
    static std::vector<LayerParamInfo>
        parseShaderFileParams(const std::string& shaderPath);

    // ── Post-FX chain (master) ─────────────────────────────────────────
    // Ordered list of post-process effects applied to the composited
    // output. Each effect's fragment shader samples `iPrev` (previous
    // stage) and writes the next stage. First effect reads the compositor
    // output; last effect's output is blitted to the window.
    bool addPostFX(const std::string& shaderPath);
    void removePostFX(int index);
    void movePostFX(int index, int delta);    // delta = -1 up, +1 down
    int  numPostFX() const { return static_cast<int>(m_postFX.size()); }
    std::string postFXPath(int index) const;

    // Per-post-fx parameter introspection + mutation.
    std::vector<LayerParamInfo> getPostFXParams(int index) const;
    void setPostFXParam(int index, const std::string& name, float value);
    void applyPostFXParamValues(int index,
        const std::vector<std::pair<std::string, float>>& values);

    std::vector<LayerParamInfo> getLayerParams(int track) const;
    void setLayerParam(int track, const std::string& name, float value);
    // Look up a single source-pass param's natural [min, max] without
    // copying the full param vector. Returns false (and leaves the
    // out-args untouched) when the layer / param doesn't resolve.
    // Used by the macro mapping evaluator to unnormalise its 0..1
    // sub-range output into the param's actual range.
    bool getLayerParamRange(int track, const std::string& name,
                              float* outMin, float* outMax) const;
    // Same idea for a chain-pass param at the given pass index.
    bool getLayerChainPassParamRange(int track, int passIdx,
                                       const std::string& name,
                                       float* outMin, float* outMax) const;
    // Apply saved name→value pairs (by name; missing names keep their default).
    void applyLayerParamValues(int track,
                                const std::vector<std::pair<std::string, float>>& values);

    // Generic A..H knobs (0..1). Index 0 = A, 7 = H.
    static constexpr int kNumKnobs = 8;
    float getLayerKnob(int track, int idx) const;
    void  setLayerKnob(int track, int idx, float value);

    // Per-knob LFO (one per A..H slot). By default disabled.
    const VisualLFO* getLayerKnobLFO(int track, int idx) const;
    void  setLayerKnobLFO(int track, int idx, const VisualLFO& lfo);
    // Most recent evaluated (base+LFO) knob value — for UI feedback only.
    float getLayerKnobDisplayValue(int track, int idx) const;

private:
    struct ContextScope {
        SDL_Window*   prevWindow  = nullptr;
        SDL_GLContext prevContext = nullptr;
        bool          restored    = false;
        ContextScope(SDL_Window* newWin, SDL_GLContext newCtx);
        ~ContextScope();
    };

    // Shared shader-parameter descriptor used by both layers and post-FX.
    struct Param {
        std::string name;
        float value;
        float min;
        float max;
        float defaultValue;
        GLint location = -1;
    };

    // ── Per-track layer ────────────────────────────────────────────────
    struct Layer {
        GLuint  fbo    = 0;
        GLuint  fboTex = 0;
        GLuint  program = 0;
        std::string                      shaderPath;
        std::filesystem::file_time_type  mtime{};
        bool                             mtimeValid = false;

        // Per-layer wall-clock origin so each layer's iTime is independent.
        std::chrono::steady_clock::time_point wallStart =
            std::chrono::steady_clock::now();
        double lastWallSeconds = 0.0;
        int    frameCounter    = 0;
        // When true, iTime / animTime / video advance are driven by
        // the transport clock (beats → seconds via current BPM) with
        // the given clip start beat as origin. False = wall-clock
        // (session-grid launch default).
        bool   transportDriven = false;
        double clipStartBeat   = 0.0;

        // Per-layer audio source (-1 = master).
        int       audioSource = -1;
        BlendMode blendMode   = BlendMode::Normal;
        float smoothedLevel    = 0.0f;
        float smoothedLow      = 0.0f;
        float smoothedMid      = 0.0f;
        float smoothedHigh     = 0.0f;
        float kickBaseline     = 0.0f;
        float kickLevel        = 0.0f;
        float kickRefractory   = 0.0f;
        float kickLastLow      = 0.0f;

        // Uniform locations — refreshed whenever the shader is recompiled.
        GLint loc_iResolution        = -1;
        GLint loc_iTime              = -1;
        GLint loc_iTimeDelta         = -1;
        GLint loc_iFrame             = -1;
        GLint loc_iMouse             = -1;
        GLint loc_iDate              = -1;
        GLint loc_iSampleRate        = -1;
        GLint loc_iBeat              = -1;
        GLint loc_iTransportPlaying  = -1;
        GLint loc_iTransportTime     = -1;
        GLint loc_iAudioLevel        = -1;
        GLint loc_iAudioLow          = -1;
        GLint loc_iAudioMid          = -1;
        GLint loc_iAudioHigh         = -1;
        GLint loc_iKick              = -1;
        GLint loc_iChannel[4]        = {-1, -1, -1, -1};
        GLint loc_iChannelResolution = -1;
        GLint loc_iChannelTime       = -1;
        GLint loc_iTextWidth         = -1;
        GLint loc_iTextTexWidth      = -1;
        // iFeedback — previous frame's chain output. Allocated lazily
        // (see feedbackTex below) when any pass actually declares it,
        // so layers without echo/feedback shaders don't pay the
        // per-frame copy cost.
        GLint loc_iFeedback          = -1;

        // 8 always-available generic knobs (knobA..knobH). Default 0.5 so
        // shaders that don't set them explicitly sit at the centre of the
        // 0..1 range. Matches hardware encoder banks like Push / Move / APC.
        float knobValues[8]  = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
        GLint loc_knobs[8]   = {-1, -1, -1, -1, -1, -1, -1, -1};

        // One optional LFO per knob slot. Modulates the knob's base value
        // on top, clamped to [0, 1]. Evaluated on the UI thread each frame.
        VisualLFO knobLFOs[8];

        // Cache of the most-recently-evaluated (base+LFO) knob value, so the
        // UI can render an arc that "breathes" with the modulation.
        float knobDisplayValues[8] = {0.5f, 0.5f, 0.5f, 0.5f,
                                       0.5f, 0.5f, 0.5f, 0.5f};

        // Text strip texture (R8). Bound to iChannel1. Re-rasterized only
        // when the text string actually changes.
        GLuint       textTex         = 0;
        std::string  currentText;
        int          textPixelW      = 0;  // rendered pixel width (for shader scale)
        bool         textDiagLogged  = false;  // diagnostic one-shot

        // Video playback (bound to iChannel2).
        std::unique_ptr<VideoDecoder> video;
        std::string  videoPath;
        GLuint       videoTex        = 0;
        int          lastVideoFrame  = -1;
        // Live video source (mutually exclusive with `video`). When
        // present, replaces the file-decode upload path each frame with
        // a pull from the LiveVideoSource's latest ready frame.
        std::unique_ptr<LiveVideoSource> liveVideo;
        std::string  liveUrl;

        // 3D model renderer (also mutually exclusive with video /
        // liveVideo — they all feed iChannel2). When present, its
        // colorTexture() is bound instead of videoTex.
#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D
        std::unique_ptr<M3DRenderer>    modelRenderer;
        // Optional Lua scene script. When present, takes over from the
        // @range-uniform-driven static-transform path — its tick()
        // return value becomes the list of instances to draw each frame.
        std::unique_ptr<M3DSceneScript> sceneScript;
        std::string                     scenePath;
        // Integrated spin from the modelSpinX/Y/Z uniforms. Accumulates
        // while the layer is alive, wrapped mod 360 so floats don't lose
        // precision across long sessions.
        float        modelSpinAccum[3] = { 0.0f, 0.0f, 0.0f };
#endif
        std::string  modelPath;
        std::chrono::steady_clock::time_point videoLaunchTime =
            std::chrono::steady_clock::now();
        // Transport-locked loop: 0 = free-running. >0 = loop every N bars.
        int          videoLoopBars   = 0;
        // Playback rate multiplier on top of the selected mode.
        float        videoRate       = 1.0f;
        // In/out fractions inside the source (0..1).
        float        videoIn         = 0.0f;
        float        videoOut        = 1.0f;
        // Transport-beat position at launch (for bar-sync mode).
        double       videoLaunchBeats = 0.0;

        // User-declared shader parameters (bound via glUniform1f each frame).
        std::vector<Param> params;

        // ── Shader chain — additional passes after pass 0 ─────────────
        // Pass 0 lives in the legacy fields above (program, shaderPath,
        // mtime, loc_*, params) so single-pass clips render identically
        // to the pre-chain code path. Each ChainPass below contributes
        // one extra ping-pong stage that reads the previous output via
        // sampler2D iPrev (black for pass 0) plus the same iChannel0..3
        // sources as pass 0. The final pass writes into L.fbo so the
        // composite stage downstream is unchanged.
        struct ChainPass {
            GLuint program = 0;
            std::string shaderPath;
            std::filesystem::file_time_type mtime{};
            bool mtimeValid = false;
            // When true, render loop skips this pass and threads
            // iPrev straight to the next active pass. Toggleable
            // cheaply via setLayerChainPassBypass — no recompile.
            bool bypassed = false;

            // Same uniform location cache layout as Layer — chain
            // passes can use every standard uniform pass 0 can, plus
            // iPrev for the previous-pass output texture.
            GLint loc_iResolution        = -1;
            GLint loc_iTime              = -1;
            GLint loc_iTimeDelta         = -1;
            GLint loc_iFrame             = -1;
            GLint loc_iMouse             = -1;
            GLint loc_iDate              = -1;
            GLint loc_iSampleRate        = -1;
            GLint loc_iBeat              = -1;
            GLint loc_iTransportPlaying  = -1;
            GLint loc_iTransportTime     = -1;
            GLint loc_iAudioLevel        = -1;
            GLint loc_iAudioLow          = -1;
            GLint loc_iAudioMid          = -1;
            GLint loc_iAudioHigh         = -1;
            GLint loc_iKick              = -1;
            GLint loc_iChannel[4]        = {-1, -1, -1, -1};
            GLint loc_iChannelResolution = -1;
            GLint loc_iChannelTime       = -1;
            GLint loc_iTextWidth         = -1;
            GLint loc_iTextTexWidth      = -1;
            GLint loc_iPrev              = -1;
            GLint loc_iFeedback          = -1;
            GLint loc_knobs[8]           = {-1, -1, -1, -1, -1, -1, -1, -1};

            std::vector<Param> params;
        };
        std::vector<ChainPass> additionalPasses;

        // iPrev location on pass 0 — defaults to a dummy black texture
        // so pass-0-only shaders that happen to declare iPrev still
        // work without special-casing.
        GLint loc_iPrev = -1;

        // Ping-pong FBOs used when additionalPasses.size() >= 1.
        // Allocated lazily in renderLayerToFBO and freed by clearLayer.
        // Sized to match the layer's main FBO (640×360).
        GLuint pingFBO[2] = {0, 0};
        GLuint pingTex[2] = {0, 0};

        // Persistent feedback texture — holds the previous frame's
        // final chain output. Lazily allocated only when at least one
        // pass declares `uniform sampler2D iFeedback`, so non-feedback
        // layers don't pay the per-frame VRAM copy. Re-uploaded each
        // frame via glCopyTexSubImage2D from L.fbo at the end of
        // renderLayerToFBO.
        GLuint feedbackTex = 0;
    };
    std::unordered_map<int, Layer> m_layers;

    // ── Post-process effect ────────────────────────────────────────────
    struct PostEffect {
        GLuint      program = 0;
        std::string shaderPath;
        std::filesystem::file_time_type mtime{};
        bool        mtimeValid = false;

        int    frameCounter    = 0;
        double lastWallSeconds = 0.0;

        // Uniform locations
        GLint loc_iResolution       = -1;
        GLint loc_iTime             = -1;
        GLint loc_iTimeDelta        = -1;
        GLint loc_iFrame            = -1;
        GLint loc_iBeat             = -1;
        GLint loc_iTransportTime    = -1;
        GLint loc_iTransportPlaying = -1;
        GLint loc_iAudioLevel       = -1;
        GLint loc_iAudioLow         = -1;
        GLint loc_iAudioMid         = -1;
        GLint loc_iAudioHigh        = -1;
        GLint loc_iKick             = -1;
        GLint loc_iPrev             = -1;

        std::vector<Param> params;
    };
    std::vector<PostEffect> m_postFX;

    // Per-frame master-audio envelope state for post-fx.
    float m_postSmoothedLevel    = 0.0f;
    float m_postSmoothedLow      = 0.0f;
    float m_postSmoothedMid      = 0.0f;
    float m_postSmoothedHigh     = 0.0f;
    float m_postKickBaseline     = 0.0f;
    float m_postKickLevel        = 0.0f;
    float m_postKickRefractory   = 0.0f;
    float m_postKickLastLow      = 0.0f;

    bool compileShaderForPostFX(PostEffect& pe, const std::string& userSrc,
                                  const std::string& label);
    void cachePostFXUniformLocations(PostEffect& pe);
    void destroyPostFX(PostEffect& pe);
    void checkPostFXHotReload(PostEffect& pe);

    // Layer helpers.
    Layer& ensureLayer(int track);
    bool   buildFBO(Layer& L);
    // Allocate the per-frame feedback texture if needed. Returns true
    // on success (or if already allocated). Idempotent.
    bool   ensureFeedbackTex(Layer& L);
    bool   compileShaderForLayer(Layer& L, const std::string& userSrc,
                                  const std::string& sourceLabel);
    void   cacheUniformLocations(Layer& L);
    // Shader-chain helpers — compile / cache uniforms for an additional
    // pass beyond pass 0. Same shader preamble + param parsing as
    // compileShaderForLayer; live-reload preserves params by name.
    bool   compileChainPass(Layer::ChainPass& cp,
                              const std::string& userSrc,
                              const std::string& sourceLabel,
                              const std::vector<std::pair<std::string,
                                                          float>>& savedValues);
    void   cacheChainPassUniformLocations(Layer::ChainPass& cp);
    // Lazily allocate the two ping-pong FBO+textures used when a layer
    // has additionalPasses. No-op once they exist; no-op on layers
    // that stay single-pass.
    bool   ensurePingFBOs(Layer& L);
    void   checkLayerHotReload(Layer& L);
    void   renderLayerToFBO(Layer& L, double transportSeconds,
                             double transportBeats, bool playing);
    float  readLayerLevel(Layer& L);
    struct AudioBands { float low, mid, high; };
    AudioBands readLayerBands(Layer& L);
    float  readLayerKick(Layer& L, float tDelta);
    void   destroyLayer(Layer& L);

    // Reads the mixer track peak (max of L/R) for this source. -1 = master.
    float rawPeakFor(int source) const;
    struct RawBands { float low, mid, high; };
    RawBands rawBandsFor(int source) const;

    // Push the union of all layers' audio sources to the mixer so it can
    // gate band analysis to only those channels we're going to read.
    void updateAudioWiring();

    // ── Shared GL ──────────────────────────────────────────────────────
    bool createOutputWindowAndContext();
    bool createBlitProgram();         // no-alpha FBO→window (first layer)
    bool createCompositeProgram();    // src-alpha-blended FBO→window (subsequent)
    bool createDummyChannels();
    GLuint compileShaderProgram(const char* vertSrc, const char* fragSrc,
                                 const char* name);
    void updateAudioTexture();

    SDL_Window*   m_outputWindow  = nullptr;
    SDL_GLContext m_outputContext = nullptr;

    static constexpr int kInternalWidth  = 640;
    static constexpr int kInternalHeight = 360;

    GLuint m_vao = 0;

    GLuint m_blitProgram   = 0;
    GLint  m_blit_tex_loc  = -1;

    // Composite shader: reads source layer + accumulator, writes blended
    // result. Supports Normal / Add / Multiply / Screen via uMode uniform.
    GLuint m_compositeProgram  = 0;
    GLint  m_comp_src_loc      = -1;
    GLint  m_comp_dst_loc      = -1;
    GLint  m_comp_alpha_loc    = -1;
    GLint  m_comp_mode_loc     = -1;

    // Ping-pong accumulator FBOs for the compositor.
    GLuint m_accumFBO[2] = {0, 0};
    GLuint m_accumTex[2] = {0, 0};
    bool   createAccumFBOs();

    GLuint m_dummyChannelTex[4] = {0, 0, 0, 0};

    static constexpr int kAudioTexW = 512;
    static constexpr int kAudioTexH = 2;
    GLuint m_audioTex = 0;
    std::vector<unsigned char> m_audioTexPixels;

    // Per-layer text texture dimensions. 2048×64 is ~30 monospace chars at
    // size 48 — plenty for a marquee, and shaders scroll via UV wrapping.
    static constexpr int kTextTexW = 2048;
    static constexpr int kTextTexH = 64;
    TextRasterizer             m_textRasterizer;
    std::vector<unsigned char> m_textScratch;  // reused CPU R8 buffer
    void uploadLayerText(Layer& L);

    const audio::AudioEngine* m_audioEngine = nullptr;

    bool m_outputVisible = false;
    bool m_fullscreen    = false;
    bool m_initialized   = false;
};

} // namespace visual
} // namespace yawn
