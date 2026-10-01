# Completed: progress-cluster auto-hide, bottom nav glyph indicator, comment cleanup + README actualization

**Status:** Completed — implements TODO "Remaining improvements" items "labels total/elapsed and progress bar hide", "Re-design navigation bar on top_layer", and "too many comments".
**Date:** 2026-10-01
**Scope:** software-only UI/state/config changes; no hardware or HA-side changes; no new packages; no new top-level YAML keys. Validation without a local esphome install: `python tests/yaml_syntax_check.py` + grep cross-reference review (no `esphome` CLI was run).

---

## Item 1 — hide time labels + progress bar when data is absent

### Behavior (implemented exactly as decided)

| Widget | Shown when | Hidden when |
|---|---|---|
| `mp_lbl_elapsed` | `player_position` (`media_position`) has a state — incl. `0` at track start (`0:00` must show) | position unknown (`has_state() == false`) |
| `mp_lbl_total` | `player_duration` (`media_duration`) has a state | duration unknown |
| `mp_bar_progress` | duration has a state (its range is rendered against the duration; a missing position simply renders 0%) | duration unknown |

Availability is checked with `has_state()` (a `homeassistant` platform sensor whose attribute is missing reports `unknown`), never `value == 0` — so `0:00` at play start displays.

### Mechanism

- All three widgets start `hidden: true` in [`packages/esp-web-radio-page_now_playing.yaml`](packages/esp-web-radio-page_now_playing.yaml) (no data at boot, and none ever arrives in wifi-only/no-HA mode — verified natural).
- The reworked `player_position` / `player_duration` handlers in [`packages/esp-web-radio-homeassistant.yaml`](packages/esp-web-radio-homeassistant.yaml) `lvgl.widget.show` the widgets whose data arrived and compute the bar percent (guarded with `has_state()`/`> 0.0f` so a NaN duration can never corrupt the bar).
- `media_player.on_idle` (in [`packages/esp-web-radio-audio.yaml`](packages/esp-web-radio-audio.yaml)) re-hides all three: when the player stops, HA clears the attributes, the sensors go `unknown` and their `on_value` never fires again — hiding must happen from the play-state hook. `on_pause` intentionally keeps the cluster visible (you see where you paused).

### Static-layout contract

Hide/show only; x/y/width/height untouched. No reflow on the Now Playing page. The `on_volume` / `on_mute` / `on_unmute` trigger blocks and the volume-popup logic were not touched.

---

## Item 2 — bottom nav bar → glyph indicator

### Settings-page mapping decision

`page_ap_setup` is **not** part of the swipe/page system (`skip: true`, shown only by AP mode), so the **cogs** glyph is kept as the third slot rendered permanently in inactive (small/muted) size, with an inline comment noting it activates when a real Settings page joins `lvgl.pages` (TODO "Info/Settings page"). Slot order = swipe order: Now Playing → Stations → Settings(placeholder).

### Geometry (per user guidance — supersedes the "3 px top/bottom" of the TODO text)

- Bar: `id: top_layer` kept (all existing `lvgl.widget.hide/show: top_layer` usages intact — `enter_ap_mode`/`exit_ap_mode`/`wifi.on_connect`).
- `align: bottom_mid`, `y: 0`, width **160** (= 1/3 of 480), height **26**, `bg_opa: TRANSP`, `clickable: false` (pure indicator, never intercepts touches; navigation is swipe-only now).
- Top edge lands at **y=294** — exactly where the Now Playing control row ends (`mp_controls` y 250 + h 44). This is a *smaller* overlay than the old 30 px buttonmatrix, so the ~40 px bottom budget the Stations page scroll area relies on still holds.
- Three 53 px slots (`nav_slot_playing`/`nav_slot_stations`/`nav_slot_settings` at x 0/53/106), each holding two stacked labels at `align: CENTER` (same point → swap is a pure hide/show, no reflow):
  - focused: `menu24` (24 px) + `${color_accent}`,
  - inactive: `nav20` (20 px) + `${color_text_muted}`.

### Fonts (flash-size impact documented)

| Font | Change | Flash impact (approx., measured at build) |
|---|---|---|
| `menu24` | gained the 3 nav glyphs (F0408/F0495/F0499); lost the 7 control glyphs (moved to `menu32`) | ~ −7 × 24 px icons ≈ −9 KB |
| `nav20` | **new**: 20 px, 3 nav icons + space | ≈ +3 KB |
| `menu32` | **new**: 32 px, 7 control icons (play, pause, skip-prev/next, volume +/-/off) + space | ≈ +18 KB |

Net ≈ +12 KB flash for the indicator + enlarged control icons. All codepoints verified present in `fonts/materialdesignicons-webfont.ttf` (checked with a cmap parser; U+F0408 play-circle-outline, U+F0495 cogs, U+F0499 radio). Glyph substitutions added: `text_nav_playing` / `text_nav_settings` / `text_nav_stations`; obsolete `text_prev` / `text_next` / `text_home` removed (only the old buttonmatrix used them). The unused `buttonmatrix` theme defaults and `header_footer` style were removed from [`packages/lvgl_theme.yaml`](packages/lvgl_theme.yaml).

The Now Playing control-row labels switched from `${font_icon}` (`menu24`) to `menu32` (per user request "move glyphs used in mp_controls there, increasing mp_control fonts to this new 32").

### Focus tracking

Page activation is signaled at the page-change sites (there is no LVGL page-change hook in use): the `current_page` global (0 = Now Playing, 1 = Stations, 2 = Settings placeholder) is kept current by
- `on_boot` (starts at 0),
- the shared swipe handlers in [`packages/swipe_navigation.yaml`](packages/swipe_navigation.yaml) — with exactly two swipeable pages + `page_wrap: true` any swipe flips `current_page` (documented inline; update the flip if a third swipeable page is added),
- `play_station` (returns to Now Playing),
- `exit_ap_mode` and the `wifi.on_connect` AP-exit branch.

Each site then runs the new `update_nav_indicator` script, which hide/shows the six glyph labels accordingly.

---

## Item 3 — comment cleanup + README actualization

- **RU → EN**: all Russian comments translated ([`packages/esp-web-radio-homeassistant.yaml`](packages/esp-web-radio-homeassistant.yaml) station/player/art blocks, [`packages/esp-web-radio-audio.yaml`](packages/esp-web-radio-audio.yaml) speaker/mixer/resampler/MUTE/pipeline comments, OTA strings are UI text and left as-is).
- **Dead commented code removed**: commented `wifi_ip` + `uptime_human` text sensors and commented `wifi_signal` RSSI + `uptime` sensors in the homeassistant package (they targeted non-existent `settings_*` widgets of the future Settings page); the disabled `web_server` block, `radio_core`/`swipe` package includes, the commented OTA password line and the commented `on_boot` leftovers in [`esp-web-radio.yaml`](esp-web-radio.yaml); `# bits_per_sample` lines in the audio package.
- **Big blocks shortened** (detail now lives in README): page_now_playing header/volume block, audio play-state/volume-hook notes (the plan-referenced parts kept: loop-protection strategy, D1 guarded push, D5 popup note, static-layout contract).
- **README actualized**: features (time cluster hide, glyph indicator, web interface bullet removed — the `web_server` was disabled code), secrets table (`ota_password` = OTA only), project tree, theming (`buttonmatrix`/`header_footer` removed, `station_button` added), UI architecture (two swipeable pages, new "Bottom nav indicator" section), bindings table (hide rules), font strategy (glyph counts, `nav20`/`menu32` rows, icon substitution list).

Explicitly preserved (per instructions): the static-layout contract block, the volume-popup/D5 note, the `volume_mute_last_pushed` guard comments, the play-state-hook notes, the `enter_ap_mode`/`exit_ap_mode` `top_layer` hide/show, and the `on_volume`/`on_mute`/`on_unmute` trigger blocks.

---

## Files changed

| File | Change |
|---|---|
| `packages/esp-web-radio-page_now_playing.yaml` | `hidden: true` on the 3 progress widgets; control labels → `menu32`; header/volume comments shortened |
| `packages/esp-web-radio-homeassistant.yaml` | Reworked `player_position`/`player_duration` handlers (show/hide + guards); dead commented code removed; RU comments → EN |
| `packages/esp-web-radio-audio.yaml` | `on_idle` hides the progress cluster; RU comments → EN; dead `bits_per_sample` lines removed; stale intermediate-refactor note dropped |
| `packages/esp-web-radio-lvgl_ui.yaml` | `top_layer` buttonmatrix → glyph indicator (id kept); `current_page` global + `update_nav_indicator` script; wiring at play_station/exit_ap_mode/wifi.on_connect; dead substitutions removed |
| `packages/swipe_navigation.yaml` | Swipe handlers flip `current_page` + refresh the indicator; header trimmed |
| `packages/display-fonts.yaml` | `text_nav_*` substitutions; `nav20` + `menu32` fonts; menu24 extras reshuffled; `text_prev/next/home` removed |
| `packages/lvgl_theme.yaml` | `buttonmatrix` theme defaults + `header_footer` style removed |
| `esp-web-radio.yaml` | `on_boot` initializes the indicator; dead commented code removed |
| `README.md` | Actualized per Item 3 + new "Bottom nav indicator" section |
| `TODO.md` | Three items marked done and moved to Completed with mechanism notes |

## Validation (no local esphome)

- `python tests/yaml_syntax_check.py` — all files OK (see run output).
- Grep matrix: no references remain to `page_prev`/`page_home`/`page_next`, `text_prev`/`text_next`/`text_home`, `header_footer`, `btnmtrx_text_font`/`lbl_text_font`, `settings_ip_value`/`settings_uptime_value`/`settings_rssi_value`, `wifi_ip`/`uptime_human`/`wifi_rssi`/`uptime_sec`. New ids (`nav_slot_*`, `nav_lbl_*`, `current_page`, `top_layer` widget, `update_nav_indicator`) defined exactly once and every reference resolves; glyph substitutions registered in both `substitutions:` and font `extras`.
- Build-host checks remain (flagged inline): `menu32`/`nav20` with `glyphs: " "` minimal base sets compile fine (fallback: copy the full glyph string from `menu24`); the swipe-flip assumption holds only while exactly two pages are swipeable; icon sizes may shift ±1 px per font metrics.

## Open follow-ups

- **Settings page** (TODO "Info/Settings page"): add a real `page_settings` to `lvgl.pages`, set `current_page = 2` at its navigation sites, and extend `update_nav_indicator` to focus the cogs slot; re-add the wifi signal / uptime sensors there.
- **Boot spinner redesign** and **AP/web-server conflict** remain open (TODO "Issues to fix").
- On-device checklist: swiping flips the indicator focus; the bar never eats touches near the bottom; the time cluster reappears after play starts and disappears on stop; wifi-only mode shows no time cluster.