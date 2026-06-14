# TODO

## Audio-thread safety

- [ ] **Race-free structural scene edits (UI↔audio quiesce handshake).**
  `App::sceneInsert` / `sceneDelete` / `sceneDuplicate` currently call
  `stopAllClipsForSceneEdit()` (immediate `QuantizeMode::None` stops on every
  track) before reallocating `Project::m_clipSlots`. That eliminates the common
  crash but leaves a ~1-audio-callback race: the stop is delivered
  asynchronously while the UI thread frees the old slot buffer immediately, so
  an audio callback already past `processCommands` can still dereference the
  dangling `&slot->clipAutomation` in `AutomationEngine::process`.

  Close it with a real handshake — e.g. an atomic "command generation" counter
  the audio callback bumps each block, with `stopAllClipsForSceneEdit()`
  spinning/yielding on the UI thread until it advances past the block that
  processed the stops, before the mutation proceeds; or a dedicated ack routed
  back through the audio→UI event queue. Relevant code:
  `src/app/App.cpp` (`stopAllClipsForSceneEdit`),
  `src/audio/AudioEngine.cpp` (command processing / `processAudio`),
  `src/automation/AutomationEngine.cpp` (`process`).
