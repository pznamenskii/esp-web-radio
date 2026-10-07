# Troubleshooting: audio crackle/slow clicks after visualizer (spectrum_tap)

**Status:** Diagnosed + fixed (awaiting on-device verification).
**Date:** 2026-10-07
**ESPHome build-host version:** 2026.9.1 (HA ESPHome Device Builder, remote).
**Affected code:** [`custom_components/spectrum_tap/speaker/spectrum_tap_speaker.h`](../custom_components/spectrum_tap/speaker/spectrum_tap_speaker.h)

---

## 1. Symptom

After inserting the `spectrum_tap` custom speaker between the mixer and the I2S DAC
(per [`plans/completed_visualizer.md`](completed_visualizer.md)):

- Audio became "slowed-down clicks and crackling" instead of music.
- The 16-bar visualizer animates, but the bars cannot be correlated with what is heard.

Visualizer works **because it taps the correct PCM before the DAC**, while the DAC itself
is configured at the wrong clock/format.

---

## 2. Audio chain (ESPHome 2026.9.1, new task-based architecture)

```
media_player (speaker platform)
  -> main_media_resampler   (ResamplerSpeaker: own FreeRTOS task, Core-agnostic, prio 1)
    -> media_mixer_input    (SourceSpeaker, ring buffer)
      -> main_mixer_speaker (MixerSpeaker: own FreeRTOS task "mixer", prio 10)
        -> spectrum_tap_speaker  (our passthrough, play() tap for FFT)   <-- INSERTED
          -> i2s_speaker         (I2SAudioSpeaker: own task "speaker_task" drains ring -> DMA)
            -> PCM5102A DAC
```

Key differences vs. the pre-2025 "Speaker" mixer:

- `MixerSpeaker` and `SourceSpeaker` live in `esphome/components/mixer/speaker/`;
  `ResamplerSpeaker` in `esphome/components/resampler/speaker/`.
- The mixer is **not** a `Speaker` — it is a `Component` with a dedicated `audio_mixer_task`
  that pulls from SourceSpeaker ring buffers and writes to `output_speaker_->play(...)`.
- `ResamplerSpeaker` also has its own `resample_task`.
- The media pipelines feed **SourceSpeakers**, not the mixer itself.

---

## 3. Root cause

**`audio_stream_info_` no longer reaches the `i2s_speaker`.**

### How stream info propagates in 2026.9.1

1. `MixerSpeaker::start(audio::AudioStreamInfo &stream_info)` does
   `output_speaker_->set_audio_stream_info({bits, channels, source_rate})`
   — only on its **direct** output speaker.
   - Before the visualizer: that was `i2s_speaker` → DAC got `{16, 2, 48000}`.
   - After the visualizer: that is **`spectrum_tap_speaker`** → the tap gets `{16, 2, 48000}`
     (confirmed by logs), but `i2s_speaker` is never updated.

2. `I2SAudioSpeakerBase::play()` auto-starts the speaker when not running and its
   `loop()` then calls `start_i2s_driver(this->audio_stream_info_)`.

3. `start_i2s_driver()` uses `audio_stream_info_` for:
   - `clk_cfg.sample_rate_hz` (the I2S clock!),
   - slot mode (mono/stereo),
   - ring buffer / DMA sizing.

4. `audio::AudioStreamInfo` default constructor = **`{16 bit, 1 channel, 16000 Hz}`**.

Therefore on first playback the DAC is configured at **16 kHz mono** while the whole
upstream chain delivers **48 kHz stereo**. The DMA/ring buffer saturates ~3x faster than
the DAC consumes it → blocking, choppy, "slow clicks and crackle". The FFT tap reads the
correct 48 kHz stereo PCM *before* the misconfigured DAC, so the visualizer tracks the real
music — hence the "can't correlate" feeling.

### Validating evidence (on-device logs)

```
[DIAG] tap stream: 2ch x 16bit @ 48000Hz | i2s output stream: 1ch x 16bit @ 16000Hz
[speaker_task]: Created ring buffer with size 16000        <- sized for 16 kHz mono!
[i2s_audio.speaker:070]: Starting                          <- starts with DEFAULT stream info
```

Also relevant: `Decoded audio has 2 channels, 44100 Hz sample rate` → resampler upconverts
to 48 kHz → mixer outputs 48 kHz stereo → tap. Band tables in `spectrum.cpp` are correct
for 48 kHz.

---

## 4. Why `register_speaker()` doesn't help

In `esphome/components/speaker/__init__.py` (2026.9.1):

```python
SPEAKER_SCHEMA = cv.Schema.extend(audio.AUDIO_COMPONENT_SCHEMA).extend({...})
async def register_speaker(var, config):
    # only sets audio_dac; NO bits_per_sample / channels / sample_rate anymore
```

The YAML keys `bits_per_sample / num_channels / sample_rate` are accepted by the schema
(via `AUDIO_COMPONENT_SCHEMA`) but **are not applied to the C++ object** by the base
registration. The tap's own `audio_stream_info_` is therefore populated at runtime by the
mixer, not at boot from YAML. (The i2s_audio speaker gets its own config via
`register_i2s_audio_component` → `set_sample_rate(...)`, `set_slot_mode(...)` etc., but its
`audio_stream_info_` is still only set externally.)

---

## 5. The fix

In [`spectrum_tap_speaker.h`](../custom_components/spectrum_tap/speaker/spectrum_tap_speaker.h):

- Added `sync_output_stream_info_()`: before forwarding each `play()`, if the output
  speaker's `audio_stream_info_` differs from the tap's, call
  `output_->set_audio_stream_info(mine)`. This guarantees `i2s_speaker` is configured
  `{16, 2, 48000}` **before** its auto-start reads it.
  - Cheap: the comparison only fires on an actual change (once per session normally).
- Kept the one-shot `[DIAG]` log (`log_stream_info_once_()`), now called **after** the sync,
  so a healthy boot shows both sides equal.

Healthy post-fix log line:

```
[DIAG] tap stream: 2ch x 16bit @ 48000Hz | i2s output stream: 2ch x 16bit @ 48000Hz
[speaker_task]: Created ring buffer with size 48000          <- sized for 48 kHz stereo
```

The mixer's own rate-switch branch also calls `output_speaker_->start()` / `finish()`; the
tap's `start()` already forwards `this->audio_stream_info_`, and `play()`-time sync covers
the auto-start path that bypasses `start()`.

---

## 6. If crackling persists after this fix

Secondary hypothesis (weaker): **CPU contention on Core 1.** In 2026.9.1 the
`speaker_task` of `i2s_audio` is created from `loop()` (Core 1, main loop) via plain
`xTaskCreate`, and shares the core with: LVGL rendering (now +16 animated bars),
`spectrum_compute()` (up to 375 FFTs/s), mixer/resampler loops. Signs to watch:

- `Loop Time` sensor / `HEARTBEAT` diag line regressing significantly,
- `lvgl took a long time for an operation` warnings during playback.

Mitigations if needed (from [`completed_visualizer.md`](completed_visualizer.md) §knobs):
raise `viz_animation` interval (40 ms → 60 ms), or reduce LVGL work while playing.

---

## 7. Key gotchas recorded for future work

| # | Gotcha |
|---|---|
| 1 | In ESPHome ≥2025, mixer/resampler are **Components with their own tasks**, not Speakers in the play() chain. |
| 2 | `set_audio_stream_info` / `get_audio_stream_info` are non-virtual on `speaker::Speaker`; stream info flows only to the *direct* output speaker. |
| 3 | `audio::AudioStreamInfo` default = **16 kHz mono 16-bit**; an unconfigured downstream speaker silently configures the DAC at 16 kHz. |
| 4 | `i2s_audio` speaker auto-starts on its first `play()` using *its own* `audio_stream_info_`. |
| 5 | Base `speaker.register_speaker()` no longer applies `bits_per_sample/channels/sample_rate` from YAML to C++. |
| 6 | A passthrough speaker between mixer and DAC **must re-sync stream info downstream**, otherwise it breaks the DAC config while leaving FFT data intact. |
| 7 | The double-buffer FFT race (`write_buf_`/`sample_ready` without atomics) can tear bands visually but does not corrupt forwarded audio. |

## 8. Relevant ESPHome 2026.9.1 source references

- `esphome/components/audio/audio.h` — `AudioStreamInfo` (default ctor `(16, 1, 16000)`).
- `esphome/components/speaker/__init__.py` — `SPEAKER_SCHEMA` / `register_speaker` (no stream info).
- `esphome/components/mixer/speaker/mixer_speaker.cpp` — `MixerSpeaker::start()`, `audio_mixer_task`.
- `esphome/components/resampler/speaker/resampler_speaker.cpp` — `ResamplerSpeaker::start_()`, `resample_task`.
- `esphome/components/i2s_audio/speaker/i2s_audio_speaker.cpp` — `play()` auto-start, `loop()` → `start_i2s_driver()`.
- `esphome/components/i2s_audio/speaker/i2s_audio_speaker_standard.cpp` — `start_i2s_driver()` uses `audio_stream_info_` for clock/slot.

---

## 9. Follow-up: visualizer "first half always at maximum" (2026-10-07)

**Symptom:** bars of bands 0–7 (~375 Hz – 3.75 kHz) permanently pinned at 70 px; only the upper bands visibly move.

**Root cause:** gain saturation, not the frequency range. [`spectrum_viz.cpp`](../custom_components/spectrum_viz/spectrum_viz.cpp) used
`target = sqrtf(spec_bands[b]) * 300.0f` on raw, unnormalized FFT power:

- Full-scale sine on one bin: `|X_k| ≈ A/2 * sum(Hann) ≈ A * 64` (N=128) → `sqrt(power) * 300 ≈ 19200 * A`.
- A bar hits the 70 px clamp already at `A ≈ 0.0036` (**−48 dBFS**). Any audible bass/melody pins the
  energy-heavy low bands, and the 0.85/tick decay keeps them up for ~1 s. The upper bands (4–18 kHz) carry
  less program energy, so they keep moving.

**Fix applied in [`spectrum_viz.cpp`](../custom_components/spectrum_viz/spectrum_viz.cpp) `tick()`:**

- Normalize: `amp = sqrtf(spec_bands[b]) / 64.0f` (per-band amplitude, 0..~1; full-scale tone reads ≈ −6 dBFS).
- dB compression: `db = 20 * log10f(amp + 1e-5f)`, `height = 70 * (1 - db / -50.0f)`, clamped to [2, 70].
- Attack 0.5 / decay 0.85 smoothing untouched.

Expected spread on typical music: lows ~40–70 px, highs ~10–40 px; nothing pinned unless genuinely near
0 dBFS. A pure upward range shift was explicitly **rejected**: it relocates the saturation instead of fixing it.

**Validation log:** one `[VIZ] bars(px): <16 values>` line per 5 s (125 ticks × 40 ms), emitted only while the
`viz_animation` interval actually runs (strip visible) — no spam when paused/idle. **Temporary:** remove
`log_tick_` / `log_bands_` members (in `spectrum_viz.h`) and the `ESP_LOGI` block once confirmed.

**Tuning knob:** `WINDOW_COHERENT_GAIN = 64.0f` → `32.0f` makes the display hotter (full-scale tone → 0 dBFS → 70 px).