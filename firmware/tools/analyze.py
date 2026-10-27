#!/usr/bin/env python3
"""
Analyse a ROLO recording made with rolo_stream.py.

    python analyze.py first-test             reads first-test.csv (+ -events.csv, -params.json)
    python analyze.py first-test --show      also opens the plot window

Prints a summary (loop timing, standing-still wobble, and every drive step:
reaction delay, time to speed, lean, stopping time and distance), and saves
<name>.png with angle, speed and stick plotted together.
"""

import argparse
import csv
import json
import os

import numpy as np

STEP_MM = 3.14159265 * 93.2 / 3200      # wheel travel per microstep (93.2 mm wheel, 3200 steps/turn)

STILL_MIN_S = 3.0          # a still stretch must last at least this long to be analysed
TAP_MAX_S = 0.6            # stick held for less than this, then released: a tap
MOVE_THRESHOLD = 150.0     # steps/s change that counts as "the robot reacted" (above the standing wobble)
STOPPED_SPEED = 100.0      # steps/s below which the robot counts as stopped
STOPPED_HOLD_S = 0.5       # ...for at least this long
PUSH_SPEED = 800.0         # steps/s reached without a command: someone pushed the robot


# ----------------------------------------------------------------------------
# Loading
# ----------------------------------------------------------------------------

def load(name):
    with open(f"{name}.csv") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise SystemExit(f"{name}.csv has no samples")

    data = {}
    for column in rows[0]:
        if column == "state":
            data[column] = np.array([r[column] for r in rows])
        else:
            data[column] = np.array([float(r[column]) for r in rows])

    events = []
    if os.path.exists(f"{name}-events.csv"):
        with open(f"{name}-events.csv") as f:
            events = [(float(r["time_s"]), r["text"]) for r in csv.DictReader(f)]

    params = {}
    if os.path.exists(f"{name}-params.json"):
        with open(f"{name}-params.json") as f:
            params = json.load(f)

    return data, events, params


# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------

def runs(mask):
    """(start, end) index pairs where mask is True, end exclusive."""
    edges = np.diff(np.concatenate(([0], mask.astype(int), [0])))
    return list(zip(np.where(edges == 1)[0], np.where(edges == -1)[0]))


def dominant_frequency(signal, rate):
    """Strongest frequency between 0.5 and 30 Hz, and its share of the signal's energy."""
    signal = signal - signal.mean()
    if len(signal) < rate:            # need at least a second of data
        return None, None
    spectrum = np.abs(np.fft.rfft(signal * np.hanning(len(signal)))) ** 2
    freqs = np.fft.rfftfreq(len(signal), 1.0 / rate)
    band = (freqs >= 0.5) & (freqs <= 30)
    if not band.any() or spectrum[band].sum() == 0:
        return None, None
    peak = np.argmax(spectrum[band])
    return freqs[band][peak], spectrum[band][peak] / spectrum[band].sum()


def first_index(condition, start=0):
    hits = np.where(condition[start:])[0]
    return start + hits[0] if len(hits) else None


def uncommanded(d, settle_s=3.0):
    """Balancing, nothing commanded, and at least settle_s since the last command
    (so the end of a drive step isn't mistaken for a push or for standing still)."""
    commanded = ((np.abs(d["joy_x"]) >= 0.01) | (np.abs(d["joy_y"]) >= 0.01) |
                 (np.abs(d["drive_speed"]) >= 1))
    t = d["time_s"]
    recent = np.zeros(len(t), dtype=bool)
    last = -1e9
    for i in range(len(t)):
        if commanded[i]:
            last = t[i]
        recent[i] = t[i] - last < settle_s
    return (d["state"] == "balancing") & ~recent


def stopped_at(d, start, end, rate):
    """First index from start where the robot stays below STOPPED_SPEED for STOPPED_HOLD_S."""
    hold = max(1, int(STOPPED_HOLD_S * rate))
    slow = np.abs(d["speed"]) < STOPPED_SPEED
    for k in range(start, end - hold):
        if slow[k:k + hold].all():
            return k
    return None


def find_pushes(d, rate):
    """(start, peak, end) indices of pushes: big speed with nobody driving."""
    free = uncommanded(d)
    fast = free & (np.abs(d["speed"]) > PUSH_SPEED)
    pushes = []
    for a, b in runs(fast):
        if pushes and a - pushes[-1][2] < rate:          # part of the previous push
            continue
        start = a
        while start > 0 and free[start - 1] and abs(d["speed"][start - 1]) > STOPPED_SPEED:
            start -= 1
        peak = a + np.argmax(np.abs(d["speed"][a:b]))
        limit = min(len(d["speed"]), b + int(5 * rate))
        end = stopped_at(d, b, limit, rate) or limit
        pushes.append((start, peak, end))
    return pushes


# ----------------------------------------------------------------------------
# Sections of the report
# ----------------------------------------------------------------------------

def timing(d):
    t, dt = d["time_s"], d["dt_us"]
    duration = t[-1] - t[0]
    rate = (len(t) - 1) / duration if duration > 0 else 0
    gaps = np.diff(t)
    print("Recording")
    print(f"  {len(t)} samples over {duration:.1f} s = {rate:.0f} samples/s")
    print(f"  loop interval: mean {dt.mean() / 1000:.2f} ms, max {dt.max() / 1000:.2f} ms, "
          f"{(dt > 7500).sum()} late loops (> 7.5 ms)")
    print(f"  gaps in the recording (> 20 ms): {(gaps > 0.02).sum()}")
    print(f"  battery: {d['battery'].min():.2f} .. {d['battery'].max():.2f} V")
    return rate


def stillness(d, rate, pushes):
    """Calm stretches: nothing commanded, balancing, and not recovering from a push."""
    quiet = uncommanded(d)
    for start, peak, end in pushes:
        quiet[max(0, start - int(rate)):min(len(quiet), end + int(rate))] = False
    stretches = [(a, b) for a, b in runs(quiet) if (b - a) / rate >= STILL_MIN_S]

    print("\nStanding still (pushes left out)")
    if not stretches:
        print("  no calm stretch of at least 3 s")
        return

    for a, b in stretches:
        # skip the first second: it is usually the end of the previous move settling
        a = min(a + int(rate), b - 1)
        seconds = (b - a) / rate
        angle, speed = d["angle"][a:b], d["speed"][a:b]
        drift = (d["position"][b - 1] - d["position"][a]) * STEP_MM / seconds
        freq, share = dominant_frequency(speed, rate)
        wobble = f"{freq:.1f} Hz ({share * 100:.0f}% of the motion)" if freq else "n/a"
        print(f"  {d['time_s'][a]:7.1f}s +{seconds:5.1f}s: angle sd {angle.std():.3f}°, "
              f"speed sd {speed.std():5.0f}, drift {drift:+5.1f} mm/s, wobble {wobble}")


def push_report(d, pushes, rate):
    print("\nPushes (no command, speed above 800 steps/s)")
    if not pushes:
        print("  none")
        return
    t = d["time_s"]
    for start, peak, end in pushes:
        direction = np.sign(d["speed"][peak]) or 1
        travel = (d["position"][start:end + 1] - d["position"][start]) * direction * STEP_MM
        furthest = travel.max() if len(travel) else 0
        rollback = max(0.0, furthest - travel[-1]) if len(travel) else 0
        lean = d["angle"][start:end + 1]
        peak_lean = lean[np.argmax(np.abs(lean))] if len(lean) else 0
        recovered = t[end] - t[start]
        print(f"  {t[start]:7.1f}s peak {d['speed'][peak]:+6.0f} steps/s: back to rest in {recovered:4.2f} s, "
              f"travelled {furthest:4.0f} mm, rolled back {rollback:4.0f} mm, peak lean {peak_lean:+.1f}°")


def steps(d, params):
    """Every change of the forward stick: start, change of speed, or release."""
    joy, speed, t = d["joy_y"], d["speed"], d["time_s"]
    changes = np.where(np.abs(np.diff(joy)) > 0.05)[0] + 1

    # merge changes closer than 0.2 s (a ramp, or a hand on the stick, is one manoeuvre)
    edges = []
    for i in changes:
        if not edges or t[i] - t[edges[-1]] > 0.2:
            edges.append(i)

    print("\nDrive steps (forward stick)")
    if not edges:
        print("  no forward/backward commands in this recording")
        return

    drive_speed = params.get("driveSpeed")
    rate = len(t) / (t[-1] - t[0] + 1e-9)
    skip = set()
    for n, i in enumerate(edges):
        if n in skip:
            continue
        end = edges[n + 1] if n + 1 < len(edges) else len(t)
        before = speed[max(i - 1, 0)]
        target = d["drive_speed"][end - 1]          # where the setpoint ended up
        joy_after = joy[min(i + 5, len(joy) - 1)]

        reacted = first_index(np.abs(speed - before) > MOVE_THRESHOLD, i)
        reaction = f"{(t[reacted] - t[i]) * 1000:4.0f} ms" if reacted is not None and reacted < end else "  n/a"
        lean = d["angle"][i:end]
        peak_lean = lean[np.argmax(np.abs(lean))] if len(lean) else 0

        tap_release = edges[n + 1] if n + 1 < len(edges) else None
        if (abs(joy_after) > 0.01 and tap_release is not None and t[tap_release] - t[i] < TAP_MAX_S
                and abs(joy[min(tap_release + 5, len(joy) - 1)]) < 0.01):
            # a tap: short push and release. Measure the whole move: how long until the
            # robot is at rest again (after it got going), how far it went, how far it came back
            skip.add(n + 1)
            end = edges[n + 2] if n + 2 < len(edges) else len(t)
            reacted = first_index(np.abs(speed - before) > MOVE_THRESHOLD, i)
            reaction = f"{(t[reacted] - t[i]) * 1000:4.0f} ms" if reacted is not None and reacted < end else "  n/a"
            lean = d["angle"][i:end]
            peak_lean = lean[np.argmax(np.abs(lean))] if len(lean) else 0
            direction = np.sign(joy_after)
            peak = i + np.argmax(speed[i:end] * direction)
            hold = max(1, int(STOPPED_HOLD_S * rate))
            slow = np.abs(speed) < STOPPED_SPEED
            stopped = next((k for k in range(peak, end - hold) if slow[k:k + hold].all()), None)
            travel = (d["position"][i:end] - d["position"][i]) * direction * STEP_MM
            furthest = travel.max() if len(travel) else 0
            back = max(0.0, furthest - travel[-1]) if len(travel) else 0
            rest = f"{t[stopped] - t[tap_release]:5.2f} s" if stopped is not None else "  n/a"
            label = "forward " if direction > 0 else "backward"
            print(f"  {t[i]:7.1f}s tap {label} {t[tap_release] - t[i]:.2f} s at {joy_after:+.2f}: "
                  f"reacts {reaction}, peak {speed[peak]:+6.0f} steps/s, at rest {rest} after release, "
                  f"went {furthest:4.0f} mm, came back {back:4.0f} mm, peak lean {peak_lean:+.1f}°")
        elif abs(joy_after) > 0.01:
            # going (or speeding up): time to 90% of the commanded speed, and overshoot
            span = target - before
            reached = first_index(np.abs(speed - before) >= 0.9 * abs(span), i) if abs(span) > 1 else None
            to_speed = f"{t[reached] - t[i]:5.2f} s" if reached is not None and reached < end else "  n/a"
            if span > 0:
                over = speed[i:end].max() - target
            else:
                over = target - speed[i:end].min()
            label = "forward " if joy_after > 0 else "backward"
            print(f"  {t[i]:7.1f}s {label} stick {joy_after:+.2f} -> {target:6.0f} steps/s: "
                  f"reacts {reaction}, 90% speed in {to_speed}, overshoot {max(over, 0):5.0f}, "
                  f"peak lean {peak_lean:+.1f}°")
        else:
            # release: time and distance until it stays stopped, and how far it rolls back
            slow = np.abs(speed) < STOPPED_SPEED
            hold = int(STOPPED_HOLD_S * len(t) / (t[-1] - t[0] + 1e-9))
            stopped = None
            for k in range(i, end - hold):
                if slow[k:k + hold].all():
                    stopped = k
                    break
            direction = np.sign(before) or 1
            travel = (d["position"][i:end] - d["position"][i]) * direction * STEP_MM
            if stopped is not None:
                stop_s = f"{t[stopped] - t[i]:5.2f} s"
                stop_mm = f"{travel[stopped - i]:5.0f} mm"
            else:
                stop_s = stop_mm = "  n/a"
            rollback = max(0.0, travel.max() - travel[-1]) if len(travel) else 0
            print(f"  {t[i]:7.1f}s release from {before:6.0f} steps/s: stops in {stop_s} "
                  f"after {stop_mm}, rolls back {rollback:4.0f} mm, peak lean {peak_lean:+.1f}°")

    if drive_speed:
        print(f"  (driveSpeed {drive_speed:.0f}, driveAccel {params.get('driveAccel', 0):.0f}, "
              f"accelLean {params.get('accelLean', 0)})")


def limits(d, params):
    print("\nClosest to the limits")
    max_target = params.get("maxTargetAngle")
    print(f"  peak speed {np.abs(d['speed']).max():.0f} steps/s (motor limit 8000)")
    peak_target = np.abs(d["target_angle"]).max()
    if max_target:
        print(f"  peak target lean {peak_target:.1f}° (maxTargetAngle {max_target})")
    else:
        print(f"  peak target lean {peak_target:.1f}°")
    print(f"  peak angle {np.abs(d['angle']).max():.1f}°")


def plot(d, events, name, show):
    import matplotlib
    if not show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    t = d["time_s"]
    fig, axes = plt.subplots(4, 1, sharex=True, figsize=(14, 10))

    axes[0].plot(t, d["angle"], label="angle")
    axes[0].plot(t, d["target_angle"], label="target angle", alpha=0.8)
    axes[0].set_ylabel("degrees")

    axes[1].plot(t, d["speed"], label="speed (balance output)")
    axes[1].plot(t, d["drive_speed"], label="drive setpoint", alpha=0.8)
    axes[1].set_ylabel("steps/s")

    axes[2].plot(t, d["joy_y"], label="stick forward")
    axes[2].plot(t, d["joy_x"], label="stick turn")
    axes[2].set_ylabel("stick")

    axes[3].plot(t, d["gyro"], label="gyro", linewidth=0.6)
    axes[3].set_ylabel("deg/s")
    axes[3].set_xlabel("time (s)")

    for ax in axes:
        for et, text in events:
            ax.axvline(et, color="grey", alpha=0.3, linewidth=0.8)
        ax.legend(loc="upper right", fontsize=8)
        ax.grid(alpha=0.3)
    for et, text in events:
        axes[0].text(et, axes[0].get_ylim()[1], text, rotation=90, fontsize=7,
                     va="top", ha="right", alpha=0.7)

    fig.suptitle(name)
    fig.tight_layout()
    fig.savefig(f"{name}.png", dpi=110)
    print(f"\nPlot saved to {name}.png")
    if show:
        plt.show()


def main():
    parser = argparse.ArgumentParser(description="Analyse a ROLO recording.")
    parser.add_argument("name", help="recording name, without .csv")
    parser.add_argument("--show", action="store_true", help="open the plot window")
    args = parser.parse_args()
    name = args.name[:-4] if args.name.endswith(".csv") else args.name

    d, events, params = load(name)
    rate = timing(d)
    pushes = find_pushes(d, rate)
    stillness(d, rate, pushes)
    push_report(d, pushes, rate)
    steps(d, params)
    limits(d, params)
    plot(d, events, name, args.show)


if __name__ == "__main__":
    main()
