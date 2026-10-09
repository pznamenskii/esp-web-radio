# ESP Web Radio

ESP32-S3 WiFi internet radio with a 480×320 LVGL touchscreen UI, I2S output to an external PCM5102 DAC, and deep Home Assistant integration via the encrypted native API. The device shows the currently playing station/artist/track on a dark-orange themed "Now Playing" page, streams audio through a FLAC-capable media pipeline (plus an announcement pipeline for TTS-style interruptions), and exposes the player, now-playing metadata, and the DAC mute as Home Assistant entities — while the four station presets themselves are defined by Home Assistant template sensors. The whole configuration is modularized into ESPHome `packages`, with the UI authored directly in YAML (ported from an LVGL Designer export) and all layout, fonts, and theming centralized in shared packages.

---

## Table of Contents

- [Features](#features)
- [Hardware](#hardware)
- [Getting started / build & flash](#getting-started--build--flash)
- [Project structure](#project-structure)
- [Configuration & secrets](#configuration--secrets)
- [Theming system](#theming-system)
- [UI architecture & page bindings](#ui-architecture--page-bindings)
- [Music Assistant](#music-assistant)
- [Time sync & device modes](#time-sync--device-modes)
- [Diagnostics & troubleshooting](#diagnostics--troubleshooting)
- [Font strategy](#font-strategy)
- [Roadmap](#roadmap)
- [Development conventions](#development-conventions)

---

## Features

- **Now Playing page** — full 480×320 LVGL page showing:
  - Station title, artist, and track labels (long text scrolls; artist/track auto-hide when empty)
  - Station logo / album-art panels hide automatically whenever their data is absent (logo waits for the station-picture roadmap item; art follows the `media_image_url` attribute)
  - **Live audio spectrum visualizer** — while playing, the `mp_visualizer` strip animates 16 FFT bands (accent-colored bars) fed by the custom `spectrum_tap` speaker that taps the PCM between the mixer and the I2S DAC (see [Audio visualizer](#audio-visualizer)); the strip (and bars) hide on pause/idle with it
  - Progress bar plus elapsed (`mm:ss`) and total (`mm:ss`) time labels — the whole cluster hides while its data is absent (elapsed needs a media position; total and the bar need a media duration; `on_idle` re-hides everything when the player stops)
  - Transport controls: station prev/next, play/pause, mute (-/+ with a popup slider)
- **Stations page** — a second LVGL page listing every available station as a button: a dynamic 2×6 grid (up to 12 slots) filled from the HA station-name sensors while the API is connected, or from the hardcoded offline list when it is not. Empty slots hide automatically; a vertical scrollbar appears once more than 8 stations are active; pressing a station plays it and returns to Now Playing.
- **Info/Settings page** — the third swipeable LVGL page with device info + settings: **WiFi signal level** (signal-strength icon `wifi_25/50/75/100` picked by RSSI thresholds −50/−65/−78 dBm plus a numeric dBm label; driven by the local `wifi_signal` sensor, which measures the ESP32 radio itself — so it also works in wifi-only/no-HA mode), **uptime** (`uptime` + `uptime_human`, formatted days/hours/minutes), **editable startup volume** (a persisted `startup_volume` global shown as a percentage; tapping the row opens a popup slider that stores the value, applies it to the player immediately for audible feedback, and is re-applied at boot as the boot volume — overriding the component-restored live volume), **visualizer toggle** (an LVGL switch bound to the persisted `visualizer_enabled` global that gates the Now Playing spectrum strip: while off the strip never shows even during playback and the animation driver does no work — the FFT/speaker tap keeps running, only the strip + animation are gated), and **last reset reason** (the `debug.reset_reason` text sensor — power-on/software/watchdog/brownout etc.; the reason survives software reboots, so it names what ended the previous session; the row shows only while the sensor has a state). The page scrolls vertically once the 5 rows exceed the viewport (Stations-page scrollbar pattern). Row icons come from `menu24` (cogs, wifi-strength, volume-high, restart) plus one new 24 px glyph `mdi-equalizer` for the visualizer row — negligible flash cost.
- **No-HA local stations** — with WiFi up but the Home Assistant API unreachable, the Stations page falls back to the built-in list in [`packages/esp-web-radio-offline_stations.yaml`](packages/esp-web-radio-offline_stations.yaml) and playback goes straight to the direct stream URL via the local media player. When HA reconnects, station names and playback routing switch back to Home Assistant automatically.
- **Home Assistant integration** — encrypted native API; the player, DAC mute switch, and media info are exposed automatically. A status indicator in the top layer (`lbl_hastatus`, `mdi-home-assistant` glyph) appears when a Home Assistant client connects and disappears on disconnect.
- **Clock with local fallback** — the title-bar clock (`lbl_time`) shows HA-provided time while the API is connected; when WiFi is up but HA is not, it falls back to an NTP-maintained local clock (`sntp`, `pool.ntp.org` / `time.google.com`); with no WiFi the clock hides. See [Time sync & device modes](#time-sync--device-modes).
- **OTA updates** — ESPHome OTA with an on-screen popup showing a live progress bar and percentage (`ota_popup` with `ota_bar_percentage` / `ota_lbl_percentage`).
- **Diagnostics & troubleshooting** — the `debug` component publishes the **last reset reason** (`reset_reason`) plus heap/PSRAM health sensors (`heap_free`, `heap_min_free`, `heap_block`, `heap_fragmentation`, `psram_free`, `loop_time`) as HA entities; the boot log prints a `BOOT` summary line (reset reason + heap at startup) and a low-frequency `HEARTBEAT` line (every 15 min: uptime, heap/PSRAM, fragmentation, loop time) paints the memory trend up to any spontaneous restart. Full investigation, workflow and mitigation knobs: [`plans/troubleshooting-restarts.md`](plans/troubleshooting-restarts.md).
- **Swipe / page navigation** — swipe left/right cycles the three navigable pages (Now Playing → Stations → Settings) with `OUT_LEFT` / `OUT_RIGHT` animations (300 ms); a transparent bottom **nav glyph indicator** in the LVGL `top_layer` marks the active page (the focused glyph renders larger than the others). `page_wrap: true` wraps around.
- **WiFi setup (AP mode) page** — when the fallback access point + captive portal becomes active (`wifi.ap_active`), the display switches to a locked `page_ap_setup` page showing the AP network name (`ap_ssid`, default "ESP Radio Fallback"), the AP password (from `!secret ap_password`, shown via `ap_password_display`), the setup URL (`${ap_url}`, default http://192.168.4.1), and a scannable **QR code** (`ap_qr`) encoding the WiFi connection string `WIFI:T:WPA;S:${ap_ssid};P:${ap_password};;` — scanning it joins the fallback AP directly. The page cannot be swiped/navigated away from and clears automatically back to Now Playing when the station reconnects; while it is up, the top-layer `wifi_status` icon blinks the `wifi-cog` glyph at ~400 ms and the LVGL idle timer is disabled so the page stays lit. See [UI architecture & page bindings](#ui-architecture--page-bindings).
- **Boot & idle UX** — spinner boot screen stays up until the first of: user touch, WiFi connects, or AP mode becomes active (shows the setup page instead). After 15 s of inactivity the backlight fades out and LVGL pauses with a "snow" pattern (LCD burn-in protection); any touch wakes the device — waking returns straight to the Now Playing page (the "idle screen"). Idle never triggers while the fallback AP + captive portal are active (`wifi.ap_active`), so the setup page stays lit.
- **Dark-orange LVGL theming** — a centralized theme in [`packages/lvgl_theme.yaml`](packages/lvgl_theme.yaml) drives widget defaults and shared named styles (see [Theming system](#theming-system)).

---

## Hardware

| Component | Detail |
|---|---|
| MCU / board | ESP32-S3 (`esp32-s3-devkitc-1`, variant `esp32s3`), **16 MB flash**, ESP-IDF framework |
| PSRAM | **8 MB octal** PSRAM, 80 MHz — required by the audio pipeline + LVGL (`CONFIG_SPIRAM_FETCH_INSTRUCTIONS`, `CONFIG_SPIRAM_RODATA`) |
| Display | JC3248W535C 480×320 TFT, **AXS15231B combo driver** (QSPI display + I2C capacitive touch); native panel is 320×480, presented **landscape** via LVGL `rotation: 90` |
| Backlight | Dedicated LEDC PWM output on GPIO1 (5 kHz, active high) — the panel driver does **not** manage the backlight. Exposed as a dimmable monochromatic light (`backlight`), restored `ALWAYS_ON` |
| DAC | PCM5102-style I2S stereo decoder, line-level analog out to an external amplifier |
| Status sensor | `binary_sensor` (platform `status`) "ESP Media Player Radio Status" |

### Wiring (from [`packages/esp-web-radio-hardware.yaml`](packages/esp-web-radio-hardware.yaml) and [`packages/esp-web-radio-audio.yaml`](packages/esp-web-radio-audio.yaml))

| Signal | GPIO | Notes |
|---|---|---|
| Display QSPI CLK | 47 | single clock, 4 data lines (`spi` type `quad`) |
| Display QSPI DATA | 21, 48, 40, 39 | 4-bit QSPI |
| Display CS | 45 | 40 MHz data rate |
| Touch I2C SDA / SCL | 4 / 8 | 400 kHz |
| Backlight (LEDC) | 1 | 5 kHz PWM, active high |
| I2S BCLK | 6 | |
| I2S LRCLK | 5 | |
| I2S DOUT | 15 | to PCM5102 DIN |
| I2S MCLK | 16 | optional — see note below |
| DAC MUTE (XSMT) | 7 | active low: LOW mutes |

**PCM5102 notes** (documented in the audio package):
- The PCM5102 has a **built-in PLL**, so an external MCLK may not be required; MCLK is still wired to GPIO16.
- The dedicated **MUTE pin** needs a **logic LOW** to mute (default = unmuted). The firmware drives it through a GPIO `switch` (`dac_mute`, inverted, `restore_mode: ALWAYS_OFF`).

Reference photos of the board and DAC pinouts/jumpers are included at the repo root: [`JC3248W535C_pin_back.png`](JC3248W535C_pin_back.png), [`PCM5102_pin_front.png`](PCM5102_pin_front.png), [`PCM5102_pin_back.png`](PCM5102_pin_back.png), [`PCM5102_jumper_functions.png`](PCM5102_jumper_functions.png).

---

## Getting started / build & flash

### Prerequisites

- **ESPHome** — typically installed on a Home Assistant host (e.g. the official ESPHome add-on) or via `pip install esphome`. Compilation for this project is normally performed on the Home Assistant host.
- A serial connection (USB) for the first flash; subsequent updates go over **OTA** (password-protected).
- Station presets live in **Home Assistant** (see [`ha_template_sensors.yaml`](ha_template_sensors.yaml)) — the device reads them through the native API. When HA is unreachable, a hardcoded fallback list (root [`offline_stations.yaml`](offline_stations.yaml) mirrors the same 4 stations with direct URLs; the device-side copy lives in [`packages/esp-web-radio-offline_stations.yaml`](packages/esp-web-radio-offline_stations.yaml)) keeps the radio fully usable.

### Secrets file

Credentials are stored in `secrets_radio.yaml` at the project root. The file is **git-ignored** and must never be committed. It is passed explicitly to ESPHome:

```yaml
# secrets_radio.yaml (do NOT commit)
wifi_ssid: "YourSSID"
wifi_password: "YourPassword"
ap_password: "FallbackAPPassword"
api_encryption_key: "base64-encoded-32-byte-key"
ota_password: "YourOTAPassword"
timezone: "Europe/Moscow"
```

See [Configuration & secrets](#configuration--secrets) for the full list of keys.

### Validate the configuration

```bash
esphome config esp-web-radio.yaml --secrets secrets_radio.yaml
```

### Compile & flash

```bash
# compile + upload over serial/USB (or OTA if the device is already running)
esphome run esp-web-radio.yaml --secrets secrets_radio.yaml
```

You can also run the lightweight helpers (pure Python, ignore ESPHome tags):

```bash
python tests/yaml_syntax_check.py      # YAML syntax for every config file
python tests/font_codepoint_check.py   # every MDI codepoint exists in the webfont
```

---

## Project structure

```
esp-web-radio/
├── esp-web-radio.yaml                     # Entry point: platform, board, flash/PSRAM,
│                                          #   packages list, wifi/api/ota, logger +
│                                          #   debug diagnostics (reset reason, heap/
│                                          #   PSRAM sensors, BOOT/heartbeat logs),
│                                          #   globals, on_boot
├── ha_template_sensors.yaml               # Home Assistant-side template sensors defining
│                                          #   4 station presets (Name + MA favorite URI,
│                                          #   library://radio/<uuid>) — import into HA,
│                                          #   consumed via the native API
├── offline_stations.yaml                  # HA-side template sensors for the same 4 stations
│                                          #   with DIRECT stream URLs — the no-HA fallback
│                                          #   list; mirrored device-side in
│                                          #   packages/esp-web-radio-offline_stations.yaml
├── TODO.md                                # Roadmap — canonical checklist (completed vs remaining)
│
├── designer/
│   └── lvgl_designer_NowPlaying.yaml      # ARCHIVED origin of the Now Playing page (LVGL
│                                          #   Designer export). Reference only — NOT included
│                                          #   in the build.
│
├── fonts/
│   └── materialdesignicons-webfont.ttf    # MDI icon font, embedded via font `extras`
│
├── packages/
│   ├── esp-web-radio-hardware.yaml        # SPI/QSPI display bus, I2C touch, backlight LEDC,
│   │                                      #   status binary_sensor
│   ├── esp-web-radio-audio.yaml           # i2s_audio, PCM5102 speaker, mixer + resamplers,
│   │                                      #   DAC mute switch, media_player esp_media_player
│   ├── esp-web-radio-homeassistant.yaml   # time sources (HA + NTP), station_1..4_url sensors,
│   │                                      #   station_1..12_name sensors, now_playing/artist/
│   │                                      #   track + art presence, player_state,
│   │                                      #   position/duration, current_station global
│   ├── esp-web-radio-offline_stations.yaml# Hardcoded station names + direct URLs for no-HA
│   │                                      #   mode (fallback when the HA API is unreachable)
│   ├── esp-web-radio-lvgl_ui.yaml         # LVGL framework (rotation, idle, buffers),
│   │                                      #   top-layer status widgets, boot screen, OTA popup,
│   │                                      #   bottom nav glyph indicator, connectivity flags +
│   │                                      #   `update_clock` / `update_nav_indicator` /
│   │                                      #   `refresh_stations` / `play_station` scripts
│   ├── esp-web-radio-page_now_playing.yaml# The main UI page: geometry + widget bindings
│   ├── esp-web-radio-page_stations.yaml   # Stations page: dynamic 2x6 station button grid,
│   │                                      #   hidden empty slots, vertical scrollbar
│   ├── esp-web-radio-page_ap_setup.yaml   # AP mode / WiFi setup page: shown when the
│   │                                      #   fallback AP + captive portal are active —
│   │                                      #   SSID, password, setup URL; locked page
│   ├── esp-web-radio-page_settings.yaml   # Info/Settings page: WiFi signal + dBm,
│   │                                      #   uptime, editable startup volume
│   │                                      #   (popup slider), visualizer toggle;
│   │                                      #   scrollable; idle/wake behavior
│   ├── lvgl_theme.yaml                    # Central dark-orange palette, lvgl.theme defaults,
│   │                                      #   shared style_definitions
│   ├── display-fonts.yaml                 # Font strategy: Roboto gfonts, Cyrillic coverage,
│   │                                      #   MDI extras, text_* icon substitutions
│   └── common-colors.yaml                 # Named color: entities (ESPHome `color:` ids)
│
├── custom_components/
│   └── spectrum_tap/                      # Custom speaker passthrough (plans/visualizer.md):
│                                          #   taps PCM between the mixer and the I2S DAC,
│                                          #   128-pt FFT -> 16 bands -> LVGL visualizer
│                                          #   (loaded via external_components in the main
│                                          #   config)
│
├── tests/
│   └── yaml_syntax_check.py               # YAML syntax check helper for the config files
│
└── *.png                                  # Hardware reference photos (board, DAC pinouts)
```

The main config pulls the packages in via `packages:` — note that [`designer/lvgl_designer_NowPlaying.yaml`](designer/lvgl_designer_NowPlaying.yaml) and [`ha_template_sensors.yaml`](ha_template_sensors.yaml) are **not** part of the ESPHome build.

---

## Configuration & secrets

The main config uses these secrets (all `!secret` usages in [`esp-web-radio.yaml`](esp-web-radio.yaml)):

| Secret | Used by |
|---|---|
| `wifi_ssid` | `wifi:` |
| `wifi_password` | `wifi:` |
| `ap_password` | fallback AP ("ESP Radio Fallback") of `wifi:`; also shown on the AP setup page via the `ap_password_display` substitution and encoded into the AP-mode WiFi-connection QR (`WIFI:` string) |
| `api_encryption_key` | encrypted `api:` |
| `ota_password` | `ota:` |
| `timezone` | `time:` platforms `homeassistant` + `sntp` in [`packages/esp-web-radio-homeassistant.yaml`](packages/esp-web-radio-homeassistant.yaml) |

> **Security:** never commit real values. `secrets_radio.yaml` is git-ignored and must be supplied to ESPHome with `--secrets secrets_radio.yaml`.

Other key settings in the main config:

- **Platform options** — `upload_speed: 921600`, `build_unflags: -Werror=all`, `board_build.flash_mode: dio`, flash clock 80 MHz, CPU 240 MHz.
- **SDK config** — 240 MHz CPU, 64 KB data cache with 64-byte lines, code/data fetch from PSRAM (`CONFIG_SPIRAM_FETCH_INSTRUCTIONS`, `CONFIG_SPIRAM_RODATA`), and TLS without server-certificate verification (`CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY`) so HTTPS radio streams (direct `https://` URLs, `http`→`https` redirects, and HA `radio_browser` streams) can be played by the media player.
- **`on_boot`** — shows `page_now_playing`, turns on the backlight, then sets `boot_done = true`.
- **Globals** — `boot_done` (bool) and `boot_spinner_angle` (int).
- **Substitutions** — `ap_ssid` ("ESP Radio Fallback", shared with `wifi.ap.ssid`), `ap_password` (shared with `wifi.ap.password` and the AP-mode WiFi-connection QR), and `ap_password_display` (from `!secret ap_password`) supply the text shown on the AP setup page.
- **Fallback mode** — when WiFi is unavailable the device starts the "ESP Radio Fallback" AP with a captive portal after `ap_timeout: 20s`; the display then switches to the setup page (see [UI architecture & page bindings](#ui-architecture--page-bindings)).

---

## Theming system

All colors and widget defaults are centralized in [`packages/lvgl_theme.yaml`](packages/lvgl_theme.yaml). The old blue-accented inline theme was removed from the UI package — [`packages/esp-web-radio-lvgl_ui.yaml`](packages/esp-web-radio-lvgl_ui.yaml) must **not** declare its own `lvgl.theme` / `style_definitions` (that would cause a duplicate-key merge conflict).

### Dark-orange palette (substitutions)

| Substitution | Value | Role |
|---|---|---|
| `color_accent` | `0xF97316` | Primary orange |
| `color_accent_dark` | `0xD14B00` | Darker orange — gradient end / pressed state |
| `color_surface` | `0x070E1B` | App background |
| `color_surface_alt` | `0x0E1216` | Alternate surface (bottom layer / footer) |
| `color_panel` | `0x162038` | Panels, cards, controls |
| `color_panel_grad` | `0x0F1929` | Panel gradient end |
| `color_text_primary` | `0xF1F5F9` | Main text |
| `color_text_muted` | `0x8E9FB8` | Secondary text |
| `color_border` | `0x162038` | Borders (same hue as panels) |

Plus font aliases: `font_body` → `f_body`, `font_small` → `f_small`, `font_title` → `f_title`, `font_icon` → `menu24`.

### `lvgl.theme` per-widget defaults

`dark_mode: true` with defaults for `label`, `button` (orange vertical gradient, pressed `0xEA580C`/`0x9A3412`), `switch`, `slider`, `bar` (orange indicator at 80% opacity), `arc`, and `spinner`.

### Shared `style_definitions`

Named styles referenced by pages with `styles: <name>`:

`card`, `panel`, `icon_button`, `primary_button`, `text_primary`, `text_muted`, `text_title`, `text_body`, `text_small`, `controls_row`, `station_button`.

### Theming rule

Pages and elements should reference **shared styles and color substitutions** (`${color_*}`) instead of raw hex literals. When the Designer export was ported into [`packages/esp-web-radio-page_now_playing.yaml`](packages/esp-web-radio-page_now_playing.yaml), all inline hex colors were replaced accordingly (only the progress-bar geometry keeps a couple of explicit colors).

---

## UI architecture & page bindings

### Layout

Three swipeable pages (480×320 landscape, LVGL `rotation: 90`), declared under `lvgl.pages` and driven by the framework block in [`packages/esp-web-radio-lvgl_ui.yaml`](packages/esp-web-radio-lvgl_ui.yaml): **Now Playing**, **Stations** and **Settings**, plus the locked **AP setup** page (`skip: true`, excluded from `lvgl.page.next/previous`). Swipe gestures (and `page_wrap: true`) cycle Now Playing ↔ Stations ↔ Settings:

[`packages/esp-web-radio-page_now_playing.yaml`](packages/esp-web-radio-page_now_playing.yaml) defines `page_now_playing`:

- Header row: station logo panel (left), station title + artist + track (center), album-art panel (right)
- Visualizer panel strip
- Progress bar + elapsed/total time labels
- Control row: prev, play/pause, next, mute, volume slider
- Persistent top layer: boot screen (dismissed by touch, `wifi.on_connect`, or AP mode), OTA popup, WiFi status icon, HA status icon, bottom nav glyph indicator

#### Stations page (`page_stations`)

[`packages/esp-web-radio-page_stations.yaml`](packages/esp-web-radio-page_stations.yaml) defines `page_stations`, reachable by a right swipe (or any next-page action):

- Dynamic 2×6 button grid (12 slots `st_btn_1`..`st_btn_12`). The `refresh_stations` script fills each slot's label from the HA `station_N_name` sensors while the API is connected, otherwise from the hardcoded `offline_station_N_name` substitutions in [`packages/esp-web-radio-offline_stations.yaml`](packages/esp-web-radio-offline_stations.yaml).
- Empty slots stay hidden and the grid height is set to the number of visible rows × 56 px, so a vertical scrollbar appears automatically once more than 8 stations are active.
- Pressing a station runs the `play_station` script: while connected, playback is routed through Home Assistant to the Music Assistant mirror (`${ma_player_entity}`) using the MA favorite URI from the HA template sensors (`library://radio/<uuid>`, playable only via HA/MA — see [Music Assistant](#music-assistant)); in no-HA mode it uses the native `media_player.play_media` action with the direct offline URL; the page then returns to Now Playing.

#### Info/Settings page (`page_settings`)

[`packages/esp-web-radio-page_settings.yaml`](packages/esp-web-radio-page_settings.yaml) defines `page_settings`, the third swipeable page (reached by a left swipe from Stations; wraps to Now Playing). Fixed 5-row layout (48 px rows at the original 56 px pitch, ending at y=326) — the page is vertically scrollable (`scrollbar_mode: AUTO`, Stations-page pattern) once the rows exceed the 320 px viewport, so content pans under the transparent bottom nav overlay; static-layout contract (hide/show only, absolute positions, no reflow):

- **WiFi signal** — a `wifi_signal` sensor (ESPHome `platform: wifi_signal`, re-added to [`packages/esp-web-radio-homeassistant.yaml`](packages/esp-web-radio-homeassistant.yaml)) feeds the `refresh_settings_info` script, which picks the signal-strength glyph by threshold (`wifi_100` ≥ −50 dBm, `wifi_75` ≥ −65 dBm, `wifi_50` ≥ −78 dBm, else `wifi_25`) and shows the numeric dBm label. Because the sensor measures the ESP32's own radio, the row works identically in HA-connected and wifi-only modes; while disconnected the icon dims to the lowest bar and the dBm label hides.
- **Uptime** — the `uptime` sensor (re-added) drives the `uptime_human` template text sensor (re-added), formatted `Xd Xh Xm` / `Xh Xm` / `Xm`; the label updates every 60 s via `refresh_settings_info`.
- **Startup volume (editable)** — a persisted `startup_volume` float global (`restore_value: true`, default 50 %) displayed as a percentage. Tapping the row (a `button` with the stations `station_button` style, so it visibly reacts to touch) opens a page-local popup (`settings_vol_popup`: horizontal slider + percent label, drawn last over the rows) that mirrors the Now Playing volume popup pattern — `show_settings_vol_popup` script with `mode: restart` and a 5 s auto-hide. On release the value is stored in the global, pushed to the player right away via the native `media_player.volume_set` action (audible feedback; the existing `on_volume` trigger keeps the Now Playing popup slider/percent label in sync) and the row label is refreshed via `refresh_settings_info`. At boot ([`esp-web-radio.yaml`](esp-web-radio.yaml)), after `sync_volume_ui`, the CONFIGURED `startup_volume` is re-applied with a guarded `volume_set` — deliberately overriding the component-restored live volume so the user-configured boot level wins (deviation from pure component-volume-restore, documented in code comments).
- **Visualizer toggle** — an LVGL `switch` (`settings_sw_visualizer`) bound to the persisted `visualizer_enabled` bool global (default ON). It gates the Now Playing spectrum strip: `on_play` shows `mp_visualizer` only while enabled, and the `viz_animation` interval skips all work while disabled (both the strip visibility AND the bar-math updates — one flag check per tick). Re-enabling while playing re-shows the strip; pause/idle hiding stays unconditional. The FFT/speaker tap (`spectrum_tap`) keeps running regardless — only the LVGL strip + animation are gated. UI-only setting: no HA switch mirror (the project has no template-switch pattern; two-way sync would risk echo loops).
- **Last reset reason** — the `reset_reason` text sensor of the `debug` component ([`esp-web-radio.yaml`](esp-web-radio.yaml)) feeds the row via `refresh_settings_info`; the reason is reported by hardware/RTC and **survives software reboots**, so after a spontaneous restart this row names what ended the previous session (power-on, software, watchdog, brown-out…). The whole row starts hidden and appears only while the sensor has a state (hide-only, no reflow). Diagnosing restarts: [`plans/troubleshooting-restarts.md`](plans/troubleshooting-restarts.md).
- **Idle/inactivity behavior** (no settings row — behavior, not data): the volume popup auto-dismisses after 5 s of inactivity (`show_volume_popup`, `mode: restart`) and waking the device from global idle (LVGL `on_idle` sleep) returns to Now Playing — implemented inside the native `touchscreen.on_release` wake handler ([`packages/esp-web-radio-hardware.yaml`](packages/esp-web-radio-hardware.yaml)) that performs `lvgl.resume`, without extra timer scripts.

The page title uses the cogs glyph (`text_nav_settings`); row icons come from `menu24` (wifi-strength-1..4, volume-high, restart) plus one new 24 px glyph `mdi-equalizer` (`text_equalizer`) for the visualizer toggle row — negligible flash cost.

#### AP setup page (`page_ap_setup`)

[`packages/esp-web-radio-page_ap_setup.yaml`](packages/esp-web-radio-page_ap_setup.yaml) adds `page_ap_setup`, auto-shown when the fallback AP + captive portal become active (`wifi.ap_active`, watched by the `ap_state_watch` interval polling every 500 ms):

- Two-column layout: an info panel (left) shows the AP **network name** (`${ap_ssid}`, default "ESP Radio Fallback"), the **password** (`${ap_password_display}`, from `!secret ap_password`), and the setup **URL** — single-sourced from the `${ap_url}` substitution in [`esp-web-radio.yaml`](esp-web-radio.yaml), default http://192.168.4.1. The right column holds a scannable **QR code** (`ap_qr` qrcode widget, inverted light-on-dark modules) encoding the WiFi connection string `WIFI:T:WPA;S:${ap_ssid};P:${ap_password};;` so a phone can join the fallback AP without typing the credentials.
- Locked: declared `skip: true` (excluded from `lvgl.page.next/previous`) and the swipe handlers are guarded while `wifi.ap_active`, so the page cannot be navigated away from; the bottom nav indicator is hidden while it is shown.
- While it is up, the `wifi_status` label blinks the `wifi-cog` glyph at ~400 ms (`blink_wifi_status` script; icon set via `lvgl.label.update` in `enter_ap_mode`).
- Idle/sleep is disabled while `wifi.ap_active` (the `on_idle` handler is guarded), so the QR/SSID/password stay readable for as long as the setup page is needed.
- Clears automatically when the station reconnects — `exit_ap_mode` restores the plain `wifi` glyph, returns to `${homepage}` (Now Playing), and `wifi_status` settles back to its steady state.

#### Audio visualizer (spectrum_tap)

Implemented per [`plans/visualizer.md`](plans/visualizer.md): a custom speaker
component (`custom_components/spectrum_tap`, loaded via `external_components`)
is inserted between the mixer and the I2S DAC (the plan's single-line change in
[`packages/esp-web-radio-audio.yaml`](packages/esp-web-radio-audio.yaml)) and
transparently forwards every PCM frame while feeding a 128-point radix-2 FFT
(Hann window, 16 logarithmic bands over ~375 Hz–18 kHz, band tables pre-computed
for the 48 kHz stereo stream):

```
media_player → resampler → mixer → spectrum_tap → i2s_speaker → I2S → PCM5102A
                                         ↓
                                   spec_bands[0..15] → 16 LVGL bars
```

- **Bars** — 16 `viz_bar_0..15` objects are declared as children of the fixed
  `mp_visualizer` strip (static-layout contract: hiding the strip hides them,
  nothing reflows).
- **Driver** — the `viz_animation` interval (40 ms = 25 fps) reads
  `spec_bands[]` and animates heights bottom-anchored with attack/decay
  smoothing (fast rise — 50/50 blend, slow fall — ×0.85, GAIN 300 — the plan's
  exact tuning). It runs only while `mp_visualizer` is visible (= playing, per
  the `on_play`/`on_pause`/`on_idle` triggers in
  [`packages/esp-web-radio-audio.yaml`](packages/esp-web-radio-audio.yaml)),
  the `visualizer_enabled` toggle (Info/Settings page) is ON, and LVGL is not
  paused; the FFT itself runs only while PCM flows through the tap, so
  idle/paused costs nothing.
- **Both pipelines** (media + announcements) are mixed before the tap point, so
  TTS interruptions animate the visualizer too.
- **Custom component internals, build-time checks and estimated memory/CPU
  impact:** see [`plans/completed_visualizer.md`](plans/completed_visualizer.md).

#### Bottom nav indicator (`top_layer`)

The old prev/home/next buttonmatrix was replaced (TODO "Re-design navigation bar on top_layer") by a **pure indicator** — it is not tappable (`clickable: false`), navigation happens by swiping only:

- **Bar geometry** — 160 px wide (1/3 of the 480 px screen), transparent background, centered at the screen bottom (`align: bottom_mid`), 26 px tall (a 24 px focused glyph with ~1 px padding). Its top edge lands at y=294, exactly where the Now Playing control row ends; this 26 px overlay is smaller than the previous 30 px buttonmatrix and stays within the ~40 px bottom budget the Stations page scroll area accounts for.
- **Slots (page order)** — Now Playing (`mdi-play-circle-outline`), Stations (`mdi-radio`), Settings (`mdi-cogs`).
- **Focus** — the active page's glyph renders in `menu24` (24 px, accent color); inactive glyphs render in `nav20` (20 px, muted). Focus is tracked by the `current_page` global and applied by the `update_nav_indicator` script, wired into every page-change site (swipe handlers, `play_station`, `exit_ap_mode`, `wifi.on_connect`, `on_boot`, and the wake branch of `touchscreen.on_release`).
- **Swipe math** — with three swipeable pages the shared handlers in [`packages/swipe_navigation.yaml`](packages/swipe_navigation.yaml) advance `current_page` modulo `${swipeable_page_count}` (left: `(cp+1) % 3`, right: `(cp+2) % 3`), matching `lvgl.page.next/previous` with `page_wrap: true` (`page_ap_setup` stays `skip: true`, so it never enters the sequence).
- `enter_ap_mode` still hides the whole `top_layer` while the AP page is up; on exit the indicator is refocused on Now Playing.

### Bindings (widget → sensor/action)

Driven by [`packages/esp-web-radio-homeassistant.yaml`](packages/esp-web-radio-homeassistant.yaml) over the native API:

| Widget ID | Type | Bound to | Direction |
|---|---|---|---|
| `mp_station_title` | label | `now_playing` sensor, `media_title` attribute | HA → UI |
| `mp_now_playing_artist` | label | `now_playing_artist` sensor, `media_artist` attribute | HA → UI (hidden when empty) |
| `mp_lnow_playing_track` | label | `now_playing_track` sensor, `media_title` attribute | HA → UI (hidden when empty) |
| `mp_station_logo` | obj (panel) | — (hidden; station-picture source pending roadmap item 4) | — |
| `mp_now_playing_art` | obj (panel) | `now_playing_art` sensor, `media_image_url` attribute | HA → UI (shown while metadata exists) |
| `mp_visualizer` | obj (panel + 16 bar children `viz_bar_0..15`) | `media_player.on_play` / `on_pause` / `on_idle` — shown only while playing; bar heights driven by the `viz_animation` interval from `spectrum_tap` FFT bands (`spec_bands[]`) | native → UI |
| `mp_bar_progress` | bar | `player_position` / `player_duration` (percent, clamped 0–100); hidden until a duration is known, re-hidden on `on_idle` | HA → UI |
| `mp_lbl_elapsed` | label | `player_position` (`mm:ss`); hidden until a position is known (0 at track start is valid and shows), re-hidden on `on_idle` | HA → UI |
| `mp_lbl_total` | label | `player_duration` (`mm:ss`); hidden until a duration is known, re-hidden on `on_idle` | HA → UI |
| `mp_lbl_play_icon` | label | `player_state` (HA) + `media_player.on_play/on_pause/on_idle` — swaps `${text_pause}` / `${text_play}` glyph | HA/native → UI |
| `volume_slider` / `mp_lbl_volume` | slider / label | `media_player` `on_volume` trigger — the component is the single source of truth; slider + `%` text follow every volume change | native → UI |
| `st_lbl_1..12` | label | `station_N_name` sensors / `offline_station_N_name` via `refresh_stations` | HA/offline → UI |
| `st_btn_1..12` | button | runs `play_station` (plays `${current_station}`) | UI → playback |
| `settings_lbl_wifi_icon` | label | `wifi_signal` (RSSI thresholds −50/−65/−78 → `wifi_100/75/50/25`) via `refresh_settings_info` | native → UI |
| `settings_lbl_rssi` | label | `wifi_signal` ("−NN dBm"); hidden while disconnected | native → UI |
| `settings_lbl_uptime` | label | `uptime_human` text sensor (`Xd Xh Xm`) via `refresh_settings_info` | native → UI |
| `settings_lbl_startup_vol` | label | `startup_volume` persisted global (configured boot volume, editable via the settings popup) via `refresh_settings_info` | native → UI |
| `settings_sw_visualizer` | switch | `visualizer_enabled` persisted global — gates `mp_visualizer` show on `on_play` + all `viz_animation` work | UI → global/now-playing |
| `settings_vol_slider` / `settings_lbl_vol_value` | slider / label | `startup_volume` global — on release: store + native `volume_set` (audible feedback; `on_volume` keeps the Now Playing popup in sync) | UI → native/global |
| `settings_lbl_reset_reason` | label | `reset_reason` debug text sensor (last reset reason) via `refresh_settings_info`; row `settings_row_reset` shown only while the sensor has a state | native → UI |
| `mp_btn_play` | button | toggles play/pause via HA when connected (`player_playing` decides which); ignored offline | UI → HA |
| `mp_btn_prev` | button | previous station (wrap `${station_count}` → 1) via `play_station` | UI → playback |
| `mp_btn_next` | button | next station (wrap 1 → `${station_count}`) via `play_station` | UI → playback |
| `mp_btn_mute` | button | native mute action (toggle); `on_mute`/`on_unmute` do glyph color, `dac_mute` hardware mute, guarded HA push | UI → native |
| `volume_slider` | slider | native set-volume action (`x / 100.0`) on release | UI → native |

**Volume/mute model (event-driven):** the speaker media player component
(`esp_media_player`, [`packages/esp-web-radio-audio.yaml`](packages/esp-web-radio-audio.yaml))
is the single source of truth for volume and mute. Every control on this page is
a pure native action (step up/down, set volume, mute toggle) with no UI or HA
side-effects; all reactions — popup slider + percent label, mute glyph color,
PCM5102 `dac_mute` hardware mute, and the guarded device→HA `is_volume_muted`
push — run inside the component's `on_volume` / `on_mute` / `on_unmute`
triggers, which fire identically for local commands and HA-originated changes.
A boot-time `sync_volume_ui` script restores the persisted volume into the UI;
right after it, `on_boot` re-applies the configured `startup_volume` (the
Info/Settings editable boot-volume setting) with a guarded `volume_set`,
overriding the component restore by design. The old HA volume/mute pull
sensors were removed. See
[`plans/refactor-media-player-hooks.md`](plans/refactor-media-player-hooks.md).

### Station selection & playback routing

A `globals` entry `current_station` (int, initial `1`, no restore) tracks the active preset; the `${station_count}` substitution (default `4`) is the wrap modulus. All playback — the Now Playing prev/next buttons, the play/pause toggle, and every Stations-page button — funnels through the `play_station` script, which picks the URL source by mode:

- **HA connected** — `homeassistant.action: media_player.play_media` against the Music Assistant mirror (`${ma_player_entity}`) with `station_1_url` … `station_4_url` (the HA template sensors now carry MA favorite URIs `library://radio/<uuid>`, resolved by HA/MA) and `media_content_type: "music"`.
- **no-HA (wifi_only)** — the native `media_player.play_media` action with the direct `offline_station_1_url` … `offline_station_4_url` values, and `mp_station_title` is filled from the offline station name.

The station name sensors (`station_1_name` … `station_12_name`) feed the Stations page through the `refresh_stations` script (also re-run on boot, WiFi connect, and HA API connect/disconnect); slots 5–12 are declared but stay hidden until matching entities exist in Home Assistant.

---

## Music Assistant

Online station playback is routed through **Music Assistant (MA)**: MA decodes any
incoming codec (including AAC/HE-AAC, which the device's own decoders cannot play)
into PCM and re-encodes it into one of the codecs the player supports
(FLAC/MP3/OPUS/WAV), so the device always receives a stream it can decode.
Playback commands target the **MA mirror** of the device player
(`${ma_player_entity}`); metadata (`media_title` / `media_artist` /
`media_image_url`) is read from that mirror; the audio itself still flows into
`media_player.esp_media_player` — the device-side pipeline (decode → resample →
mixer → spectrum_tap → I2S) is unchanged. The offline/no-HA path is untouched.

### Setting up MA (HA side)

1. Install the **Music Assistant add-on** on HAOS and the `music_assistant`
   integration in HA (MA server ≥ 2.4; the MA Home Assistant plugin installs
   automatically on HAOS).
2. In MA UI add the player provider **"Home Assistant Media Players"** and select
   `media_player.esp_media_player`.
3. Add the **Radio Browser** music source.
4. Create **favorites** for the stations (Radio Browser → add to library).
5. Find the **MA mirror entity_id** (HA → Settings → Devices & services → Music
   Assistant → `media_player.*` entities) and write it into `ma_player_entity` in
   [`esp-web-radio.yaml`](esp-web-radio.yaml) — the default is
   `media_player.esp_media_player`, i.e. the old direct behavior, until switched.
6. Find the **favorite URIs** (HA Logbook after the first playback via MA UI, or
   the `music_assistant.search` service) and write them into
   [`ha_template_sensors.yaml`](ha_template_sensors.yaml); re-import the file in HA.

### Station → URI mapping

Values live in [`ha_template_sensors.yaml`](ha_template_sensors.yaml); `station_count`
in [`esp-web-radio.yaml`](esp-web-radio.yaml) is `12` (offline fallback still covers only
slots 1–4). The numeric library IDs below are the MA favorite IDs captured so far —
verify each via the HA Logbook / `music_assistant.search` after setup:

| # | Station | URI |
|---|---|---|
| 1 | Радио Дача | `library://radio/1` |
| 2 | Радио Ваня | `library://radio/6` |
| 3 | Наше Радио | `library://radio/2` |
| 4 | Rock FM | `library://radio/8` |
| 5 | Авторадио | `library://radio/16` |
| 6 | Дорожное Радио | `library://radio/13` |
| 7 | Радио Maximum | `library://radio/10` |
| 8 | DFM | `library://radio/14` |
| 9 | NRJ | `library://radio/12` |
| 10 | Русское Радио | `library://radio/15` |
| 11 | Ultra | `library://radio/17` |
| 12 | Retro FM | `library://radio/9` |

### Codec

The stream delivered to HA players defaults to **MP3** (up to 48 kHz/16 bit); MA
decodes any input to PCM and encodes into the player codec, so the device always
gets one of its supported codecs. The MA player setting "Output codec": **MP3** is
the default and the fallback for flaky networks; **FLAC** is the recommended
quality option (no generation loss — AAC → PCM → FLAC — and the device decodes
FLAC natively; see the plan §3.3).

### Metadata

Metadata is read from the MA mirror (`${ma_player_entity}`); `player_state`
stays on the original `media_player.esp_media_player` (the play/pause glyph is
already driven by the native `on_play`/`on_pause`/`on_idle` triggers).

- **Title** (`mp_station_title`) is additionally set *immediately* by the
  `play_station` HA branch from the selected `station_N_name` sensor — the
  mirror's `media_title` can lag behind the play command (fixes the old TODO
  "station name is not updated"); the `now_playing` sensor refines it when MA
  metadata arrives.
- **Artist** (`media_artist`) and **cover presence** (`media_image_url` →
  `mp_now_playing_art`) come straight from the mirror.
- **Track** (`mp_lnow_playing_track`) reads `media_album_name`: for radio
  streams MA keeps the station name in `media_title` and maps the ICY
  "Artist - Song" pair into `media_artist` + `media_album_name`.
  ⚠️ Verify on device; if MA exposes the song elsewhere, remap the
  `now_playing_track` attribute in [`packages/esp-web-radio-homeassistant.yaml`](packages/esp-web-radio-homeassistant.yaml)
  (an empty value just keeps the label hidden, so a wrong mapping degrades
  gracefully).

### Limitations

MA exposes no stable direct stream URLs — the URL sensors remain the play_media
source (MA favorite URIs instead of radio_browser ones). Live radio streams carry
no position/duration, so the progress bar stays hidden on radio.

Full migration plan (rollout order, validation checklist, rollback): see
[`plans/migration-music-assistant.md`](plans/migration-music-assistant.md).

---

## Station logos & artwork

Each Stations-page button carries a **logo slot** (`st_logo_1..12`, 40×40
accent-tinted rounded rect) whose child is an `lvgl.image` fed by the built-in
[`online_image`](https://esphome.io/components/online_image.html) component —
12 stubs live in [`packages/esp-web-radio-station_images.yaml`](packages/esp-web-radio-station_images.yaml)
(`station_img_1..12`: PNG, `resize: 40x40`, downloads only on demand via
`set_url`; the compile-time URLs are dead placeholders that are never fetched).
Until a logo downloads, the tinted slot itself is the placeholder; PNG
transparency composites over it.

**How logos are delivered** — the third station parameter in
[`ha_template_sensors.yaml`](ha_template_sensors.yaml), `Radio Station N Image`,
holds **either**:

- a **plain file name / sub-path relative to `/local/`** — `radio-1.png` (the
  file `/config/www/radio-1.png`, served at `<ha_url>/local/radio-1.png`) or
  `icons/radio-1.png` (the file `/config/www/icons/radio-1.png`), **or**
- an **absolute http(s) URL** (any web resource, used as-is instead of the HA
  host).

**Where to upload:** `/config/www/` (or any `/config/www/<subdir>/` you
reference). Example: a sensor value of `icons/radio-1.png` needs the file at
`/config/www/icons/radio-1.png`.

**Failsafes** (the 2026-10-08 build abort-crashed during a boot-time burst of
12 simultaneous HTTPS fetches, all 404 — logos are strictly non-critical and
must never take the device down): `refresh_stations` converts relative paths
to `<ha_url>/local/<path>` (a leading `www/` is stripped for backward
compatibility) and pushes them via `online_image.set_url` with `update: false`
— **no download starts from sensor updates**. The dedicated
`refresh_station_images` script ([`packages/esp-web-radio-lvgl_ui.yaml`](packages/esp-web-radio-lvgl_ui.yaml))
then fetches the images **one at a time** (1 s apart, at most a single
HTTPS/TLS request in flight); each image has a 16 KB decode buffer (vs the
64 KB default) and an `on_error` handler ([`packages/esp-web-radio-station_images.yaml`](packages/esp-web-radio-station_images.yaml))
that logs `[IMG] station N logo download failed` and keeps the tinted
placeholder — no retry storms. No reflash is needed to change a logo. Setup:

1. Add `ha_url: "https://<ha-host>"` to `secrets_radio.yaml` (HA base URL,
   no trailing slash).
2. Upload the PNGs to `/config/www/` (or put any web URLs into the sensors)
   and re-import `ha_template_sensors.yaml` in HA.

Notes: PNG decoding (lodepng) adds some flash; decode/resize buffers use PSRAM
(a 40×40 RGB565 slot is 3.2 KB). MA's local artwork endpoint (`media_image_url`,
already wired for the Now Playing `mp_now_playing_art` panel) remains the
natural source for per-track cover art. A fully offline alternative — compiled
`image:` assets baked into flash (≈3 KB per logo, zero runtime) — is possible
but unnecessary while HA is the logo host.

---

## Time sync & device modes

The title-bar clock (`lbl_time` in the LVGL `top_layer`) is fed by two time sources with a fixed priority, selected by the `update_clock` script in [`packages/esp-web-radio-lvgl_ui.yaml`](packages/esp-web-radio-lvgl_ui.yaml):

| Device mode | Condition | Clock source | Stations list / playback |
|---|---|---|---|
| **online** | HA API connected (`ha_connected`) | `esptime` — `platform: homeassistant` | HA station names; playback via the MA mirror (`library://` favorite URIs) |
| **wifi_only** | WiFi up, no HA API (`wifi_connected`) | `wifi_time` — `platform: sntp` (NTP-maintained local clock) | offline list (`offline_station_N_*`); native direct-URL playback |
| **offline** | no WiFi (`wifi_connected == false`) | none — `lbl_time` hidden | AP setup page shown; no station list |

Both `time` platforms are declared in [`packages/esp-web-radio-homeassistant.yaml`](packages/esp-web-radio-homeassistant.yaml) and share the `timezone` secret:

- `esptime` (Home Assistant) is preferred whenever the native API is connected — the **online** mode source.
- `wifi_time` (NTP via `sntp`, servers `pool.ntp.org` / `time.google.com`) maintains the ESPHome internal clock, which acts as the device's local RTC while powered — software-only, no external RTC chip is wired on this board. It is used in **wifi_only** mode and takes over immediately if HA disconnects while WiFi stays up.

`update_clock` selection logic (single place, triggered by `api.on_client_connected/disconnected`, `wifi.on_connect/on_disconnect`, both platforms' `on_time` at the minute boundary, and sntp `on_time_sync` for an immediate refresh after an NTP sync):

1. `ha_connected && esptime.now().is_valid()` → show `lbl_time` with `esptime` HH:MM.
2. else `wifi_connected && wifi_time.now().is_valid()` → show `lbl_time` with `wifi_time` HH:MM.
3. else → hide `lbl_time`.

Connectivity flags (both `bool` globals in [`packages/esp-web-radio-lvgl_ui.yaml`](packages/esp-web-radio-lvgl_ui.yaml)):

- `ha_connected` — set by the `api.on_client_connected` / `on_client_disconnected` handlers.
- `wifi_connected` — set by the `wifi.on_connect` / `on_disconnect` handlers. Required because the internal clock keeps ticking after a WiFi drop, so WiFi state alone distinguishes **offline** from **wifi_only**.

> Station selection and playback routing per mode were implemented in Improvements 4–6 — see [`plans/improvements-4-6.md`](plans/improvements-4-6.md).

---

## Diagnostics & troubleshooting

Added for the TODO item "add logs to troubleshoot spontaneous device restarts" — full investigation, documented capabilities table, signal cheat-sheet, reproduction workflow and mitigation knobs live in [`plans/troubleshooting-restarts.md`](plans/troubleshooting-restarts.md). Short version:

### Instrumentation (in [`esp-web-radio.yaml`](esp-web-radio.yaml))

- **`debug` component** (`update_interval: 60s`) publishing HA entities: `Reset Reason` (text sensor, survives software reboots), `Heap Free`, `Heap Min Free` (lowest free heap since boot — leak/peak detection), `Heap Max Block`, `Heap Fragmentation` (> ~50 % is the allocation-failure danger zone), `Free PSRAM`, `Loop Time` (longest main-loop iteration — blocking-render detection).
- **Boot summary** — the first `on_boot` action logs `[diag] BOOT reset_reason=… uptime=… s heap_free=… psram_free=…`; after a spontaneous restart this line names the reason of the crashed session.
- **Heartbeat** — `interval: heartbeat_diag` every **15 min** logs one `[diag] HEARTBEAT uptime=… heap_free=… heap_min_free=… frag=…% psram_free=… loop_time=…` INFO line, so a captured serial log shows the memory trend up to the crash.

### Logger tuning (documented)

- Steady state stays `DEBUG` (the default; cheap on modern ESPHome). `baud_rate: 115200` explicit.
- On the ESP32-S3 the logger default transport is `USB_SERIAL_JTAG` (native USB, GPIO19/20) — the same path used for the captured logs in the git-ignored `logs/` directory.
- **`VERBOSE` is the reproduction mode only**: it surfaces IDF panic/backtrace/allocation detail but slows the device and can destabilize audio/LVGL timing — switch it in, reproduce, capture, switch back.

### Reading the signals (fast triage)

| After a spontaneous restart you see | Likely cause | Next step |
|---|---|---|
| Next-boot `reset_reason` = `POWERON_RESET` | clean power cycle or true power loss | check PSU/logs for brownout |
| `RTCWDT_BROWN_OUT_RESET` | brown-out (power dip, usually silent) | PSU/wiring |
| `TASK_WDT` / `TG0/TG1WDT_SYS_RESET` / `RTCWDT_SYS_RESET` / `RTCWDT_CPU_RESET` | watchdog reset | find the stalled task in the pre-crash TWDT message |
| `SW_SYS_RESET` / `SW_CPU_RESET` | software restart — typically after a panic handler | decode the backtrace in the preceding log |
| `Guru Meditation Error` + backtrace | CPU exception | `addr2line` the top addresses against the flashed ELF |

Heap exhaustion instead leaves allocation-failure logs and a decaying `heap_min_free`/`psram_free` trend across heartbeats; mitigation knobs (all documented): lower `media_player.buffer_size` (default 1,000,000 B per pipeline), `task_stack_in_psram: true`, or `network.enable_high_performance: false` if the docs' OOM note applies.

---

## Font strategy

Centralized in [`packages/display-fonts.yaml`](packages/display-fonts.yaml).

- **Cyrillic + Latin coverage** — station names can contain Cyrillic (e.g. "Радио Дача"), so every text font includes the Cyrillic alphabet alongside ASCII (explicit glyph strings including `Ёё«»—–°`). The icon-only fonts `menu32` / `nav20` keep a minimal base set (space glyph only) to limit flash usage.
- **Google Fonts** — fonts are loaded from `gfonts://Roboto` (no local font files needed for text).
- **MDI webfont extras** — UI icons come from `fonts/materialdesignicons-webfont.ttf`, embedded via font `extras` (13 glyphs in `menu24` incl. `mdi-restart` + `mdi-equalizer`, 7 in `menu32`, 3 in `nav20`). Each icon is exposed as a `text_*` / `wifi_*` substitution so pages reference readable names instead of raw code points.

| Font id | Size | Used for |
|---|---|---|
| `menu24` | 24 px | Status icons + focused (active) nav glyphs; `${font_icon}` alias |
| `nav20` | 20 px | Inactive bottom-nav glyphs (the three nav icons only) |
| `menu32` | 32 px | Now Playing control-row icons (play/pause, skip, volume) |
| `roboto42` | 42 px | Large text (reserved) |
| `f_title` | 26 px | Page titles (`${font_title}`) |
| `f_body` | 17 px | Body text, time labels, LVGL `default_font` (`${font_body}`) |
| `f_small` | 13 px | Small/muted labels (`${font_small}`) |

Icon substitutions defined: `text_hastatus` (mdi-home-assistant), `text_nav_playing` (mdi-play-circle-outline), `text_nav_settings` (mdi-cogs), `text_nav_stations` (mdi-radio), `wifi_25/50/75/100` (mdi-wifi-strength-1..4), `text_wifi` (mdi-wifi), `text_wifi_cog` (mdi-wifi-cog), `text_play`, `text_pause`, `text_skip_prev`, `text_skip_next`, `text_volume_high`, `text_volume_plus`, `text_volume_minus`, `text_volume_off`, `text_restart` (mdi-restart), `text_equalizer` (mdi-equalizer).

---

## Roadmap

Tracked in [`TODO.md`](TODO.md).

---

## Development conventions

- **Unique IDs** — widget/sensor/global IDs share one global namespace across all packages; never reuse an ID.
- **No duplicate theme/style blocks** — `lvgl.theme` and `style_definitions` live only in [`packages/lvgl_theme.yaml`](packages/lvgl_theme.yaml); redeclaring them elsewhere causes a duplicate-key merge error.
- **No orphan widget references** — when deleting a widget, remove every `lvgl.widget.update/show/hide` reference and any bindings that target it.
- **Use styles/substitutions, not raw hex** — reference `${color_*}` substitutions and named `style_definitions` instead of hard-coding colors on widgets.
- **Adding icons** — when introducing a new MDI glyph, add it to **both** the font `extras` list in [`packages/display-fonts.yaml`](packages/display-fonts.yaml) and a `text_*` substitution; then reference the substitution from the UI.
- **Designer re-imports** — the Designer export in [`designer/`](designer) is archived reference only; the build reads [`packages/esp-web-radio-page_now_playing.yaml`](packages/esp-web-radio-page_now_playing.yaml). Keep the port and the archive in sync manually.
- **Custom components** — local components live in `custom_components/` and are loaded explicitly via `external_components:` in the main config (currently `spectrum_tap`). When updating, keep the folder in sync with the source archive `plans/spectrum_tap.zip`.

## LICENSE
MDI WebFonts: Apache 2.0
FFT Vizualizer: MIT, code borrowed from [anod](https://github.com/anod) https://github.com/anod/esp32-s3-box-3b-winamp-radio