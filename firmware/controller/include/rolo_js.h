#pragma once

// Served at "/rolo.js": the connection and stream decoding shared by every page.
// Only include this from web.cpp.
//
// The sample layout must match stream::Sample in stream.h (and tools/rolo_stream.py).
static const char ROLO_JS[] = R"js(
const ROLO = (() => {
  const VERSION = 1;
  const HEADER_SIZE = 8;     // magic 'R', version, count, dropped
  const SAMPLE_SIZE = 68;
  const STATES = ['balancing', 'ready', 'lying down'];
  const FLOATS = ['angle', 'gyroRate', 'accelAngle', 'targetAngle', 'speed', 'driveSpeed',
                  'driveAccel', 'turn', 'joyX', 'joyY', 'leftSpeed', 'rightSpeed', 'battery'];

  // One binary batch -> array of samples (empty if it isn't one we understand)
  function decode(buffer) {
    const v = new DataView(buffer);
    if (buffer.byteLength < HEADER_SIZE || v.getUint8(0) !== 82 || v.getUint8(1) !== VERSION) return [];
    const count = v.getUint16(2, true);
    const samples = [];
    for (let i = 0; i < count; i++) {
      const o = HEADER_SIZE + i * SAMPLE_SIZE;
      if (o + SAMPLE_SIZE > buffer.byteLength) break;
      const flags = v.getUint8(o + 6);
      const s = {
        timeUs: v.getUint32(o, true),
        dtUs: v.getUint16(o + 4, true),
        state: STATES[flags & 3] || '?',
        balancing: (flags & 3) === 0,
        armDown: !!(flags & 4),
        gamepad: !!(flags & 8),
        batteryLow: !!(flags & 16),
        batteryCritical: !!(flags & 32),
      };
      FLOATS.forEach((name, k) => { s[name] = v.getFloat32(o + 8 + k * 4, true); });
      s.position = v.getInt32(o + 60, true);
      s.positionDiff = v.getInt32(o + 64, true);
      samples.push(s);
    }
    return samples;
  }

  // Connects (and reconnects), asks for the given feeds ('live', 'pad'), and calls
  // onSample(sample) with each live sample and onJson(msg) with every text message.
  // Returns send(text).
  function connect({ feeds = ['live'], onSample = () => {}, onJson = () => {}, onStatus = () => {} }) {
    let ws = null;
    function open() {
      ws = new WebSocket(`ws://${location.host}/ws`);
      ws.binaryType = 'arraybuffer';
      ws.onopen = () => {
        for (const feed of feeds) ws.send(`${feed} on`);
        onStatus(true);
      };
      ws.onclose = () => { onStatus(false); setTimeout(open, 1000); };
      ws.onmessage = (e) => {
        if (e.data instanceof ArrayBuffer) {
          const samples = decode(e.data);
          if (samples.length) onSample(samples[samples.length - 1]);
        } else {
          onJson(JSON.parse(e.data));
        }
      };
    }
    open();
    return (text) => { if (ws && ws.readyState === WebSocket.OPEN) ws.send(text); };
  }

  // Shows the connection and robot state in two badges
  function badges(connEl, stateEl) {
    return {
      status(connected) {
        connEl.textContent = connected ? 'connected' : 'disconnected';
        connEl.className = 'badge ' + (connected ? 'ok' : 'bad');
      },
      sample(s) {
        stateEl.textContent = s.state;
        stateEl.className = 'badge ' + (s.balancing ? 'ok' : 'bad');
      },
    };
  }

  // The Live section: the same numbers on every page. Fills container with one box per
  // value and returns show(sample).
  const LIVE = [
    ['angle', 'Angle', 1, '°'], ['targetAngle', 'Target', 1, '°'], ['speed', 'Speed', 0, ''],
    ['driveSpeed', 'Drive speed', 0, ''], ['turn', 'Turn', 0, ''], ['gyroRate', 'Gyro', 0, '°/s'],
    ['position', 'Position', 0, ''], ['battery', 'Battery', 2, 'V'], ['dtUs', 'Loop', 0, 'µs'],
  ];
  function live(container) {
    const nums = {};
    for (const [key, label] of LIVE) {
      const box = document.createElement('div');
      box.className = 'value';
      box.innerHTML = `<div class="label">${label}</div><div class="num">-</div>`;
      container.appendChild(box);
      nums[key] = box.querySelector('.num');
    }
    return (s) => {
      for (const [key, , digits, unit] of LIVE) nums[key].textContent = s[key].toFixed(digits) + (unit ? ' ' + unit : '');
      // Battery warning, from the robot (thresholds in config.h): orange when low, red when critical
      nums.battery.style.color = s.batteryCritical ? '#e03131' : s.batteryLow ? '#e08a00' : '';
    };
  }

  return { decode, connect, badges, live };
})();
)js";
