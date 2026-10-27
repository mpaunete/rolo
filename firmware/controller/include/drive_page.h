#pragma once

// Served at "/drive". Only include this from web.cpp.
static const char DRIVE_PAGE[] = R"html(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>ROLO drive</title>
<style>
  :root { --bg:#f6f7f9; --card:#fff; --text:#1d2330; --muted:#6b7385; --line:#e3e6ec; --accent:#2f6fde; --ok:#1f9d55; --bad:#d64545; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#14171c; --card:#1d2129; --text:#e6e9ef; --muted:#9aa3b5; --line:#2c323d; --accent:#6b9bff; --ok:#3ccf7a; --bad:#ff6b6b; }
  }
  * { box-sizing: border-box; }
  html, body { height:100%; }
  body { margin:0; padding:16px; background:var(--bg); color:var(--text); font:14px/1.4 system-ui, sans-serif;
         display:flex; flex-direction:column; gap:14px; overscroll-behavior:none; }
  h1 { font-size:20px; margin:0; display:flex; gap:12px; align-items:center; flex-wrap:wrap; }
  h2 { font-size:14px; text-transform:uppercase; letter-spacing:.05em; color:var(--muted); margin:0 0 10px; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:14px; margin-bottom:14px; }
  .badge { font-size:12px; padding:2px 8px; border-radius:99px; background:var(--line); color:var(--muted); text-decoration:none; }
  .badge.ok { background:var(--ok); color:#fff; } .badge.bad { background:var(--bad); color:#fff; }
  .grid { display:grid; grid-template-columns:repeat(auto-fill, minmax(100px, 1fr)); gap:8px; }
  .value { background:var(--card); border:1px solid var(--line); border-radius:8px; padding:8px; }
  .value .label { color:var(--muted); font-size:12px; }
  .value .num { font:600 18px ui-monospace, monospace; }
  #pad { flex:1; display:flex; align-items:center; justify-content:center; min-height:280px; }
  #stick { position:relative; width:min(80vw, 60vh, 360px); aspect-ratio:1; border-radius:50%;
           background:var(--card); border:2px solid var(--line); touch-action:none; user-select:none; }
  #stick::before, #stick::after { content:''; position:absolute; background:var(--line); }
  #stick::before { left:50%; top:10%; bottom:10%; width:1px; }
  #stick::after { top:50%; left:10%; right:10%; height:1px; }
  #knob { position:absolute; width:34%; height:34%; left:33%; top:33%; border-radius:50%;
          background:var(--accent); opacity:.85; pointer-events:none; }
  #hint { text-align:center; color:var(--muted); }
  #arm { align-self:center; width:min(80vw, 360px); padding:18px; font-size:18px; font-weight:600;
         border-radius:12px; border:2px solid var(--line); background:var(--card); color:var(--text);
         touch-action:none; user-select:none; }
  #arm.held { background:var(--accent); color:#fff; border-color:var(--accent); }
</style>
</head>
<body>
<h1>ROLO drive <a href="/calibration" class="badge">calibration</a> <a href="/gamepad" class="badge">gamepad</a>
  <span id="conn" class="badge">connecting</span> <span id="state" class="badge">-</span></h1>

<div class="card">
  <h2>Live</h2>
  <div class="grid" id="live"></div>
</div>

<div id="pad"><div id="stick"><div id="knob"></div></div></div>
<div id="hint">Drag to drive. Let go to stop.</div>
<button id="arm">Arm (hold)</button>

<script src="/rolo.js"></script>
<script>
const $ = (id) => document.getElementById(id);
const SEND_MS = 50;     // 20 commands a second while held
const DEADZONE = 0.08;  // ignore tiny movements around the centre

let x = 0, y = 0;       // joystick, -1..1 (y up = forward)
let held = false;

const badges = ROLO.badges($('conn'), $('state'));
const showLive = ROLO.live($('live'));
const send = ROLO.connect({
  feeds: ['live'],
  onStatus: badges.status,
  onSample: (s) => { showLive(s); badges.sample(s); },
});

function shape(v) {
  // Dead zone, then square the rest for finer control at low speed
  const a = Math.max(0, (Math.abs(v) - DEADZONE) / (1 - DEADZONE));
  return Math.sign(v) * a * a;
}

function moveTo(clientX, clientY) {
  const r = $('stick').getBoundingClientRect();
  let dx = (clientX - (r.left + r.width / 2)) / (r.width / 2);
  let dy = (clientY - (r.top + r.height / 2)) / (r.height / 2);
  const len = Math.hypot(dx, dy);
  if (len > 1) { dx /= len; dy /= len; }   // keep the knob inside the circle
  x = dx; y = -dy;
  $('knob').style.transform = `translate(${dx * 100}%, ${dy * 100}%)`;
}

function release() {
  held = false;
  x = 0; y = 0;
  $('knob').style.transform = '';
  send('drive 0 0');
}

const stick = $('stick');
stick.addEventListener('pointerdown', (e) => { held = true; stick.setPointerCapture(e.pointerId); moveTo(e.clientX, e.clientY); });
stick.addEventListener('pointermove', (e) => { if (held) moveTo(e.clientX, e.clientY); });
stick.addEventListener('pointerup', release);
stick.addEventListener('pointercancel', release);
document.addEventListener('visibilitychange', () => { if (document.hidden) { release(); armUp(); } });

// Arm: down while the button is held, back up when released
const armButton = $('arm');
function armDown(e) { armButton.setPointerCapture(e.pointerId); armButton.classList.add('held'); send('armbutton down'); }
function armUp() { armButton.classList.remove('held'); send('armbutton up'); }
armButton.addEventListener('pointerdown', armDown);
armButton.addEventListener('pointerup', armUp);
armButton.addEventListener('pointercancel', armUp);

// While held, keep sending: the robot stops by itself if commands stop arriving
setInterval(() => {
  if (held) send(`drive ${shape(x).toFixed(3)} ${shape(y).toFixed(3)}`);
}, SEND_MS);

</script>
</body>
</html>
)html";