#!/usr/bin/env python3
"""
Standalone UDP sender that mimics the 6 real microphones (2 rows of 3)
talking to the backend over the actual network path (as opposed to
POST /api/simulate, which bypasses the socket entirely). Use this to test
the real UDP listener, a firewall, packet loss, multi-host setups, etc.

Usage:
    python tools/simulate_udp_sender.py --host 127.0.0.1 --port 5005
    python tools/simulate_udp_sender.py --x 0.02 --y 1.18 --jitter-us 8
    python tools/simulate_udp_sender.py --random --count 20 --interval 1.5

Each run picks (or is given) an impact point (x, y) in meters in the same
frame as backend/config.py (front-row mics on y=0, back row behind them,
target above at positive y), computes the true propagation time from that
point to each of the 6 mics at the configured speed of sound, subtracts the
earliest one so every mic's value is a delta *since whichever mic triggered
first* (exactly what real mic hardware reports -- there's no shared absolute
clock to anchor to, and voltage thresholding happens entirely on the
downstream Nucleo board before a packet is ever sent), adds optional
Gaussian timing jitter, and sends one UDP JSON packet per mic:

    {"mic_id": <1-6>, "delta_us": <int, >= 0>}

The backend requires all 6 mics to report for a shot to correlate -- see
README.md -- so --dropout-prob is useful for testing that a missing mic
correctly blocks (rather than silently mis-solves) an event.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import random
import socket
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "backend"))
import config  # noqa: E402  (backend/config.py)


def send_shot(sock: socket.socket, host: str, port: int, x: float, y: float,
              speed_of_sound_mps: float, jitter_us: float, dropout_prob: float) -> None:
    travel_us = {}
    for mic_id, (mx, my) in config.MIC_POSITIONS_M.items():
        dist = math.hypot(x - mx, y - my)
        jitter = random.gauss(0, jitter_us) if jitter_us > 0 else 0.0
        travel_us[mic_id] = (dist / speed_of_sound_mps) * 1e6 + jitter

    t_min = min(travel_us.values())
    for mic_id, t_us in travel_us.items():
        if random.random() < dropout_prob:
            print(f"  mic {mic_id}: (dropped, simulating packet loss)")
            continue
        delta_us = max(0, int(round(t_us - t_min)))
        packet = json.dumps({"mic_id": mic_id, "delta_us": delta_us}).encode("utf-8")
        sock.sendto(packet, (host, port))
        print(f"  mic {mic_id}: delta_us={delta_us}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1", help="Backend UDP host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=config.UDP_PORT, help=f"Backend UDP port (default: {config.UDP_PORT})")
    parser.add_argument("--x", type=float, default=None, help="Impact x position, meters")
    parser.add_argument("--y", type=float, default=None, help="Impact y position, meters")
    parser.add_argument("--random", action="store_true", help="Pick random point(s) on the target instead of --x/--y")
    parser.add_argument("--count", type=int, default=1, help="Number of shots to send")
    parser.add_argument("--interval", type=float, default=1.0, help="Seconds between shots")
    parser.add_argument("--jitter-us", type=float, default=5.0, help="Gaussian timing noise stddev, microseconds")
    parser.add_argument("--dropout-prob", type=float, default=0.0, help="Probability [0-1] a given mic's packet is dropped")
    parser.add_argument("--speed-of-sound", type=float, default=config.DEFAULT_SPEED_OF_SOUND_MPS, help="m/s")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cx, cy = config.TARGET_CENTER_M
    max_radius = config.RING_RADII_M[-1]

    for i in range(args.count):
        if args.x is not None and args.y is not None and not args.random:
            x, y = args.x, args.y
        else:
            r = max_radius * 1.15 * (random.random() ** 0.5)
            theta = random.uniform(0, 2 * math.pi)
            x, y = cx + r * math.cos(theta), cy + r * math.sin(theta)

        print(f"[{i + 1}/{args.count}] simulated shot at x={x:.4f} y={y:.4f} -> {args.host}:{args.port}")
        send_shot(sock, args.host, args.port, x, y, args.speed_of_sound, args.jitter_us, args.dropout_prob)

        if i < args.count - 1:
            time.sleep(args.interval)


if __name__ == "__main__":
    main()
