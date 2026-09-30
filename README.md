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
- [Time sync & device modes](#time-sync--device-modes)
- [Font strategy](#font-strategy)
- [Roadmap](#roadmap)
- [Development conventions](#development-conventions)

---

## Features

- **Now Playing page** — full 480×320 LVGL page showing:
  - Station title, artist, and track labels (long text scrolls; artist/track auto-hide when empty)
  - The station logo / album-art panels and the visualizer strip hide automatically whenever their data is absent (logo waits for the station-picture roadmap item; art follows the `media_image_url` attribute; the visualizer shows only while playing)
  - Progress bar plus elapsed (`mm:ss`) and total (`mm:ss`) time labels
  - Transport controls: station prev/next, play/pause, mute, and a volume slider
- **Stations page** — a third LVGL page listing every available station as a button: a dynamic 2×6 grid (up to 12 slots) filled from the HA station-name sensors while the API is connected, or from the hardcoded offline list when it is not. Empty slots hide automatically; a vertical scrollbar appears once more than 8 stations are active; pressing a station plays it and returns to Now Playing.
- **No-HA local stations** — with WiFi up but the Home Assistant API unreachable, the Stations page falls back to the built-in list in [`packages/esp-web-radio-offline_stations.yaml`](packages/esp-web-radio-offline_stations.yaml) and playback goes straight to the direct stream URL via the local media player. When HA reconnects, station names and playback routing switch back to Home Assistant automatically.
- **Home Assistant integration** — encrypted native API; the player, DAC mute switch, and media info are exposed automatically. A status indicator in the top layer (`lbl_hastatus`, `mdi-home-assistant` glyph) appears when a Home Assistant client connects and disappears on disconnect.
- **Clock with local fallback** — the title-bar clock (`lbl_time`) shows HA-provided time while the API is connected; when WiFi is up but HA is not, it falls back to an NTP-maintained local clock (`sntp`, `pool.ntp.org` / `time.google.com`); with no WiFi the clock hides. See [Time sync & device modes](#time-sync--device-modes).
- **OTA updates** — ESPHome OTA with an on-screen popup showing a live progress bar and percentage (`ota_popup` with `ota_bar_percentage` / `ota_lbl_percentage`).
- **Swipe / page navigation** — swipe left/right flips pages with `OUT_LEFT` / `OUT_RIGHT` animations (300 ms); a persistent bottom `buttonmatrix` in the LVGL `top_layer` provides prev / home / next page buttons on every page. `page_wrap: true` wraps around.
- **WiFi setup (AP mode) page** — when the fallback access point + captive portal becomes active (`wifi.ap_active`), the display switches to a locked `page_ap_setup` page showing the AP network name (`ap_ssid`, default "ESP Radio Fallback"), the AP password (from `!secret ap_password`, shown via `ap_password_display`), the setup URL (`${ap_url}`, default http://192.168.4.1), and a scannable **QR code** (`ap_qr`) encoding the WiFi connection string `WIFI:T:WPA;S:${ap_ssid};P:${ap_password};;` — scanning it joins the fallback AP directly. The page cannot be swiped/navigated away from and clears automatically back to Now Playing when the station reconnects; while it is up, the top-layer `wifi_status` icon blinks the `wifi-cog` glyph at ~400 ms and the LVGL idle timer is disabled so the page stays lit. See [UI architecture & page bindings](#ui-architecture--page-bindings).
- **Boot & idle UX** — spinner boot screen stays up until the first of: user touch, WiFi connects, or AP mode becomes active (shows the setup page instead). After 15 s of inactivity the backlight fades out and LVGL pauses with a "snow" pattern (LCD burn-in protection); any touch wakes the device. Idle never triggers while the fallback AP + captive portal are active (`wifi.ap_active`), so the setup page stays lit.
- **Dark-orange LVGL theming** — a centralized theme in [`packages/lvgl_theme.yaml`](packages/lvgl_theme.yaml) drives widget defaults and shared named styles (see [Theming system](#theming-system)).
- **Web interface** — built-in `web_server` (port 18080, v2, admin auth) for controlling entities via REST.

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

You can also run the lightweight YAML syntax check helper (pure Python, ignores ESPHome tags):

```bash
python tests/yaml_syntax_check.py
```

---

## Project structure

```
esp-web-radio/
├── esp-web-radio.yaml                     # Entry point: platform, board, flash/PSRAM,
│                                          #   packages list, wifi/api/ota/web_server,
│                                          #   globals, on_boot
├── ha_template_sensors.yaml               # Home Assistant-side template sensors defining
│                                          #   4 station presets (Name + URL, radio_browser)
│                                          #   — import into HA, consumed via the native API
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
│   │                                      #   track + art presence, player_state, volume/
│   │                                      #   position/duration, current_station global
│   ├── esp-web-radio-offline_stations.yaml# Hardcoded station names + direct URLs for no-HA
│   │                                      #   mode (fallback when the HA API is unreachable)
│   ├── esp-web-radio-lvgl_ui.yaml         # LVGL framework (rotation, idle, buffers),
│   │                                      #   top-layer status widgets, boot screen, OTA popup,
│   │                                      #   swipe + bottom nav buttonmatrix, connectivity
│   │                                      #   flags + `update_clock` script + the
│   │                                      #   `refresh_stations` / `play_station` scripts
│   ├── esp-web-radio-page_now_playing.yaml# The main UI page: geometry + widget bindings
│   ├── esp-web-radio-page_stations.yaml   # Stations page: dynamic 2x6 station button grid,
│   │                                      #   hidden empty slots, vertical scrollbar
│   ├── esp-web-radio-page_ap_setup.yaml   # AP mode / WiFi setup page: shown when the
│   │                                      #   fallback AP + captive portal are active —
│   │                                      #   SSID, password, setup URL; locked page
│   ├── lvgl_theme.yaml                    # Central dark-orange palette, lvgl.theme defaults,
│   │                                      #   shared style_definitions
│   ├── display-fonts.yaml                 # Font strategy: Roboto gfonts, Cyrillic coverage,
│   │                                      #   MDI extras, text_* icon substitutions
│   └── common-colors.yaml                 # Named color: entities (ESPHome `color:` ids)
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
| `ota_password` | `ota:` **and** `web_server` HTTP auth (user `admin`) |
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

`dark_mode: true` with defaults for `label`, `button` (orange vertical gradient, pressed `0xEA580C`/`0x9A3412`), `buttonmatrix` (panel-gradient items, themed fonts), `switch`, `slider`, `bar` (orange indicator at 80% opacity), `arc`, and `spinner`.

### Shared `style_definitions`

Named styles referenced by pages with `styles: <name>`:

`header_footer`, `card`, `panel`, `icon_button`, `primary_button`, `text_primary`, `text_muted`, `text_title`, `text_body`, `text_small`, `controls_row`.

### Theming rule

Pages and elements should reference **shared styles and color substitutions** (`${color_*}`) instead of raw hex literals. When the Designer export was ported into [`packages/esp-web-radio-page_now_playing.yaml`](packages/esp-web-radio-page_now_playing.yaml), all inline hex colors were replaced accordingly (only the progress-bar geometry keeps a couple of explicit colors).

---

## UI architecture & page bindings

### Layout

Three navigable pages (480×320 landscape, LVGL `rotation: 90`), all declared under `lvgl.pages` and driven by the framework block in [`packages/esp-web-radio-lvgl_ui.yaml`](packages/esp-web-radio-lvgl_ui.yaml): **Now Playing**, **Stations**, and the locked **AP setup** page. The AP page is `skip: true`, so prev/next buttons and swipe gestures cycle only between Now Playing and Stations:

[`packages/esp-web-radio-page_now_playing.yaml`](packages/esp-web-radio-page_now_playing.yaml) defines `page_now_playing`:

- Header row: station logo panel (left), station title + artist + track (center), album-art panel (right)
- Visualizer panel strip
- Progress bar + elapsed/total time labels
- Control row: prev, play/pause, next, mute, volume slider
- Persistent top layer: boot screen (dismissed by touch, `wifi.on_connect`, or AP mode), OTA popup, WiFi status icon, HA status icon, bottom nav `buttonmatrix`

#### Stations page (`page_stations`)

[`packages/esp-web-radio-page_stations.yaml`](packages/esp-web-radio-page_stations.yaml) defines `page_stations`, reachable via the bottom-nav **next** button or a right swipe:

- Dynamic 2×6 button grid (12 slots `st_btn_1`..`st_btn_12`). The `refresh_stations` script fills each slot's label from the HA `station_N_name` sensors while the API is connected, otherwise from the hardcoded `offline_station_N_name` substitutions in [`packages/esp-web-radio-offline_stations.yaml`](packages/esp-web-radio-offline_stations.yaml).
- Empty slots stay hidden and the grid height is set to the number of visible rows × 56 px, so a vertical scrollbar appears automatically once more than 8 stations are active.
- Pressing a station runs the `play_station` script: playback goes through Home Assistant while connected (the HA template sensors use `media-source://radio_browser/…` URLs that only HA can resolve) or through the native `media_player.play_media` action with the direct offline URL in no-HA mode; the page then returns to Now Playing.

#### AP setup page (`page_ap_setup`)

[`packages/esp-web-radio-page_ap_setup.yaml`](packages/esp-web-radio-page_ap_setup.yaml) adds `page_ap_setup`, auto-shown when the fallback AP + captive portal become active (`wifi.ap_active`, watched by the `ap_state_watch` interval polling every 500 ms):

- Two-column layout: an info panel (left) shows the AP **network name** (`${ap_ssid}`, default "ESP Radio Fallback"), the **password** (`${ap_password_display}`, from `!secret ap_password`), and the setup **URL** — single-sourced from the `${ap_url}` substitution in [`esp-web-radio.yaml`](esp-web-radio.yaml), default http://192.168.4.1. The right column holds a scannable **QR code** (`ap_qr` qrcode widget, inverted light-on-dark modules) encoding the WiFi connection string `WIFI:T:WPA;S:${ap_ssid};P:${ap_password};;` so a phone can join the fallback AP without typing the credentials.
- Locked: declared `skip: true` (excluded from `lvgl.page.next/previous`) and the swipe / prev / home / next handlers are guarded while `wifi.ap_active`, so the page cannot be navigated away from; the bottom nav bar is hidden while it is shown.
- While it is up, the `wifi_status` label blinks the `wifi-cog` glyph at ~400 ms (`blink_wifi_status` script; icon set via `lvgl.label.update` in `enter_ap_mode`).
- Idle/sleep is disabled while `wifi.ap_active` (the `on_idle` handler is guarded), so the QR/SSID/password stay readable for as long as the setup page is needed.
- Clears automatically when the station reconnects — `exit_ap_mode` restores the plain `wifi` glyph, returns to `${homepage}` (Now Playing), and `wifi_status` settles back to its steady state.

### Bindings (widget → sensor/action)

Driven by [`packages/esp-web-radio-homeassistant.yaml`](packages/esp-web-radio-homeassistant.yaml) over the native API:

| Widget ID | Type | Bound to | Direction |
|---|---|---|---|
| `mp_station_title` | label | `now_playing` sensor, `media_title` attribute | HA → UI |
| `mp_now_playing_artist` | label | `now_playing_artist` sensor, `media_artist` attribute | HA → UI (hidden when empty) |
| `mp_lnow_playing_track` | label | `now_playing_track` sensor, `media_title` attribute | HA → UI (hidden when empty) |
| `mp_station_logo` | obj (panel) | — (hidden; station-picture source pending roadmap item 4) | — |
| `mp_now_playing_art` | obj (panel) | `now_playing_art` sensor, `media_image_url` attribute | HA → UI (shown while metadata exists) |
| `mp_visualizer` | obj (panel) | `media_player.on_play` / `on_pause` / `on_idle` — shown only while `player_playing` | native → UI |
| `mp_bar_progress` | bar | `player_position` / `player_duration` (percent, clamped 0–100) | HA → UI |
| `mp_lbl_elapsed` | label | `player_position` (`mm:ss`) | HA → UI |
| `mp_lbl_total` | label | `player_duration` (`mm:ss`) | HA → UI |
| `mp_lbl_play_icon` | label | `player_state` (HA) + `media_player.on_play/on_pause/on_idle` — swaps `${text_pause}` / `${text_play}` glyph | HA/native → UI |
| `volume_slider` | slider | `player_volume` sensor, `volume_level` (×100) | HA → UI |
| `st_lbl_1..12` | label | `station_N_name` sensors / `offline_station_N_name` via `refresh_stations` | HA/offline → UI |
| `st_btn_1..12` | button | runs `play_station` (plays `${current_station}`) | UI → playback |
| `mp_btn_play` | button | toggles play/pause via HA when connected (`player_playing` decides which); ignored offline | UI → HA |
| `mp_btn_prev` | button | previous station (wrap `${station_count}` → 1) via `play_station` | UI → playback |
| `mp_btn_next` | button | next station (wrap 1 → `${station_count}`) via `play_station` | UI → playback |
| `mp_btn_mute` | button | `media_player.volume_mute` | UI → HA |
| `volume_slider` | slider | `media_player.volume_set` (`x / 100.0`) on release | UI → HA |

### Station selection & playback routing

A `globals` entry `current_station` (int, initial `1`, no restore) tracks the active preset; the `${station_count}` substitution (default `4`) is the wrap modulus. All playback — the Now Playing prev/next buttons, the play/pause toggle, and every Stations-page button — funnels through the `play_station` script, which picks the URL source by mode:

- **HA connected** — `homeassistant.action: media_player.play_media` with `station_1_url` … `station_4_url` (the HA template sensors use `media-source://radio_browser/…`, resolvable only by HA).
- **no-HA (wifi_only)** — the native `media_player.play_media` action with the direct `offline_station_1_url` … `offline_station_4_url` values, and `mp_station_title` is filled from the offline station name.

The station name sensors (`station_1_name` … `station_12_name`) feed the Stations page through the `refresh_stations` script (also re-run on boot, WiFi connect, and HA API connect/disconnect); slots 5–12 are declared but stay hidden until matching entities exist in Home Assistant.

---

## Time sync & device modes

The title-bar clock (`lbl_time` in the LVGL `top_layer`) is fed by two time sources with a fixed priority, selected by the `update_clock` script in [`packages/esp-web-radio-lvgl_ui.yaml`](packages/esp-web-radio-lvgl_ui.yaml):

| Device mode | Condition | Clock source | Stations list / playback |
|---|---|---|---|
| **online** | HA API connected (`ha_connected`) | `esptime` — `platform: homeassistant` | HA station names; playback via HA (`media-source://`) |
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

## Font strategy

Centralized in [`packages/display-fonts.yaml`](packages/display-fonts.yaml).

- **Cyrillic + Latin coverage** — station names can contain Cyrillic (e.g. "Радио Дача"), so every font includes the Cyrillic alphabet alongside ASCII. `menu24`, `roboto42` use the `GF_Latin_Core` + `GF_Cyrillic_Core` glyphsets; `f_title`/`f_body`/`f_small` carry an explicit glyph string including `Ёё«»—–°`.
- **Google Fonts** — fonts are loaded from `gfonts://Roboto` (no local font files needed for text).
- **MDI webfont extras** — UI icons come from `fonts/materialdesignicons-webfont.ttf`, embedded via font `extras` (16 glyphs). Each icon is exposed as a `text_*` / `wifi_*` substitution so pages reference readable names instead of raw code points.

| Font id | Size | Used for |
|---|---|---|
| `menu24` | 24 px | Default icon/nav font — buttons, buttonmatrix, status icons, play/pause glyphs |
| `roboto42` | 42 px | Large text (reserved) |
| `f_title` | 26 px | Page titles (`${font_title}`) |
| `f_body` | 17 px | Body text, time labels, LVGL `default_font` (`${font_body}`) |
| `f_small` | 13 px | Small/muted labels (`${font_small}`) |

Icon substitutions defined: `text_hastatus` (mdi-home-assistant), `text_prev`, `text_next`, `text_home`, `wifi_25/50/75/100` (mdi-wifi-strength-1..4), `text_wifi` (mdi-wifi), `text_wifi_cog` (mdi-wifi-cog), `text_play`, `text_pause`, `text_skip_prev`, `text_skip_next`, `text_volume_high`, `text_volume_off`.

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

## LICENSE
MDI WebFonts: Apache 2.0
FFT Vizualizer: MIT, code borrowed from @anod https://github.com/anod/esp32-s3-box-3b-winamp-radio