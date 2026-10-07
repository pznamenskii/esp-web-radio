#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/speaker/speaker.h"
#include "spectrum.h"

#include <functional>

namespace esphome::spectrum_tap {

class SpectrumTapSpeaker : public speaker::Speaker, public Component {
public:
    void set_output_speaker(speaker::Speaker *output) { output_ = output; }

    void setup() override { spectrum_init(); }

    void loop() override {
        if (spectrum_compute()) {
            // spec_bands[0..15] updated — trigger LVGL redraw
            if (on_update_)
                on_update_();
        }
    }

    // Register callback for LVGL drawing
    void set_on_update(std::function<void()> cb) { on_update_ = std::move(cb); }

    // ── Speaker: intercept play, delegate the rest ──
    size_t play(const uint8_t *data, size_t length) override {
        feed_fft_samples(data, length);
        log_stream_info_once_();
        return output_->play(data, length);
    }

#ifdef USE_ESP32
    size_t play(const uint8_t *data, size_t length, TickType_t ticks_to_wait) override {
        feed_fft_samples(data, length);
        log_stream_info_once_();
        return output_->play(data, length, ticks_to_wait);
    }
#endif

    void start() override {
        log_stream_info_once_();
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
    // ── DIAGNOSTIC (temporary, remove after root-cause validation) ──
    // One-shot per boot: prints the stream info the tap believes it has vs. the
    // stream info currently configured on the underlying i2s speaker.
    //   EXPECTED:   tap stream: 2ch x 16bit @ 48000 | i2s output stream: 2ch x 16bit @ 48000
    //   SUSPECTED:  tap stream: 2ch x 16bit @ 48000 | i2s output stream: 1ch x 16bit @ 16000
    // The second case means the i2s DAC was never handed the mixer's stream info
    // (it kept AudioStreamInfo's default 16 kHz mono) and start_i2s_driver()
    // configured the PCM5102 at the wrong rate -> slow clicks/crackle.
    void log_stream_info_once_() {
        static bool logged = false;
        if (logged || output_ == nullptr)
            return;
        logged = true;
        const auto &mine = this->get_audio_stream_info();
        const auto &out = output_->get_audio_stream_info();
        ESP_LOGI("spectrum_tap",
                 "[DIAG] tap stream: %uch x %ubit @ %uHz | i2s output stream: %uch x %ubit @ %uHz",
                 (unsigned) mine.get_channels(), (unsigned) mine.get_bits_per_sample(),
                 (unsigned) mine.get_sample_rate(), (unsigned) out.get_channels(),
                 (unsigned) out.get_bits_per_sample(), (unsigned) out.get_sample_rate());
    }

    speaker::Speaker *output_{nullptr};
    std::function<void()> on_update_{nullptr};
};

}  // namespace esphome::spectrum_tap