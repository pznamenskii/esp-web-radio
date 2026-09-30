# Plan: Improvements 1–3 — AP-mode polish (idle, QR code, glyphs)

**Status:** Proposed — source of truth for the Code-mode implementation subtask.
**Scope:** Software-only UI/state/config changes. No hardware or HA-side changes. No new YAML files (the `qrcode` widget slots into the existing AP setup page).
**Constraint:** There is **no local `esphome` installation**. Validation is limited to `python tests/yaml_syntax_check.py`, a fontTools codepoint pre-flight, and manual cross-reference review. **Never run any `esphome` CLI command.**

---

## TODO items covered (in order, from `### Remaining improvements`)

1. [ ] **disable going idle when in AP mode**
2. [ ] **show QR code in AP mode via "qrcode" widget**
3. [ ] **change AP mode blinking wifi glyph to wifi-cog; and change "wifi_100" to "wifi" glyph**

(These are the first three incomplete items in the *Remaining improvements* section of [`TODO.md`](../TODO.md:19). The two items under *Issues to fix* are tracked separately.)

---

## Current behavior (context)

- AP mode is driven by polling `wifi.ap_active` every 500 ms in [`packages/esp-web-radio-lvgl_ui.yaml`](../packages/esp-web-radio-lvgl_ui.yaml:190); rising edge → `script.enter_ap_mode` (lines 67–83), falling edge → `script.exit_ap_mode` (lines 85–97). `enter_ap_mode` shows `page_ap_setup`, hides the top-layer nav bar and `boot_screen`, shows + starts blinking `wifi_status`.
- LVGL idle: [`lvgl.on_idle`](../packages/esp-web-radio-lvgl_ui.yaml:225) fires after `${idle_timeout}` = 15 s ([substitution, line 12](../packages/esp-web-radio-lvgl_ui.yaml:12)) with **no AP-mode guard** — backlight fades off and LVGL pauses with `show_snow`. That would darken/snow the screen right while the user is doing WiFi setup.
- AP page [`packages/esp-web-radio-page_ap_setup.yaml`](../packages/esp-web-radio-page_ap_setup.yaml:13) shows title, hero icon (`wifi_100` glyph), 2 step lines, an info panel (SSID / password / URL) and a hint — no QR.
- `wifi_status` (top-layer label, [`esp-web-radio-lvgl_ui.yaml` line 335](../packages/esp-web-radio-lvgl_ui.yaml:335)) always renders `${wifi_100}` = `\U000F0928` (mdi-wifi-strength-4), even though signal strength is never measured (RSSI sensor commented out) — the icon is semantically "connected", not a real signal meter.
- Icon substitutions/glyphs live in [`packages/display-fonts.yaml`](../packages/display-fonts.yaml:1) and are embedded in the `menu24` font `extras` list (lines 32–49).

---

## Improvement 1 — disable going idle when in AP mode

**File:** [`packages/esp-web-radio-lvgl_ui.yaml`](../packages/esp-web-radio-lvgl_ui.yaml:225) — the `lvgl: on_idle:` block (lines 225–233).

### Change
Wrap the entire `then:` body of `on_idle` in an `if / not wifi.ap_active` guard so the idle-off/pause **never executes while the fallback AP is active**:

```yaml
  on_idle:   # turn off backlight on idle (skipped while AP mode is active)
    - timeout: ${idle_timeout}
      then:
        - if:
            condition:
              not:
                - wifi.ap_active
            then:
              - logger.log: LVGL is idle for ${idle_timeout}
              - light.turn_off:
                  id: backlight
                  transition_length: 5s
              - lvgl.pause:
                  show_snow: true     # LCD burnout protection
```

Notes:
- Use the native `wifi.ap_active` condition, **not** the mirrored `ap_active` global: the global lags up to 500 ms and can stay stale `true` after a reconnect. `wifi.ap_active` is already used for the same purpose by the swipe/nav guards, so this is consistent with existing code.
- Keep the existing wake branch inside `enter_ap_mode` (lines 71–76) untouched — it still handles the case where the device idled *before* the AP rose (STA failing while asleep).

### Pitfalls / interactions
- LVGL's inactivity timer still fires every 15 s during AP mode; the guarded body makes it a no-op. Harmless.
- After this change the AP screen stays lit indefinitely while `wifi.ap_active` is true — that is the intended UX (user needs the QR/SSID/password visible).
- No interaction with `blink_wifi_status` (it only toggles widget visibility, and LVGL will no longer pause during AP).

---

## Improvement 2 — show QR code in AP mode via "qrcode" widget

> **SUPERSEDED (2026-09-29):** the QR originally encoded the captive-portal URL `${ap_url}`. It now encodes the **WiFi connection string** `WIFI:T:WPA;S:${ap_ssid};P:${ap_password};;`, built from the `ap_ssid` substitution and a newly added `ap_password` substitution in [`esp-web-radio.yaml`](../esp-web-radio.yaml:9) (shared with `wifi.ap.password`, so the QR always matches what the AP broadcasts). All layout/geometry notes below remain valid; only the `text:` value of `ap_qr` changed (single-line, double-quoted).

**Widget availability (verified):** ESPHome LVGL ships a `qrcode` widget — source [`esphome/components/lvgl/widgets/qrcode.py`](https://github.com/esphome/esphome/blob/dev/esphome/components/lvgl/widgets/qrcode.py) and it is listed in the [LVGL widgets docs](https://esphome.io/components/lvgl/widgets/#qrcode). Schema keys: **`text`** (required content), **`size`** (required, int — square canvas side in px), **`dark_color`** (default `black`), **`light_color`** (default `white`). Runtime updates are possible via the `lvgl.qrcode.update` action (same keys) — not needed here since the connection string is static.

**Files:**

1. [`esp-web-radio.yaml`](../esp-web-radio.yaml:9) — add an `ap_url` substitution next to the existing `ap_ssid` (lines 9–11) so the page text and the QR never drift:
   ```yaml
   ap_url: "http://192.168.4.1"
   ```
2. [`packages/esp-web-radio-page_ap_setup.yaml`](../packages/esp-web-radio-page_ap_setup.yaml:13) — restructure the page body into a two-column row: info panel (left) + QR code (right).

### Layout change (children of `ap_card`, 480×320, `pad_all: 0`)

| Widget | Anchor | Suggested geometry |
|---|---|---|
| `ap_lbl_title` | line 27 | keep, `y: 20` |
| `ap_lbl_icon` | line 35 | keep, `y: 54`, glyph → `${text_wifi_cog}` (see Improvement 3) |
| `ap_lbl_step1` | line 42 | `y: 92` |
| `ap_lbl_step2` | line 51 | `y: 114` |
| `ap_info_panel` | line 60 | `x: 24, y: 148, width: 250, height: 128`; widen inner label widths from 330 → ~220 and keep the 3 rows (SSID `y:8`, password `y:40`, URL `y:72`) |
| **`ap_qr`** (new) | after panel | `x: 288, y: 152, size: 120, text: "WIFI:T:WPA;S:${ap_ssid};P:${ap_password};;"` (supersedes the original `${ap_url}` decision — see note above) |
| `ap_lbl_hint` | line 97 | keep, `BOTTOM_MID`, `y: -18` |

```yaml
              - qrcode:
                  id: ap_qr
                  x: 288
                  y: 152
                  size: 120
                  text: "WIFI:T:WPA;S:${ap_ssid};P:${ap_password};;"
                  dark_color: ${color_text_primary}   # modules = light
                  light_color: ${color_panel}         # background = dark
                  clickable: false
```

### Color choice
The theme is dark; use **inverted** modules (light modules on dark background) referencing theme substitutions — this complies with the project's "no raw hex" convention. Modern phone cameras auto-invert and scan this fine. If field testing shows poor scans, fall back to a white card: put a small `obj` (`bg_color: 0xFFFFFF`) behind the QR and use `dark_color: 0x000000`, `light_color: 0xFFFFFF` (a deliberate exception like the progress bar's explicit colors).

### Pitfalls / interactions
- Requires an ESPHome version with the `qrcode` widget (present on `dev`; introduced with the LVGL 9 era — the project already uses `lvgl.pause/show_snow` and `lvgl.widget.redraw`, i.e. a recent toolchain). **Verify on the installed version** before finalizing (see Validation). Fallback if unavailable: pre-generate a QR PNG (Python `segno` or `qrcode`, e.g. `images/ap_qr.png`), declare it with the ESPHome `image:` component and display with an `image` LVGL widget (`src: <image id>`); same position/size.
- Memory: the widget renders into an internal canvas ≈ `size² × 2` bytes (~29 KB at 120 px). Keep `size ≤ 128`.
- ID `ap_qr` must be globally unique (project convention).
- Interacts with Improvement 1: the QR stays visible because idle is disabled during AP.
- `ap_lbl_url` text should switch to `${ap_url}` for single-source-of-truth (was hard-coded `http://192.168.4.1`).

---

## Improvement 3 — glyphs: wifi-cog in AP mode, plain wifi instead of wifi_100

**Codepoints (verified against Pictogrammers):**
- `mdi-wifi` = **U+F05A9** ([icon page](https://pictogrammers.com/library/mdi/icon/wifi/))
- `mdi-wifi-cog` = **U+F16BE** ([icon page](https://pictogrammers.com/library/mdi/icon/wifi-cog/), added MDI 5.8.55)
- existing `wifi_100` = U+F0928 (mdi-wifi-strength-4) — stays defined (future RSSI item reuses `wifi_25/50/75/100`).

### Change A — [`packages/display-fonts.yaml`](../packages/display-fonts.yaml:1)
1. Add two substitutions next to the `wifi_*` block (around line 11):
   ```yaml
   text_wifi:                "\U000F05A9"   # mdi-wifi
   text_wifi_cog:            "\U000F16BE"   # mdi-wifi-cog
   ```
2. Add both codepoints to the `menu24` font `extras` glyph list (lines 32–49), with `# mdi-wifi` / `# mdi-wifi-cog` comments.

### Change B — [`packages/esp-web-radio-lvgl_ui.yaml`](../packages/esp-web-radio-lvgl_ui.yaml)
| Location | Change |
|---|---|
| `wifi_status` label, line 335 | `text: "${wifi_100}"` → `text: "${text_wifi}"` (steady state = plain wifi) |
| `script.enter_ap_mode`, after `lvgl.widget.show: wifi_status` (line 80) | add `lvgl.label.update: id: wifi_status, text: "${text_wifi_cog}"` before `script.execute: blink_wifi_status` — the blinking indicator shows wifi-cog during AP |
| `script.exit_ap_mode`, right after `script.stop: blink_wifi_status` (line 88) | add `lvgl.label.update: id: wifi_status, text: "${text_wifi}"` |
| `wifi.on_connect` (lines 131–147) | after `lvgl.widget.show: wifi_status` add the same `lvgl.label.update ... text: "${text_wifi}"` (idempotent safety net for reconnect-while-AP) |

```yaml
      - lvgl.label.update:
          id: wifi_status
          text: "${text_wifi_cog}"
```

### Change C — [`packages/esp-web-radio-page_ap_setup.yaml`](../packages/esp-web-radio-page_ap_setup.yaml:39)
- `ap_lbl_icon`: `text: "${wifi_100}"` → `text: "${text_wifi_cog}"` (the whole page is the AP/config context).

### Pitfalls / interactions
- **Pre-flight (must do in Code mode):** confirm the bundled [`fonts/materialdesignicons-webfont.ttf`](../fonts/materialdesignicons-webfont.ttf) actually contains U+F16BE and U+F05A9 — a TTF older than MDI 5.8 would lack wifi-cog and ESPHome would log a missing-glyph warning / render blank:
  ```bash
  python -c "from fontTools.ttLib import TTFont; m=TTFont('fonts/materialdesignicons-webfont.ttf').getBestCmap(); print('wifi', 0xF05A9 in m, 'wifi_cog', 0xF16BE in m)"
  ```
  If U+F16BE is missing, either update the TTF from a newer MDI release or pick a settings/wifi glyph that *is* present.
- Both glyph swaps use the `menu24` font, which covers both codepoints after Change A; runtime `lvgl.label.update` keeps the widget's font.
- Cross-package substitution references are already proven in this project (e.g. `${font_icon}` in page_now_playing).
- The label swap is config-time (substitutions), matching the existing `mp_lbl_play_icon` play/pause pattern.
- Update README wording: line 33 and line 253 say `wifi_status` "blinks the `wifi_100` glyph" → change to wifi-cog / wifi; line 333 icon list gains `text_wifi`, `text_wifi_cog`.

---

## Docs / checklist updates

- [`TODO.md`](../TODO.md:21) — mark the three items `[x]` with a short mechanism note (idle guard via `wifi.ap_active`; `ap_qr` qrcode widget with the `WIFI:` connection string `WIFI:T:WPA;S:${ap_ssid};P:${ap_password};;` — originally `${ap_url}`, superseded; glyph substitutions `text_wifi`/`text_wifi_cog`) and move them to completed section.
- [`README.md`](../README.md:33) — AP-mode bullet + AP page section (lines 247–254) and font strategy icon list (line 333) as described above.
- [`tests/yaml_syntax_check.py`](../tests/yaml_syntax_check.py:10) — **no change** (no new YAML files).

---

## Validation (no `esphome`)

1. **YAML syntax:** run `python tests/yaml_syntax_check.py` from the repo root — all 8 listed files must print `OK`.
2. **Glyph pre-flight:** run the fontTools snippet above; both codepoints must exist in the TTF.
3. **Cross-reference grep checks:**
   - Every `${text_wifi}`, `${text_wifi_cog}`, `${ap_url}` usage is defined in a `substitutions:` block (display-fonts.yaml / esp-web-radio.yaml).
   - All widget ids referenced by actions exist: `wifi_status`, `boot_screen`, `ap_qr`, `ap_lbl_*`, `top_layer` (nav matrix) — no typos, no duplicates.
   - `ap_qr` (qrcode) appears only once, under `lvgl.pages[].widgets` of `page_ap_setup`.
   - `wifi.ap_active` guard shape matches the existing swipe/nav guards in the same file.
4. **Manual review of merged model:** the change adds no new top-level keys; `qrcode` is a child widget of an existing page; the only additions to existing blocks are guarded statements inside `on_idle`, `enter_ap_mode`, `exit_ap_mode`, `wifi.on_connect`.
5. **Hardware checklist (deferred to flash on the real device):** see matrix below.

## Behavior verification matrix (for the device)

| Scenario | Expected |
|---|---|
| Device in AP mode, no touch for > 15 s | Backlight stays on, no snow, QR/SSID/password remain readable |
| AP mode ends (router back / user completes captive portal) | Normal idle behavior resumes (fade-out + snow after 15 s idle) |
| AP page shown | QR encoding `WIFI:T:WPA;S:ESP Radio Fallback;P:<ap_password>;;` renders; phone camera scans it and joins the fallback AP (captive portal then opens automatically) |
| AP page shown | Top-layer `wifi_status` blinks the wifi-cog glyph at ~400 ms |
| WiFi connected (normal mode) | `wifi_status` shows the plain wifi glyph, steady |
| AP hero icon | Shows wifi-cog (config context), not wifi-strength-4 |

## Risks / notes

- **Installed ESPHome version unknown** — qrcode widget support and `wifi.ap_active` condition must both exist; both are current-ESPHome features. The project's existing usage (`show_snow`, `widget.redraw`) already implies a recent release. If qrcode is unsupported, use the PNG `image` fallback described in Improvement 2.
- **QR scanner tolerance** for inverted (light-on-dark) codes is good but not universal; the white-card variant is the fallback if the primary fails in the field.
- Do not remove `wifi_25/50/75/100` substitutions — the roadmap's signal-level icons item still needs them.
- Keep `enter_ap_mode`'s display-wake branch intact — it covers the idle-before-AP race.
- No new packages/top-level keys → package merge conflicts are not a risk for this batch.