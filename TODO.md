# TODO

## Audio-thread safety

- [x] **Race-free structural scene edits (UI↔audio quiesce handshake).** Fixed.
  `App::stopAllClipsForSceneEdit()` now sends the unquantized stops and
  then blocks in the new `AudioEngine::quiesceCommands()` until the audio
  thread has *consumed* them — `sendCommand()` increments an atomic
  produced-counter (release) after each successful push, and
  `processCommands()` stores the produced-count observed at the *start*
  of each drain into an atomic consumed-counter (release). A consumed
  value ≥ the produced value observed after the sends proves that drain
  popped every stop (the produced RMWs form a release sequence). The
  handshake handles the three engine states: stream stopped (no audio
  thread → return), suspended (callbacks skip the drain → wait out one
  heartbeat; the queued stops are consumed before rendering resumes),
  and running (spin, typically ~1 callback period, 250 ms timeout with
  a warning — the clip graveyard TTL remains as memory-safety backstop,
  no longer as the mechanism). Related hardening in the same pass:
  `MidiClipEngine::scheduleStop(QuantizeMode::None)` now drops the
  pending launch slot (a queued quantized clip could otherwise fire
  after the scene edit and re-point the track at a graveyarded clip —
  `ClipEngine` already did this), and `MidiClipEngine::stopNow()` nulls
  the cached `clipAutomation` pointer (`ClipEngine`'s immediate stop
  already did). Tests: `AudioEngineQuiesce.*`,
  `MidiClipEngineTest.ImmediateStopDropsQueuedPendingLaunch`,
  `MidiClipEngineTest.ImmediateStopClearsCachedAutomationPointer`.
  Relevant code: `src/audio/AudioEngine.cpp` (`sendCommand`,
  `processCommands`, `quiesceCommands`), `src/audio/MidiClipEngine.cpp`
  (`scheduleStop`, `stopNow`), `src/app/App.cpp`
  (`stopAllClipsForSceneEdit`).
