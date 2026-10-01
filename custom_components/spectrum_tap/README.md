# spectrum_tap — FFT Spectrum Visualizer Speaker Tap for ESPHome

Transparent speaker passthrough that captures PCM audio for a 128-point
radix-2 FFT and exposes 16 logarithmic frequency bands for LVGL visualization.

## Architecture

```
media_player → resampler → mixer → spectrum_tap → i2s_speaker → I2S → DAC
                                         ↓
                                   feed_fft_samples()  (Core 0)
                                         ↓
                                   spectrum_compute()  (Core 1, loop)
                                         ↓
                                   spec_bands[0..15] → LVGL bars
```

## Files

| File | Purpose |
|------|---------|
| `__init__.py` | Component metadata |
| `speaker/__init__.py` | Speaker platform registration |
| `spectrum.h` / `spectrum.cpp` | FFT engine (128-point radix-2, Hann window, 16 bands) |
| `spectrum_tap.h` | C++ speaker wrapper (intercepts `play()`, forwards to output) |
| `spectrum_tap.cpp` | Empty (all inline) |

## YAML Integration

Insert `spectrum_tap` between your mixer and I2S speaker:

```yaml
speaker:
  - platform: i2s_audio
    id: i2s_speaker
    # ... your I2S config ...

  - platform: spectrum_tap
    id: spectrum_tap_speaker
    output_speaker: i2s_speaker

  - platform: mixer
    id: main_mixer_speaker
    output_speaker: spectrum_tap_speaker  # ← was i2s_speaker
    source_speakers:
      - id: announcement_mixer_input
      - id: media_mixer_input
```

## LVGL Drawing

In your `lvgl` section, create 16 bars and update them via `set_on_update()`:

```cpp
id(spectrum_tap_speaker).set_on_update([&]() {
    static float smooth[16] = {};
    for (int b = 0; b < 16; b++) {
        float target = sqrtf(spec_bands[b]) * 300.0f;  // GAIN
        if (target > smooth[b])
            smooth[b] = smooth[b] * 0.5f + target * 0.5f;
        else
            smooth[b] = smooth[b] * 0.85f;
        int h = (int) smooth[b];
        if (h < 2) h = 2;
        if (h > 200) h = 200;
        lv_obj_set_height(bars[b], h);
    }
});
```

## Tuning

| Parameter | Location | Effect |
|-----------|----------|--------|
| `FFT_N` | `spectrum.h` | FFT size (128 = fast, 256/512 = better resolution) |
| `VIZ_BANDS` | `spectrum.h` | Number of equalizer bars |
| `band_bin_start/end` | `spectrum.cpp` | Frequency range per band |
| `GAIN` (300.0f) | LVGL lambda | Bar sensitivity |
| `0.5 / 0.85` | LVGL lambda | Attack/decay speed |

## Credits

FFT engine adapted from [anod/esp32-s3-box-3b-winamp-radio](https://github.com/anod/esp32-s3-box-3b-winamp-radio).
Band tables recalculated for 48000 Hz sample rate.
