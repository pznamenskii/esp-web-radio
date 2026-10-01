# Completed: Info/Settings page + MDI glyph codepoint fixes

**Status:** Completed — implements TODO "Remaining improvements" item "Info/Settings page: wifi signal level, uptime, startup volume level, idle/inactivity timer to idle screen" and fixes the wrong MDI glyph codepoints reported in the TODO ("Issues to fix": "Glyphs are wrong - i.e. I see 'steam app' instead of 'play next'").
**Date:** 2026-10-01
**Scope:** software-only UI/state/config changes; new package `packages/esp-web-radio-page_settings.yaml` (the one allowed new package); no hardware or HA-side changes. Validation without a local esphome install: `python tests/yaml_syntax_check.py` + `python tests/font_codepoint_check.py` + grep cross-reference review (no `esphome` CLI was run).

---

## Task A — glyph codepoint fixes (authoritative table)

The user supplied the authoritative codepoint table from the actual Material Design Icons font. Every substitution and font-`extras` entry in [`packages/display-fonts.yaml`](packages/display-fonts.yaml) was audited against it. Five codepoints were WRONG (they exist in the font but map to *different* glyphs — e.g. U+F0495 is not `mdi-cogs`):

| Substitution | Old (wrong) | New (authoritative) | Glyph |
|---|---|---|---|
| `text_nav_playing` | `U+F0408` | `U+F040D` | mdi-play-circle-outline |
| `text_nav_settings` | `U+F0495` | `U+F08D6` | mdi-cogs |
| `text_nav_stations` | `U+F0499` | `U+F0439` | mdi-radio |
| `text_volume_plus` | `U+F057F` | `U+F075D` | mdi-volume-plus (old was mdi-volume-low) |
| `text_volume_minus` | `U+F0580` | `U+F075E` | mdi-volume-minus (old was mdi-volume-medium) |

All five were corrected in the `substitutions:` block AND in the matching font `extras` lists (`menu24`, `nav20` for the nav glyphs; `menu32` for volume +/-). Inline comments already carried the correct glyph names, so only the codepoints changed.

### Verification

- New permanent helper [`tests/font_codepoint_check.py`](tests/font_codepoint_check.py) parses the TTF cmap (format 4 + 12) and checks (1) the full authoritative table and (2) every `\U000F…` escape in `packages/*.yaml`.
- Result: **all 75 codepoints FOUND** in `fonts/materialdesignicons-webfont.ttf` (18 substitutions × occurrences + extras).
- The leftover throwaway [`_tmp_font_check.py`](../_tmp_font_check.py) (root, old wrong targets incl. `U+F104D`) was **deleted**.
- Flash impact: zero — same number of glyphs per font, only codepoints changed.

---

## Task B — Info/Settings page

### New page package `packages/esp-web-radio-page_settings.yaml`

Follows the page-package conventions (own `lvgl.pages` entry, `<<: !include swipe_navigation.yaml`, theme styles/color substitutions, absolute geometry, static-layout contract). Included from the root `packages:` block after `page_ap_setup`, so the merged `lvgl.pages` order is Now Playing → Stations → AP setup (`skip: true`) → Settings; the effective swipe order is Now Playing(0) → Stations(1) → Settings(2).

Fixed 3-row layout (48 px rows, 56 px pitch, ends at y=214 — above the bottom nav bar at y=294):

1. **WiFi signal** — icon `settings_lbl_wifi_icon` (strength glyph) + numeric label `settings_lbl_rssi`.
2. **Uptime** — `settings_lbl_uptime` (`Xd Xh Xm` / `Xh Xm` / `Xm`).
3. **Startup volume** — `settings_lbl_startup_vol` (percentage).

Title row = cogs glyph (`text_nav_settings`, `menu24`) + "Настройки" title. All icons reuse glyphs already embedded in `menu24` (cogs, wifi-strength-1..4, volume-high) → no new font entries, no flash cost.

### Wiring into the page system

- `page_settings` added to `lvgl.pages` (via the root include); `current_page == 2` is produced by the swipe math below.
- **3-page swipe math** ([`packages/swipe_navigation.yaml`](packages/swipe_navigation.yaml)): the old `1 - id(current_page)` flip (2 pages) was replaced by modulo-`${swipeable_page_count}` math (`swipeable_page_count: 3` substitution in [`packages/esp-web-radio-lvgl_ui.yaml`](packages/esp-web-radio-lvgl_ui.yaml)): left swipe → `(cp+1) % 3`, right swipe → `(cp+2) % 3`. This matches `lvgl.page.next/previous` with `page_wrap: true` (which already skips `page_ap_setup`), so the indicator can never desync from the rendered page.
- **Nav indicator activation**: `update_nav_indicator`'s fallback branch (previously the "Settings placeholder — always inactive") now renders the cogs slot focused (`nav_lbl_settings_big` shown, small hidden) for `current_page == 2`. The placeholder comments in the `current_page` global and the `top_layer` slot 3 were updated.
- **Navigation entry points**: swipe reaches the page naturally (Now Playing → Stations → Settings, wrap-around). The indicator stays a PURE indicator (`clickable: false`, unchanged) — no taps.
- Wake-from-idle returns to Now Playing via the native `touchscreen.on_release` handler (see idle section below).

### Feature implementations

#### a. WiFi signal level

Re-added the `wifi_signal` sensor (`platform: wifi_signal`, `update_interval: 60s`) in [`packages/esp-web-radio-homeassistant.yaml`](packages/esp-web-radio-homeassistant.yaml) — the pattern the project had before cleanup. It measures the ESP32's own radio, so it works in **wifi-only/no-HA mode** too (documented; no HA entity needed). Thresholds in `refresh_settings_info`: `>= -50 dBm → wifi_100`, `>= -65 → wifi_75`, `>= -78 → wifi_50`, else `wifi_25`; the numeric label shows `"−NN dBm"`. While disconnected the icon dims to the lowest bar (`color_text_muted`) and the dBm label hides (driven by `wifi_connected` in the same script, called from `wifi.on_connect/on_disconnect`).

#### b. Uptime

Re-added `uptime` sensor (`platform: uptime`, 60 s) + `uptime_human` template text sensor (60 s, days/hours/minutes formatting) in the homeassistant package (the exact platforms removed during cleanup). `uptime_human.on_value` → `refresh_settings_info` → `settings_lbl_uptime`.

#### c. Startup volume

New `startup_volume` float global (initial 0.5). Captured in `on_boot` in [`esp-web-radio.yaml`](esp-web-radio.yaml) right after `script.execute: sync_volume_ui` (`lambda: 'id(startup_volume) = id(volume_level);'`). Documented decision: component restore happens during setup (before `on_boot`), so `sync_volume_ui`'s read is already the persisted boot volume — capturing immediately is correct. If that ever moves after boot, re-read lazily on first page show instead (flagged in the package header).

#### d. Idle/inactivity behavior

Review decision (project reviewer clarified the requirement): the TODO's "idle/inactivity timer to idle screen" refers to the **volume popup inactivity** — which already auto-dismisses after 5 s (`show_volume_popup`, `mode: restart`, untouched from the volume refactor). Returning to Now Playing after **global** idle is not required, but is a good idea and is implemented **without any new timer scripts or hooks**: the native `touchscreen.on_release` wake handler (the same handler that performs `lvgl.resume` when the device wakes from the LVGL `on_idle` sleep) now also shows `${homepage}`, sets `current_page = 0` and refreshes the nav indicator — waking always lands on the Now Playing "idle screen". Overlays (boot screen, OTA, volume popup) keep their own lifecycles and are deliberately not touched there.

Chosen mechanism (flag for on-device verification): `lvgl.page.show` inside `on_release` right after `lvgl.resume` — confirm on the device that the page animation/snow transition is clean and the indicator refocuses.

### Constraints honored

- Static-layout contract: hide/show only, fixed geometry (rows never reflow; the dBm label hides in place).
- `on_volume`/`on_mute`/`on_unmute` trigger blocks, the volume popup logic and the progress/time hide logic were NOT touched.
- Only one new package (`page_settings.yaml`); no duplicate ids; `page_ap_setup` stays AP-mode-only (`skip: true`, excluded from swipe math).
- Page content ends at y=214, well inside the 294→320 bottom nav footprint.

---

## Files changed

| File | Change |
|---|---|
| `packages/display-fonts.yaml` | **Task A**: 5 wrong codepoints fixed in substitutions + font extras (F0408→F040D, F0495→F08D6, F0499→F0439, F057F→F075D, F0580→F075E) |
| `packages/esp-web-radio-page_settings.yaml` | **NEW**: Settings page (3 info rows), `startup_volume` global, `refresh_settings_info` script |
| `packages/esp-web-radio-lvgl_ui.yaml` | `swipeable_page_count` substitution; `current_page`/nav comments; `update_nav_indicator` cogs branch active; `top_layer` slot-3 comment; `wifi.on_connect/disconnect` call `refresh_settings_info`; header comment |
| `packages/swipe_navigation.yaml` | Modulo-`${swipeable_page_count}` swipe math (3 pages) + header |
| `packages/esp-web-radio-hardware.yaml` | `touchscreen.on_release` wake branch: return to Now Playing + indicator refocus (no overlay handling) |
| `packages/esp-web-radio-homeassistant.yaml` | Re-added `wifi_signal`, `uptime`, `uptime_human` sensors with `refresh_settings_info` hooks |
| `esp-web-radio.yaml` | `page_settings` package include; `on_boot` captures `startup_volume` + seeds `refresh_settings_info` |
| `tests/yaml_syntax_check.py` | Added the new page package to the checked file list |
| `tests/font_codepoint_check.py` | **NEW**: permanent MDI codepoint verifier (authoritative table + YAML scan) |
| `README.md` | Features (Settings page bullet, 3-page swipe, idle/wake), Settings page section, bottom-nav section (active cogs slot + swipe math), bindings rows, project tree, validation helpers |
| `TODO.md` | Info/Settings item checked off + Completed entry |

## Validation (no local esphome)

- `python tests/yaml_syntax_check.py` — all 14 files OK.
- `python tests/font_codepoint_check.py` — all 75 codepoints FOUND in the webfont.
- Grep matrix: `page_settings`/`settings_*` ids/`startup_volume`/`wifi_signal`/`uptime`/`uptime_human`/`refresh_settings_info` defined once and referenced consistently; old wrong codepoints `F0408/F0495/F0499/F057F/F0580` absent from `packages/`; no `reset_idle_screen_timer`/`start_idle_screen_timer` leftovers; `_tmp_font_check.py` removed from the repo root.
- Build-host flags remain (inline comments): `lvgl.page.show` inside `on_release` after `lvgl.resume` (verify page transition on device); `wifi_signal`/`uptime` platform availability on the installed ESPHome version; the `%` modulo lambda types (int arithmetic).

## Open follow-ups

- TODO "Dynamic WiFi and HA API connection status display" — the Settings page already renders signal-dependent icons; a dedicated top-layer indicator is still open.
- On-device checklist: swipe NP↔ST↔SET with indicator focus following; waking from idle lands on Now Playing; Settings rows show RSSI/dBm, uptime, boot volume; wifi-only mode shows real local RSSI.