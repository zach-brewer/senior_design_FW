"""
UDP listener for incoming microphone readings.

Packet format (JSON, UTF-8, one JSON object per UDP datagram):

    {"mic_id": 1, "delta_us": 0}

  - mic_id: integer, must match a key in config.MIC_POSITIONS_M.
  - delta_us: integer microseconds *elapsed since whichever mic in this
    shot crossed its trigger threshold first*. The mic that triggered first
    reports 0; every other mic reports how much later it saw the signal.
    Each mic computes this itself (e.g. from a shared hardware trigger/fan-
    out line it watches alongside its own detector), so there's no shared
    absolute clock or epoch to synchronize -- only local elapsed time needs
    to be measured accurately. See README.md.

There's no voltage field here: each mic's 0.2V trigger threshold is applied
entirely on the downstream Nucleo board, so a mic that hasn't crossed it
simply never sends a packet in the first place. By the time a reading
reaches this listener, it's already a valid trigger.

Any datagram that isn't valid JSON, or is missing a field, or has an
unrecognized mic_id, is logged and dropped.
"""
from __future__ import annotations

import json
import logging
import socket
import threading
from typing import Callable

logger = logging.getLogger("udp_listener")


class UdpListener:
    def __init__(self, host: str, port: int, valid_mic_ids: set,
                 on_reading: Callable[[int, int], None]):
        self._host = host
        self._port = port
        self._valid_mic_ids = valid_mic_ids
        self._on_reading = on_reading
        self._sock: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._running = False

    def start(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self._host, self._port))
        self._sock.settimeout(0.5)
        self._running = True
        self._thread = threading.Thread(target=self._run, name="udp-listener", daemon=True)
        self._thread.start()
        logger.info("UDP listener bound to %s:%s", self._host, self._port)

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=2)
        if self._sock:
            self._sock.close()

    def _run(self) -> None:
        while self._running:
            try:
                data, addr = self._sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                break

            try:
                payload = json.loads(data.decode("utf-8"))
                mic_id = int(payload["mic_id"])
                delta_us = int(payload["delta_us"])
            except (json.JSONDecodeError, KeyError, ValueError, TypeError) as exc:
                logger.warning("Dropped malformed packet from %s: %s (%s)", addr, data[:200], exc)
                continue

            if mic_id not in self._valid_mic_ids:
                logger.warning("Dropped packet with unknown mic_id=%s from %s", mic_id, addr)
                continue

            self._on_reading(mic_id, delta_us)
