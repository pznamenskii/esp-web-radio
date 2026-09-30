Отлично, теперь у меня есть полная картина вашего пайплайна. Давайте я найду точный C++ интерфейс `speaker::Speaker` в актуальном ESPHome, чтобы скелет компонента был компилируемым.
Теперь у меня есть полный интерфейс `speaker::Speaker` из ESPHome 2026.x и понимание того, как регистрируются speaker-платформы. Вот готовое решение для вашего проекта.

## Идея

Вставить **кастомный speaker-компонент** `spectrum_tap` между миксером и I2S-выходом. Он работает как прозрачный проход: перехватывает PCM в `play()`, кормит FFT, затем передаёт данные дальше на `i2s_speaker`. Меняется только одна строка в YAML.

```
media_player → resampler → mixer → spectrum_tap → i2s_speaker → I2S → PCM5102A
                                         ↓
                                   feed_fft_samples()
                                         ↓
                                   spectrum_compute() в loop()
                                         ↓
                                   spec_bands[0..15] → LVGL
```

## Структура файлов

```
ваш_проект/
├── ваш_конфиг.yaml
└── custom_components/
    └── spectrum_tap/
        ├── __init__.py              # метаданные компонента
        ├── speaker/
        │   └── __init__.py          # регистрация speaker-платформы
        ├── spectrum_tap.h           # C++ класс-обёртка
        ├── spectrum_tap.cpp         # реализация
        ├── spectrum.h               # FFT-движок (из anod, с правками)
        └── spectrum.cpp             # FFT-движок (из anod, с правками)
```

## Python: регистрация компонента

**`custom_components/spectrum_tap/__init__.py`**

```python
CODEOWNERS = ["@your_github"]
DEPENDENCIES = ["speaker"]
```

**`custom_components/spectrum_tap/speaker/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import speaker
from esphome.const import CONF_ID, CONF_OUTPUT_SPEAKER

spectrum_tap_ns = cg.esphome_ns.namespace("spectrum_tap")
SpectrumTapSpeaker = spectrum_tap_ns.class_(
    "SpectrumTapSpeaker", speaker.Speaker, cg.Component
)

CONFIG_SCHEMA = (
    speaker.SPEAKER_SCHEMA.extend(
        {
            cv.Required(CONF_ID): cv.declare_id(SpectrumTapSpeaker),
            cv.Required(CONF_OUTPUT_SPEAKER): cv.use_id(speaker.Speaker),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await speaker.register_speaker(var, config)
    await cg.register_component(var, config)

    output = await cg.get_variable(config[CONF_OUTPUT_SPEAKER])
    cg.add(var.set_output_speaker(output))
```

## C++: FFT-движок (адаптированный из anod)

**`spectrum.h`** — почти без изменений, но добавлен `extern` для доступа из LVGL:

```cpp
#pragma once
#include <math.h>
#include <string.h>
#include <stdint.h>

static constexpr int FFT_N = 128;
static constexpr int VIZ_BANDS = 16;

// Доступно извне (для LVGL-отрисовки)
extern float spec_bands[VIZ_BANDS];

void spectrum_init();
bool spectrum_compute();
void feed_fft_samples(const uint8_t *data, int size);
```

**`spectrum.cpp`** — код из anod с одной правкой: `band_bin` таблицы пересчитаны для **48000 Гц** вместо 44100. При 48000/128 = 375 Гц на бин; диапазон бинов 1–48 покрывает 375 Гц – 18 кГц.

```cpp
#include "spectrum.h"

// ── Core 0 side: lightweight sample capture ──
static float sample_bufs[2][FFT_N];
static volatile int write_buf_ = 0;
static volatile int accum_idx = 0;
static volatile bool sample_ready = false;
static volatile bool spectrum_ready = false;

// ── Core 1 side: FFT computation ──
static float fft_input[FFT_N * 2];
static float fft_window[FFT_N];
static float fft_twiddle[FFT_N];
static float fft_twiddle_s[FFT_N];

float spec_bands[VIZ_BANDS] = {0};

// Логарифмические полосы для 48000 Гц (375 Гц/бин)
// Бины 1-48 → 375 Гц – 18 кГц
static const uint8_t band_bin_start[VIZ_BANDS] = {1, 2, 3, 4, 5, 6, 7,  9, 11, 13, 16, 20, 24, 29, 35, 41};
static const uint8_t band_bin_end[VIZ_BANDS]   = {1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 19, 23, 28, 34, 40, 48};

static void fft_radix2(float *data, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = data[i * 2], ti = data[i * 2 + 1];
            data[i * 2] = data[j * 2]; data[i * 2 + 1] = data[j * 2 + 1];
            data[j * 2] = tr; data[j * 2 + 1] = ti;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1, step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < half; j++) {
                int tw = j * step;
                float wr = fft_twiddle[tw], wi = fft_twiddle_s[tw];
                int e = (i + j) * 2, o = (i + j + half) * 2;
                float tr = data[o] * wr - data[o + 1] * wi;
                float ti = data[o] * wi + data[o + 1] * wr;
                data[o]     = data[e]     - tr;
                data[o + 1] = data[e + 1] - ti;
                data[e]     += tr;
                data[e + 1] += ti;
            }
        }
    }
}

void spectrum_init() {
    for (int i = 0; i < FFT_N; i++) {
        fft_window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (FFT_N - 1)));
        float angle = -2.0f * (float)M_PI * i / FFT_N;
        fft_twiddle[i] = cosf(angle);
        fft_twiddle_s[i] = sinf(angle);
    }
    spectrum_ready = true;
}

bool spectrum_compute() {
    if (!sample_ready) return false;
    sample_ready = false;
    int read_buf = 1 - write_buf_;
    for (int i = 0; i < FFT_N; i++) {
        fft_input[i * 2]     = sample_bufs[read_buf][i] * fft_window[i];
        fft_input[i * 2 + 1] = 0.0f;
    }
    fft_radix2(fft_input, FFT_N);
    for (int b = 0; b < VIZ_BANDS; b++) {
        float sum = 0.0f;
        for (int i = band_bin_start[b]; i <= band_bin_end[b]; i++) {
            float re = fft_input[i * 2], im = fft_input[i * 2 + 1];
            sum += re * re + im * im;
        }
        int bin_count = band_bin_end[b] - band_bin_start[b] + 1;
        spec_bands[b] = sum / (float)bin_count;
    }
    return true;
}

void feed_fft_samples(const uint8_t *data, int size) {
    if (!spectrum_ready) return;
    const int16_t *samples = reinterpret_cast<const int16_t *>(data);
    int num_frames = size / 4; // 16-bit stereo = 4 байта/фрейм
    int wb = write_buf_;
    for (int i = 0; i < num_frames; i++) {
        float mono = (float)(samples[i * 2] + samples[i * 2 + 1]) * (0.5f / 32768.0f);
        sample_bufs[wb][accum_idx] = mono;
        accum_idx++;
        if (accum_idx >= FFT_N) {
            accum_idx = 0;
            write_buf_ = 1 - wb;
            wb = write_buf_;
            sample_ready = true;
        }
    }
}
```

## C++: speaker-обёртка

**`spectrum_tap.h`**

```cpp
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/speaker/speaker.h"
#include "spectrum.h"

namespace esphome::spectrum_tap {

class SpectrumTapSpeaker : public speaker::Speaker, public Component {
public:
    void set_output_speaker(speaker::Speaker *output) { output_ = output; }

    void setup() override { spectrum_init(); }

    void loop() override {
        if (spectrum_compute()) {
            // spec_bands[0..15] обновлены — перерисовка LVGL
            if (on_update_) on_update_();
        }
    }

    // Регистрация колбэка для отрисовки
    void set_on_update(std::function<void()> cb) { on_update_ = std::move(cb); }

    // ── Speaker: перехват play, делегирование остального ──
    size_t play(const uint8_t *data, size_t length) override {
        feed_fft_samples(data, length);
        return output_->play(data, length);
    }

#ifdef USE_ESP32
    size_t play(const uint8_t *data, size_t length, TickType_t ticks_to_wait) override {
        feed_fft_samples(data, length);
        return output_->play(data, length, ticks_to_wait);
    }
#endif

    void start() override {
        output_->set_audio_stream_info(this->audio_stream_info_);
        output_->start();
        this->state_ = speaker::STATE_RUNNING;
    }

    void stop() override {
        output_->stop();
        this->state_ = speaker::STATE_STOPPED;
    }

    void finish() override {
        output_->finish();
        this->state_ = speaker::STATE_STOPPED;
    }

    bool has_buffered_data() const override {
        return output_->has_buffered_data();
    }

    void set_volume(float volume) override { output_->set_volume(volume); }
    void set_mute_state(bool mute_state) override { output_->set_mute_state(mute_state); }

    void dump_config() override {
        ESP_LOGCONFIG("spectrum_tap", "Spectrum Tap Speaker (FFT=%d, bands=%d)", FFT_N, VIZ_BANDS);
    }

private:
    speaker::Speaker *output_{nullptr};
    std::function<void()> on_update_{nullptr};
};

}  // namespace esphome::spectrum_tap
```

**`spectrum_tap.cpp`** — пустой, вся логика в заголовке (inline):

```cpp
#include "spectrum_tap.h"
// Вся реализация в spectrum_tap.h (inline-методы)
```

## Изменения в YAML

Единственное изменение — вставить `spectrum_tap` между миксером и I2S:

```yaml
speaker:
  - platform: i2s_audio
    id: i2s_speaker
    dac_type: external
    i2s_dout_pin: GPIO15
    channel: stereo
    sample_rate: 48000
    bits_per_sample: 16bit

  # НОВЫЙ компонент: перехват PCM для FFT
  - platform: spectrum_tap
    id: spectrum_tap_speaker
    output_speaker: i2s_speaker

  - platform: mixer
    id: main_mixer_speaker
    output_speaker: spectrum_tap_speaker   # ← было i2s_speaker
    source_speakers:
      - id: announcement_mixer_input
      - id: media_mixer_input

  # ... resampler'ы без изменений ...
```

## Подключение LVGL-отрисовки

В секции `lvgl` — добавьте колбэк отрисовки к вашему `mp_visualizer`. Создайте 16 баров и обновляйте их высоты из `spec_bands[]`:

```yaml
lvgl:
  # ... ваша конфигурация ...

  on_idle:
    - lambda: |-
        // Создаём бары один раз
        static lv_obj_t *bars[16] = {};
        static bool inited = false;

        auto viz = id(mp_visualizer);

        if (!inited) {
            lv_obj_t *parent = lv_obj_get_parent(viz);  // или сам viz
            int bar_w = 10, gap = 4, x0 = 8, y_bottom = 200;
            for (int i = 0; i < 16; i++) {
                bars[i] = lv_obj_create(parent);
                lv_obj_set_size(bars[i], bar_w, 2);
                lv_obj_set_pos(bars[i], x0 + i * (bar_w + gap), y_bottom);
                lv_obj_set_style_bg_color(bars[i], lv_color_hex(0x00FF00), 0);
                lv_obj_clear_flag(bars[i], LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_style_border_width(bars[i], 0, 0);
                lv_obj_set_style_radius(bars[i], 2, 0);
                lv_obj_set_style_pad_all(bars[i], 0, 0);
            }
            inited = true;

            // Регистрируем колбэк в spectrum_tap
            id(spectrum_tap_speaker).set_on_update([&]() {
                static float smooth[16] = {};
                for (int b = 0; b < 16; b++) {
                    float target = sqrtf(spec_bands[b]) * 300.0f;  // GAIN
                    // Сглаживание: быстрый подъём, медленный спад
                    if (target > smooth[b])
                        smooth[b] = smooth[b] * 0.5f + target * 0.5f;
                    else
                        smooth[b] = smooth[b] * 0.85f;
                    int h = (int)smooth[b];
                    if (h < 2) h = 2;
                    if (h > 200) h = 200;
                    lv_obj_set_height(bars[b], h);
                    lv_obj_align(bars[b], LV_ALIGN_BOTTOM_LEFT,
                                 8 + b * 14, 0);
                }
            });
        }
```

Если бары не создаются через `on_idle` (например, виджет ещё не готов), перенесите создание в `page:` `on_load` или используйте `setup:` секцию lvgl.

## Ключевые нюансы

**Формат данных совпадает.** Ваш I2S: 16-bit stereo, 48000 Гц → 4 байта на фрейм. `feed_fft_samples()` ожидает именно это (`size / 4`).

**Двойная буферизация работает на двух ядрах.** `play()` вызывается из задачи миксера (Core 0), `loop()` — из главного цикла ESPHome (Core 1). Никаких мьютексов — switch буфера атомарен.

**`set_audio_stream_info` не virtual.** Базовый класс `Speaker` хранит `audio_stream_info_` в protected, и не-виртуальный `set_audio_stream_info` вызывается на нашем объекте (через указатель на базу). Поэтому в `start()` мы вручную форвардим `this->audio_stream_info_` на `output_`.

**`audio_output_callback_` не форвардится.** Медиаплеер регистрирует колбэк прогресса на pipeline output speaker. Поскольку реальное воспроизведение идёт на `i2s_speaker`, его колбэки не дойдут до нашего `CallbackManager`. Для визуализатора это не критично, но если заметите проблемы с позицией трека — можно зарегистрировать колбэк напрямую на `i2s_speaker`.

**Sample rate.** Таблицы `band_bin_start/end` уже пересчитаны для 48000 Гц (375 Гц/бин). Если вернётесь на 44100 — поменяйте обратно на оригинальные значения из anod.

---

Готово! Архив собран: [spectrum_tap.zip]

## Что внутри

```
spectrum_tap/
├── __init__.py            # метаданные компонента
├── speaker/
│   └── __init__.py        # регистрация speaker-платформы в ESPHome
├── spectrum.h             # заголовок FFT-движка (константы, прототипы)
├── spectrum.cpp           # 128-точечный radix-2 FFT + захват PCM (из anod, правки под 48 кГц)
├── spectrum_tap.h         # C++ класс SpectrumTapSpeaker — перехват play(), делегирование на I2S
├── spectrum_tap.cpp       # пустой (всё inline)
└── README.md              # документация по интеграции
```

## Что нужно сделать

1. **Распаковать** архив в папку `custom_components/` вашего проекта ESPHome.

2. **В YAML** вставить `spectrum_tap` между миксером и I2S — заменить `output_speaker: i2s_speaker` у миксера на `output_speaker: spectrum_tap_speaker`, и добавить сам компонент:

   ```yaml
   - platform: spectrum_tap
     id: spectrum_tap_speaker
     output_speaker: i2s_speaker
   ```

3. **В секции `lvgl`** создать 16 баров и подключить `set_on_update()` — пример есть в README.

## Чего ожидать при первой компиляции

- Если ESPHome не найдёт `set_audio_stream_info` или `audio_stream_info_` — это значит, что в вашей сборке API `speaker::Speaker` немного отличается. В этом случае загляните в `esphome/components/speaker/speaker.h` из вашей версии и сверьте сигнатуры методов `start()`, `play()`, `stop()` — я исходила из актуального интерфейса 2026.x.
- Если `has_buffered_data()` объявлен как `virtual` и возвращает `bool` без `const` — уберите `const` в `spectrum_tap.h`.

Подскажите после первой попытки компиляции, какие будут ошибки (если будут) — я помогу их исправить.