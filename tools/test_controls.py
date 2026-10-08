#!/usr/bin/env python3
"""End-to-end control test for the Pod Player.

Presses every control through the web API and checks, from the board's USB serial log, that the
right frames reached the DFPlayer (and nothing else), that the reported state matches, that bad
input is refused, and that frames are spaced out.

The Mac must reach the player over the network (same Wi-Fi as the player) and be connected to
it by USB. Audio will play while this runs.

The firmware must be built with frame logging on (production builds leave it off):

  arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc player \\
      --build-property "compiler.cpp.extra_flags=-DLOG_LEVEL=2"
  arduino-cli upload  -p /dev/cu.usbmodemXXXX --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc player

Usage:  python3 tools/test_controls.py http://172.20.10.2 [/dev/cu.usbmodemXXXX]
"""
import glob
import json
import os
import re
import sys
import termios
import threading
import time
import tty
import urllib.error
import urllib.request

BASE = sys.argv[1].rstrip("/") if len(sys.argv) > 1 else None
if not BASE:
    sys.exit(__doc__)
PORT = sys.argv[2] if len(sys.argv) > 2 else (sorted(glob.glob("/dev/cu.usbmodem*")) or [None])[0]
if not PORT:
    sys.exit("No USB serial port found; pass it as the second argument.")

TX = re.compile(r"DF tx: cmd 0x([0-9A-F]{2}) param (\d+)")
MIN_GAP = 0.100        # seconds; the firmware uses 120 ms between frames
tx_log = []            # (time, cmd, param)
lock = threading.Lock()


def serial_reader():
    fd = os.open(PORT, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    buf = b""
    while True:
        try:
            data = os.read(fd, 4096)
        except BlockingIOError:
            time.sleep(0.01)
            continue
        except OSError:
            time.sleep(0.2)
            continue
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            m = TX.search(line.decode("latin1"))
            if m:
                with lock:
                    tx_log.append((time.time(), int(m.group(1), 16), int(m.group(2))))


def call(path, method="POST", header=True):
    req = urllib.request.Request(BASE + path, method=method, data=b"" if method == "POST" else None)
    if header:
        req.add_header("X-Pod", "1")
    try:
        with urllib.request.urlopen(req, timeout=8) as r:
            body = r.read().decode()
            code = r.status
    except urllib.error.HTTPError as e:
        body = e.read().decode()
        code = e.code
    try:
        return code, json.loads(body)
    except ValueError:
        return code, {"raw": body}


def status():
    return call("/api/status", "GET")[1]


passed = failed = 0


def check(label, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print(f"  PASS  {label}")
    else:
        failed += 1
        print(f"  FAIL  {label}  {detail}")


def frames_since(mark):
    with lock:
        return [(t, c, p) for (t, c, p) in tx_log if t >= mark]


def step(label, path, expect_tx, expect_code=200, expect_state=None, expect_track=None,
         method="POST", header=True, wait=1.0):
    """Run one request, then compare the frames the board sent with expect_tx."""
    mark = time.time()
    code, body = call(path, method, header)
    time.sleep(wait)
    got = [(c, p) for (_, c, p) in frames_since(mark)]
    ok = got == expect_tx and code == expect_code
    detail = f"code {code} (want {expect_code}), frames {[(hex(c), p) for c, p in got]} (want {[(hex(c), p) for c, p in expect_tx]})"
    if expect_state is not None:
        ok = ok and body.get("state") == expect_state
        detail += f", state {body.get('state')} (want {expect_state})"
    if expect_track is not None:
        ok = ok and body.get("track") == expect_track
        detail += f", track {body.get('track')} (want {expect_track})"
    check(label, ok, detail)
    return body


threading.Thread(target=serial_reader, daemon=True).start()
time.sleep(1.0)

print(f"Player {BASE}, serial {PORT}")
s = status()
print("Start status:", s)
if s.get("link") != "ok" or s.get("count", 0) < 3:
    sys.exit("Need link 'ok' and at least 3 tracks on the card for this test.")
count, vmax, orig_vol, orig_rep = s["count"], s["volumeMax"], s["volume"], s["repeat"]

print("\nHealth")
code, h = call("/api/health", "GET")
check("health endpoint answers", code == 200 and "uptime" in h, str(h))
check("free heap is healthy (> 60 KB)", h.get("heapFree", 0) > 60000, str(h.get("heapFree")))
check("lowest free heap since boot is healthy (> 40 KB)", h.get("heapMin", 0) > 40000, str(h.get("heapMin")))
check("no crash resets recorded", h.get("crashes", 1) == 0, f"crashes {h.get('crashes')} (last reset: {h.get('reset')})")

STOP, PLAY, PAUSE, SET_VOL = 0x16, s.get("playCmd", 0x12), 0x0E, 0x06
RESUME = 0x0D

print("\nTransport")
call("/api/stop")
time.sleep(0.8)
call("/api/repeat?mode=off")
t0 = status()["track"]
step("play from stopped starts the current track", "/api/play", [(PLAY, t0)], expect_state="playing", expect_track=t0)
step("play while playing does nothing", "/api/play", [], expect_state="playing")
step("pause while playing pauses", "/api/pause", [(PAUSE, 0)], expect_state="paused")
step("pause while paused does nothing", "/api/pause", [], expect_state="paused")
step("play while paused resumes", "/api/play", [(RESUME, 0)], expect_state="playing")
nxt = t0 % count + 1
step("next stops then starts the next track", "/api/next", [(STOP, 0), (PLAY, nxt)], expect_state="playing", expect_track=nxt)
step("prev stops then starts the previous track", "/api/prev", [(STOP, 0), (PLAY, t0)], expect_state="playing", expect_track=t0)
step("stop stops", "/api/stop", [(STOP, 0)], expect_state="stopped")
step("stop while stopped does nothing", "/api/stop", [], expect_state="stopped")
nxt = t0 % count + 1
step("next from stopped starts the next track (no stop first)", "/api/next", [(PLAY, nxt)], expect_state="playing", expect_track=nxt)
step("pause then next starts the next track and plays", "/api/pause", [(PAUSE, 0)], expect_state="paused")
nxt2 = nxt % count + 1
step("next while paused starts the next track and plays", "/api/next", [(STOP, 0), (PLAY, nxt2)], expect_state="playing", expect_track=nxt2)

print("\nWrap-around and direct track")
step(f"track {count} (last) plays", f"/api/track?n={count}", [(STOP, 0), (PLAY, count)], expect_track=count)
step("next from the last track wraps to 1", "/api/next", [(STOP, 0), (PLAY, 1)], expect_track=1)
step(f"prev from track 1 wraps to {count}", "/api/prev", [(STOP, 0), (PLAY, count)], expect_track=count)
step("track 2 plays", "/api/track?n=2", [(STOP, 0), (PLAY, 2)], expect_track=2)

print("\nRate limit")
mark = time.time()
c1, _ = call("/api/next")
c2, _ = call("/api/next")
time.sleep(1.2)
got = [(c, p) for (_, c, p) in frames_since(mark)]
check("two instant 'next' presses: second refused (429), only one change sent",
      (c1, c2) == (200, 429) and got == [(STOP, 0), (PLAY, 3)], f"codes {(c1, c2)}, frames {got}")

print("\nBad input is refused and sends nothing")
for label, path in [("track 0", "/api/track?n=0"), ("track letters", "/api/track?n=abc"),
                    ("track past the end", f"/api/track?n={count + 1}"), ("track negative", "/api/track?n=-1"),
                    ("track missing", "/api/track"), ("volume letters", "/api/volume?v=abc"),
                    ("volume missing", "/api/volume"), ("repeat bad mode", "/api/repeat?mode=loud")]:
    step(label, path, [], expect_code=400, wait=0.5)

print("\nVolume")
step(f"volume 999 is clamped to {vmax}", "/api/volume?v=999", [(SET_VOL, vmax)])
check("status reports the clamped volume", status()["volume"] == vmax)
step("volume -5 is clamped to 0", "/api/volume?v=-5", [(SET_VOL, 0)])
step(f"volume restored to {orig_vol}", f"/api/volume?v={orig_vol}", [(SET_VOL, orig_vol)])

print("\nRepeat (no frames sent)")
modes = []
for _ in range(3):
    modes.append(call("/api/repeat")[1]["repeat"])
check("repeat cycles off -> all -> one -> off", modes == ["all", "one", "off"], str(modes))
check("repeat mode=one sets one", call("/api/repeat?mode=one")[1]["repeat"] == "one")
call(f"/api/repeat?mode={orig_rep}")

print("\nGuards")
step("action without the X-Pod header is refused (403)", "/api/next", [], expect_code=403, header=False, wait=0.5)
step("GET on an action route does nothing", "/api/next", [], expect_code=404, method="GET", wait=0.5)

print("\nFrame spacing")
with lock:
    times = [t for (t, _, _) in tx_log]
gaps = [b - a for a, b in zip(times, times[1:])]
close = [round(g, 3) for g in gaps if g < MIN_GAP]
check(f"no two frames closer than {int(MIN_GAP * 1000)} ms", not close, f"too close: {close}")

call("/api/stop")
print(f"\n{passed} passed, {failed} failed")
sys.exit(1 if failed else 0)
