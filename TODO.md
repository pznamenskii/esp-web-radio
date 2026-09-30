### Completed

- [x] Now Playing page from Designer
- [x] Bind Now Playing labels and buttons
- [x] Extract theme and styles, match remaining pages
- [x] top layer needs to be narrowed down to allow page to fit
- [x] Time in the top-layer title bar
- [x] No-HA mode — time sync (NTP `wifi_time` + `update_clock` fallback until the HA API connects)
- [x] When device enters AP mode with captive portal (is_ap_active) - display a new page with AP name and password prompting user to connect to AP and complete WiFi setup; wifi_status widget indicator shall blink wifi_100 glyph while AP is active (page_ap_setup, wifi.ap_active polling, wifi_status blink)
- [x] Boot screen shall be up till WiFi connects, user touches boot screen or WiFI AP with captive portal comes up (is_ap_active) (dismissed on touch / wifi.on_connect / enter_ap_mode)
- [x] Disable going idle when in AP mode (`on_idle` guard `if: not wifi.ap_active` — backlight/LVGL never pause while the fallback AP is up)
- [x] Show QR code in AP mode via "qrcode" widget (`ap_qr` qrcode widget encoding the WiFi connection string `WIFI:T:WPA;S:${ap_ssid};P:${ap_password};;`; originally rendered `${ap_url}` — superseded per [`plans/improvements-1-3.md`](plans/improvements-1-3.md); `ap_url` and `ap_password` substitutions added in `esp-web-radio.yaml`)
- [x] Change AP mode blinking wifi glyph to wifi-cog; change "wifi_100" to "wifi" glyph (`text_wifi` / `text_wifi_cog` substitutions + `menu24` extras; `wifi_status` uses `${text_wifi}` with runtime `lvgl.label.update` swaps in `enter_ap_mode` / `exit_ap_mode` / `wifi.on_connect`; AP hero icon = wifi-cog)
- [x] On Now Playing page hide widgets and labels if their intended info is not available (`mp_station_logo` hidden — no station-picture source yet (roadmap item 4); `mp_now_playing_art` shown only while the `now_playing_art` sensor reports a `media_image_url`; `mp_visualizer` shown only while `media_player.on_state` reports playing; artist/track auto-hide was already in place; title/artist/track block repositioned for the hidden-logo steady state) — per [`plans/improvements-4-6.md`](plans/improvements-4-6.md)
- [x] Add Stations page — dynamic station button grid (`page_stations`, 12 slots `st_btn_1..12` fed by the `refresh_stations` script from HA `station_N_name` sensors or the offline names; empty slots hidden; grid height set to rows×56 so a vertical scrollbar appears once more than 8 stations are active; pressing a slot plays via `play_station` and returns to Now Playing) — per [`plans/improvements-4-6.md`](plans/improvements-4-6.md)
- [x] No-HA mode — local stations list from offline_stations.yaml, stored as hardcoded substitutions in `packages/esp-web-radio-offline_stations.yaml`; `play_station` routes playback through HA (`media-source://`) while connected and through the native `media_player.play_media` action with direct URLs otherwise; `refresh_stations` re-populates the Stations page with HA sensors when the API reconnects — per [`plans/improvements-4-6.md`](plans/improvements-4-6.md)

### Issues to fix

- [x] Glyphs are wrong - i.e. I see "steam app" instead of "play next"
- [x] backlight does not light up when coming back from idle sleep
- [x] after playback starts, touching pause button does not do anything
- [x] swipe to change pages does not work, button navigation works
- [X] it seems when now playing page elements are hidden due to no value, other elements change their positions - this shall not happen and all elements positions must be static
- [x] no mute button
- [x] volume slider feels misplaced and does not react in "wifi only" mode; let's replace with "-" (volume-minus glyph), "mute" (volume-off glyph when active in orange accent, in gray accent when inactive) "+"  (volume-plus glyph) buttons and when - or + is pressed have a horizontal volume slider popup (disappear after 5 sec of inactivity)
- [ ] Arc on boot page is not animated, re-design
- [ ] AP conflicts with web server and requires authentication

### Remaining improvements

- [ ] Re-design navigation bar on top_layer: instead of buttons let's just have dots-style indication of our location but instead of dots let's have glyphs: "play-circle-outline" for now playing page, "cogs" for settings page and "radio" for stations page; when page is activy, relevant glyph size increases to induicate focus while other pages glyphs stay in lower font
- [ ] Station widget shall display station logo (from name>file mapping?)
- [ ] artist and track identification/lookup
- [ ] Offline stations list editable via the built-in web server
- [ ] Info/Settings page: wifi signal level, uptime, startup volume level, idle/inactivity timer to idle screen
- [ ] Dynamic WiFi and HA API connection status display (hidden icons if no connection; for wifi different icons depending on signal level)
