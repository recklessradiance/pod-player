// Scripted UI test: drives the real page in headless Chrome against the simulated player.
import { spawn } from 'node:child_process';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const MOCK = 'http://127.0.0.1:8099';
const sleep = ms => new Promise(r => setTimeout(r, ms));
const mock = async (q = '') => (await fetch(MOCK + (q ? '/__set?' + q : '/api/status'))).json();

const chrome = spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', [
  '--headless=new', '--disable-gpu', '--remote-debugging-port=9333', '--no-first-run',
  '--window-size=520,900', '--user-data-dir=' + mkdtempSync(join(tmpdir(), 'uitest-')), 'about:blank',
], { stdio: 'ignore' });

let targets;
for (let i = 0; i < 40; i++) {
  try { targets = await (await fetch('http://127.0.0.1:9333/json')).json(); if (targets.length) break; } catch (e) {}
  await sleep(250);
}
const ws = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
await new Promise(r => (ws.onopen = r));
let id = 0; const pending = new Map(); const pageErrors = [];
ws.onmessage = ev => {
  const m = JSON.parse(ev.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  else if (m.method === 'Runtime.exceptionThrown') pageErrors.push(m.params.exceptionDetails.text + ' ' + (m.params.exceptionDetails.exception?.description || ''));
  else if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') pageErrors.push('console.error ' + JSON.stringify(m.params.args.map(a => a.value)));
};
const send = (method, params = {}) => new Promise(r => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const ev = async expr => (await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true })).result.result.value;
await send('Runtime.enable');

let passed = 0, failed = 0;
const check = (name, ok, detail = '') => { if (ok) { passed++; console.log('  PASS ', name); } else { failed++; console.log('  FAIL ', name, detail); } };
const click = sel => ev(`document.querySelector('${sel}').click()`);
const text = sel => ev(`document.querySelector('${sel}').textContent`);
const cls = (sel, c) => ev(`document.querySelector('${sel}').classList.contains('${c}')`);

await mock('state=stopped&track=3&repeat=off&volume=8&link=ok');
await send('Page.navigate', { url: MOCK + '/' });
await sleep(1800);

console.log('Load');
check('track number shown', (await text('#track')) === '3', await text('#track'));
check('count shown', (await text('#count')) === '/ 8', await text('#count'));
check('volume text', (await text('#volTxt')) === '8 / 15', await text('#volTxt'));
check('15 volume segments, 8 lit', (await ev(`document.querySelectorAll('#segs button').length`)) === 15 &&
      (await ev(`document.querySelectorAll('#segs button.lit').length`)) === 8);
check('stopped button highlighted', await cls('#bStop', 'on'));

console.log('Transport');
await click('#bPlay'); await sleep(500);
let s = await mock();
check('Play -> player playing', s.state === 'playing', s.state);
check('Play button lit, LCD animating, LEDs chasing', (await cls('#bPlay', 'on')) && (await cls('#lcd', 'playing')) && (await ev(`document.getElementById('leds').className`)) === 'leds playing');
check('title shows playing track', (await ev('document.title')).includes('Track 3'), await ev('document.title'));
await sleep(1500);
check('elapsed time ticks', /^0:0[1-9]|^0:[1-5]\d$/.test(await text('#time')), await text('#time'));
await click('#bPause'); await sleep(500);
s = await mock();
check('Pause -> player paused, pause lit, one LED', s.state === 'paused' && (await cls('#bPause', 'on')) && (await ev(`document.getElementById('leds').className`)) === 'leds paused', s.state);
await sleep(300); await click('#bPlay'); await sleep(500);
check('Play resumes', (await mock()).state === 'playing');
await sleep(300); await click('#bNext'); await sleep(500);
s = await mock();
check('Next -> track 4, playing', s.track === 4 && s.state === 'playing', JSON.stringify(s));
check('display follows (track 4)', (await text('#track')) === '4', await text('#track'));
await sleep(400); await click('#bPrev'); await sleep(500);
check('Prev -> track 3', (await mock()).track === 3);
await sleep(400);
await ev(`document.getElementById('bNext').click(); document.getElementById('bNext').click();`); await sleep(700);
check('two instant Next taps change only one track (double-tap lock)', (await mock()).track === 4, JSON.stringify(await mock()));
await sleep(400); await click('#bStop'); await sleep(500);
s = await mock();
check('Stop -> stopped, stop lit, time cleared', s.state === 'stopped' && (await cls('#bStop', 'on')) && (await text('#time')) === '', s.state);

console.log('Volume');
await click('#segs button:nth-child(12)'); await sleep(500);
check('tap segment 12 -> volume 12', (await mock()).volume === 12, JSON.stringify(await mock()));
await ev(`(() => { const b = document.getElementById('bUp'); b.dispatchEvent(new PointerEvent('pointerdown', {bubbles:true})); b.dispatchEvent(new PointerEvent('pointerup', {bubbles:true})); })()`); await sleep(500);
check('VOL+ -> 13', (await mock()).volume === 13, JSON.stringify(await mock()));
await ev(`(() => { const b = document.getElementById('bDown'); for (let i=0;i<3;i++){ b.dispatchEvent(new PointerEvent('pointerdown', {bubbles:true})); b.dispatchEvent(new PointerEvent('pointerup', {bubbles:true})); } })()`); await sleep(600);
check('three quick VOL- taps coalesce into one request and land on 10', (await mock()).volume === 10, JSON.stringify(await mock()));
await ev(`(() => { const b = document.getElementById('bUp'); for (let i=0;i<30;i++){ b.dispatchEvent(new PointerEvent('pointerdown', {bubbles:true})); b.dispatchEvent(new PointerEvent('pointerup', {bubbles:true})); } })()`); await sleep(600);
check('volume cannot exceed the cap (15)', (await mock()).volume === 15, JSON.stringify(await mock()));

console.log('Repeat and jump');
await click('#bRep'); await sleep(500);
check('Repeat -> all, button lit, label ALL', (await mock()).repeat === 'all' && (await cls('#bRep', 'on')) && (await text('#bRep small')) === 'ALL', await text('#bRep small'));
await sleep(300); await click('#bRep'); await sleep(500); await sleep(300); await click('#bRep'); await sleep(500);
check('Repeat cycles back to off', (await mock()).repeat === 'off');
await ev(`document.getElementById('goto').value = '6'; document.getElementById('bGo').click()`); await sleep(500);
check('Jump to 6', (await mock()).track === 6, JSON.stringify(await mock()));
await sleep(400);
await ev(`document.getElementById('goto').value = '99'; document.getElementById('bGo').click()`); await sleep(400);
check('Jump to 99 refused with a message, track unchanged', (await mock()).track === 6 && (await text('#toast')).includes('1 to 8'), await text('#toast'));
await ev(`document.getElementById('goto').value = '0'; document.getElementById('bGo').click()`); await sleep(300);
check('Jump to 0 refused', (await mock()).track === 6);

console.log('Keyboard');
await mock('state=stopped');
await sleep(2000);
const key = k => ev(`document.dispatchEvent(new KeyboardEvent('keydown', {key: ${JSON.stringify(k)}, bubbles:true}))`);
await key(' '); await sleep(500);
check('Space plays', (await mock()).state === 'playing');
await sleep(300); await key(' '); await sleep(500);
check('Space again pauses', (await mock()).state === 'paused');
await sleep(300); await key('ArrowRight'); await sleep(500);
check('Right arrow = next', (await mock()).track === 7, JSON.stringify(await mock()));
await sleep(400); await key('s'); await sleep(500);
check('S = stop', (await mock()).state === 'stopped');

console.log('More panel');
await ev(`document.getElementById('more').open = true`); await sleep(700);
check('device details loaded', (await text('#info')).includes('Signal -58 dBm'), await text('#info'));

console.log('Error and disabled states');
await mock('link=no_sd'); await sleep(2200);
check('no SD: message shown on the LCD', (await text('#msg')).includes('No SD card'), await text('#msg'));
check('no SD: transport buttons disabled', await ev(`document.getElementById('bPlay').disabled && document.getElementById('bNext').disabled`));
check('no SD: LCD dimmed', await cls('#lcd', 'dim'));
await mock('link=ok&error=6'); await sleep(2200);
check('error 6 shown as "Track file not found"', (await text('#msg')).includes('Track file not found'), await text('#msg'));
await mock('error=0');

console.log('Connection loss');
await ev(`fetch('${MOCK}/api/status').then(()=>0)`);
chrome.stdout?.destroy;
await fetch(MOCK + '/__set?state=stopped');
// stop the simulated player by asking the page to talk to a dead port
await ev(`(() => { const o = window.fetch; window.fetch = (u, i) => (String(u).startsWith('/api/status') ? Promise.reject(new Error('down')) : o(u, i)); })()`);
await sleep(4500);
check('offline banner appears after repeated failures', (await ev(`getComputedStyle(document.getElementById('offline')).display`)) === 'block');

console.log('Page errors');
check('no JavaScript errors during the whole run', pageErrors.length === 0, JSON.stringify(pageErrors));

console.log(`\n${passed} passed, ${failed} failed`);
ws.close(); chrome.kill();
process.exit(failed ? 1 : 0);
