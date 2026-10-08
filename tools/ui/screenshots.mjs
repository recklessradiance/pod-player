// Takes the README screenshots: renders the real page (player/web_page.h) at phone size against the
// simulated player, in light and dark, in several states. macOS Chrome path.
//   node tools/ui/screenshots.mjs        (writes docs/images/*.png)
import { spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const outDir = resolve(here, '../../docs/images');
const MOCK = 'http://127.0.0.1:8099';
const sleep = ms => new Promise(r => setTimeout(r, ms));
const mock = q => fetch(MOCK + '/__set?' + q).then(r => r.json());

const sim = spawn('python3', ['-I', join(here, 'mock_player.py')], { stdio: 'ignore' });
const chrome = spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', [
  '--headless=new', '--disable-gpu', '--remote-debugging-port=9334', '--no-first-run', '--hide-scrollbars',
  '--user-data-dir=' + mkdtempSync(join(tmpdir(), 'shots-')), 'about:blank'], { stdio: 'ignore' });

let targets;
for (let i = 0; i < 40; i++) {
  try { targets = await (await fetch('http://127.0.0.1:9334/json')).json(); if (targets.length) break; } catch (e) {}
  await sleep(250);
}
const ws = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
await new Promise(r => (ws.onopen = r));
let id = 0; const pending = new Map();
ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } };
const send = (method, params = {}) => new Promise(r => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const ev = async expr => (await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true })).result.result.value;

async function shot(name, { state, dark = false, height = 760, openMore = false }) {
  await mock(state);
  await send('Emulation.setDeviceMetricsOverride', { width: 390, height, deviceScaleFactor: 2, mobile: true });
  await send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-color-scheme', value: dark ? 'dark' : 'light' }] });
  await send('Page.navigate', { url: MOCK + '/' });
  await sleep(1800);
  if (openMore) { await ev(`document.getElementById('more').open = true`); await sleep(900); }
  const r = await send('Page.captureScreenshot', { format: 'png' });
  writeFileSync(join(outDir, name + '.png'), Buffer.from(r.result.data, 'base64'));
  console.log('wrote', name + '.png');
}

await shot('ui-playing', { state: 'state=playing&track=3&repeat=all&volume=8&link=ok&error=0' });
await shot('ui-paused-dark', { state: 'state=paused&track=7&repeat=one&volume=11&link=ok&error=0', dark: true });
await shot('ui-no-sd', { state: 'state=stopped&track=3&repeat=off&volume=8&link=no_sd&error=0' });
await shot('ui-more', { state: 'state=playing&track=3&repeat=off&volume=8&link=ok&error=0', height: 900, openMore: true });

ws.close(); chrome.kill(); sim.kill();
