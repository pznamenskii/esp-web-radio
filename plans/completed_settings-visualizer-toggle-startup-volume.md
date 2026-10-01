# Completed: Settings visualizer toggle + editable startup volume

**Status:** Completed — two enhancements to the Info/Settings page (`packages/esp-web-radio-page_settings.yaml`) interacting with the audio package (`on_play` gating), the Now Playing page (`viz_animation` gating), the root config (`on_boot` volume override + switch sync) and the font package (one new glyph).
**Date:** 2026-10-01
**Scope:** software-only UI/state/config changes. No new packages, no hardware/HA-side changes, no changes to `custom_components/` (`spectrum_tap` untouched). Validation without a local esphome install: `python tests/yaml_syntax_check.py` (14/14 OK) + `python tests/font_codepoint_check.py` (79/79 FOUND, incl. the new `U+F01DC`) + grep cross-reference matrix. No `esphome` CLI was run.

---

## Enhancement 1 — Visualizer on/off toggle

### Mechanism
- **Persisted global** `visualizer_enabled` (bool, `restore_value: true`, `initial_value: "true"`) declared in [`packages/esp-web-radio-page_settings.yaml`](../packages/esp-web-radio-page_settings.yaml) (owned by the settings package — that's where its UI lives; the audio package and Now Playing page reference it by id, one global namespace).
- **Toggle row** (`settings_row_visualizer`, y=222): caption "Визуализатор" + an LVGL **`switch`** widget (`settings_sw_visualizer`, 48×30, right-aligned). The theme already defines `switch` styles (panel track / orange checked / light knob) in [`packages/lvgl_theme.yaml`](../packages/lvgl_theme.yaml) — no new styling. Row icon = new `mdi-equalizer` glyph (`text_equalizer`, `U+F01DC`).
- **Switch `on_value`** persists the state (`id(visualizer_enabled) = x`) and — only when `player_playing` is already true — re-gates the strip live: disabled while playing → `lvgl.widget.hide: mp_visualizer`; re-enabled while playing → `show`. Pause/idle hiding is NOT touched (stays unconditional in the media-player triggers).
- **`on_play` gating** ([`packages/esp-web-radio-audio.yaml`](../packages/esp-web-radio-audio.yaml)): `lvgl.widget.show: mp_visualizer` is now wrapped in `if id(visualizer_enabled)`. `on_pause`/`on_idle` blocks unchanged.
- **`viz_animation` gating** ([`packages/esp-web-radio-page_now_playing.yaml`](../packages/esp-web-radio-page_now_playing.yaml)): the interval guard became `id(visualizer_enabled) && !lv_obj_has_flag(id(mp_visualizer), LV_OBJ_FLAG_HIDDEN)` (+ the existing `not: lvgl.is_paused`) — when disabled the ENTIRE interval body is skipped, so the bar-math (sqrtf smoothing + `lv_obj_set_pos/set_height`) does no work either, just one flag check per tick.
- **Boot sync**: `on_boot` runs `lvgl.switch.update: settings_sw_visualizer = id(visualizer_enabled)` so the widget reflects the restored persisted value before the page can be shown (if this fires `on_value` it only re-writes the same value; `player_playing` is false at boot, so no show/hide side effect).

### Deliberately NOT done
- **FFT/speaker capture keeps running** regardless of the toggle (it is cheap; the plan says so) — only the LVGL strip visibility + animation updates are gated. `spectrum_tap` component files untouched.
- **No HA switch mirror** — the project has exactly one switch platform (`gpio` `dac_mute`); there is no template/restore-switch precedent, and a two-way HA↔LVGL sync (switch triggers + `on_value`) would duplicate persistence and risk echo loops. Kept UI-only, documented here and in the settings package comment.

## Enhancement 2 — Editable startup volume

### Mechanism
- **`startup_volume` is now PERSISTED** (float, `restore_value: true`, `initial_value: "0.5"`), replacing the old boot-capture-only global (`restore_value: no` + `on_boot` copy from `volume_level`). The row label always shows THIS configured value.
- **Interactive row** (`settings_row_startup_vol`, y=166): changed `obj` → **`button`** with the stations `station_button` style + `on_press` (the project's established interactive-row pattern — see `st_btn_N` on the Stations page), so it visibly reacts to touch (theme `button.pressed` orange flash). `on_press` → `script.execute: show_settings_vol_popup`.
- **Page-local popup** (`settings_vol_popup`, 380×50 at x=50/y=222..272, drawn last inside the page so it renders over the rows) mirrors the Now Playing volume popup exactly: horizontal slider `settings_vol_slider` (0..100, `adv_hittest`) + percent label `settings_lbl_vol_value`, hidden by default.
- **`show_settings_vol_popup` script** (`mode: restart`, 5 s auto-hide): seeds slider + label from the global, shows the overlay, `delay: 5s`, hides. Every re-trigger (row tap or slider release) restarts the 5 s countdown — no native timer, same as `show_volume_popup`.
- **Slider `on_release`** (commits): `id(startup_volume) = x / 100.0f` → (if `x > 0 && muted`: `media_player.volume_mute OFF`, same as the Now Playing slider) → **`media_player.volume_set` with the new value** — immediate audible feedback. The existing **`on_volume` trigger** then re-syncs the Now Playing popup slider + percent label automatically (verified wiring: `on_volume` sets `volume_level` and updates `volume_slider`/`mp_lbl_volume`; it fires identically for local commands and HA-originated changes — no manual mirroring added). Then `refresh_settings_info` refreshes the row label and `show_settings_vol_popup` restarts the auto-hide.

### Boot-volume semantics (deliberate deviation, documented)
`on_boot` ([`esp-web-radio.yaml`](../esp-web-radio.yaml)) order:
1. `sync_volume_ui` — mirrors the component-restored live volume into the popup UI.
2. **Guarded override**: `if fabsf(id(startup_volume) - id(volume_level)) > 0.001f` → `media_player.volume_set volume: id(startup_volume)`. The CONFIGURED startup volume wins over the component-restored live volume by design (the user asked for a specific boot level; the component restore would otherwise resurrect last session's live adjustments). The guard avoids a redundant set when they already agree; the resulting `on_volume` event re-syncs the Now Playing popup UI.
3. `lvgl.switch.update` for the visualizer toggle (see Enhancement 1).
4. `refresh_settings_info` — seeds the settings rows.

Why guarded rather than unconditional-idempotent: it is one extra `if`, avoids a redundant volume event, and self-documents that we only deviate when actually needed. Epsilon 0.001 absorbs float representation noise (`x / 100.0f` vs restored NVS bits).

## Layout — settings page

The user review relaxed the "all rows above y=294" constraint in favor of usability: the page now uses the **Stations-page scrollbar pattern** (`scrollbar_mode: "AUTO"`) and keeps the original comfortable 48 px rows at 56 px pitch:
- Row 1 WiFi — y=54 · Row 2 uptime — y=110 · Row 3 startup volume (button) — y=166 · Row 4 visualizer toggle — y=222 · Row 5 reset reason (hidden while unknown) — y=278 (ends y=326, below the fold — reachable by scrolling).
- The bottom nav bar is a transparent `top_layer` overlay; scrolled content pans under it (same as the Stations page).
- Static-layout contract preserved: absolute positions, hide/show only, no reflow; scrolling never re-lays out siblings.
- The popup is page content too — it pans with the page and always reappears over the same rows (right under the row that opens it).

## Files changed

| File | Change |
|---|---|
| `packages/esp-web-radio-page_settings.yaml` | `startup_volume` → persisted; NEW `visualizer_enabled` global; NEW `show_settings_vol_popup` script; startup-volume row → interactive `button` + on_press; NEW visualizer row (`settings_row_visualizer` + `settings_sw_visualizer` switch + `settings_lbl_visualizer_*`); NEW `settings_vol_popup`/`settings_vol_slider`/`settings_lbl_vol_value`; row y-positions + `scrollbar_mode: AUTO`; header/geometry comments |
| `packages/esp-web-radio-audio.yaml` | `on_play`: `show mp_visualizer` gated on `visualizer_enabled` (pause/idle + all volume/mute triggers untouched) |
| `packages/esp-web-radio-page_now_playing.yaml` | `viz_animation` interval guard: `visualizer_enabled &&` (full body skipped when off); integration comment |
| `esp-web-radio.yaml` | `on_boot`: boot-capture line REMOVED; guarded `volume_set` override applying configured `startup_volume`; `lvgl.switch.update` sync for the toggle; comments |
| `packages/display-fonts.yaml` | NEW `text_equalizer` substitution (`U+F01DC`, mdi-equalizer) + `menu24` extras entry |
| `README.md` | Features (Settings bullet), Settings-page section (5-row scrollable layout, editable startup volume, visualizer toggle), bindings table (3 new/updated rows), volume/mute model (boot override note), visualizer section (toggle gate), font strategy (13 menu24 glyphs, `text_equalizer`), project tree |
| `TODO.md` | Two new Completed entries (no matching open TODO items existed) |
| `plans/completed_settings-visualizer-toggle-startup-volume.md` | This document |

## Validation (no local esphome)

- `python tests/yaml_syntax_check.py` — 14/14 OK.
- `python tests/font_codepoint_check.py` — 79/79 codepoints FOUND (`U+F01DC` present in the webfont, substitution + extras both verified).
- Grep matrix: `visualizer_enabled` defined once (settings globals), referenced in root `on_boot`, audio `on_play`, now-playing `viz_animation`, settings switch `on_value`; `settings_sw_visualizer` defined once + referenced in `on_boot`; `settings_vol_popup`/`settings_vol_slider`/`settings_lbl_vol_value`/`show_settings_vol_popup` defined once and referenced consistently; old boot-capture line `id(startup_volume) = id(volume_level)` absent; `settings_row_startup_vol` references resolve to the new button.

## Build-time flags (cannot be verified locally)

- **`lvgl.switch.update` action + switch `on_value`** — the update-action family (`lvgl.slider.update`, `lvgl.bar.update`) is already used in this project; `lvgl.switch.update` is the documented sibling. If the installed ESPHome release rejects it, fallback: set the checked state via raw LVGL in a lambda (`lv_obj_add_state(id(settings_sw_visualizer), LV_STATE_CHECKED)` / `clear_state`).
- **Switch `on_value` variable** `x` is expected to be `bool`. Fallback if it arrives as int/other: cast (`(bool)x`).
- **`fabsf` in the `on_boot` lambda** — `<cmath>` is available in the generated TU (the now-playing interval already uses `sqrtf`). Fallback: plain comparison `(a > b ? a - b : b - a) > 0.001f`.
- **`media_player.volume_set` inside `on_boot`** — valid (component setup precedes `on_boot`); the component's own restore already ran during setup, so the guarded override sees the real restored value.
- **`button` row pressed state** — the theme `button.pressed` orange flash applies (by design, gives tap feedback); the `station_button` style overrides the resting look. If the flash is unwanted on-device, add a `pressed:` override to a row-specific style.
- **Scrollable page + swipe navigation** — the Stations page already combines `scrollbar_mode: AUTO` with the shared swipe handlers; the settings page follows the same proven combination.
- **New glyph** `U+F01DC` (mdi-equalizer) — verified present in `fonts/materialdesignicons-webfont.ttf`; name mapping per the MDI cheatsheet (the project's convention; the font check only proves existence, like all previous glyphs).

## On-device checklist

1. Visualizer toggle: play → strip shows; toggle OFF while playing → strip hides immediately; toggle ON while playing → strip reappears; pause/idle still hides unconditionally; with the toggle OFF a fresh play never shows the strip; no bars leak anywhere when disabled.
2. Startup volume: tap the row → popup appears under it with the current %; drag + release → row label updates, Now Playing popup slider/% follow (via `on_volume`), and the volume audibly changes; popup auto-hides after 5 s; repeated taps reset the timer.
3. Reboot: device comes up at the CONFIGURED startup volume (not last session's live volume) — verify with the Now Playing popup on boot; the toggle switch matches the previously saved visualizer state.
4. Settings page scrolls vertically (scrollbar appears) with rows 1-4 visible and the reset-reason row below the fold; swipe navigation still works on the page.