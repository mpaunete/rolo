#!/usr/bin/env python3
"""
Record ROLO's live data stream to CSV, optionally while driving a repeatable test.

Connects to the robot's WebSocket, asks for the 200 Hz stream, and writes:
    <name>.csv          every control-loop sample
    <name>-events.csv   every command sent to the robot, plus test markers
    <name>-params.json  the robot's parameters (rewritten whenever they change)

Commands typed in the terminal are sent to the robot (and recorded as events).

    python rolo_stream.py <robot-ip>
    python rolo_stream.py <robot-ip> --name push-test --seconds 30
    python rolo_stream.py <robot-ip> --test step --amount 0.5

Tests (the robot must be standing and balancing; it waits until it is):
    still   20 s standing still (wobble baseline)
    step    forward, release, backward, release
    tap     a short push forward, release, a short push backward, release
    turn    turn right, release, turn left, release
    ramp    speed rising slowly to --amount, hold, release
    all     still (10 s), step, turn, ramp

Stop with Ctrl+C: the robot is told to stop driving first.
Sample layout must match stream::Sample in controller/include/stream.h.
"""

import argparse
import asyncio
import csv
import json
import struct
import sys
import time
from datetime import datetime

import websockets

STREAM_VERSION = 1
HEADER = struct.Struct("<BBHI")              # magic 'R', version, count, dropped total
SAMPLE = struct.Struct("<IHBB13fii")         # stream::Sample, 68 bytes, little-endian
assert SAMPLE.size == 68

COLUMNS = [
    "time_s", "dt_us", "state", "arm_down", "gamepad",
    "angle", "gyro", "accel_angle", "target_angle", "speed",
    "drive_speed", "drive_accel", "turn", "joy_x", "joy_y",
    "left_speed", "right_speed", "battery", "position", "position_diff",
    "battery_low", "battery_critical",
]
STATES = {0: "balancing", 1: "ready", 2: "lay_down"}

DRIVE_INTERVAL = 0.05      # resend the joystick 20 times a second (the robot stops after 0.5 s without)


class Recorder:
    def __init__(self, name):
        self.name = name
        self.samples_file = open(f"{name}.csv", "w", newline="")
        self.events_file = open(f"{name}-events.csv", "w", newline="")
        self.samples = csv.writer(self.samples_file)
        self.events = csv.writer(self.events_file)
        self.samples.writerow(COLUMNS)
        self.events.writerow(["time_s", "text"])
        self.events_file.flush()

        self.first_us = None      # robot time of the first sample
        self.last_raw_us = None   # to unwrap micros(), which rolls over every ~71 minutes
        self.wraps = 0
        self.count = 0
        self.dropped = 0
        self.last = None          # last sample row
        self.last_time = 0.0      # robot time of the last sample, seconds

    def unwrap(self, raw_us):
        if self.last_raw_us is not None and raw_us < self.last_raw_us - 2**31:
            self.wraps += 1
        self.last_raw_us = raw_us
        return raw_us + self.wraps * 2**32

    def add_batch(self, data):
        magic, version, count, dropped = HEADER.unpack_from(data, 0)
        if magic != ord("R") or version != STREAM_VERSION:
            print(f"\nunexpected batch (magic {magic}, version {version}): firmware and tool out of step?")
            return
        self.dropped = dropped

        for i in range(count):
            f = SAMPLE.unpack_from(data, HEADER.size + i * SAMPLE.size)
            time_us, dt_us, flags = f[0], f[1], f[2]
            floats = f[4:17]
            position, position_diff = f[17], f[18]

            us = self.unwrap(time_us)
            if self.first_us is None:
                self.first_us = us
            self.last_time = (us - self.first_us) / 1e6

            row = [
                f"{self.last_time:.6f}", dt_us,
                STATES.get(flags & 0x03, "?"), (flags >> 2) & 1, (flags >> 3) & 1,
                *(f"{v:.4f}" for v in floats),
                position, position_diff,
                (flags >> 4) & 1, (flags >> 5) & 1,
            ]
            self.samples.writerow(row)
            self.last = row
            self.count += 1

    def state(self):
        return self.last[2] if self.last else None

    def add_event(self, text, time_us=None):
        """Robot events carry the robot's time; the tool's own markers use the last sample's time."""
        if time_us is not None and self.first_us is not None:
            t = (time_us - self.first_us) / 1e6
        else:
            t = self.last_time
        self.events.writerow([f"{t:.6f}", text])
        self.events_file.flush()
        print(f"\n[{t:9.3f}s] {text}")

    def save_params(self, items):
        params = {item["name"]: item["value"] for item in items}
        with open(f"{self.name}-params.json", "w") as f:
            json.dump(params, f, indent=2)

    def close(self):
        self.samples_file.close()
        self.events_file.close()


# ----------------------------------------------------------------------------
# Repeatable tests: a list of (label, seconds, x, y). x/y can be numbers, or
# functions of the fraction (0..1) through that step, for ramps.
# ----------------------------------------------------------------------------

def test_steps(name, a):
    still = [("still", 20, 0, 0)]
    step = [
        ("settle", 3, 0, 0),
        ("forward", 2.5, 0, a),
        ("release", 4, 0, 0),
        ("backward", 2.5, 0, -a),
        ("release", 4, 0, 0),
    ]
    tap = [
        ("settle", 3, 0, 0),
        ("tap forward", 0.3, 0, a),
        ("release", 4, 0, 0),
        ("tap backward", 0.3, 0, -a),
        ("release", 4, 0, 0),
    ]
    turn = [
        ("settle", 3, 0, 0),
        ("turn right", 2, a, 0),
        ("release", 3, 0, 0),
        ("turn left", 2, -a, 0),
        ("release", 3, 0, 0),
    ]
    ramp = [
        ("settle", 3, 0, 0),
        ("ramp up", 5, 0, lambda f: a * f),
        ("hold", 2, 0, a),
        ("release", 4, 0, 0),
    ]
    tests = {
        "still": still,
        "step": step,
        "tap": tap,
        "turn": turn,
        "ramp": ramp,
        "all": [("still", 10, 0, 0)] + step + turn + ramp,
    }
    return tests[name]


async def drive(ws, x, y):
    await ws.send(f"drive {x:.3f} {y:.3f}")


async def run_test(ws, recorder, name, amount):
    """Drive the robot through a test, recording markers. Stops if the robot falls."""
    print(f"\nTest '{name}' at {amount:.2f}: waiting for the robot to balance...")
    waited = 0.0
    while recorder.state() != "balancing":
        await asyncio.sleep(0.1)
        waited += 0.1
        if waited >= 3.0:   # every 3 s, say what we're waiting for
            waited = 0.0
            if recorder.count == 0:
                print("\n  no data from the robot yet: the stream isn't arriving (check the robot's serial output)")
            else:
                print(f"\n  robot state is '{recorder.state()}': stand it up so it starts balancing")

    recorder.add_event(f"test start: {name} {amount:.2f}")
    try:
        for label, seconds, x, y in test_steps(name, amount):
            recorder.add_event(f"test: {label}")
            started = time.monotonic()
            while (elapsed := time.monotonic() - started) < seconds:
                if recorder.state() != "balancing":
                    recorder.add_event("test aborted: robot not balancing")
                    return
                fraction = elapsed / seconds
                await drive(ws, x(fraction) if callable(x) else x, y(fraction) if callable(y) else y)
                await asyncio.sleep(DRIVE_INTERVAL)
        recorder.add_event("test end")
    finally:
        await drive(ws, 0, 0)   # never leave the robot driving


async def read_commands(ws):
    """Send whatever is typed in the terminal, one command per line."""
    loop = asyncio.get_running_loop()
    while True:
        line = await loop.run_in_executor(None, sys.stdin.readline)
        if not line:
            return
        line = line.strip()
        if line:
            await ws.send(line)


async def status(recorder):
    """One status line per second."""
    previous = 0
    while True:
        await asyncio.sleep(1)
        rate = recorder.count - previous
        previous = recorder.count
        r = recorder.last
        if r:
            print(f"\r{recorder.count:8d} samples  {rate:3d}/s  dropped {recorder.dropped}  "
                  f"{r[2]:9s} angle {float(r[5]):6.2f}  speed {float(r[9]):7.0f}  "
                  f"battery {float(r[17]):5.2f} V   ", end="", flush=True)


async def receive(ws, recorder):
    async for message in ws:
        if isinstance(message, bytes):
            recorder.add_batch(message)
            continue
        try:
            msg = json.loads(message)
        except ValueError:
            continue
        if msg.get("type") == "event":
            recorder.add_event(msg["text"], msg["t"])
        elif msg.get("type") == "params":
            recorder.save_params(msg["items"])
        # other text messages are telemetry for the web pages


async def run(args):
    name = args.name or datetime.now().strftime("rolo-%Y%m%d-%H%M%S")
    recorder = Recorder(name)
    url = f"ws://{args.host}/ws"
    print(f"Connecting to {url}, recording to {name}.csv (Ctrl+C to stop)")

    try:
        async with websockets.connect(url, max_size=None) as ws:
            await ws.send("stream on")
            background = [asyncio.create_task(status(recorder)),
                          asyncio.create_task(read_commands(ws))]
            receiver = asyncio.create_task(receive(ws, recorder))

            try:
                if args.test:
                    await run_test(ws, recorder, args.test, args.amount)
                    await asyncio.sleep(2)               # record the robot settling afterwards
                elif args.seconds:
                    await asyncio.sleep(args.seconds)
                else:
                    await receiver                       # until Ctrl+C or the robot disconnects
            finally:
                await drive(ws, 0, 0)
                await ws.send("stream off")
                for task in background + [receiver]:
                    task.cancel()
    finally:
        recorder.close()
        print(f"\nSaved {recorder.count} samples to {name}.csv ({recorder.dropped} dropped)")


def main():
    parser = argparse.ArgumentParser(description="Record ROLO's live 200 Hz data stream to CSV.")
    parser.add_argument("host", help="robot IP address or hostname (the console command 'net' shows it)")
    parser.add_argument("--name", help="output file name without .csv (default: rolo-<date>-<time>)")
    parser.add_argument("--seconds", type=float, help="stop after this many seconds")
    parser.add_argument("--test", choices=["still", "step", "tap", "turn", "ramp", "all"],
                        help="drive a repeatable test while recording")
    parser.add_argument("--amount", type=float, default=0.5,
                        help="stick deflection for the test, 0..1 (default 0.5)")
    args = parser.parse_args()
    args.amount = max(0.0, min(1.0, args.amount))

    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
