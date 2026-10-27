#pragma once

// Served at "/gamepad". Only include this from web.cpp.
static const char GAMEPAD_PAGE[] = R"html(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ROLO gamepad</title>
<style>
  :root { --bg:#f6f7f9; --card:#fff; --text:#1d2330; --muted:#6b7385; --line:#e3e6ec; --accent:#2f6fde; --ok:#1f9d55; --bad:#d64545; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#14171c; --card:#1d2129; --text:#e6e9ef; --muted:#9aa3b5; --line:#2c323d; --accent:#6b9bff; --ok:#3ccf7a; --bad:#ff6b6b; }
  }
  * { box-sizing: border-box; }
  body { margin:0; padding:16px; background:var(--bg); color:var(--text); font:14px/1.4 system-ui, sans-serif; }
  h1 { font-size:20px; margin:0 0 12px; display:flex; gap:12px; align-items:center; flex-wrap:wrap; }
  h2 { font-size:14px; text-transform:uppercase; letter-spacing:.05em; color:var(--muted); margin:0 0 10px; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:14px; margin-bottom:14px; }
  .badge { font-size:12px; padding:2px 8px; border-radius:99px; background:var(--line); color:var(--muted); text-decoration:none; }
  .badge.ok { background:var(--ok); color:#fff; } .badge.bad { background:var(--bad); color:#fff; }
  .buttons { display:flex; gap:8px; flex-wrap:wrap; }
  button { padding:8px 14px; border-radius:8px; border:1px solid var(--line); background:var(--bg); color:var(--text); cursor:pointer; }
  .row { display:flex; gap:16px; flex-wrap:wrap; align-items:center; }
  .stick { width:120px; height:120px; border-radius:50%; border:2px solid var(--line); position:relative; }
  .stick .dot { position:absolute; width:16px; height:16px; margin:-8px 0 0 -8px; border-radius:50%; background:var(--accent); left:50%; top:50%; }
  .stick .label { position:absolute; bottom:-20px; width:100%; text-align:center; color:var(--muted); font-size:12px; }
  .bar { width:24px; height:120px; border:2px solid var(--line); border-radius:6px; position:relative; overflow:hidden; }
  .bar .fill { position:absolute; bottom:0; width:100%; background:var(--accent); height:0; }
  .keys { display:grid; grid-template-columns:repeat(auto-fill, minmax(64px, 1fr)); gap:6px; flex:1; }
  .key { border:1px solid var(--line); border-radius:6px; padding:6px 4px; text-align:center; font:12px ui-monospace, monospace; color:var(--muted); }
  .key.on { background:var(--accent); color:#fff; border-color:var(--accent); }
  .grid { display:grid; grid-template-columns:repeat(auto-fill, minmax(110px, 1fr)); gap:8px; }
  .value { border:1px solid var(--line); border-radius:8px; padding:8px; }
  .value .label { color:var(--muted); font-size:12px; }
  .value .num { font:600 16px ui-monospace, monospace; }
  #none { color:var(--muted); }
</style>
</head>
<body>
<h1>ROLO gamepad <a href="/drive" class="badge">drive</a> <a href="/calibration" class="badge">calibration</a>
  <span id="conn" class="badge">connecting</span> <span id="state" class="badge">-</span> <span id="pad" class="badge">-</span></h1>

<div class="card">
  <h2>Live</h2>
  <div class="grid" id="robot"></div>
</div>

<div class="card">
  <h2>Pairing</h2>
  <div class="buttons">
    <button id="pair">-</button>
    <button id="forget">Forget paired controllers</button>
    <button id="rumble">Rumble test</button>
  </div>
  <p id="none">No controller connected. Hold the controller's sync/pair button until its lights run.</p>
</div>

<div class="card" id="live" hidden>
  <h2 id="model">Controller</h2>
  <div class="row" style="margin-bottom:24px">
    <div class="stick" id="ls"><div class="dot"></div><div class="label">left (drive)</div></div>
    <div class="stick" id="rs"><div class="dot"></div><div class="label">right</div></div>
    <div class="bar" id="brake"><div class="fill"></div></div>
    <div class="bar" id="throttle"><div class="fill"></div></div>
    <div class="keys" id="keys"></div>
  </div>
  <div class="grid" id="values"></div>
</div>

<script src="/rolo.js"></script>
<script>
const $ = (id) => document.getElementById(id);

// Bit positions from Bluepad32 (Xbox-style names: A = bottom face button)
const BUTTONS = ['A', 'B', 'X', 'Y', 'L1', 'R1', 'L2', 'R2', 'L3', 'R3'];
const MISC = ['Home', 'Select', 'Start', 'Capture'];
const DPAD = ['Up', 'Down', 'Right', 'Left'];

const keys = {};
for (const name of [...BUTTONS, ...MISC, ...DPAD]) {
  const k = document.createElement('div');
  k.className = 'key';
  k.textContent = name;
  $('keys').appendChild(k);
  keys[name] = k;
}

const VALUES = ['lx', 'ly', 'rx', 'ry', 'brake', 'throttle', 'battery', 'gyroX', 'gyroY', 'gyroZ', 'accelX', 'accelY', 'accelZ'];
const values = {};
for (const name of VALUES) {
  const box = document.createElement('div');
  box.className = 'value';
  box.innerHTML = `<div class="label">${name}</div><div class="num">-</div>`;
  $('values').appendChild(box);
  values[name] = box.querySelector('.num');
}

let pairing = true;

const badges = ROLO.badges($('conn'), $('state'));
const showLive = ROLO.live($('robot'));
const send = ROLO.connect({
  feeds: ['live', 'pad'],
  onStatus: badges.status,
  onSample: (s) => { showLive(s); badges.sample(s); },
  onJson: (msg) => { if (msg.type === 'pad') show(msg); },
});

function moveDot(id, x, y) {
  const dot = $(id).querySelector('.dot');
  dot.style.left = (50 + (x / 512) * 50) + '%';
  dot.style.top = (50 + (y / 512) * 50) + '%';
}

function fill(id, v) {
  $(id).querySelector('.fill').style.height = (v / 1023 * 100) + '%';
}

function show(p) {
  pairing = p.pairing;
  $('pair').textContent = pairing ? 'Pairing: on (click to stop)' : 'Pairing: off (click to allow)';
  $('pad').textContent = p.connected ? 'gamepad connected' : 'no gamepad';
  $('pad').className = 'badge ' + (p.connected ? 'ok' : 'bad');
  $('none').hidden = p.connected;
  $('live').hidden = !p.connected;
  if (!p.connected) return;

  $('model').textContent = p.model;
  moveDot('ls', p.lx, p.ly);
  moveDot('rs', p.rx, p.ry);
  fill('brake', p.brake);
  fill('throttle', p.throttle);

  BUTTONS.forEach((n, i) => keys[n].classList.toggle('on', (p.buttons >> i) & 1));
  MISC.forEach((n, i) => keys[n].classList.toggle('on', (p.misc >> i) & 1));
  DPAD.forEach((n, i) => keys[n].classList.toggle('on', (p.dpad >> i) & 1));

  const v = { lx: p.lx, ly: p.ly, rx: p.rx, ry: p.ry, brake: p.brake, throttle: p.throttle,
              battery: p.battery ? Math.round(p.battery / 2.55) + '%' : 'n/a',
              gyroX: p.gyro[0], gyroY: p.gyro[1], gyroZ: p.gyro[2],
              accelX: p.accel[0], accelY: p.accel[1], accelZ: p.accel[2] };
  for (const name of VALUES) values[name].textContent = v[name];
}

$('pair').onclick = () => send(pairing ? 'pad pair off' : 'pad pair on');
$('forget').onclick = () => { if (confirm('Forget all paired controllers? They will need pairing again.')) send('pad forget'); };
$('rumble').onclick = () => send('pad rumble');

</script>
</body>
</html>
)html";