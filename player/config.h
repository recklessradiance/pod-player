#pragma once

// ESP32-C3 SuperMini wiring. Change here if the wiring differs.
#define DFPLAYER_RX_PIN 3     // ESP32 RX  <- DFPlayer TX
#define DFPLAYER_TX_PIN 4     // ESP32 TX  -> DFPlayer RX (a 1 kohm resistor in this wire reduces clicks)
#define OLED_SDA_PIN    6
#define OLED_SCL_PIN    7

// Volume. The DFPlayer range is 0 to 30. The 8 ohm 0.5 W speaker can be overdriven by the
// DFPlayer's amplifier, so the top of the range is capped.
#define VOLUME_MAX      15
#define VOLUME_DEFAULT  8

// Number of tracks in /mp3 on the card. 0 asks the DFPlayer (it counts every file on the card,
// so keep only the tracks on it, or set the real number here).
#define TRACK_COUNT_OVERRIDE 0
#define MAX_TRACK_NUMBER     3000    // upper bound accepted by the API while the count is unknown

// How tracks are started.
//   1 = by position (command 0x03): the Nth file on the card, in the order the files were
//       written to it. File names do not matter. Matches the file count the module reports.
//   0 = by name (command 0x12): needs /mp3/0001.mp3, /mp3/0002.mp3, ... with four-digit names.
#define PLAY_BY_INDEX        1

// Network
#define HOSTNAME              "thepod"             // http://thepod.local
#define AP_SSID               "thepod-player"      // setup / fallback hotspot name
#define AP_PASSWORD           ""                   // empty = unique per device: "pod-" + 6 hex digits of the chip ID,
                                                   // shown on the OLED. Set 8+ characters to override.
#define PORTAL_TIMEOUT_S      300                  // how long the setup page stays open

// Some ESP32-C3 SuperMini boards connect more reliably with a lower transmit power.
// Set to 0 to leave the default.
#define LOWER_WIFI_TX_POWER   1

// How long a changed setting must stay unchanged before it is saved (flash wear).
#define SAVE_DELAY_MS         2000

// DFPlayer timing. Clone chips drop commands that arrive too close together.
#define DF_CMD_GAP_MS         120    // minimum time between frames sent to the DFPlayer
#define DF_STOP_GAP_MS        200    // pause after a stop before the next play
#define TRACK_CHANGE_MIN_MS   300    // track changes requested faster than this are refused
#define FINISH_IGNORE_MS      2000   // ignore "track finished" this soon after starting a track
#define MAX_MISSING_SKIPS     3      // give up after this many missing files in a row

// Logging: 0 off, 1 events (connection, errors, resets), 2 also every DFPlayer frame.
// tools/test_controls.py needs level 2:  --build-property build.extra_flags=-DLOG_LEVEL=2
#ifndef LOG_LEVEL
#define LOG_LEVEL             1
#endif

// Reliability
#define WDT_TIMEOUT_MS        15000  // restart if the main loop stalls this long
#define NET_RETRY_MS          60000  // in fallback hotspot mode, try the saved Wi-Fi this often
#define NET_JOIN_MS           20000  // how long each such attempt waits
