#pragma once

#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Pod Player</title>
<style>
  :root { --bg:#f4f4f6; --card:#fff; --text:#1b1b1f; --muted:#6b6b76; --accent:#2a6df4; --btn:#e8e9ee; --warn:#c0392b; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#141417; --card:#1f1f24; --text:#f2f2f5; --muted:#9a9aa6; --accent:#6c9bff; --btn:#2c2c33; --warn:#ff7a6b; }
  }
  * { box-sizing: border-box; }
  body { margin:0; font-family: system-ui, sans-serif; background:var(--bg); color:var(--text);
         display:flex; justify-content:center; padding:16px; }
  main { width:100%; max-width:420px; background:var(--card); border-radius:16px; padding:20px; }
  h1 { font-size:1rem; margin:0 0 12px; color:var(--muted); font-weight:600; }
  #track { font-size:4rem; font-weight:700; text-align:center; line-height:1.1; }
  #of, #state { text-align:center; color:var(--muted); min-height:1.3em; }
  #state.warn { color:var(--warn); }
  .row { display:flex; gap:8px; margin:14px 0; }
  button { flex:1; padding:14px 0; font-size:1.15rem; border:0; border-radius:12px;
           background:var(--btn); color:var(--text); cursor:pointer; }
  button.on { background:var(--accent); color:#fff; }
  button:disabled { opacity:.4; cursor:not-allowed; }
  button:active:not(:disabled) { transform: scale(.97); }
  button.small { font-size:.9rem; padding:12px 0; }
  label { display:flex; justify-content:space-between; color:var(--muted); font-size:.9rem; }
  input[type=range] { width:100%; }
  input[type=number] { flex:1; min-width:0; padding:12px; font-size:1rem; border-radius:12px;
                       border:1px solid var(--btn); background:var(--bg); color:var(--text); }
  #net { text-align:center; color:var(--muted); font-size:.8rem; margin-top:8px; }
</style>
</head>
<body>
<main>
  <h1>Pod Player</h1>
  <div id="track">--</div>
  <div id="of">connecting...</div>
  <div id="state"></div>

  <div class="row">
    <button id="bPrev"  onclick="act('prev')"  title="Previous track">&#9198;</button>
    <button id="bPlay"  onclick="act('play')"  title="Play">&#9654;</button>
    <button id="bPause" onclick="act('pause')" title="Pause">&#9208;</button>
    <button id="bStop"  onclick="act('stop')"  title="Stop">&#9209;</button>
    <button id="bNext"  onclick="act('next')"  title="Next track">&#9197;</button>
  </div>

  <label><span>Volume</span><span id="vol">--</span></label>
  <input type="range" id="slider" min="0" max="30" value="0">

  <div class="row">
    <button id="rep" onclick="act('repeat')">Repeat: off</button>
  </div>

  <div class="row">
    <input type="number" id="goto" min="1" step="1" inputmode="numeric" placeholder="Track number">
    <button id="bGo" onclick="goTrack()" style="flex:0 0 90px">Play</button>
  </div>

  <div class="row">
    <button class="small" onclick="act('rescan')">Rescan SD card</button>
    <button class="small" onclick="wifiReset()">Change Wi-Fi</button>
  </div>

  <div id="net"></div>
</main>
<script>
const $ = id => document.getElementById(id);
let dragging = false, volMax = 15, trackMax = 0, locked = false, noteTimer = 0;

async function post(path) {
  const r = await fetch(path, { method: 'POST', headers: { 'X-Pod': '1' } });
  let s = null;
  try { s = await r.json(); } catch (e) {}
  return { ok: r.ok, s };
}
function note(text) {
  const el = $('state');
  el.textContent = text;
  el.classList.add('warn');
  clearTimeout(noteTimer);
  noteTimer = setTimeout(() => { el.classList.remove('warn'); refresh(); }, 3000);
}
async function run(path) {
  if (locked) return;                       // ignore double taps
  locked = true;
  setTimeout(() => { locked = false; }, 300);
  try {
    const res = await post(path);
    if (res.s) { render(res.s); if (!res.ok && res.s.msg) note(res.s.msg); }
    else if (!res.ok) note('request refused');
  } catch (e) { note('offline'); }
}
function act(name) { run('/api/' + name); }
function goTrack() {
  const n = parseInt($('goto').value, 10);
  if (!Number.isInteger(n) || n < 1 || (trackMax && n > trackMax)) { note('enter a track from 1 to ' + (trackMax || '?')); return; }
  run('/api/track?n=' + n);
}
function wifiReset() {
  if (!confirm('Forget this Wi-Fi network and restart into setup?')) return;
  post('/api/wifi-reset').catch(() => {});
  $('state').textContent = 'restarting into setup...';
}
function render(s) {
  const usable = s.link === 'ok' || s.link === 'checking';
  const linkMsg = { checking: 'checking SD card...', no_player: 'DFPlayer not responding',
                    no_sd: 'No SD card detected', empty: 'SD card has no tracks' };
  const errMsg = { 1: 'player busy or no card', 5: 'track number out of range',
                   6: 'track file not found', 8: 'SD card read failed' };
  trackMax = s.count;
  $('track').textContent = s.track || '--';
  $('of').textContent = s.link === 'ok' ? 'of ' + s.count : (linkMsg[s.link] || '');
  if (!$('state').classList.contains('warn')) {
    $('state').textContent = s.error ? s.state + ' - ' + (errMsg[s.error] || 'error ' + s.error) : s.state;
  }
  $('bPlay').classList.toggle('on', s.state === 'playing');
  $('bPause').classList.toggle('on', s.state === 'paused');
  $('bStop').classList.toggle('on', s.state === 'stopped');
  for (const id of ['bPrev', 'bPlay', 'bPause', 'bStop', 'bNext', 'bGo']) $(id).disabled = !usable;
  $('goto').max = s.count || '';
  $('rep').textContent = 'Repeat: ' + s.repeat;
  $('net').textContent = (s.wifi === 'sta' ? 'Wi-Fi ' : 'Hotspot ') + s.ip;
  volMax = s.volumeMax;
  $('slider').max = volMax;
  if (!dragging) { $('slider').value = s.volume; $('vol').textContent = s.volume + ' / ' + volMax; }
}
async function refresh() {
  try { render(await (await fetch('/api/status')).json()); }
  catch (e) { $('state').textContent = 'offline'; }
}
$('slider').addEventListener('input', e => { dragging = true; $('vol').textContent = e.target.value + ' / ' + volMax; });
$('slider').addEventListener('change', async e => {
  const v = e.target.value;
  try { render((await post('/api/volume?v=' + v)).s); } catch (err) { note('offline'); }
  dragging = false;
});
refresh();
setInterval(refresh, 1500);
</script>
</body>
</html>
)HTML";
