#pragma once

// Served at "/calibration". Only include this from web.cpp.
static const char CALIBRATION_PAGE[]= R"html(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ROLO Calibration</title>
<style>
  :root { --bg:#f6f7f9; --card:#fff; --text:#1d2330; --muted:#6b7385; --line:#e3e6ec; --accent:#2f6fde; --ok:#1f9d55; --bad:#d64545; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#14171c; --card:#1d2129; --text:#e6e9ef; --muted:#9aa3b5; --line:#2c323d; --accent:#6b9bff; --ok:#3ccf7a; --bad:#ff6b6b; }
  }
  * { box-sizing: border-box; }
  body { margin:0; padding:16px; background:var(--bg); color:var(--text); font:14px/1.4 system-ui, sans-serif; }
  h1 { font-size:20px; margin:0 0 12px; display:flex; gap:12px; align-items:center; }
  h2 { font-size:14px; text-transform:uppercase; letter-spacing:.05em; color:var(--muted); margin:0 0 10px; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:14px; margin-bottom:14px; }
  .badge { font-size:12px; padding:2px 8px; border-radius:99px; background:var(--line); color:var(--muted); }
  .badge.ok { background:var(--ok); color:#fff; } .badge.bad { background:var(--bad); color:#fff; }
  .grid { display:grid; grid-template-columns:repeat(auto-fill, minmax(130px, 1fr)); gap:8px; }
  .value { border:1px solid var(--line); border-radius:8px; padding:8px; }
  .value .label { color:var(--muted); font-size:12px; }
  .value .num { font:600 18px ui-monospace, monospace; }
  .param { display:grid; grid-template-columns:130px 1fr 100px; gap:10px; align-items:center; padding:6px 0; border-bottom:1px solid var(--line); }
  .param:last-child { border-bottom:none; }
  .param .name { font-family:ui-monospace, monospace; }
  .param .range { color:var(--muted); font-size:11px; }
  .param input[type=number] { width:100%; padding:4px; background:var(--bg); color:var(--text); border:1px solid var(--line); border-radius:6px; }
  input[type=range] { width:100%; }
  .buttons { display:flex; gap:8px; flex-wrap:wrap; margin-top:12px; }
  button { padding:8px 14px; border-radius:8px; border:1px solid var(--line); background:var(--bg); color:var(--text); cursor:pointer; }
  button.primary { background:var(--accent); color:#fff; border-color:var(--accent); }
  #term { font:12px/1.4 ui-monospace, monospace; white-space:pre-wrap; height:180px; overflow-y:auto;
          margin:0 0 8px; padding:8px; background:var(--bg); border:1px solid var(--line); border-radius:6px; }
  #term .command { color:var(--accent); }
  .cmd { display:flex; gap:8px; }
  .cmd input { flex:1; padding:8px; font:14px ui-monospace, monospace; background:var(--bg); color:var(--text);
               border:1px solid var(--line); border-radius:8px; }
  @media (max-width:520px) { .param { grid-template-columns:1fr 90px; } .param input[type=range] { grid-column:1 / -1; grid-row:2; } }
</style>
</head>
<body>
<h1>ROLO Calibration <a href="/drive" class="badge">drive</a> <a href="/gamepad" class="badge">gamepad</a> <span id="conn" class="badge">connecting</span> <span id="state" class="badge">-</span></h1>

<div class="card">
  <h2>Live</h2>
  <div class="grid" id="live"></div>
</div>

<div class="card">
  <h2>Parameters</h2>
  <div id="params"></div>
  <div class="buttons">
    <button class="primary" id="save">Save</button>
    <button id="load">Load saved</button>
    <button id="reset">Defaults</button>
  </div>
</div>

<div class="card">
  <h2>Terminal</h2>
  <pre id="term"></pre>
  <form class="cmd" id="cmdForm" autocomplete="off">
    <input id="cmd" placeholder="command, e.g. kick 62  (↑ ↓ history)" autocapitalize="off" spellcheck="false">
    <button class="primary">Send</button>
  </form>
</div>

<script src="/rolo.js"></script>
<script>
const $ = (id) => document.getElementById(id);
const controls = {};  // name -> {range, number, editing}

const badges = ROLO.badges($('conn'), $('state'));
const showLive = ROLO.live($('live'));
const send = ROLO.connect({
  feeds: ['live', 'log'],
  onStatus: (connected) => {
    badges.status(connected);
    if (connected) { $('term').textContent = ''; pending = ''; }   // the robot sends its recent output again
  },
  onSample: (s) => { showLive(s); badges.sample(s); },
  onJson: (msg) => {
    if (msg.type === 'params') showParams(msg.items);
    else if (msg.type === 'log') showLog(msg.text);
  },
});

// Terminal: everything the robot prints (the same as its serial port), the commands it
// runs ("> ..."), oldest at the top. Sends what you type, like typing on the serial port.
const TERM_LINES = 200;
let pending = '';   // text after the last newline: shown once its line is complete

function showLog(text) {
  const lines = (pending + text).split('\n');
  pending = lines.pop();
  for (const line of lines) termLine(line, line.startsWith('> ') ? 'command' : '');
}
const history = [];
let historyAt = 0;

function termLine(text, cls) {
  const term = $('term');
  const atBottom = term.scrollTop + term.clientHeight >= term.scrollHeight - 4;
  const line = document.createElement('div');
  line.textContent = text;
  if (cls) line.className = cls;
  term.appendChild(line);
  while (term.childElementCount > TERM_LINES) term.firstChild.remove();
  if (atBottom) term.scrollTop = term.scrollHeight;   // don't jump while you're scrolled up reading
}

$('cmdForm').addEventListener('submit', (e) => {
  e.preventDefault();
  const text = $('cmd').value.trim();
  if (!text) return;
  send(text);
  if (history[history.length - 1] !== text) history.push(text);
  historyAt = history.length;
  $('cmd').value = '';
});

$('cmd').addEventListener('keydown', (e) => {
  if (e.key !== 'ArrowUp' && e.key !== 'ArrowDown') return;
  e.preventDefault();
  historyAt = Math.max(0, Math.min(history.length, historyAt + (e.key === 'ArrowUp' ? -1 : 1)));
  $('cmd').value = history[historyAt] || '';
});

function stepFor(min, max) {
  const raw = (max - min) / 1000;
  return Math.pow(10, Math.floor(Math.log10(raw)));
}

function showParams(items) {
  for (const p of items) {
    let c = controls[p.name];
    if (!c) {
      const row = document.createElement('div');
      row.className = 'param';
      const step = stepFor(p.min, p.max);
      row.innerHTML =
        `<div><div class="name">${p.name}</div><div class="range">${p.min} … ${p.max}</div></div>` +
        `<input type="range" min="${p.min}" max="${p.max}" step="${step}">` +
        `<input type="number" min="${p.min}" max="${p.max}" step="${step}">`;
      $('params').appendChild(row);
      c = { range: row.querySelector('[type=range]'), number: row.querySelector('[type=number]'), editing: false };
      controls[p.name] = c;

      // Slider: send while dragging, at most every 50 ms
      let last = 0;
      c.range.addEventListener('input', () => {
        c.number.value = c.range.value;
        const now = Date.now();
        if (now - last > 50) { last = now; send(`set ${p.name} ${c.range.value}`); }
      });
      c.range.addEventListener('change', () => send(`set ${p.name} ${c.range.value}`));
      c.range.addEventListener('pointerdown', () => { c.editing = true; });
      c.range.addEventListener('pointerup', () => { c.editing = false; });

      // Number box: send when the value is committed (Enter or leaving the field)
      c.number.addEventListener('focus', () => { c.editing = true; });
      c.number.addEventListener('blur', () => { c.editing = false; });
      c.number.addEventListener('change', () => {
        c.range.value = c.number.value;
        send(`set ${p.name} ${c.number.value}`);
      });
    }
    if (!c.editing) {
      c.range.value = p.value;
      c.number.value = p.value;
    }
  }
}

$('save').onclick = () => send('save');
$('load').onclick = () => send('load');
$('reset').onclick = () => { if (confirm('Restore defaults from config.h? (not saved until you press Save)')) send('reset'); };

</script>
</body>
</html>
)html";