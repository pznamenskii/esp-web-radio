# Troubleshooting spontaneous device restarts — investigation & implemented diagnostics

**Status:** Solutions document — implements TODO "Remaining improvements" item "add logs to troubleshoot spontaneous device restarts".
**Date:** 2026-10-01
**Scope:** software-only diagnostics. Adds the ESPHome `debug` component (reset reason + heap/PSRAM sensors), logger annotation, boot + heartbeat logging, and a reset-reason row on the Info/Settings page. No hardware or HA-side changes. Validation without a local esphome install: `python tests/yaml_syntax_check.py` + `python tests/font_codepoint_check.py` + grep cross-reference review (no `esphome` CLI was run; framework internals were not researched — everything below is **documented ESPHome / ESP-IDF behavior**).

---

## Context

The device is an ESP32-S3 (16 MB flash, 8 MB octal PSRAM) running the **ESP-IDF framework** (required by the `speaker` media player) with a heavy resource footprint:

- Two audio pipelines (media `format: NONE` → all decoders compiled; announcement `FLAC`) through `mixer` + `resampler` speakers ([`packages/esp-web-radio-audio.yaml`](../packages/esp-web-radio-audio.yaml)).
- A full 480×320 LVGL UI (three swipeable pages + AP setup page, boot spinner, OTA popup).
- TLS-capable HTTP audio source (`CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY`), encrypted native API, OTA, fallback AP + captive portal.

The ESPHome docs explicitly warn: *"Audio and voice components consume a significant amount of resources (RAM, CPU)… Crashes are likely to occur if you include too many additional components"* — with LVGL on top, **memory pressure is the primary suspect** for the spontaneous restarts, but watchdog stalls and brownout are also plausible. The user already captures serial logs into the git-ignored `logs/` directory (referenced as `logs/play_issue_2118.txt` and `logs/log_2040.txt` in the packages); this plan formalizes that workflow.

---

## Part 1 — Investigated options and capabilities

All facts below come from the official documentation (esphome.io components pages for `debug`, `logger`, `media_player/speaker`, plus documented ESP-IDF core-dump/watchdog behavior). No framework source was inspected.

### Capabilities table

| Feature | What it tells you | How to use it here | Status |
|---|---|---|---|
| `debug` component — `reset_reason` text sensor | The reason for the **last** boot/reset in human-readable form: power-on, software reset, watchdog resets (task/timer/CPU), brown-out, external pin reset, deep sleep… It **survives software reboots** (stored by hardware/RTC), so after a spontaneous restart + auto-reboot it names the previous session's killer. | New entity "Reset Reason" (+ shown on the Settings page). Check it right after any unexpected reboot. | ✅ implemented |
| `debug` component — `free` sensor | Free internal-DRAM heap bytes (live). | "Heap Free" entity; included in BOOT + HEARTBEAT logs. | ✅ implemented |
| `debug` component — `min_free` sensor | **Lowest free heap since boot** — catches memory leaks and peak usage. ESP32 family only. | "Heap Min Free" entity + HEARTBEAT; the key trend for OOM correlation. | ✅ implemented |
| `debug` component — `block` sensor | Largest contiguous free RAM block. Multi-KB audio/LVGL buffers fail when the heap is fragmented even if `free` looks OK. | "Heap Max Block" entity. | ✅ implemented |
| `debug` component — `fragmentation` sensor | Heap fragmentation metric; docs: ~0% clean, **> ~50% may cause allocation failures**. | "Heap Fragmentation" entity + HEARTBEAT. | ✅ implemented |
| `debug` component — `psram` sensor | Free PSRAM bytes (ESP32 family). Audio pipeline + LVGL mostly allocate from PSRAM here. | "Free PSRAM" entity + BOOT/HEARTBEAT logs. | ✅ implemented |
| `debug` component — `loop_time` sensor | Longest time between main-loop iterations — reveals blocking main-thread work (heavy LVGL renders, flash ops) that can starve scheduler-sensitive audio/TCP tasks. | "Loop Time" entity + HEARTBEAT (raw value; unit is build-version dependent — flagged below). | ✅ implemented |
| `debug` component — startup banner | On boot the component prints ESPHome version, free heap at startup, reset reason, flash info, chip info (ESP-IDF version, MAC). | No config needed; appears in every captured boot log — confirms which firmware/version ran at crash time. | ✅ free (component added) |
| `debug` component — `device` text sensor | Richer info string (chip model/cores/revision, EFuse MAC, reset + wakeup reason, ESPHome/IDF versions). | Not implemented: duplicates `reset_reason` and the banner; one more entity for little signal here. | ⬜ documented, skipped |
| `debug` component — `cpu_frequency` sensor | CPU frequency Hz. | Not useful: fixed 240 MHz. | ⬜ skipped |
| `logger` — `level` | Global severity gate (`NONE`…`VERY_VERBOSE`, default `DEBUG`). `VERBOSE` adds sensor state changes + IDF component debug output; **docs warn VERBOSE is for short debugging sessions only** (slows the device, may cause network timeouts/instability). | Steady state kept at `DEBUG`; `VERBOSE` is the "reproduction mode" knob (see Workflow). | ✅ annotated |
| `logger` — `baud_rate` | Serial log baud; default `115200`; `0` disables UART. | Kept `115200` (explicit, self-documenting). | ✅ annotated |
| `logger` — `hardware_uart` | Log transport UART. On the **ESP32-S3 the documented default interface is `USB_SERIAL_JTAG`** (GPIO19/20 native USB), i.e. the same port used to flash — that is the capture path for `logs/*.txt`. | Untouched (changing it would break the existing capture setup; I2S/LVGL don't share the log UART). | ⬜ documented, unchanged |
| `logger` — `tx_buffer_size` / `task_log_buffer_size` | Buffering knobs; `task_log_buffer_size` (default 768 B) prevents API disconnects when many threads log; can be set to 0 to reclaim RAM. | Not changed now; documented as a memory-saving lever if heap is tight. | ⬜ documented |
| `logger` — per-tag `logs:` map / `logger.set_level` | Raise/lower the level for one tag at runtime (`wifi`, `main`, …) while keeping the global level low. | Useful trick: keep global `DEBUG` and raise only `wifi: VERBOSE` during repro. | ⬜ documented |
| `logger.log` action (`level`/`tag`) | Print INFO/WARN messages with a custom tag. | Used for BOOT + HEARTBEAT lines (`tag: diag`, `level: INFO`). | ✅ implemented |
| IDF core dumps | `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`/`TO_UART` + `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF` + a `coredump` flash partition; after a panic the dump survives the reboot and is decoded with the IDF tooling (`espcoredump.py info_corefile` / monitor `decode_coredump`, conceptually) against the build ELF to recover the faulting address/backtrace. | Not enabled (needs a custom `board_build.partitions` + sdkconfig; see "Candidate knobs" and "Not verified"). | ⬜ documented, optional |
| Watchdogs (Task WDT / Interrupt WDT) | **No ESPHome YAML knob exists** — TWDT is IDF-internal behavior surfaced via logs: "Guru Meditation Error: Task watchdog got triggered", followed by the offending task names and stack high-water marks. | Read the log lines when present; correlate with `loop_time` spikes and audio stalls. | ⬜ IDF-internal (documented) |
| Brownout detector | IDF enables it by default; a VDD dip triggers an immediate reset **usually without any log output**. | Diagnosed *after the fact*: next boot reports `RTCWDT_BROWN_OUT_RESET` in the Reset Reason entity / Settings row. Hardware fix = PSU/wiring; threshold is an IDF Kconfig (`CONFIG_ESP*_BROWNOUT_*`) reachable via `sdkconfig_options` (exact symbol per IDF version — verify on build host). | ⬜ IDF-internal (documented) |
| Memory sensors + audio knobs | `debug` heap sensors (above) + documented `media_player` (speaker) knobs: `buffer_size` (default **1,000,000 bytes per pipeline**, range 4000–4,000,000), `task_stack_in_psram` (default `false`; move audio tasks to PSRAM), network `enable_high_performance` (default `true` with PSRAM guaranteed; 512 KB TCP windows + 512 WiFi RX buffers). | Read the knobs; see "Candidate root causes". | ⬜ documented (not changed) |
| Log transport | Serial via USB-Serial-JTAG (above) OR `esphome logs <config>` over serial/API/OTA; the HA dashboard also has a Logs button; the native API streams logs to a connected HA client. | Capture serial at 115200 into `logs/`; for post-crash reads use `esphome logs` / dashboard Logs (device must be back up). | ✅ documented workflow |
| `esphome.on_shutdown` | Documented core trigger that runs on *graceful* shutdown (deep sleep / OTA restart). | Not wired: spontaneous crash/panic paths never reach it, and behavior across ESPHome versions varies — it adds no signal for this bug. | ⬜ documented, skipped |

---

## Part 2 — Implemented changes

### 1. `debug` component + sensors — [`esp-web-radio.yaml`](../esp-web-radio.yaml)

```yaml
debug:
  update_interval: 60s

sensor:
  - platform: debug
    free:          { id: heap_free,          name: "Heap Free" }
    min_free:      { id: heap_min_free,      name: "Heap Min Free" }
    block:         { id: heap_block,         name: "Heap Max Block" }
    fragmentation: { id: heap_fragmentation, name: "Heap Fragmentation" }
    psram:         { id: psram_free,         name: "Free PSRAM" }
    loop_time:     { id: loop_time,          name: "Loop Time" }

text_sensor:
  - platform: debug
    reset_reason:
      id: reset_reason
      name: "Reset Reason"
      on_value:
        - script.execute: refresh_settings_info
```

- Uses the **current documented nested-key schema** (verified against esphome.io/components/debug/). If the installed ESPHome predates it, switch to the legacy form (see "Not verified locally").
- All entities are published to HA by default (consistent with `wifi_signal`/`uptime`) → HA history plots the memory trend across restarts for free.
- Interpretation notes (documented): heap sensors report the **internal DRAM heap**; PSRAM is separate (`psram` sensor). `fragmentation` > ~50% is the danger zone; `block` shows whether large contiguous allocations (audio buffers, TLS, LVGL) can still succeed.
- Wi-Fi signal was **not** duplicated — the project already has the `wifi_signal` sensor ([`packages/esp-web-radio-homeassistant.yaml`](../packages/esp-web-radio-homeassistant.yaml)).

### 2. Logger annotation — [`esp-web-radio.yaml`](../esp-web-radio.yaml)

- `level: DEBUG` kept (it is the documented default; ESPHome ≥ 2026.4 moved routine state-change logging to VERBOSE so DEBUG is cheap in steady state).
- `baud_rate: 115200` made explicit; `hardware_uart` left at the ESP32-S3 default (`USB_SERIAL_JTAG`).
- Rationale for not switching to `VERBOSE`: docs warn it degrades performance and can cause network/connection instability — counterproductive on an audio+LVGL device and it would muddy exactly the timing-sensitive behavior we want to observe. VERBOSE is the short-term reproduction mode (Workflow step 1).

### 3. Boot + heartbeat logging — [`esp-web-radio.yaml`](../esp-web-radio.yaml)

- **BOOT line** (first action of `esphome.on_boot`, `INFO`, tag `diag`):

  ```
  [diag] BOOT reset_reason=POWERON_RESET uptime=0 s heap_free=123456 B psram_free=7777777 B
  ```

  The reset reason is read from the sensor — because it survives software reboots, after a spontaneous restart this line names the reason of the crashed session.
- **HEARTBEAT** (new `interval` `heartbeat_diag`, every 15 min, `INFO`, tag `diag`):

  ```
  [diag] HEARTBEAT uptime=900 s heap_free=... B heap_min_free=... B frag=...% psram_free=... B loop_time=...
  ```

  One line per 15 minutes — negligible noise, but in a captured log it draws the memory trend *up to the crash instant*.

### 4. Info/Settings page — [`packages/esp-web-radio-page_settings.yaml`](../packages/esp-web-radio-page_settings.yaml)

- New **row 4** "Причина сброса" (Last reset reason) at y=222 (bottom edge y=270 — still above the bottom nav bar at y=294; the layout budget comfortably fits a 4th 48 px row, so no conflict had to be worked around).
- Row is `hidden: true` initially and shown only while `reset_reason` has a state (hide/show only, static geometry, no reflow) — driven by a new branch in `refresh_settings_info`, re-seeded by the sensor's `on_value`.
- Value label widened relative to the other rows (x=210, w=222) so the longest documented reason strings (`RTCWDT_BROWN_OUT_RESET`, 23 chars) fit at 17 px without clipping.

### 5. Font — [`packages/display-fonts.yaml`](../packages/display-fonts.yaml)

- New 24 px glyph `mdi-restart` (`\U000F0709`) as `${text_restart}`, added to the `menu24` extras — the first icon change since the Settings page shipped; one glyph ≈ negligible flash. Verified by `tests/font_codepoint_check.py`.

### Deliberately NOT implemented

| Option | Why not |
|---|---|
| `debug.device` text sensor / `cpu_frequency` sensor | Redundant (banner already prints chip/IDF/version info) or useless (fixed clock). |
| `esphome.on_shutdown` logging | Only fires on graceful shutdown; a spontaneous restart never reaches it. Adds false confidence, not signal. |
| IDF core dump to flash | Needs a custom partition table + sdkconfig; heavier change, only worthwhile if serial capture keeps missing the crash (see Workflow step 6). |
| `media_player.buffer_size` / `task_stack_in_psram` / network tuning | Mitigations, not diagnostics — deliberately left untouched until evidence points at them (see "Candidate root causes"). |
| `logger.level: VERBOSE` as steady state | Docs: short-term debugging only; degrades performance and can destabilize the device. |

---

## Reading the new signals

### Healthy boot log (what normal looks like)

```
[00:00:00] [I][main:...]: ESPHome ...   (version banner)
[00:00:00] [D][debug:...]: Reset reason: POWERON_RESET        ← debug banner (or RTC_SW_SYS_RESET after OTA)
[00:00:00] [I][radio:...]: ...           (existing init logs)
[00:00:xx] [I][diag]: BOOT reset_reason=POWERON_RESET uptime=0 s heap_free=... B psram_free=... B
...
[00:15:00] [I][diag]: HEARTBEAT uptime=900 s heap_free=... B heap_min_free=... B frag=...% psram_free=... B loop_time=...
```

Signs of health: `heap_free` and `psram_free` flat across heartbeats, `fragmentation` low and stable, `loop_time` small, no `Guru Meditation` / `abort()` / `Task watchdog` / allocation failures.

### Signatures to look for immediately BEFORE a spontaneous restart

| Signature in the log | Meaning | What to do next |
|---|---|---|
| `Guru Meditation Error: Core 1 panic'ed (...)` + register dump + **backtrace** (`0x4201…: ...` addresses) | CPU exception/panic — decode the top backtrace address against the build ELF | Decode (Workflow step 3); note the reason (`LoadProhibited`/`StoreProhibited` = bad pointer, `IllegalInstruction` = memory corruption, etc.) |
| `assert failed: ...` / `abort() was called at PC ...` | An ESPHome/IDF assertion fired | Same as above |
| `Task watchdog got triggered. The following tasks did not reset the watchdog in time:` + task list + stack high-water marks | A task blocked > TWDT timeout (default ~5 s) — e.g. audio task starved by LVGL/flash/OOM | Correlate with `loop_time` spikes; check for long LVGL renders or heavy OTA/animations |
| `... allocation failed ...` / `heap_caps_malloc failed` / `CONFIG_SPIRAM...` errors | Heap exhaustion / PSRAM allocation failure | Compare with `heap_min_free`/`psram_free` trend; apply memory knobs |
| Nothing at all, then reboot with next-boot reason `RTCWDT_BROWN_OUT_RESET` | Brown-out (power dip) — resets usually print nothing | Check PSU/wiring; consider brownout threshold Kconfig |
| Next-boot reason `TG0WDT_SYS_RESET` / `TG1WDT_SYS_RESET` / `RTCWDT_SYS_RESET` / `TASK_WDT` / `RTCWDT_CPU_RESET` | Watchdog-family reset (task WDT, timer group, RTC WDT) | Backtrack the pre-crash log for the watchdog message |
| Next-boot reason `SW_CPU_RESET` / `SW_SYS_RESET` | Software-initiated restart — often the aftermath of a panic handler calling `esp_restart()` | The panic backtrace is in the *preceding* log section; don't stop at the reason |

### Documented `reset_reason` values (ESP32 family, per the debug component docs)

`POWERON_RESET`, `EXT_CPU_RESET` / `EXT_SYS_RESET`, `SW_CPU_RESET`, `SW_SYS_RESET`, `DEEPSLEEP_RESET`, `TG0WDT_SYS_RESET`, `TG1WDT_SYS_RESET`, `RTCWDT_SYS_RESET`, `INTRUSION_RESET`, `TASK_WDT`, `RTCWDT_CPU_RESET`, `RTCWDT_BROWN_OUT_RESET`, `RTCWDT_RTC_RESET`. (Exact set printed on the board depends on the IDF version — treat unknown strings as "look them up in the IDF docs for the built version".)

---

## Part 3 — Recommended troubleshooting workflow

1. **Reproduce with a serial capture at VERBOSE.** Temporarily set `logger.level: VERBOSE` (revert afterwards!), re-flash via OTA, open the USB-Serial-JTAG port at 115200 (or `esphome logs esp-web-radio.yaml --secrets secrets_radio.yaml` / the dashboard Logs button) and save the stream into `logs/` (e.g. `logs/restart_YYYYMMDD.txt`) **before** the device starts acting up. VERBOSE surfaces IDF debug output around panics and allocation failures that DEBUG hides.
   - Alternative if full VERBOSE is too noisy: keep `DEBUG` and add a per-tag raise (`logger.logs: wifi: VERBOSE` or `logger.set_level` at runtime) to zoom into one subsystem.
2. **Wait for the event; capture the aftermath.** When it restarts, the *next* boot's `[diag] BOOT reset_reason=...` line tells you what ended the previous session; the debug banner shows the firmware version that was running.
3. **Decode any backtrace.** Take the top addresses from the `Guru Meditation` backtrace and map them with the classic tooling: `addr2line -e .esphome/build/esp-web-radio/.pioenvs/esp-web-radio/firmware.elf -f -C <addr>` (or the IDF monitor/`espcoredump.py` equivalent) on the build host that produced the flashed binary. The decoder needs the **same ELF** as the running firmware — keep the build artifacts of the exact flashed version.
4. **Correlate the memory trend.** Line up the HEARTBEAT lines: flat vs. decaying `heap_min_free`/`psram_free`, growing `fragmentation`, shrinking `block`. Also open HA history for the "Heap Min Free"/"Free PSRAM"/"Reset Reason" entities — the new sensors give server-side visibility even without serial.
5. **Check the Settings page.** After any reboot the "Причина сброса" row (and the HA "Reset Reason" entity) shows the last reason instantly — the fastest triage step for "was it a crash, watchdog, brownout or a clean OTA?".
6. **If serial capture keeps missing the crash** (device dies silently, e.g. brownout or watchdog reset before UART flushes): enable an IDF **core dump to flash** — `sdkconfig_options` with `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH: y` + `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF: y` + a custom `board_build.partitions` with a `coredump` partition sized for the flash; after a panic, read the partition (esptool read_flash) and decode with `espcoredump.py info_corefile` / the monitor's `decode_coredump`. Documented workflow, but untested here — expect to iterate on the partition layout.
7. **If the evidence points at memory:** iterate one knob at a time (table below), keeping the same capture procedure, and watch `heap_min_free` / `psram_free` on the heartbeats.
8. **If WDT/stalls:** reproduce while watching `loop_time` and the LVGL activity (swipes, popups, boot spinner, OTA) — a long main-loop render starving the audio/TCP tasks shows up both in `loop_time` and in the TWDT task list.
9. **Revert** `logger.level` to `DEBUG` once the issue is understood.

---

## Candidate root causes & config knobs (documented ones only)

| # | Hypothesis (specific to this project) | Supporting evidence to collect | Documented knob(s) to try |
|---|---|---|---|
| 1 | **Internal-heap exhaustion** — decoders (all of MP3/FLAC/OPUS/WAV are compiled with `format: NONE`), TLS, network and LVGL buffers competing for DRAM | Heartbeats: `heap_free`/`heap_min_free` → 0 before crash; allocation-failure logs; `fragmentation` climbing past ~50 % | Lower `media_player.buffer_size` per pipeline (default 1,000,000 B; try 400,000–600,000, minimum 4,000 for a minimal test); `task_stack_in_psram: true` only if logs show allocation failures; reduce `logger.task_log_buffer_size`; remove unneeded components |
| 2 | **PSRAM pressure / misconfiguration** — the audio pipeline + LVGL live mostly in PSRAM; octal 80 MHz PSRAM must be stable | Heartbeats: `psram_free` decaying; boot banner "PSRAM initialized"; crashes while buffering/decoding | Verify 8 MB PSRAM presence/speed (`psram:` block already octal/80MHz); `task_stack_in_psram: true`; if the PSRAM chip is flaky, wiring/quality is the fix |
| 3 | **Audio pipeline buffer bloat** — two pipelines × 1 MB default buffers = up to 2 MB allocated at start/stream time | Boot log allocation sizes; `heap_free` cliff right after playback starts | `media_player.buffer_size` reductions; docs recommend for memory-constrained devices: single pipeline, smaller buffers, `WAV` + low sample rate + 1 channel |
| 4 | **LVGL + audio concurrency** — a long LVGL render (full-page redraw, popups, boot spinner, OTA progress) blocks the main loop / competes for bus + RAM, starving the audio/TCP tasks → TWDT | `loop_time` spikes in heartbeats; "Task watchdog got triggered" naming `main`/`esp_media_player` tasks; stutter before the crash | LVGL `buffer_size` (currently 25 %) can be lowered (RAM trade-off); simplify the boot-screen animation; ensure OTA/render heavy work doesn't overlap playback; cooperative delays in scripts already yield (per project convention) |
| 5 | **Watchdog during long stalls** (flash ops, OTA write, WiFi reconnects with the fallback AP + captive portal) | `TASK_WDT`/`TG*WDT_SYS_RESET`/`RTCWDT_SYS_RESET` reasons + TWDT log lines | No ESPHome knob (IDF-internal) — address the underlying stall; split long operations, avoid blocking waits in the main loop |
| 6 | **Brownout** — power-supply dips under the combined audio + display + WiFi load (common with USB power or thin wires) | Silent reset; next boot reason `RTCWDT_BROWN_OUT_RESET` | Hardware (PSU/wiring); optionally raise the IDF brownout threshold via `sdkconfig_options` (`CONFIG_ESP*_BROWNOUT_*`, exact symbol per IDF version) |
| 7 | **Network/TCP instability under VERBOSE** | Only while testing at VERBOSE — disconnect/timeout storms before the crash | Don't run VERBOSE long-term; per-tag levels instead; `network.enable_high_performance: false` only if the docs' OOM note applies |

---

## What could NOT be verified locally (no `esphome` on this machine) — on-device/build-host flags

1. **Debug schema version.** Config uses the **current documented nested-key schema** (`free:` / `reset_reason:` children). If the installed ESPHome release predates it, validation fails and the legacy form must be used:
   ```yaml
   sensor:
     - platform: debug
       type: free
       name: "Heap Free"
   # ... type: block / min_free / fragmentation / psram / loop_time
   text_sensor:
     - platform: debug
       type: reset_reason
       name: "Reset Reason"
   ```
2. **Sensor availability** — `min_free`, `psram`, `loop_time` are documented as ESP32-family/version-gated; confirm each compiles on the installed ESPHome/IDF combo (drop `loop_time` first if any is rejected).
3. **`logger.log` `args` typing** — the ternary `int`/`float`/`const char*` args with `%d`/`%.0f`/`%s` compile per the documented `format`/`args` contract, but the exact argument packing is version-dependent; if a build error appears, replace the lambdas with `snprintf` into a `char[]` + a single string arg (pattern used elsewhere in the project).
4. **`reset_reason` value set** — the exact enum strings printed depend on the IDF version's `soc/reset_reasons.h`; the cheat-sheet above lists the documented ESP32-family set.
5. **`logger.hardware_uart` default** — docs say ESP32-S3 → `USB_SERIAL_JTAG` (19/20); confirm the board actually logs over native USB (the earlier `logs/*.txt` captures prove the current setup works — do not change it).
6. **`media_player.buffer_size` / `task_stack_in_psram` keys** — documented on the speaker media player page; accepted by the installed version only if the speaker platform release includes them (the project already flags `volume_increment` the same way).
7. **Core-dump-to-flash** — documented IDF workflow, but the exact `partitions.csv`/`sdkconfig_options` combination for this 16 MB layout is untested; expect iteration.
8. **`mdi-restart` glyph** — verified locally via `tests/font_codepoint_check.py` against `fonts/materialdesignicons-webfont.ttf` (this one IS validated, not just documented).
9. **UI row placement** — y=222..270 fits above the nav bar (y=294) by geometry; confirm visually on the device that the row renders inside the 480×320 canvas and the caption/value don't clip for the longest reason strings.

---

## Files changed

| File | Change |
|---|---|
| [`esp-web-radio.yaml`](../esp-web-radio.yaml) | `debug` component + 6 sensors + `reset_reason` text sensor; logger annotation (`baud_rate`, VERBOSE guidance); `on_boot` BOOT diag log; `heartbeat_diag` interval (15 min) |
| [`packages/display-fonts.yaml`](../packages/display-fonts.yaml) | `${text_restart}` substitution + `mdi-restart` (`\U000F0709`) in the `menu24` extras |
| [`packages/esp-web-radio-page_settings.yaml`](../packages/esp-web-radio-page_settings.yaml) | Row 4 "Причина сброса" (hidden until the sensor has a state); `refresh_settings_info` reset-reason branch; header/geometry comments |
| [`README.md`](../README.md) | Features + Info/Settings section (reset-reason row), new "Diagnostics & troubleshooting" section, bindings rows, project-tree/font notes, TOC |
| [`TODO.md`](../TODO.md) | Item checked off + Completed entry |

## Validation (no local esphome)

- `python tests/yaml_syntax_check.py` — expect **14/14 OK** (no new package files; all edits landed in existing files).
- `python tests/font_codepoint_check.py` — expect the new `mdi-restart` (`U+F0709`) FOUND in the webfont.
- Grep matrix — new ids (`reset_reason`, `heap_free`, `heap_min_free`, `heap_block`, `heap_fragmentation`, `psram_free`, `loop_time`, `heartbeat_diag`, `settings_row_reset`, `settings_lbl_reset_icon/caption/reason`, `text_restart`) defined once, referenced consistently; no stale references; volume/mute triggers, progress/time hide logic and nav indicator logic untouched.