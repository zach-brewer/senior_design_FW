"""
Groups raw per-mic readings into shot "events" -- one reading from every
mic, all within a short arrival window of each other -- so the TDOA solver
always gets a fully matched set.

Readings here carry `delta_us` (elapsed time since whichever mic triggered
first for this shot, per udp_listener.py) rather than an absolute
timestamp -- the correlator doesn't care either way, it just needs *some*
per-mic numeric value to hand to the TDOA solver once a matched set closes.

All 6 mics are required for every event: the downstream Nucleo board only
ever sends a reading for a mic whose analog signal already crossed the
0.2V trigger threshold, so there's no "partial" shot to reason about here --
a dead/unplugged mic (or one whose signal genuinely never crossed threshold)
just means that shot never correlates, same as before.

This is deliberately simple (last-reading-per-mic, wall-clock TTL) rather
than a full sequence-ID based matcher. It works well for a single shooter at
a time; see README.md for the tradeoff and how to extend it if you need to
resolve overlapping, closely-spaced shots.
"""
from __future__ import annotations

import time
from dataclasses import dataclass
from threading import Lock
from typing import Callable, Dict, Optional


@dataclass
class PendingReading:
    delta_us: int
    received_at: float  # wall-clock seconds (time.time()) when the server got it


class EventCorrelator:
    def __init__(self, correlation_window_s: float, pending_ttl_s: float,
                 required_mic_ids: set, on_event: Callable[[Dict[int, int]], None]):
        self._window = correlation_window_s
        self._ttl = pending_ttl_s
        self._required = set(required_mic_ids)
        self._pending: Dict[int, PendingReading] = {}
        self._lock = Lock()
        self._on_event = on_event

    def add_reading(self, mic_id: int, delta_us: int) -> None:
        now = time.time()
        with self._lock:
            self._expire_stale(now)
            self._pending[mic_id] = PendingReading(delta_us=delta_us, received_at=now)

            if self._required.issubset(self._pending.keys()):
                times = {mid: self._pending[mid].delta_us for mid in self._required}
                received_ats = [self._pending[mid].received_at for mid in self._required]
                if max(received_ats) - min(received_ats) <= self._window:
                    for mid in self._required:
                        del self._pending[mid]
                    fire = True
                else:
                    fire = False
            else:
                fire = False

        if fire:
            self._on_event(times)

    def _expire_stale(self, now: float) -> None:
        stale = [mid for mid, r in self._pending.items() if now - r.received_at > self._ttl]
        for mid in stale:
            del self._pending[mid]

    def status(self) -> Dict[int, Optional[PendingReading]]:
        with self._lock:
            return dict(self._pending)

    def reset(self) -> None:
        """Drop any partial (not-yet-matched) readings. Called on arm/disarm
        transitions so a stray reading from one session can't glue onto the
        next one's first shot."""
        with self._lock:
            self._pending.clear()
