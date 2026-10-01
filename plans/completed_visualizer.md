# Completed: audio visualizer (spectrum_tap + 16-bar FFT display on Now Playing)

**Status:** Completed — implements TODO "Remaining improvements" item "add visualizer as per existing plan".
**Date:** 2026-10-01
**Plan:** [`plans/visualizer.md`](visualizer.md) (primary source of truth) + the referenced asset archive [`plans/spectrum_tap.zip`](spectrum_tap.zip).
**Scope:** new custom component (`custom_components/spectrum_tap/`) + speaker-pipeline rewiring + 16 LVGL bars in the existing `mp_visualizer` strip + a polling animation driver. No hardware changes, no new packages, no new fonts/glyphs, no changes to the volume/mute hooks, popup logic, progress/time hide logic, nav indicator/swipe logic, or the Info/Settings page.
**Validation without a local esphome install:** `python tests/yaml_syntax_check.py` (14/14 OK), `python tests/font_codepoint_check.py` (77/77 OK), grep cross-reference matrix for all new ids (no duplicates, every reference resolves). No `esphome` CLI was run.

---

## What the plan required vs. what was implemented

### 1. Custom speaker component `spectrum_tap` (plan §"Структура файлов" / §"C++")
The plan ships a transparent speaker passthrough that intercepts PCM in `play()`, feeds a 128-point radix-2 FFT (Hann window, band tables recalculated for **48 kHz**), and publishes 16 power bands in the global `spec_bands[]`.

**Implemented:** exactly as delivered — [`plans/spectrum_tap.zip`](spectrum_tap.zip) extracted verbatim into
`custom_components/spectrum_tap/` (repo root, matching the plan's "распаковать в папку `custom_components/`"):

```
custom_components/spectrum_tap/
├── __init__.py            # CODEOWNERS + DEPENDENCIES = ["speaker"]
├── speaker/__init__.py    # speaker-platform registration (CONF_OUTPUT_SPEAKER)
├── spectrum.h             # FFT_N=128, VIZ_BANDS=16, spec_bands extern
├── spectrum.cpp           # double-buffered capture + radix-2 FFT + 16 bands @48 kHz
├── spectrum_tap.h         # SpectrumTapSpeaker: play() tap + start/stop/volume delegation
├── spectrum_tap.cpp       # empty (all inline)
└── README.md              # component-internal docs (kept as shipped)
```

The only YAML-side additions are documented and deterministic:
- [`esp-web-radio.yaml`](../esp-web-radio.yaml) declares `external_components:` → `source: type: local, path: esp-web-radio/custom_components`, `components: [spectrum_tap]` (the `esp-web-radio/` prefix follows the repo's existing package-include convention; ESPHome would also auto-discover `custom_components/`, the explicit block just makes it deterministic).

### 2. Speaker chain rewiring (plan §"Изменения в YAML" — "единственное изменение")
The plan's exact one-line change: insert `spectrum_tap` between the mixer and the I2S DAC.

```
media_player → resampler → mixer → spectrum_tap → i2s_speaker → I2S → PCM5102A
                                         ↓
                                   feed_fft_samples()  (Core 0)
                                         ↓
                                   spectrum_compute()  (Core 1, loop)
                                         ↓
                                   spec_bands[0..15] → LVGL bars
```

**Implemented** in [`packages/esp-web-radio-audio.yaml`](../packages/esp-web-radio-audio.yaml):
- new `- platform: spectrum_tap / id: spectrum_tap_speaker / output_speaker: i2s_speaker`,
- `main_mixer_speaker.output_speaker: i2s_speaker` → `spectrum_tap_speaker`.

Nothing else in the audio package changed — the `on_play`/`on_pause`/`on_idle` triggers and the `on_volume`/`on_mute`/`on_unmute` blocks are untouched. Both pipelines (media + announcement) flow through the tap, so announcements animate the visualizer too.

### 3. LVGL rendering (plan §"Подключение LVGL-отрисовки")
The plan sketches 16 bars animated from `spec_bands[]` with attack/decay smoothing (`GAIN 300.0f`, attack `0.5`, decay `0.85`, min 2, max 200 px).

**Implemented** in [`packages/esp-web-radio-page_now_playing.yaml`](../packages/esp-web-radio-page_now_playing.yaml):
- 16 plain `obj` bars (`viz_bar_0..15`) declared as **children of the existing `mp_visualizer` strip** (fixed 472×75 slot at x=4/y=137). Each bar is absolutely positioned (w=22, pitch=28, x0=15 → right margin 15), accent-colored (`${color_accent}` at 85% opacity, radius 2, no border, `clickable: false`). The static-layout contract is preserved by construction: the bars live inside the fixed strip and hiding the parent hides them — no sibling reflow, hide/show only.
- a new `interval: viz_animation` (40 ms = 25 fps) samples `spec_bands[]` and animates bar heights bottom-anchored (`y = 75 - h - 3`, clamp h to **2..70** px to fit the 75 px strip; the plan's 200 px clamp was meant for its full-height sketch panel).

### 4. Integration with the existing show/hide triggers (required by the task)
The media-player triggers already do `lvgl.widget.show: mp_visualizer` on play and hide it on pause/idle. The driver piggybacks on exactly that:

- The interval guard is `!lv_obj_has_flag(id(mp_visualizer), LV_OBJ_FLAG_HIDDEN)` — while the strip is hidden the interval does one flag check per tick and nothing else. No changes were needed (or made) to `on_play`/`on_pause`/`on_idle`.
- A second guard (`not: lvgl.is_paused`) skips updates while the LVGL idle snow screen is up (music keeps playing then; rendering is paused).
- The FFT itself only runs while audio flows: `feed_fft_samples()` is called from `play()`, so paused/idle produces no new bands and `spectrum_compute()` returns false — the whole pipeline is naturally idle when not playing.

---

## Plan-vs-reality gaps and the alternatives chosen

| # | Plan said | Reality / gap | Chosen documented alternative |
|---|---|---|---|
| 1 | Register an LVGL draw callback via `id(spectrum_tap_speaker).set_on_update([&]{...})` (fires on **every** completed FFT buffer — up to **375 Hz** at 48 kHz/128 pts) | The callback body would run inside the component's `loop()` and needs `id(...)` widget expressions + a `[&]` capture — the plan itself notes `set_audio_stream_info`/`has_buffered_data` signatures must be verified against the installed ESPHome (unverifiable here, no local esphome). At 375 Hz it would also hammer LVGL (≈6 000 widget ops/s). | **Polling interval at 25 fps** (`viz_animation`, 40 ms) using **only documented ESPHome building blocks** (`interval` + `lambda` + LVGL C API) and the plan's exact math (GAIN 300, attack 0.5, decay 0.85). `set_on_update` stays available in the component for future use. Visually equivalent; bounded LVGL load. The plan itself suggested moving creation away from `on_idle` if widgets aren't ready — we declare bars in YAML, which are ready before `on_boot`. |
| 2 | Draw the 16 bars at absolute screen coordinates on the visualizer's parent | Would violate this project's static-layout contract (bars floating outside the fixed strip) | Bars are YAML children of `mp_visualizer` with fixed geometry; hide/show follows the parent automatically. |
| 3 | `esphome` CLI validation / on-device compile | No local esphome, no device (per task constraints) | Static validation only (syntax check, font check, grep matrix) + build-host check items below. |
| 4 | FFT accuracy/format assumptions | Component expects 16-bit stereo (4 B/frame) at 48 kHz — matches this project's `i2s_speaker` (`channel: stereo`, `sample_rate: 48000`, `bits_per_sample: 16bit`); the plan's key-nuance section confirms this is the intended data format. | No change needed. If sample rate ever drops to 44.1 kHz, `band_bin_start/end` in `spectrum.cpp` must be recalculated (documented in the component README and plan). |

### Build-host check items (cannot be verified locally — flagged per repo convention)
- **`speaker::Speaker` API drift**: the plan warns `set_audio_stream_info`/`audio_stream_info_`/`has_buffered_data()` (const-ness) may differ across ESPHome releases. Fixes are documented in [`custom_components/spectrum_tap/README.md`](../custom_components/spectrum_tap/README.md): drop `const`, or forward `start()` differently, per the installed `esphome/components/speaker/speaker.h`.
- **`sqrtf` in the interval lambda**: `math.h` is expected to be transitively available in the generated TU (standard for ESP-IDF/ESPHome builds). Fallback if a compile error appears: replace the smoothing block with power-domain scaling (`target = spec_bands[b] * 90000.0f`), or move the helper into a header loaded via `esphome: includes:`.
- **`lvgl.is_paused` condition** inside `and:`/`not:` — standard documented ESPHome automation syntax; if the installed release rejects the combination, drop the second guard (the visibility guard alone is sufficient).
- **Announcement playback animates the visualizer too** (both pipelines share the tapped mixer output) — verify on-device that this is acceptable; to restrict to media only, gate on `player_playing` instead of widget visibility.

---

## Files changed

| File | Change |
|---|---|
| `custom_components/spectrum_tap/*` | **New** — component extracted verbatim from [`plans/spectrum_tap.zip`](spectrum_tap.zip): `__init__.py`, `speaker/__init__.py`, `spectrum.h`, `spectrum.cpp`, `spectrum_tap.h`, `spectrum_tap.cpp`, `README.md` |
| `esp-web-radio.yaml` | **New** `external_components:` block loading `spectrum_tap` from `esp-web-radio/custom_components` (local source, deterministic) |
| `packages/esp-web-radio-audio.yaml` | `spectrum_tap` speaker platform inserted between mixer and I2S; `main_mixer_speaker.output_speaker` → `spectrum_tap_speaker` (plan's single-line change + platform block). Volume/mute/play-state trigger blocks untouched |
| `packages/esp-web-radio-page_now_playing.yaml` | 16 `viz_bar_0..15` objs inside `mp_visualizer` (fixed geometry, themed accent); new `interval: viz_animation` (40 ms) driving heights from `spec_bands[]` with the plan's attack/decay math, guarded by strip visibility + `lvgl.is_paused` |
| `README.md` | Visualizer feature bullet, project tree, bindings row, new "Audio visualizer" section, conventions note |
| `TODO.md` | "add visualizer as per existing plan" checked off + Completed entry |
| `plans/completed_visualizer.md` | This document |

No changes to: `lvgl_theme.yaml`, `common-colors.yaml`, `display-fonts.yaml` (no new glyphs — bars are plain colored rectangles), `esp-web-radio-homeassistant.yaml`, `swipe_navigation.yaml`, other pages, or the volume popup/progress/nav logic.

---

## How it renders & how it's driven

- **Source:** the `spectrum_tap` speaker taps the **mixed** PCM (media + announcements) between the mixer and the I2S DAC in `play()` (Core 0), double-buffered, no mutexes (buffer flip is atomic — plan's dual-core design). `spectrum_compute()` in `loop()` (Core 1) runs a 128-point radix-2 FFT (precomputed Hann window + twiddles) and aggregates bins into 16 **logarithmic bands** covering ~375 Hz–18 kHz at 48 kHz (375 Hz/bin; band tables shipped pre-adjusted for 48 kHz).
- **Rendering:** `viz_animation` polls `spec_bands[]` at 25 fps, applies per-band smoothing (attack: blend 50/50 on rise, decay: ×0.85 on fall — the plan's exact tuning), clamps to 2..70 px, and sets each bar's position/height bottom-anchored inside the 472×75 strip. Bars render in the theme accent color at 85% opacity.
- **Show/hide integration:** unchanged `on_play`/`on_pause`/`on_idle` triggers show/hide `mp_visualizer`; the bars are children of the strip, so they appear/disappear with it, and the driver checks the same visibility flag. Works identically in HA mode, wifi-only mode, and during announcements.
- **Idle:** when LVGL pauses (snow screen) the second guard stops updates; when playback stops the FFT stops producing data, so no work remains.

## Build-time flags

- `external_components` (documented ESPHome key) with a **local** source — no `github://` dependencies, no network at build time beyond the existing fonts.
- Component is compiled like any ESPHome platform component (`USE_ESP32` guards the `TickType_t` overload of `play()`).
- No new `sdkconfig_options`, no new `platformio_options`, no new packages.

## Memory / CPU impact (estimated — audio + LVGL are RAM/CPU heavy on ESP32-S3)

| Resource | Estimate | Notes / knobs |
|---|---|---|
| PSRAM/RAM (static) | ≈ **2.9 KB** | `sample_bufs[2][128]` floats = 1 KB, `fft_input[256]` = 1 KB, windows/twiddles 3×128 floats = 1.5 KB (static, once) — negligible against the 8 MB PSRAM + the audio pipeline's ~1 MB buffers |
| CPU (capture, Core 0) | trivial | one add + one float store per stereo frame inside the existing PCM copy path |
| CPU (FFT, Core 1 `loop()`) | ≈ 168k butterfly ops/s worst case (375 FFTs/s × 448 butterflies) | a 128-point radix-2 runs in tens of µs; the plan's own numbers assume this is cheap. Only active while PCM flows |
| CPU (LVGL) | 16 bars × 2 calls (set_pos + set_height) × 25 fps = **800 widget ops/s** | bounded by the 40 ms interval; each op invalidates only the bar's area — merged by LVGL before rendering. `loop_time` sensor (debug component) can verify no blocking |
| Flash | ≈ +6–8 KB | 16 bars are YAML-generated LVGL objects (no new fonts; the only flash growth is the component code, mostly the FFT tables) |
| Render bandwidth | 16 small rects/frame | negligible on the QSPI 40 MHz display next to the existing full-page redraws |

**Documented knobs if CPU/RAM ever becomes a concern:** raise `interval` (e.g. 60 ms → 16 fps) for the LVGL side; lower `FFT_N` is not advised below 128 (band resolution); sample-rate swap back to 44.1 kHz requires recalculating `band_bin_start/end` (see `spectrum.cpp`); the existing `media_player.buffer_size` / `task_stack_in_psram` mitigation knobs from [`troubleshooting-restarts.md`](troubleshooting-restarts.md) remain available if the added load changes the memory trend — watch the `HEARTBEAT` diag line's `loop_time` and heap/PSRAM fields on-device.

## On-device checklist (after first build)

1. While playing, the strip shows 16 orange bars reacting to the music (low bands left, high right); bars peak fast and decay slowly.
2. Pause → strip hides immediately; resume → bars re-animate from where they froze.
3. Stop/idle → strip hides; no bars visible anywhere else on the page (static layout intact).
4. Announcement (TTS) → bars animate for the announcement audio too (mixed at the tap point) — acceptable per design.
5. `HEARTBEAT`/`BOOT` diag lines and `loop_time` sensor show no significant regression vs. pre-visualizer values.
6. If the strip never appears: confirm `spectrum_tap` loaded (ESPHome logs component init + `dump_config` "Spectrum Tap Speaker (FFT=128, bands=16)"); if compilation fails on `speaker::Speaker` API, apply the fixes listed under build-host check items.