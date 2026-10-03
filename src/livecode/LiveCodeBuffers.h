#pragma once

// LiveCodeBuffers — programmatic sample creation + manipulation for the
// live-coding layer (docs/live-coding.md §4.5). Lua-facing API:
//
//   yawn.new_buffer{ frames = n, channels = c,
//                    fill = fn(frame, ch, frames, channels)  -- per sample
//                  | data = { {ch1...}, {ch2...} } }         -- or copied in
//              -> handle
//   yawn.buffer_data(handle) -> { frames, channels, sr, data = {...} }
//   yawn.buffer_info(handle) -> { frames, channels, sr }
//   yawn.free_buffer(handle) -> bool
//
//   (all *in place*, returning the same handle for chaining)
//   yawn.buffer_gain(handle, g)
//   yawn.buffer_normalize(handle, peak)
//   yawn.buffer_fade(handle, inFrames, outFrames)
//   yawn.buffer_mix(dst, src, level)     -- dst += src * level
//   yawn.buffer_reverse(handle)
//
//   (new handles)
//   yawn.buffer_slice(handle, first, len)
//   yawn.buffer_concat(a, b)             -- frame-concat; needs same channels
//   yawn.buffer_repeat(handle, n)
//   yawn.buffer_mixdown(handle)          -- channels averaged to 1
//
//   yawn.fft(table)   -> interleaved {re, im, re, im, ...} (radix-2, n = #t)
//   yawn.ifft(table)  -> reals { ... }   (1/n scaled inverse)
//   yawn.polyblep(phase, dt) -> polyBLEP-2 discontinuity correction
//
// Handles live in the manager's registry (shared with load_audio_file /
// yawn.render results) — everything load_sample/render/save_audio_buffer
// consume works on hand-forged buffers too.

struct lua_State;

namespace yawn {
namespace livecode {

// Appends the buffer/synthesis functions into the existing global `yawn`
// table (called from LiveCodeEngine::registerAPI after yawn is created).
void registerBufferAPI(lua_State* L);

} // namespace livecode
} // namespace yawn
