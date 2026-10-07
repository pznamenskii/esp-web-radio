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
        sync_output_stream_info_();
        log_stream_info_once_();
        return output_->play(data, length);
    }

#ifdef USE_ESP32
    size_t play(const uint8_t *data, size_t length, TickType_t ticks_to_wait) override {
        feed_fft_samples(data, length);
        sync_output_stream_info_();
        log_stream_info_once_();
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
    // The mixer sets THIS speaker's stream info at runtime (MixerSpeaker::start()
    // in ESPHome 2026.x: output_speaker_->set_audio_stream_info(...)), but never
    // touches the underlying i2s speaker. The i2s speaker auto-starts from its own
    // first play() using ITS OWN audio_stream_info_ (default 16 kHz mono), so
    // without this sync the PCM5102 would be clocked at the wrong rate while the
    // upstream chain delivers 48 kHz stereo -> slow clicks/crackle (validated by
    // DIAG logs: tap 2ch@48000 vs i2s 1ch@16000). Push our info down whenever it
    // differs; cheap because it only fires on an actual change.
    void sync_output_stream_info_() {
        if (output_ == nullptr)
            return;
        if (output_->get_audio_stream_info() != this->get_audio_stream_info()) {
            const auto &mine = this->get_audio_stream_info();
            ESP_LOGD("spectrum_tap", "Syncing output stream info -> %uch x %ubit @ %uHz",
                     (unsigned) mine.get_channels(), (unsigned) mine.get_bits_per_sample(),
                     (unsigned) mine.get_sample_rate());
            output_->set_audio_stream_info(mine);
        }
    }

    // One-shot per boot (fires on first play()): prints the stream info the tap
    // believes it has vs. the stream info actually configured on the underlying
    // i2s speaker. Call AFTER sync_output_stream_info_() so a healthy boot shows
    // both sides equal, e.g.:
    //   [DIAG] tap stream: 2ch x 16bit @ 48000Hz | i2s output stream: 2ch x 16bit @ 48000Hz
    // If the i2s side ever shows the AudioStreamInfo default (1ch x 16bit @ 16000),
    // the stream-info propagation to the DAC is broken again (see
    // plans/troubleshooting-visualizer-audio-crackle.md).
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