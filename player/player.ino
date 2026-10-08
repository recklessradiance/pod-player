// Pod Player: ESP32-C3 SuperMini + DFPlayer Mini + 0.96" SSD1306 OLED, controlled from a web page.
//
// - The DFPlayer plays the files on its microSD card by position (see PLAY_BY_INDEX in config.h).
// - The OLED shows play state, repeat mode, track number, volume and the network address.
// - A small web page (http://thepod.local, or the IP shown on the OLED) controls everything.
// - First start: opens a hotspot with a setup page (captive portal). Join it with a phone, pick
//   your Wi-Fi from the list and enter the password; it is saved and used from then on.
//   If no Wi-Fi can be joined, a plain hotspot with the control page is the fallback.
// - Volume, track and repeat mode are saved to flash and restored at boot.
//
// Guardrails (why the code is shaped the way it is):
// - Every frame to the DFPlayer goes through a queue with a minimum gap, because clone chips
//   drop commands that arrive too close together.
// - A track change while something plays sends "stop" first, then "play", because starting a
//   track over a playing one is unreliable on these modules.
// - A missing file (error 6) skips to the next or previous track instead of going silent.
// - "Track finished" messages that arrive right after starting a track are ignored.
// - The API accepts POST only, with a custom header, so a web page from another site cannot
//   press the buttons. All inputs are validated; track changes are rate limited.
// - Nothing claims to be playing when there is no module, no card, or no tracks.
//
// Wiring and decisions: docs/design-decisions.md. Pins and settings: config.h.

#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "web_page.h"

#define LOGE(...) do { if (LOG_LEVEL >= 1) Serial.printf(__VA_ARGS__); } while (0)
#define LOGD(...) do { if (LOG_LEVEL >= 2) Serial.printf(__VA_ARGS__); } while (0)

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);
WebServer server(80);
Preferences prefs;

enum PlayState { STOPPED, PLAYING, PAUSED };
enum RepeatMode { REPEAT_OFF, REPEAT_ALL, REPEAT_ONE };

static PlayState state = STOPPED;
static RepeatMode repeatMode = REPEAT_OFF;
static uint8_t volume = VOLUME_DEFAULT;
static uint16_t currentTrack = 1;
static uint16_t trackCount = TRACK_COUNT_OVERRIDE;
static bool wifiSta = false;
static String ipText = "0.0.0.0";

// Counters reported by /api/health.
static struct {
  uint32_t txFrames, rxFrames, queueDrops, rejoins, boots, crashes;
} stats;
static const char *resetReason = "unknown";

// Hotspot password: unique per device unless config.h sets one.
static char apPass[16];


// ---------------------------------------------------------------- DFPlayer / SD status

static bool dfSeen = false;                 // any valid frame received from the DFPlayer
static int8_t sdFlag = -1;                  // -1 unknown, 0 no card, 1 card present
static bool countKnown = (TRACK_COUNT_OVERRIDE != 0);
static uint8_t queries = 0;                 // track-count questions sent so far
static uint8_t lastError = 0;               // last DFPlayer error code
static uint32_t lastErrorAt = 0;

enum LinkState { LINK_CHECKING, LINK_OK, LINK_NO_PLAYER, LINK_NO_SD, LINK_EMPTY };

static LinkState linkState() {
  if (!dfSeen) return queries >= 4 ? LINK_NO_PLAYER : LINK_CHECKING;
  if (sdFlag == 0) return LINK_NO_SD;
  if (countKnown) return trackCount == 0 ? LINK_EMPTY : LINK_OK;
  return queries >= 8 ? LINK_NO_SD : LINK_CHECKING;   // frames arrive but nothing about files
}

static bool canPlay() {
  LinkState l = linkState();
  return l == LINK_OK || l == LINK_CHECKING;
}

static const char *blockReason() {
  switch (linkState()) {
    case LINK_NO_SD:     return "no SD card";
    case LINK_EMPTY:     return "no tracks on the card";
    case LINK_NO_PLAYER: return "DFPlayer not responding";
    default:             return "busy, try again";
  }
}

#if PLAY_BY_INDEX
static const uint8_t PLAY_CMD = 0x03;      // Nth file on the card, any file names
#else
static const uint8_t PLAY_CMD = 0x12;      // /mp3/NNNN.mp3
#endif

static bool dirty = false;
static uint32_t dirtyAt = 0;
static uint32_t lastFinishAt = 0;
static uint32_t lastPlayCmdAt = 0;          // when the last "play track" was queued
static uint32_t lastTrackReqAt = 0;         // rate limit for track changes
static int8_t skipDir = 0;                  // direction to skip in if a file is missing
static uint16_t skipsLeft = 0;
static uint8_t retryLeft = 0;

// ---------------------------------------------------------------- DFPlayer link

struct DfCmd {
  uint8_t cmd;
  uint16_t param;
  uint16_t gapAfter;                        // wait this long after sending before the next frame
};
static const uint8_t DF_QUEUE_SIZE = 16;
static DfCmd dfQueue[DF_QUEUE_SIZE];
static uint8_t qHead = 0;
static uint8_t qCount = 0;
static uint32_t nextTxAt = 0;

// Writes one 10-byte frame to the module immediately. Use dfEnqueue() for normal commands.
static void dfWrite(uint8_t cmd, uint16_t param = 0) {
  uint8_t f[10] = {0x7E, 0xFF, 0x06, cmd, 0x00,
                   (uint8_t)(param >> 8), (uint8_t)(param & 0xFF), 0, 0, 0xEF};
  uint16_t sum = 0;
  for (uint8_t i = 1; i <= 6; i++) sum += f[i];
  sum = (uint16_t)(0 - sum);
  f[7] = sum >> 8;
  f[8] = sum & 0xFF;
  Serial1.write(f, sizeof(f));
  stats.txFrames++;
  LOGD("DF tx: cmd 0x%02X param %u\n", cmd, param);
}

static uint8_t dfQueueFree() {
  return DF_QUEUE_SIZE - qCount;
}

static bool dfEnqueue(uint8_t cmd, uint16_t param = 0, uint16_t gapAfter = DF_CMD_GAP_MS) {
  if (qCount >= DF_QUEUE_SIZE) {
    stats.queueDrops++;
    return false;
  }
  dfQueue[(qHead + qCount) % DF_QUEUE_SIZE] = {cmd, param, gapAfter};
  qCount++;
  return true;
}

static void dfPump() {
  if (qCount == 0 || (int32_t)(millis() - nextTxAt) < 0) return;
  DfCmd c = dfQueue[qHead];
  qHead = (qHead + 1) % DF_QUEUE_SIZE;
  qCount--;
  dfWrite(c.cmd, c.param);
  nextTxAt = millis() + c.gapAfter;
}

static void markDirty() {
  dirty = true;
  dirtyAt = millis();
}

// ---------------------------------------------------------------- playback logic

static uint16_t nextTrackNumber() {
  if (trackCount == 0) return currentTrack + 1;
  return (currentTrack % trackCount) + 1;
}

static uint16_t prevTrackNumber() {
  if (currentTrack > 1) return currentTrack - 1;
  return trackCount ? trackCount : 1;
}

// Starts track n. dir is the direction to skip in if the file turns out to be missing.
// stopFirst sends "stop" before "play", which is needed when something is playing.
static bool requestTrack(uint16_t n, int8_t dir, bool continuing, bool stopFirst) {
  if (!canPlay()) return false;
  if (dfQueueFree() < 3) return false;
  if (n < 1) n = 1;
  if (trackCount && n > trackCount) n = 1;
  currentTrack = n;
  if (stopFirst) dfEnqueue(0x16, 0, DF_STOP_GAP_MS);
  dfEnqueue(PLAY_CMD, n);                  // start track n
  state = PLAYING;
  lastPlayCmdAt = millis();
  if (!continuing) {
    skipDir = dir;
    skipsLeft = MAX_MISSING_SKIPS;
    retryLeft = 1;
  }
  markDirty();
  return true;
}

static bool doPlay() {
  if (state == PLAYING) return true;                       // already playing: nothing to do
  if (state == PAUSED) {
    if (dfQueueFree() < 2) return false;
    dfEnqueue(0x0D);                                       // resume
    state = PLAYING;
    return true;
  }
  return requestTrack(currentTrack, +1, false, false);     // stopped: start the current track
}

static void doPause() {
  if (state != PLAYING) return;                            // pausing only makes sense while playing
  dfEnqueue(0x0E);
  state = PAUSED;
}

static void doStop() {
  if (state == STOPPED) return;
  dfEnqueue(0x16);
  state = STOPPED;
}

static void setVolume(long v) {
  if (v < 0) v = 0;
  if (v > VOLUME_MAX) v = VOLUME_MAX;
  volume = (uint8_t)v;
  dfEnqueue(0x06, volume);
  markDirty();
}

static void setRepeat(RepeatMode m) {
  repeatMode = m;
  markDirty();
}

static void onTrackFinished() {
  if (state != PLAYING) return;
  if (repeatMode == REPEAT_ONE) {
    requestTrack(currentTrack, +1, false, false);
    return;
  }
  bool last = trackCount && currentTrack >= trackCount;
  if (last && repeatMode == REPEAT_OFF) {
    state = STOPPED;
    return;
  }
  requestTrack(nextTrackNumber(), +1, false, false);
}

// The module could not find the file we just asked for: skip in the direction of travel.
static void onMissingTrack() {
  if (state != PLAYING || millis() - lastPlayCmdAt > 4000) return;
  if (skipDir != 0 && skipsLeft > 0) {
    skipsLeft--;
    uint16_t n = skipDir > 0 ? nextTrackNumber() : prevTrackNumber();
    LOGE("Track %u not found, trying %u\n", currentTrack, n);
    if (requestTrack(n, skipDir, true, false)) return;
  }
  state = STOPPED;                                         // nothing playable found: stop honestly
}

static void dfHandle(uint8_t cmd, uint16_t param) {
  stats.rxFrames++;
  LOGD("DF rx: cmd 0x%02X param %u\n", cmd, param);
  dfSeen = true;
  switch (cmd) {
    case 0x3D:                     // finished playing a track on the SD card
      if (millis() - lastFinishAt > 800 && millis() - lastPlayCmdAt > FINISH_IGNORE_MS) {
        lastFinishAt = millis();   // some clones report it twice
        onTrackFinished();
      }
      break;
    case 0x3F:                     // power-up: online storage devices (bit 1 = SD card)
      if (param & 0x02) {
        sdFlag = 1;
        countKnown = (TRACK_COUNT_OVERRIDE != 0);   // forget any count taken while the card was out
        queries = 0;
      } else if (param == 0) {
        sdFlag = 0;
      }
      break;
    case 0x3A:                     // storage inserted (2 = SD card)
      if (param == 2) {
        sdFlag = 1;
        countKnown = (TRACK_COUNT_OVERRIDE != 0);
        queries = 0;
      }
      break;
    case 0x3B:                     // storage removed (2 = SD card)
      if (param == 2) {
        sdFlag = 0;
        trackCount = TRACK_COUNT_OVERRIDE;
        countKnown = (TRACK_COUNT_OVERRIDE != 0);
        state = STOPPED;
      }
      break;
    case 0x48:                     // reply: number of files on the SD card
      if (param == 0 && sdFlag == 0) break;        // no card: nothing to count
      if (param > 9999) break;                     // not a plausible count: ignore the frame
      sdFlag = 1;
      if (TRACK_COUNT_OVERRIDE == 0) {
        trackCount = param;
        countKnown = true;
        if (trackCount && currentTrack > trackCount) currentTrack = 1;
      }
      break;
    case 0x40:                     // error: 1 busy/no card, 5 out of range, 6 not found, 8 SD read failed
      lastError = param;
      lastErrorAt = millis();
      if (param == 8) sdFlag = 0;
      if (param == 5 || param == 6) {
        onMissingTrack();
      } else if (param == 1 && state == PLAYING && retryLeft > 0 && millis() - lastPlayCmdAt < 3000) {
        retryLeft--;               // module was still busy: ask once more
        dfEnqueue(PLAY_CMD, currentTrack);
        lastPlayCmdAt = millis();
      }
      break;
    default:
      break;
  }
}

static void dfPoll() {
  static uint8_t rx[10];
  static uint8_t len = 0;
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    if (len == 0 && b != 0x7E) continue;
    rx[len++] = b;
    if (len == 2 && rx[1] != 0xFF) {                       // not a frame header: resynchronise
      len = (b == 0x7E) ? 1 : 0;
      if (len == 1) rx[0] = 0x7E;
      continue;
    }
    if (len == 10) {
      if (rx[9] == 0xEF) dfHandle(rx[3], ((uint16_t)rx[5] << 8) | rx[6]);
      len = 0;
    }
  }
}

// ---------------------------------------------------------------- saved state

static void loadState() {
  if (!prefs.begin("player", false)) LOGE("Saved settings unavailable; using defaults\n");
  volume = prefs.getUChar("vol", VOLUME_DEFAULT);
  if (volume > VOLUME_MAX) volume = VOLUME_MAX;
  currentTrack = prefs.getUShort("trk", 1);
  if (currentTrack < 1) currentTrack = 1;
  uint8_t r = prefs.getUChar("rep", 0);
  repeatMode = (RepeatMode)(r > 2 ? 0 : r);
}

static void saveIfDue() {
  if (dirty && millis() - dirtyAt > SAVE_DELAY_MS) {
    prefs.putUChar("vol", volume);
    prefs.putUShort("trk", currentTrack);
    prefs.putUChar("rep", (uint8_t)repeatMode);
    dirty = false;
  }
}

// ---------------------------------------------------------------- display

static void showMessage(const char *line1, const char *line2 = "", const char *line3 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 16, line1);
  u8g2.drawStr(0, 32, line2);
  u8g2.drawStr(0, 48, line3);
  u8g2.sendBuffer();
}

static void drawScreen() {
  char buf[24];
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x10_tr);
  const char *st = state == PLAYING ? "> PLAY" : state == PAUSED ? "|| PAUSE" : "[] STOP";
  u8g2.drawStr(0, 9, st);
  if (millis() - lastErrorAt < 3000 && lastError != 0) {
    snprintf(buf, sizeof(buf), lastError == 6 ? "NO FILE" : "ERR %u", lastError);
  } else {
    snprintf(buf, sizeof(buf), "%s", repeatMode == REPEAT_ALL ? "ALL" : repeatMode == REPEAT_ONE ? "ONE" : "");
  }
  u8g2.drawStr(66, 9, buf);
  u8g2.drawStr(104, 9, wifiSta ? "WiFi" : "AP");
  u8g2.drawHLine(0, 11, 128);

  u8g2.setFont(u8g2_font_logisoso22_tn);
  snprintf(buf, sizeof(buf), "%u", currentTrack);
  u8g2.drawStr((128 - u8g2.getStrWidth(buf)) / 2, 38, buf);

  u8g2.setFont(u8g2_font_6x10_tr);
  switch (linkState()) {
    case LINK_OK:        snprintf(buf, sizeof(buf), "of %u", trackCount); break;
    case LINK_EMPTY:     snprintf(buf, sizeof(buf), "NO TRACKS"); break;
    case LINK_NO_SD:     snprintf(buf, sizeof(buf), "NO SD CARD"); break;
    case LINK_NO_PLAYER: snprintf(buf, sizeof(buf), "NO DFPLAYER"); break;
    default:             snprintf(buf, sizeof(buf), "checking SD..."); break;
  }
  u8g2.drawStr((128 - u8g2.getStrWidth(buf)) / 2, 49, buf);

  if ((millis() / 4000) % 2 == 0) {                       // volume bar
    u8g2.drawStr(0, 62, "VOL");
    u8g2.drawFrame(24, 54, 80, 9);
    u8g2.drawBox(26, 56, (uint16_t)volume * 76 / VOLUME_MAX, 5);
    snprintf(buf, sizeof(buf), "%u", volume);
    u8g2.drawStr(108, 62, buf);
  } else {                                                // network address
    snprintf(buf, sizeof(buf), "%s", ipText.c_str());
    u8g2.drawStr((128 - u8g2.getStrWidth(buf)) / 2, 62, buf);
  }

  u8g2.sendBuffer();
}

// ---------------------------------------------------------------- network

static void makeApPassword() {
  if (strlen(AP_PASSWORD) >= 8) {
    snprintf(apPass, sizeof(apPass), "%s", AP_PASSWORD);
  } else {
    snprintf(apPass, sizeof(apPass), "pod-%06X", (unsigned)(ESP.getEfuseMac() & 0xFFFFFF));
  }
}

static void startNetwork() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
#if LOWER_WIFI_TX_POWER
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  showMessage("Connecting Wi-Fi", "saved network...");

  WiFiManager wm;
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setAPCallback([](WiFiManager *) {
    // Called when the setup hotspot opens.
    char line[32];
    snprintf(line, sizeof(line), "pass: %s", apPass);
    showMessage("Join Wi-Fi:", AP_SSID, line);
    LOGE("Setup hotspot open: join %s (password %s) and pick your network.\n", AP_SSID, apPass);
  });

  if (wm.autoConnect(AP_SSID, apPass)) {
    wifiSta = true;
    ipText = WiFi.localIP().toString();
  } else {
    // Nothing joined before the portal timed out: plain hotspot with the control page.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, apPass);
    wifiSta = false;
    ipText = WiFi.softAPIP().toString();
  }
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

// Keeps the connection alive:
// - on the home network: reconnect when the link drops, and follow IP address changes;
// - in fallback hotspot mode: periodically try the saved network again, so the player comes
//   back by itself after a power cut where the router was slower to start than the player.
static void netTick() {
  static uint32_t lastRetry = 0;
  static uint32_t lastIpCheck = 0;
  static uint32_t tryStart = 0;
  static bool trying = false;
  uint32_t now = millis();

  if (wifiSta) {
    if (WiFi.status() == WL_CONNECTED) {
      if (now - lastIpCheck > 5000) {
        lastIpCheck = now;
        String ip = WiFi.localIP().toString();
        if (ip != ipText) ipText = ip;
      }
    } else if (now - lastRetry > 10000) {
      lastRetry = now;
      stats.rejoins++;
      LOGE("Wi-Fi link lost, reconnecting\n");
      WiFi.reconnect();
    }
    return;
  }

  if (!trying) {
    if (now - lastRetry > NET_RETRY_MS) {
      lastRetry = now;
      trying = true;
      tryStart = now;
      stats.rejoins++;
      LOGE("Trying the saved Wi-Fi again\n");
      WiFi.mode(WIFI_AP_STA);                 // keep the hotspot up while trying
      WiFi.begin();                           // saved credentials
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    trying = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    wifiSta = true;
    ipText = WiFi.localIP().toString();
    MDNS.end();
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
    LOGE("Rejoined the saved Wi-Fi: http://%s/\n", ipText.c_str());
  } else if (now - tryStart > NET_JOIN_MS) {
    trying = false;
    WiFi.disconnect(false);                   // give up for now; the hotspot stays up
  }
}

static const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default:                return "other";
  }
}

static bool resetWasCrash() {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: case ESP_RST_BROWNOUT:
      return true;
    default:
      return false;
  }
}

// ---------------------------------------------------------------- web API

static void sendStatus(int code = 200, const char *msg = nullptr) {
  const char *st = state == PLAYING ? "playing" : state == PAUSED ? "paused" : "stopped";
  const char *rp = repeatMode == REPEAT_ALL ? "all" : repeatMode == REPEAT_ONE ? "one" : "off";
  LinkState ls = linkState();
  const char *lk = ls == LINK_OK ? "ok" : ls == LINK_EMPTY ? "empty" : ls == LINK_NO_SD ? "no_sd"
                 : ls == LINK_NO_PLAYER ? "no_player" : "checking";
  char buf[360];
  int n = snprintf(buf, sizeof(buf),
      "{\"track\":%u,\"count\":%u,\"playCmd\":%u,\"link\":\"%s\",\"error\":%u,"
      "\"volume\":%u,\"volumeMax\":%u,\"state\":\"%s\",\"repeat\":\"%s\","
      "\"wifi\":\"%s\",\"ip\":\"%s\",\"queue\":%u",
      currentTrack, trackCount, PLAY_CMD, lk,
      (unsigned)((millis() - lastErrorAt < 5000) ? lastError : 0),
      volume, (unsigned)VOLUME_MAX, st, rp, wifiSta ? "sta" : "ap", ipText.c_str(), qCount);
  if (msg && n > 0 && n < (int)sizeof(buf)) snprintf(buf + n, sizeof(buf) - n, ",\"msg\":\"%s\"", msg);
  size_t len = strlen(buf);
  if (len < sizeof(buf) - 2) { buf[len] = '}'; buf[len + 1] = 0; }
  server.send(code, "application/json", buf);
}

static void sendHealth() {
  char buf[420];
  snprintf(buf, sizeof(buf),
      "{\"uptime\":%lu,\"heapFree\":%u,\"heapMin\":%u,\"rssi\":%d,\"reset\":\"%s\","
      "\"boots\":%lu,\"crashes\":%lu,\"txFrames\":%lu,\"rxFrames\":%lu,"
      "\"queueDrops\":%lu,\"rejoins\":%lu,\"wifi\":\"%s\"}",
      (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
      wifiSta ? WiFi.RSSI() : 0, resetReason,
      (unsigned long)stats.boots, (unsigned long)stats.crashes, (unsigned long)stats.txFrames,
      (unsigned long)stats.rxFrames, (unsigned long)stats.queueDrops, (unsigned long)stats.rejoins,
      wifiSta ? "sta" : "ap");
  server.send(200, "application/json", buf);
}

// Actions must carry a custom header. A web page on another site cannot add one without the
// browser first asking this device for permission, which it never grants.
static bool apiGuard() {
  if (server.header("X-Pod") != "1") {
    server.send(403, "text/plain", "forbidden");
    return false;
  }
  return true;
}

// Strict integer argument: digits only (optional leading minus), at most 6 characters.
static bool parseNumber(const char *name, long &out) {
  if (!server.hasArg(name)) return false;
  String a = server.arg(name);
  if (a.length() == 0 || a.length() > 6) return false;
  for (size_t i = 0; i < a.length(); i++) {
    char c = a[i];
    if (!(c >= '0' && c <= '9') && !(i == 0 && c == '-' && a.length() > 1)) return false;
  }
  out = a.toInt();
  return true;
}

static bool trackChangeTooFast() {
  if (millis() - lastTrackReqAt < TRACK_CHANGE_MIN_MS) return true;
  lastTrackReqAt = millis();
  return false;
}

static void setupRoutes() {
  static const char *headers[] = {"X-Pod"};
  server.collectHeaders(headers, 1);

  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/status", HTTP_GET, []() { sendStatus(); });
  server.on("/api/health", HTTP_GET, []() { sendHealth(); });

  server.on("/api/play", HTTP_POST, []() {
    if (!apiGuard()) return;
    if (!doPlay()) sendStatus(409, blockReason()); else sendStatus();
  });
  server.on("/api/pause", HTTP_POST, []() {
    if (!apiGuard()) return;
    doPause();
    sendStatus();
  });
  server.on("/api/stop", HTTP_POST, []() {
    if (!apiGuard()) return;
    doStop();
    sendStatus();
  });
  server.on("/api/toggle", HTTP_POST, []() {
    if (!apiGuard()) return;
    if (state == PLAYING) { doPause(); sendStatus(); }
    else if (!doPlay()) sendStatus(409, blockReason());
    else sendStatus();
  });
  server.on("/api/next", HTTP_POST, []() {
    if (!apiGuard()) return;
    if (trackChangeTooFast()) { sendStatus(429, "too fast"); return; }
    if (!requestTrack(nextTrackNumber(), +1, false, state != STOPPED)) sendStatus(409, blockReason());
    else sendStatus();
  });
  server.on("/api/prev", HTTP_POST, []() {
    if (!apiGuard()) return;
    if (trackChangeTooFast()) { sendStatus(429, "too fast"); return; }
    if (!requestTrack(prevTrackNumber(), -1, false, state != STOPPED)) sendStatus(409, blockReason());
    else sendStatus();
  });
  server.on("/api/track", HTTP_POST, []() {
    if (!apiGuard()) return;
    long n;
    long limit = trackCount ? trackCount : MAX_TRACK_NUMBER;
    if (!parseNumber("n", n) || n < 1 || n > limit) { sendStatus(400, "bad track number"); return; }
    if (trackChangeTooFast()) { sendStatus(429, "too fast"); return; }
    if (!requestTrack((uint16_t)n, +1, false, state != STOPPED)) sendStatus(409, blockReason());
    else sendStatus();
  });
  server.on("/api/volume", HTTP_POST, []() {
    if (!apiGuard()) return;
    long v;
    if (!parseNumber("v", v)) { sendStatus(400, "bad volume"); return; }
    setVolume(v);
    sendStatus();
  });
  server.on("/api/repeat", HTTP_POST, []() {
    if (!apiGuard()) return;
    if (server.hasArg("mode")) {
      String m = server.arg("mode");
      if (m == "off") setRepeat(REPEAT_OFF);
      else if (m == "all") setRepeat(REPEAT_ALL);
      else if (m == "one") setRepeat(REPEAT_ONE);
      else { sendStatus(400, "bad repeat mode"); return; }
    } else {
      setRepeat((RepeatMode)((repeatMode + 1) % 3));
    }
    sendStatus();
  });
  server.on("/api/rescan", HTTP_POST, []() {      // re-read the card (after swapping or editing it)
    if (!apiGuard()) return;
    doStop();
    if (TRACK_COUNT_OVERRIDE == 0) { trackCount = 0; countKnown = false; }
    sdFlag = -1;
    queries = 0;
    dfEnqueue(0x09, 2, 300);
    sendStatus();
  });
  server.on("/api/wifi-reset", HTTP_POST, []() {
    if (!apiGuard()) return;
    server.send(200, "text/plain", "Wi-Fi settings cleared; restarting into setup");
    delay(300);
    WiFiManager wm;
    wm.resetSettings();
    ESP.restart();
  });

  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

// ---------------------------------------------------------------- main

static void startWatchdog() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = WDT_TIMEOUT_MS;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;                 // restart the board if the main loop stalls
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
  esp_task_wdt_add(NULL);
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);                 // never stall when no computer is reading the USB serial
  makeApPassword();
  loadState();

  resetReason = resetReasonText();
  stats.boots = prefs.getULong("boots", 0) + 1;
  stats.crashes = prefs.getULong("crashes", 0) + (resetWasCrash() ? 1 : 0);
  prefs.putULong("boots", stats.boots);
  prefs.putULong("crashes", stats.crashes);
  LOGE("Boot %lu, reset reason: %s, crashes so far: %lu\n",
       (unsigned long)stats.boots, resetReason, (unsigned long)stats.crashes);

  u8g2.begin();
  showMessage("Pod Player", "starting...");

  Serial1.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  delay(1500);                     // the DFPlayer needs a moment after power-up
  dfWrite(0x09, 2);                // source: microSD card
  delay(300);
  dfWrite(0x06, volume);
  delay(150);
  nextTxAt = millis();

  startNetwork();
  setupRoutes();
  LOGE("Pod Player ready: http://%s/  (%s)\n", ipText.c_str(), wifiSta ? "Wi-Fi" : "hotspot");
  startWatchdog();
}

void loop() {
  esp_task_wdt_reset();
  server.handleClient();
  dfPoll();
  dfPump();
  saveIfDue();

  // The DFPlayer needs a few seconds to read the card; keep asking until it answers.
  static uint32_t lastQuery = 0;
  if (TRACK_COUNT_OVERRIDE == 0 && !countKnown && queries < 20 &&
      millis() > 3000 && millis() - lastQuery > 2000 && dfQueueFree() > 2) {
    lastQuery = millis();
    queries++;
    dfEnqueue(0x48);
  }

  static uint32_t lastDraw = 0;
  if (millis() - lastDraw > 250) {
    lastDraw = millis();
    drawScreen();
  }

  netTick();
}
