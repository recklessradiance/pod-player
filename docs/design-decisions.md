# Pod Player: Design Decisions

An MP3 player built around an ESP32-C3 SuperMini, a DFPlayer Mini, a 0.96" SSD1306 OLED and a
speaker, controlled from a web page on the local network.

History: it was first prototyped on a KK2.1.5 flight-controller board. That board stopped answering
after a failed flash and was dropped, and its files were removed from the repo. They are in git
history, for example the stock firmware backup:
`git show 824ee8e:backups/kk2-1-5-original/flash.hex`.

## Hardware

| Part | Notes |
|---|---|
| ESP32-C3 SuperMini | Runs the player, the web server and the OLED. Powered over USB-C. |
| DFPlayer Mini (MP3-TF-16P V3.0, YX5200 type) | Reads the microSD card and decodes MP3. Serial link at 9600 baud. |
| microSD card | FAT32. The module plays the Nth file on the card, in the order the files were written. |
| 0.96" SSD1306 OLED, 128x64, I2C | Shows state, repeat mode, track, elapsed time, volume and the network address. |
| 8 ohm 0.5 W speaker | On the DFPlayer SPK1 and SPK2 pins (the two terminals; neither goes to ground). |

| DFPlayer Mini | ESP32-C3 SuperMini |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| TX | GPIO3 (ESP32 RX) |
| RX | GPIO4 (ESP32 TX); a 1 kohm resistor in this wire reduces clicks |
| SPK1 / SPK2 | speaker |

| OLED | ESP32-C3 SuperMini |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO6 |
| SCL | GPIO7 |

Pins are set in `player/config.h`. GPIO2, 8 and 9 are strapping pins on this board and are avoided.
All grounds are joined.

Power: USB-C 5 V into the ESP32-C3. Only 3.3 V is available for the DFPlayer, which limits the
volume it can deliver; the 0.5 W speaker is easy to overdrive, so `VOLUME_MAX` is capped at 15 of 30.
For more volume, power the DFPlayer from a 5 V pin or a LiPo rail (3.7 to 4.2 V) with a 100 uF
capacitor across its supply pins.

Controls: the web page only (no physical buttons).

## Decisions

- **Decoder:** DFPlayer Mini. The ESP32-C3 could decode MP3 itself with an SD card module and an
  I2S audio chip, which would give file names, tags and any playback order; not done.
- **Control:** web page on the local network. Wi-Fi is entered on a phone through a setup portal.
- **Track names:** not available. The module reports track numbers only.
- **Bluetooth, shuffle, over-the-air updates:** not done.

## Player firmware: behavior, guardrails and operation

Source: `player/` (`player.ino`, `config.h`, `web_page.h`). Test: `tools/test_controls.py`.

**Controls (web page)**: previous, play, pause, stop, next, volume slider, repeat (off / all / one),
jump to a track, rescan the SD card, change the Wi-Fi network. Next and previous always start the
neighbouring track (wrapping at the ends), whether playing, paused or stopped. Play while playing
and pause while paused do nothing.

**Web UI (`player/web_page.h`)**: a Switch Joy-Con inspired layout with a Game Boy style LCD, all inline
(no fonts, scripts or images from the internet, so it works on a hotspot with no internet). Blue
left controller: a D-pad with volume up and down and previous and next. Red right controller: A play,
B pause, Y stop, X repeat. The LCD shows the state, track and count, estimated elapsed time, an
equaliser animation while playing, and the status or error message. Volume is a row of segments you can
tap. Four LEDs chase while playing and hold one lit while paused. Extras: dark mode, vibration on tap
where supported, hold the D-pad to repeat volume, keyboard shortcuts (space, arrows, S, R), controls
dimmed with a clear message when there is no module, card or tracks, a "lost connection" banner, polling
paused while the tab is hidden, and a "More" panel (jump to a track, rescan the card, reset the module,
change Wi-Fi, device details).
UI preview and test: `python3 tools/ui/mock_player.py` serves the real page against a simulated player;
`node tools/ui/ui_test.mjs` drives it in headless Chrome and checks 36 behaviours (macOS Chrome path).

**Playback guardrails**
- Every frame to the DFPlayer goes through a queue with a minimum gap (`DF_CMD_GAP_MS`), because
  clone chips drop commands that arrive too close together.
- A track change while something plays sends stop first, then play (`DF_STOP_GAP_MS` apart);
  starting a track over a playing one is unreliable on these modules.
- Tracks start by position (command `0x03`, `PLAY_BY_INDEX 1`), so file names do not matter. The
  module counts every file on the card, so keep only the tracks on it. On a Mac, remove the hidden
  `._*` copies with `dot_clean` before using the card.
- A missing file (error 6) skips in the direction of travel, at most `MAX_MISSING_SKIPS` times in
  a row, then stops and says so.
- "Track finished" messages that arrive within `FINISH_IGNORE_MS` of starting a track are ignored.
- Nothing claims to be playing when there is no module, no card or no tracks. The OLED and the
  page show which of those it is.
- Track changes closer than `TRACK_CHANGE_MIN_MS` are refused (HTTP 429).
- Volume is clamped to `VOLUME_MAX`; track numbers are checked against the card.

**Web API guardrails**: actions are POST only and must carry the header `X-Pod: 1`, so a page from
another site cannot press the buttons (the browser would need a preflight this device never
grants). Numeric arguments are parsed strictly; bad input gets HTTP 400 and sends nothing.

**Reliability**
- Task watchdog (`WDT_TIMEOUT_MS`, 15 s): the board restarts if the main loop stalls.
- USB serial logging never blocks (`Serial.setTxTimeoutMs(0)`), so an unplugged board is not slowed.
- Wi-Fi: reconnects when the link drops and follows IP changes. `WIFI_MEMORY` in `config.h` chooses
  whether the network is remembered: 0 remembers it, 1 forgets it at power-up only, 2 (current) forgets
  it at every start, so the setup portal opens each time and the password is entered on a phone.
  Because the portal library writes the network to flash when it is saved, "forgetting" means it is
  erased at the start of the next boot. With 2, any restart (power cut, watchdog, update) leaves the
  player waiting in the portal until someone sets the Wi-Fi again; it falls back to its own hotspot with
  the control page after `PORTAL_TIMEOUT_S`. With 0 or 1, the fallback hotspot retries the saved network
  every `NET_RETRY_MS`.
- Hotspot password is unique per device (`pod-` + 6 hex digits of the chip ID) and shown on the
  OLED while the setup portal is open. Set `AP_PASSWORD` in `config.h` to override.
- Status and health JSON use fixed buffers (no heap churn over long uptimes).
- `/api/health`: uptime, free and lowest heap, signal strength, last reset reason, boot and crash
  counters, DFPlayer frame counters, queue drops, Wi-Fi rejoins.
- Logging: `LOG_LEVEL` 0 off, 1 events (default), 2 also every DFPlayer frame.
- Volume is sent again `VOLUME_RESEND_MS` after power-up, and after a module reset or card change,
  because the module ignores some early commands (DIYPOD does the same). A reset returns the module
  to full volume, so the volume is restored in the same command burst.
- Module reset (`/api/reset-module`, "Reset module" on the page): stop, reset the DFPlayer, select
  the SD card, restore the volume and re-read the card. Refused if repeated within
  `MODULE_RESET_MIN_MS`.
- Elapsed play time (OLED, page and `elapsed` in the status): counted on the ESP32 from the moment
  a track is started, pauses excluded, so it is an estimate (the module cannot report position, and
  track length is unknown). It resets on every new track.

**Testing**: build with `--build-property "compiler.cpp.extra_flags=-DLOG_LEVEL=2"`, flash, put the
Mac on the same network as the player and connect it by USB, then run
`python3 tools/test_controls.py http://<player-ip>`. It presses every control through the web API
and checks the frames the board sends to the DFPlayer, the reported state, input refusal, the
rate limit, the header check and frame spacing. Audio plays while it runs.

**Known limits and unverified items**
- Natural end of a track (`0x3D` "finished", which drives auto-advance and repeat) has not been
  verified on this module. Test with a very short track. If a module does not send it, auto-advance
  would stall. The reliable fix is wiring the DFPlayer BUSY pin to a free GPIO.
- DFPlayer on 3.3 V gives limited volume. The 0.5 W speaker is easy to overdrive, so keep
  `VOLUME_MAX` modest.
- Files must be 16-bit PCM MP3 at 22.05, 44.1 or 48 kHz; other rates are skipped by the chip.
- A 1 kohm resistor in the ESP32 TX to DFPlayer RX wire reduces clicks.
- Not done by choice: over-the-air updates, splitting the firmware into modules, host-side unit tests.
