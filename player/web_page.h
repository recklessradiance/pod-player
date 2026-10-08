#pragma once

#include <pgmspace.h>

// Pod Player web UI: a Switch Joy-Con inspired layout with a Game Boy style LCD.
// Everything is inline (no fonts, scripts or images from the internet), because the player is
// often reached over a phone hotspot or a router with no internet.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#e60012">
<title>Pod Player</title>
<style>
  :root {
    --bg:#e9eaee; --card:#fff; --ink:#2d2d2d; --muted:#6d6f78;
    --blue:#0ab9e6; --red:#ff3c28; --btn:#2d2d2d; --btn-ink:#fff;
    --lcd:#9bbc0f; --lcd-ink:#0f380f; --lcd-dim:#4d6b12;
  }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#17181c; --card:#25262b; --ink:#f2f2f5; --muted:#9a9ca6; --btn:#0f0f11; }
  }
  * { box-sizing:border-box; -webkit-tap-highlight-color:transparent; }
  html { background:var(--bg); }
  body { margin:0; padding:14px 14px calc(14px + env(safe-area-inset-bottom)); color:var(--ink);
         font-family:system-ui,-apple-system,"Segoe UI",sans-serif; touch-action:manipulation; }
  .console { max-width:440px; margin:0 auto; }
  button { font:inherit; color:inherit; border:0; cursor:pointer; }
  button:focus-visible, summary:focus-visible, input:focus-visible { outline:3px solid #ffd400; outline-offset:2px; }
  button:disabled { cursor:not-allowed; }

  /* header */
  .top { display:flex; align-items:center; justify-content:space-between; padding:2px 6px 12px; }
  .brand { font-weight:900; letter-spacing:.18em; font-size:1.05rem; }
  .brand b { color:var(--red); } .brand i { color:var(--blue); font-style:normal; }
  .leds { display:flex; gap:6px; }
  .leds span { width:10px; height:10px; border-radius:50%; background:var(--muted); opacity:.35; }
  .leds.paused span:first-child, .leds.playing span { background:#3ddc4a; opacity:1; box-shadow:0 0 8px #3ddc4a; }
  .leds.playing span { animation:chase 1.2s infinite; }
  .leds.playing span:nth-child(2) { animation-delay:.15s; } .leds.playing span:nth-child(3) { animation-delay:.3s; }
  .leds.playing span:nth-child(4) { animation-delay:.45s; }
  @keyframes chase { 0%,100% { opacity:1; } 50% { opacity:.25; } }

  /* LCD */
  .screen { background:var(--card); border-radius:26px; padding:14px; box-shadow:0 6px 18px rgba(0,0,0,.12); }
  .lcd { position:relative; overflow:hidden; background:var(--lcd); color:var(--lcd-ink); border-radius:12px;
         border:3px solid rgba(0,0,0,.28); padding:12px 16px 14px; min-height:210px;
         font-family:ui-monospace,"SF Mono",Menlo,Consolas,monospace; }
  .lcd::after { content:""; position:absolute; inset:0; pointer-events:none;
                background:repeating-linear-gradient(to bottom,rgba(0,0,0,.07) 0 1px,transparent 1px 3px); }
  .lcd.dim { filter:saturate(.4) brightness(.85); }
  .row1 { display:flex; justify-content:space-between; font-size:.8rem; font-weight:700; letter-spacing:.06em; }
  .big { display:flex; align-items:baseline; justify-content:center; gap:8px; margin:6px 0 0; }
  #track { font-size:clamp(60px,22vw,92px); font-weight:900; line-height:1; letter-spacing:-.03em; }
  #count { font-size:1.1rem; font-weight:700; color:var(--lcd-dim); }
  #time { text-align:center; font-size:1.25rem; font-weight:700; min-height:1.6em; }
  .eq { display:flex; justify-content:center; align-items:flex-end; gap:5px; height:26px; margin-top:4px; }
  .eq span { width:8px; height:5px; background:var(--lcd-ink); opacity:.55; border-radius:1px; }
  .playing .eq span { animation:eq .9s infinite ease-in-out; opacity:1; }
  .playing .eq span:nth-child(2) { animation-delay:.2s; } .playing .eq span:nth-child(3) { animation-delay:.4s; }
  .playing .eq span:nth-child(4) { animation-delay:.1s; } .playing .eq span:nth-child(5) { animation-delay:.3s; }
  @keyframes eq { 0%,100% { height:5px; } 50% { height:24px; } }
  #msg { text-align:center; font-size:.85rem; font-weight:700; min-height:1.3em; margin-top:6px; }
  #msg.warn { text-decoration:underline wavy; }

  /* controllers */
  .pads { display:grid; grid-template-columns:1fr 1fr; gap:12px; margin-top:14px; }
  .joy { border-radius:30px; padding:14px; box-shadow:0 6px 14px rgba(0,0,0,.18); }
  .joy.l { background:var(--blue); } .joy.r { background:var(--red); }
  .dia { display:grid; grid-template-columns:repeat(3,1fr); grid-template-rows:repeat(3,1fr); gap:6px; aspect-ratio:1; }
  .dia .t { grid-area:1/2; } .dia .lf { grid-area:2/1; } .dia .rt { grid-area:2/3; } .dia .b { grid-area:3/2; }
  .pad { position:relative; width:100%; aspect-ratio:1; border-radius:50%; background:var(--btn); color:var(--btn-ink);
         box-shadow:0 4px 0 rgba(0,0,0,.4); display:flex; flex-direction:column; align-items:center; justify-content:center;
         gap:1px; user-select:none; -webkit-user-select:none; transition:transform .05s, box-shadow .05s; }
  .pad:active:not(:disabled) { transform:translateY(3px); box-shadow:0 1px 0 rgba(0,0,0,.4); }
  .pad:disabled { opacity:.35; }
  .pad, .btn, #segs button { -webkit-touch-callout:none; }
  .pad.on { background:#fff; color:#111; }
  .pad svg { width:44%; height:44%; fill:currentColor; }
  .pad small { font-size:.52rem; font-weight:800; letter-spacing:.06em; opacity:.85; }
  .pad .k { position:absolute; top:3px; right:6px; font-size:.6rem; font-weight:900; opacity:.55; }

  /* volume */
  .vol { display:flex; align-items:center; gap:10px; background:var(--card); border-radius:20px; padding:12px 14px; margin-top:14px;
         box-shadow:0 6px 14px rgba(0,0,0,.1); }
  .vol svg { width:22px; height:22px; fill:var(--muted); flex:none; }
  #segs { display:flex; gap:3px; flex:1; }
  #segs button { flex:1; height:26px; border-radius:5px; background:var(--muted); opacity:.3; padding:0; }
  #segs button.lit { background:var(--blue); opacity:1; }
  #volTxt { font-weight:800; font-variant-numeric:tabular-nums; min-width:3.6em; text-align:right; }

  /* more */
  details { background:var(--card); border-radius:20px; padding:4px 14px; margin-top:14px; box-shadow:0 6px 14px rgba(0,0,0,.1); }
  summary { cursor:pointer; padding:12px 0; font-weight:700; color:var(--muted); list-style:none; }
  summary::-webkit-details-marker { display:none; }
  summary::after { content:" +"; } details[open] summary::after { content:" -"; }
  .field { display:flex; gap:8px; margin:0 0 12px; }
  input[type=number] { flex:1; min-width:0; padding:12px; font-size:1rem; border-radius:12px; border:2px solid var(--muted);
                       background:var(--bg); color:var(--ink); }
  .btn { flex:1; padding:12px 8px; border-radius:12px; background:var(--btn); color:var(--btn-ink); font-weight:700; font-size:.9rem;
         box-shadow:0 3px 0 rgba(0,0,0,.4); }
  .btn:active:not(:disabled) { transform:translateY(2px); box-shadow:0 1px 0 rgba(0,0,0,.4); }
  #info { font-size:.78rem; color:var(--muted); padding:0 0 12px; line-height:1.5; }

  #offline { display:none; background:#b3261e; color:#fff; border-radius:12px; padding:10px 14px; margin-bottom:12px; font-weight:700; text-align:center; }
  #toast { position:fixed; left:50%; bottom:calc(18px + env(safe-area-inset-bottom)); transform:translate(-50%,20px); opacity:0;
           background:var(--ink); color:var(--card); padding:12px 18px; border-radius:999px; font-weight:700; max-width:90vw;
           transition:opacity .2s, transform .2s; pointer-events:none; }
  #toast.show { opacity:1; transform:translate(-50%,0); }
  @media (prefers-reduced-motion: reduce) { * { animation:none !important; transition:none !important; } }
</style>
</head>
<body>
<div class="console">
  <div id="offline" role="alert">Lost connection to the player. Retrying...</div>

  <div class="top">
    <div class="brand"><b>P</b>O<i>D</i></div>
    <div class="leds" id="leds" aria-hidden="true"><span></span><span></span><span></span><span></span></div>
  </div>

  <section class="screen" aria-label="Now playing">
    <div class="lcd" id="lcd">
      <div class="row1"><span id="stateTxt">CONNECTING</span><span id="repTxt"></span></div>
      <div class="big"><span id="track">--</span><span id="count"></span></div>
      <div id="time"></div>
      <div class="eq" aria-hidden="true"><span></span><span></span><span></span><span></span><span></span></div>
      <div id="msg" aria-live="polite"></div>
    </div>
  </section>

  <div class="pads">
    <div class="joy l"><div class="dia">
      <button class="pad t"  id="bUp"   aria-label="Volume up"><svg viewBox="0 0 24 24"><path d="M12 5l8 12H4z"/></svg><small>VOL+</small></button>
      <button class="pad lf" id="bPrev" aria-label="Previous track"><svg viewBox="0 0 24 24"><path d="M6 5h2v14H6zM20 5v14L9 12z"/></svg><small>PREV</small></button>
      <button class="pad rt" id="bNext" aria-label="Next track"><svg viewBox="0 0 24 24"><path d="M16 5h2v14h-2zM4 5v14l11-7z"/></svg><small>NEXT</small></button>
      <button class="pad b"  id="bDown" aria-label="Volume down"><svg viewBox="0 0 24 24"><path d="M12 19L4 7h16z"/></svg><small>VOL-</small></button>
    </div></div>
    <div class="joy r"><div class="dia">
      <button class="pad t"  id="bRep"   aria-label="Repeat"><span class="k">X</span><svg viewBox="0 0 24 24"><path d="M7 7h10v3l4-4-4-4v3H5v6h2zM17 17H7v-3l-4 4 4 4v-3h12v-6h-2z"/></svg><small>REPEAT</small></button>
      <button class="pad lf" id="bStop"  aria-label="Stop"><span class="k">Y</span><svg viewBox="0 0 24 24"><path d="M6 6h12v12H6z"/></svg><small>STOP</small></button>
      <button class="pad rt" id="bPlay"  aria-label="Play"><span class="k">A</span><svg viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg><small>PLAY</small></button>
      <button class="pad b"  id="bPause" aria-label="Pause"><span class="k">B</span><svg viewBox="0 0 24 24"><path d="M6 5h4v14H6zM14 5h4v14h-4z"/></svg><small>PAUSE</small></button>
    </div></div>
  </div>

  <div class="vol" aria-label="Volume">
    <svg viewBox="0 0 24 24"><path d="M3 9v6h4l5 5V4L7 9H3zm13.5 3a4.5 4.5 0 0 0-2.5-4v8a4.5 4.5 0 0 0 2.5-4z"/></svg>
    <div id="segs"></div>
    <span id="volTxt">--</span>
  </div>

  <details id="more">
    <summary>More</summary>
    <div class="field">
      <input type="number" id="goto" min="1" step="1" inputmode="numeric" placeholder="Jump to track number" aria-label="Track number">
      <button class="btn" id="bGo" style="flex:0 0 80px">Play</button>
    </div>
    <div class="field">
      <button class="btn" id="bScan">Rescan card</button>
      <button class="btn" id="bModule">Reset module</button>
      <button class="btn" id="bWifi">Change Wi&#8209;Fi</button>
    </div>
    <div id="info">Loading...</div>
  </details>
</div>
<div id="toast" role="status"></div>

<script>
const $ = id => document.getElementById(id);
let S = null, fails = 0, volMax = 15, trackMax = 0, locked = false, toastTimer = 0, volTimer = 0, pendingVol = null;
let elapsedBase = 0, elapsedAt = 0, stateNow = 'stopped';
const BTN = ['bUp','bDown','bPrev','bNext','bPlay','bPause','bStop','bRep','bGo'];

function buzz() { if (navigator.vibrate) navigator.vibrate(8); }
function toast(text) {
  const t = $('toast');
  t.textContent = text;
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 2600);
}
async function post(path) {
  const r = await fetch(path, { method: 'POST', headers: { 'X-Pod': '1' } });
  let s = null;
  try { s = await r.json(); } catch (e) {}
  return { ok: r.ok, s };
}
async function run(path) {
  buzz();
  if (locked) return;                         // ignore double taps
  locked = true;
  setTimeout(() => { locked = false; }, 280);
  try {
    const res = await post(path);
    if (res.s) { render(res.s); if (!res.ok && res.s.msg) toast(res.s.msg); }
    else if (!res.ok) toast('Request refused');
    fails = 0;
  } catch (e) { fails++; toast('No answer from the player'); }
}

// ---- volume: update the display at once, send the final value after a short pause
function setVol(v) {
  v = Math.max(0, Math.min(volMax, v));
  if (S) S.volume = v;
  drawVolume(v);
  pendingVol = v;
  clearTimeout(volTimer);
  volTimer = setTimeout(async () => {
    const send = pendingVol;
    pendingVol = null;
    try { const res = await post('/api/volume?v=' + send); if (res.s) render(res.s); } catch (e) { toast('No answer from the player'); }
  }, 120);
}
function stepVol(d) { buzz(); setVol((S ? S.volume : 0) + d); }
function holdRepeat(el, d) {
  let t1 = 0, t2 = 0;
  const stop = () => { clearTimeout(t1); clearInterval(t2); };
  el.addEventListener('pointerdown', e => {
    e.preventDefault();
    stepVol(d);
    t1 = setTimeout(() => { t2 = setInterval(() => stepVol(d), 160); }, 420);
  });
  ['pointerup', 'pointerleave', 'pointercancel'].forEach(n => el.addEventListener(n, stop));
}
function drawVolume(v) {
  const segs = $('segs');
  if (segs.children.length !== volMax) {
    segs.innerHTML = '';
    for (let i = 1; i <= volMax; i++) {
      const b = document.createElement('button');
      b.setAttribute('aria-label', 'Set volume ' + i);
      b.onclick = () => { buzz(); setVol(i === (S && S.volume) ? i - 1 : i); };
      segs.appendChild(b);
    }
  }
  [...segs.children].forEach((b, i) => b.classList.toggle('lit', i < v));
  $('volTxt').textContent = v + ' / ' + volMax;
}

// ---- elapsed time (estimated by the player; the module cannot report its position)
function fmt(sec) {
  sec = Math.max(0, Math.floor(sec));
  return Math.floor(sec / 60) + ':' + String(sec % 60).padStart(2, '0');
}
function tickTime() {
  if (stateNow === 'stopped') { $('time').textContent = ''; return; }
  const extra = stateNow === 'playing' ? (Date.now() - elapsedAt) / 1000 : 0;
  $('time').textContent = fmt(elapsedBase + extra);
}
setInterval(tickTime, 500);

// ---- drawing the status
function render(s) {
  S = s;
  const usable = s.link === 'ok' || s.link === 'checking';
  const linkMsg = { checking: 'Checking SD card...', no_player: 'DFPlayer not responding',
                    no_sd: 'No SD card detected', empty: 'SD card has no tracks' };
  const errMsg = { 1: 'Player busy or no card', 5: 'Track number out of range',
                   6: 'Track file not found', 8: 'SD card read failed' };
  const label = { playing: 'PLAYING', paused: 'PAUSED', stopped: 'STOPPED' }[s.state] || s.state;

  trackMax = s.count;
  volMax = s.volumeMax;
  elapsedBase = s.elapsed || 0;
  elapsedAt = Date.now();
  stateNow = s.state;
  tickTime();

  $('stateTxt').textContent = (s.state === 'playing' ? '\u25B6 ' : s.state === 'paused' ? '|| ' : '\u25A0 ') + label;
  $('repTxt').textContent = s.repeat === 'off' ? '' : 'REPEAT ' + s.repeat.toUpperCase();
  $('track').textContent = s.track || '--';
  $('count').textContent = s.link === 'ok' ? '/ ' + s.count : '';
  const note = s.link !== 'ok' ? (linkMsg[s.link] || '') : (s.error ? (errMsg[s.error] || 'Error ' + s.error) : '');
  $('msg').textContent = note;
  $('msg').classList.toggle('warn', !!note && s.link !== 'checking');

  const lcd = $('lcd');
  lcd.classList.toggle('playing', s.state === 'playing');
  lcd.classList.toggle('dim', !usable);
  $('leds').className = 'leds ' + (s.state === 'playing' ? 'playing' : s.state === 'paused' ? 'paused' : '');
  document.title = (s.state === 'playing' ? '\u25B6 ' : s.state === 'paused' ? '|| ' : '') + 'Track ' + s.track + ' - Pod Player';

  $('bPlay').classList.toggle('on', s.state === 'playing');
  $('bPause').classList.toggle('on', s.state === 'paused');
  $('bStop').classList.toggle('on', s.state === 'stopped');
  $('bRep').classList.toggle('on', s.repeat !== 'off');
  $('bRep').querySelector('small').textContent = s.repeat === 'off' ? 'REPEAT' : s.repeat === 'all' ? 'ALL' : 'ONE';
  BTN.forEach(id => { $(id).disabled = !usable; });
  $('goto').max = s.count || '';
  if (pendingVol === null) drawVolume(s.volume);
}
async function refresh() {
  if (document.hidden) return;
  try {
    render(await (await fetch('/api/status')).json());
    fails = 0;
  } catch (e) { fails++; }
  $('offline').style.display = fails >= 2 ? 'block' : 'none';
}

// ---- buttons
$('bPlay').onclick = () => run('/api/play');
$('bPause').onclick = () => run('/api/pause');
$('bStop').onclick = () => run('/api/stop');
$('bNext').onclick = () => run('/api/next');
$('bPrev').onclick = () => run('/api/prev');
$('bRep').onclick = () => run('/api/repeat');
holdRepeat($('bUp'), +1);
holdRepeat($('bDown'), -1);
$('bGo').onclick = () => {
  const n = parseInt($('goto').value, 10);
  if (!Number.isInteger(n) || n < 1 || (trackMax && n > trackMax)) { toast('Enter a track from 1 to ' + (trackMax || '?')); return; }
  run('/api/track?n=' + n);
};
$('goto').addEventListener('keydown', e => { if (e.key === 'Enter') $('bGo').click(); });
$('bScan').onclick = () => { run('/api/rescan'); toast('Reading the card...'); };
$('bModule').onclick = () => {
  if (confirm('Reset the DFPlayer module? Playback stops and it takes a few seconds to come back.')) {
    run('/api/reset-module'); toast('Resetting the module...');
  }
};
$('bWifi').onclick = () => {
  if (!confirm('Forget this Wi-Fi network and restart into setup?')) return;
  post('/api/wifi-reset').catch(() => {});
  toast('Restarting into Wi-Fi setup...');
};

// keyboard: space play/pause, arrows, S stop, R repeat
document.addEventListener('keydown', e => {
  if (/INPUT|TEXTAREA|SUMMARY/.test(document.activeElement.tagName) || e.metaKey || e.ctrlKey) return;
  const k = e.key;
  if (k === ' ') { e.preventDefault(); run(S && S.state === 'playing' ? '/api/pause' : '/api/play'); }
  else if (k === 'ArrowRight') run('/api/next');
  else if (k === 'ArrowLeft') run('/api/prev');
  else if (k === 'ArrowUp') { e.preventDefault(); stepVol(+1); }
  else if (k === 'ArrowDown') { e.preventDefault(); stepVol(-1); }
  else if (k === 's' || k === 'S') run('/api/stop');
  else if (k === 'r' || k === 'R') run('/api/repeat');
});

// device details, loaded when "More" is opened
$('more').addEventListener('toggle', async () => {
  if (!$('more').open) return;
  try {
    const h = await (await fetch('/api/health')).json();
    const up = h.uptime >= 3600 ? Math.floor(h.uptime / 3600) + ' h ' + Math.floor(h.uptime % 3600 / 60) + ' min'
                                : Math.floor(h.uptime / 60) + ' min';
    $('info').innerHTML = (S ? (S.wifi === 'sta' ? 'Wi-Fi ' : 'Hotspot ') + S.ip + '<br>' : '') +
      (h.wifi === 'sta' ? 'Signal ' + h.rssi + ' dBm<br>' : '') +
      'Up ' + up + ' &middot; free memory ' + Math.round(h.heapFree / 1024) + ' KB<br>' +
      'Last restart: ' + h.reset + ' &middot; crashes ' + h.crashes;
  } catch (e) { $('info').textContent = 'Could not read device details.'; }
});

document.addEventListener('visibilitychange', refresh);
refresh();
setInterval(refresh, 1500);
</script>
</body>
</html>
)HTML";
