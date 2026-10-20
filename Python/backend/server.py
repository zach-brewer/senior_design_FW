"""
Flask + Flask-SocketIO backend for the TDOA acoustic shot-locating system.

Responsibilities:
  - Listen on UDP for raw per-mic readings (see udp_listener.py), but only
    while the system is "armed" -- see Arm/disarm below.
  - Correlate readings from all 6 mics into shot events (see correlator.py).
    Each mic's 0.2V trigger threshold is enforced entirely on the downstream
    Nucleo board -- a mic that hasn't crossed it never sends a packet, so
    this backend doesn't see voltage at all and simply requires all 6 mics
    to report before it will solve a shot.
  - Solve each event for (x, y) + target score (see tdoa.py). Each mic
    already reports its reading as an elapsed delta since whichever mic
    triggered first, so that mic is this shot's own reference/"trigger" mic
    by construction (its delta is 0) and every other mic's time is >= 0.
  - Group every shot fired between an arm and the next disarm into a "shot
    group," and compute the smallest circle enclosing that group (see
    geometry.py) once it closes.
  - Serve the frontend, a small REST API, and push real-time updates over
    Socket.IO so the browser doesn't have to poll.
  - Let the website push a new mic trigger threshold to the Nucleo board
    over UDP (POST /api/nucleo/voltage-threshold) -- see "Nucleo threshold
    push" below.
  - Tell the frontend what target background images are available (see
    config.TARGET_TYPES) and serve them as static files. Purely visual --
    does not touch scoring, which always uses config.RING_RADII_M /
    TARGET_CENTER_M regardless of which target image is showing.

Nucleo threshold push:
    The Nucleo board owns the actual 0.2V-by-default analog trigger
    threshold and applies it locally per mic; this backend never receives
    voltage (see "Network protocol" in README.md). The website still needs
    to be able to *set* that threshold without reflashing the Nucleo, so
    POST /api/nucleo/voltage-threshold fires a one-way UDP packet at
    config.NUCLEO_HOST:NUCLEO_PORT with the new value. This is fire-and-
    forget -- nothing is read back, so "sent successfully" only means the
    packet left this machine, not that the Nucleo received or applied it.

Arm/disarm:
    The system ignores all incoming mic packets until armed -- POST
    /api/arm starts a new shot group and starts accepting data; POST
    /api/disarm stops accepting data and closes out that group (computing
    its enclosing circle, if it has at least one shot). This mirrors a
    physical "go/no-go" trigger for a shooting session rather than having
    the backend running and listening at all times.

Run:
    python server.py
Then open http://localhost:8000 (or the HTTP_PORT you configured).
"""
from __future__ import annotations

import itertools
import json
import logging
import math
import mimetypes
import random
import socket
import time
from datetime import datetime, timezone
from threading import Lock

from flask import Flask, jsonify, request, send_from_directory
from flask_socketio import SocketIO

# Some older/misconfigured systems' mimetypes database doesn't know ".webp"
# (used by the target images under frontend/targets/), which would make
# Flask's static file serving send it with a generic/incorrect content type.
# Registering it explicitly here costs nothing and removes that foot-gun.
mimetypes.add_type("image/webp", ".webp")

import config
import geometry
import tdoa
from correlator import EventCorrelator
from udp_listener import UdpListener

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s [%(name)s] %(message)s")
logger = logging.getLogger("server")

app = Flask(__name__, static_folder="../frontend", static_url_path="")
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

# ---------------------------------------------------------------------------
# Shared, mutable state (guarded by _state_lock)
# ---------------------------------------------------------------------------
_state_lock = Lock()
_mic_status: dict[int, dict] = {
    mic_id: {"delta_us": None, "received_at": None} for mic_id in config.MIC_POSITIONS_M
}
_event_history: list[dict] = []
_speed_of_sound_mps = config.DEFAULT_SPEED_OF_SOUND_MPS
# The last voltage threshold value sent (or about to be sent) to the Nucleo.
# This is purely a UI convenience -- nothing reads the Nucleo's own current
# threshold back, so this can drift from reality if someone changes it some
# other way (e.g. directly on the Nucleo).
_voltage_threshold_v = config.DEFAULT_VOLTAGE_THRESHOLD_V
_nucleo_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
_event_id_counter = itertools.count(1)

# Arm/disarm + shot-group state
_armed = False
_group_id_counter = itertools.count(1)
_current_group_id: int | None = None
_current_group_started_at: str | None = None
_groups: list[dict] = []


def _iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _serializable_mic_status() -> dict:
    now = time.time()
    out = {}
    with _state_lock:
        for mic_id, info in _mic_status.items():
            age_s = None
            if info["received_at"] is not None:
                age_s = round(now - info["received_at"], 3)
            out[str(mic_id)] = {
                "mic_id": mic_id,
                "delta_us": info["delta_us"],
                "received_at_iso": info["received_at_iso"] if info.get("received_at_iso") else None,
                "age_s": age_s,
            }
    return out


def _system_status() -> dict:
    with _state_lock:
        shots_in_group = sum(1 for e in _event_history if e["group_id"] == _current_group_id) if _armed else 0
        return {
            "armed": _armed,
            "current_group_id": _current_group_id,
            "current_group_started_at": _current_group_started_at,
            "current_group_shot_count": shots_in_group,
        }


def _send_voltage_threshold_to_nucleo(value: float) -> bool:
    """Fire-and-forget UDP push of a new trigger threshold to the Nucleo
    board at config.NUCLEO_HOST:NUCLEO_PORT. The exact wire format here is
    arbitrary (a one-field JSON object) -- the Nucleo firmware can parse it
    however is convenient; change this function if you want a different
    format. Returns True only if the OS accepted the packet for sending;
    that does NOT mean the Nucleo received or applied it, since nothing is
    read back over this connection."""
    payload = json.dumps({"voltage_threshold_v": value}).encode("utf-8")
    try:
        _nucleo_sock.sendto(payload, (config.NUCLEO_HOST, config.NUCLEO_PORT))
        return True
    except OSError:
        logger.exception("Failed to send voltage threshold to Nucleo at %s:%s",
                          config.NUCLEO_HOST, config.NUCLEO_PORT)
        return False


def handle_mic_reading(mic_id: int, delta_us: int) -> None:
    """Called for every individual mic reading, whether from the real UDP
    listener or the /api/simulate endpoint. While the system isn't armed,
    this is a no-op -- nothing is stored, displayed, or correlated, matching
    a physical trigger that simply isn't listening yet.

    There's no voltage here to check: the Nucleo board only ever sends a
    reading for a mic that's already crossed its 0.2V threshold, so every
    reading that reaches this point is, by construction, a valid trigger."""
    with _state_lock:
        if not _armed:
            return
        now = time.time()
        _mic_status[mic_id] = {
            "delta_us": delta_us,
            "received_at": now,
            "received_at_iso": _iso_now(),
        }

    socketio.emit("mic_reading", {
        "mic_id": mic_id,
        "delta_us": delta_us,
        "received_at_iso": _iso_now(),
    })

    correlator.add_reading(mic_id, delta_us)


def _on_correlated_event(times_us: dict) -> None:
    with _state_lock:
        c = _speed_of_sound_mps
        group_id = _current_group_id
        if group_id is None:
            # Shouldn't happen (armed is required to reach the correlator at
            # all), but guard against a disarm racing a just-completed event.
            logger.warning("Discarding a correlated event with no open group: %s", times_us)
            return

    # Each mic already reports its reading as an elapsed delta since
    # whichever mic triggered first for this shot (see udp_listener.py), so
    # that mic's own value is 0 and this just identifies it -- it's this
    # shot's own reference/"trigger" mic, rather than always measuring from
    # one fixed mic (which could go negative if the source was closer to a
    # different mic).
    earliest_mic_id = min(times_us, key=times_us.get)

    try:
        result = tdoa.solve_tdoa(
            times_us=times_us,
            mic_positions_m=config.MIC_POSITIONS_M,
            speed_of_sound_mps=c,
            target_center_m=config.TARGET_CENTER_M,
            reference_mic_id=earliest_mic_id,
        )
    except Exception:
        logger.exception("TDOA solve failed for readings %s", times_us)
        return

    score, distance_m = tdoa.score_for_point(result.x, result.y, config.TARGET_CENTER_M, config.RING_RADII_M)

    event = {
        "id": next(_event_id_counter),
        "group_id": group_id,
        "server_time_iso": _iso_now(),
        "x_m": round(result.x, 4),
        "y_m": round(result.y, 4),
        "score": score,
        "distance_from_center_m": round(distance_m, 4),
        "residual_m": round(result.residual, 5),
        "converged": result.converged,
        "speed_of_sound_mps": c,
        "reference_mic_id": result.reference_mic_id,
        # The delta_us values as reported by the hardware (or /api/simulate),
        # kept for anyone who wants to re-crunch the numbers -- not meant for
        # on-screen display (see relative_to_trigger_ms).
        "mic_times_us": {str(k): v for k, v in times_us.items()},
        # What the UI actually shows: each mic's arrival time relative to
        # whichever mic was first to see this particular shot (0 for that
        # mic itself, >= 0 for every other mic).
        "relative_to_trigger_ms": {str(k): round(v / 1000.0, 4) for k, v in result.relative_us.items()},
        "tdoa_us": result.tdoa_s,
    }

    with _state_lock:
        _event_history.append(event)
        del _event_history[: max(0, len(_event_history) - config.MAX_EVENT_HISTORY)]

    socketio.emit("shot_event", event)
    logger.info("Shot event #%s (group %s) -> (%.3f, %.3f) m, score=%s, trigger=mic %s",
                event["id"], group_id, result.x, result.y, score, earliest_mic_id)


correlator = EventCorrelator(
    correlation_window_s=config.EVENT_CORRELATION_WINDOW_S,
    pending_ttl_s=config.PENDING_READING_TTL_S,
    required_mic_ids=set(config.MIC_POSITIONS_M.keys()),
    on_event=_on_correlated_event,
)

udp_listener = UdpListener(
    host=config.UDP_HOST,
    port=config.UDP_PORT,
    valid_mic_ids=set(config.MIC_POSITIONS_M.keys()),
    on_reading=handle_mic_reading,
)


# ---------------------------------------------------------------------------
# REST API
# ---------------------------------------------------------------------------
@app.get("/api/config")
def api_config():
    with _state_lock:
        c = _speed_of_sound_mps
        v = _voltage_threshold_v
    return jsonify({
        "mic_positions_m": {str(k): v2 for k, v2 in config.MIC_POSITIONS_M.items()},
        "target_center_m": config.TARGET_CENTER_M,
        "ring_radii_m": config.RING_RADII_M,
        "speed_of_sound_mps": c,
        "voltage_threshold_v": v,
        "nucleo_host": config.NUCLEO_HOST,
        "nucleo_port": config.NUCLEO_PORT,
        "udp_port": config.UDP_PORT,
        "target_types": config.TARGET_TYPES,
        "target_type_order": config.TARGET_TYPE_ORDER,
        "default_target_type": config.DEFAULT_TARGET_TYPE,
    })


@app.post("/api/nucleo/voltage-threshold")
def api_send_voltage_threshold():
    """Push a new mic trigger threshold to the Nucleo board. One-way: see
    _send_voltage_threshold_to_nucleo's docstring -- `sent` only reflects
    whether the UDP packet was handed off locally without a socket error."""
    global _voltage_threshold_v
    body = request.get_json(force=True, silent=True) or {}
    if "voltage_threshold_v" not in body:
        return jsonify({"error": "voltage_threshold_v is required"}), 400
    try:
        value = max(0.0, float(body["voltage_threshold_v"]))
    except (TypeError, ValueError):
        return jsonify({"error": "voltage_threshold_v must be a number"}), 400

    with _state_lock:
        _voltage_threshold_v = value
    sent = _send_voltage_threshold_to_nucleo(value)

    result = {"voltage_threshold_v": value, "sent": sent}
    socketio.emit("voltage_threshold_sent", result)
    logger.info("Voltage threshold %.3f V %s to Nucleo at %s:%s",
                value, "sent" if sent else "FAILED to send", config.NUCLEO_HOST, config.NUCLEO_PORT)
    return jsonify(result)


@app.get("/api/status")
def api_status():
    return jsonify(_system_status())


@app.post("/api/arm")
def api_arm():
    global _armed, _current_group_id, _current_group_started_at
    with _state_lock:
        if _armed:
            return jsonify({"error": "already armed"}), 409
        _armed = True
        _current_group_id = next(_group_id_counter)
        _current_group_started_at = _iso_now()
        group_id, started_at = _current_group_id, _current_group_started_at
    correlator.reset()
    socketio.emit("armed_changed", {"armed": True, "group_id": group_id, "started_at": started_at})
    logger.info("Armed -- starting group #%s", group_id)
    return jsonify(_system_status())


@app.post("/api/disarm")
def api_disarm():
    global _armed, _current_group_id, _current_group_started_at
    with _state_lock:
        if not _armed:
            return jsonify({"error": "not armed"}), 409
        closing_group_id = _current_group_id
        started_at = _current_group_started_at
        shots = [e for e in _event_history if e["group_id"] == closing_group_id]
        _armed = False
        _current_group_id = None
        _current_group_started_at = None

    correlator.reset()

    group_record = None
    if shots:
        points = [(s["x_m"], s["y_m"]) for s in shots]
        circle = geometry.minimal_enclosing_circle(points)
        cx, cy, r = circle if circle else (points[0][0], points[0][1], 0.0)
        group_record = {
            "id": closing_group_id,
            "started_at_iso": started_at,
            "ended_at_iso": _iso_now(),
            "shot_ids": [s["id"] for s in shots],
            "shot_count": len(shots),
            "center_x_m": round(cx, 4),
            "center_y_m": round(cy, 4),
            "radius_m": round(r, 4),
            "extreme_spread_m": round(geometry.extreme_spread(points), 4),
        }
        with _state_lock:
            _groups.append(group_record)
            del _groups[: max(0, len(_groups) - config.MAX_GROUP_HISTORY)]
        socketio.emit("group_closed", group_record)
        logger.info("Disarmed -- closed group #%s with %d shot(s), radius=%.3f m",
                    closing_group_id, len(shots), r)
    else:
        logger.info("Disarmed -- group #%s had no shots, discarding", closing_group_id)

    socketio.emit("armed_changed", {"armed": False, "group_id": None, "started_at": None})
    return jsonify({**_system_status(), "closed_group": group_record})


@app.get("/api/groups")
def api_groups():
    with _state_lock:
        return jsonify(list(_groups))


@app.get("/api/mics/status")
def api_mic_status():
    return jsonify(_serializable_mic_status())


@app.get("/api/events")
def api_events():
    limit = request.args.get("limit", default=200, type=int)
    with _state_lock:
        events = list(_event_history[-limit:])
    return jsonify(events)


@app.delete("/api/events")
def api_clear_events():
    with _state_lock:
        _event_history.clear()
        _groups.clear()
    socketio.emit("events_cleared", {})
    return jsonify({"status": "cleared"})


@app.patch("/api/settings")
def api_settings():
    global _speed_of_sound_mps
    body = request.get_json(force=True, silent=True) or {}
    with _state_lock:
        if "speed_of_sound_mps" in body:
            _speed_of_sound_mps = float(body["speed_of_sound_mps"])
        elif "temperature_c" in body:
            _speed_of_sound_mps = tdoa.speed_of_sound_from_temperature(float(body["temperature_c"]))
        c = _speed_of_sound_mps
    socketio.emit("settings_updated", {"speed_of_sound_mps": c})
    return jsonify({"speed_of_sound_mps": c})


@app.post("/api/simulate")
def api_simulate():
    """
    Synthesize a shot at a given (or random) point on the target, compute the
    elapsed-delta-since-first-trigger each mic would report (plus optional
    timing noise), and feed those readings through the exact same pipeline a
    real UDP packet would go through. Useful for demos and for testing the
    frontend without physical hardware. Requires the system to be armed,
    same as real hardware would.

    Optional JSON body fields: x_m/y_m (fixed impact point, else random),
    noise_us (timing jitter stddev, default 5.0).
    """
    with _state_lock:
        if not _armed:
            return jsonify({"error": "not armed"}), 409
        c = _speed_of_sound_mps

    body = request.get_json(force=True, silent=True) or {}
    cx, cy = config.TARGET_CENTER_M
    max_radius = config.RING_RADII_M[-1]

    if "x_m" in body and "y_m" in body:
        x, y = float(body["x_m"]), float(body["y_m"])
    else:
        # Random point, biased toward the target rather than uniform over
        # the bounding box, using a random angle + radius.
        r = max_radius * 1.15 * (random.random() ** 0.5)
        theta = random.uniform(0, 2 * math.pi)
        x = cx + r * math.cos(theta)
        y = cy + r * math.sin(theta)

    noise_us = float(body.get("noise_us", 5.0))  # simulated timing jitter, microseconds

    # Real mic hardware reports delta_us already relative to whichever mic
    # triggered first (see udp_listener.py) -- there's no shared absolute
    # clock to anchor to, so we compute each mic's true travel time, then
    # subtract the smallest one to get the same kind of delta a real device
    # would send.
    travel_us = {}
    for mic_id, (mx, my) in config.MIC_POSITIONS_M.items():
        dist = ((x - mx) ** 2 + (y - my) ** 2) ** 0.5
        jitter = random.gauss(0, noise_us) if noise_us > 0 else 0.0
        travel_us[mic_id] = (dist / c) * 1e6 + jitter

    t_min = min(travel_us.values())
    for mic_id, t_us in travel_us.items():
        delta_us = max(0, int(round(t_us - t_min)))
        handle_mic_reading(mic_id, delta_us)

    return jsonify({"status": "simulated", "x_m": round(x, 4), "y_m": round(y, 4)})


@app.get("/")
def index():
    return send_from_directory(app.static_folder, "index.html")


if __name__ == "__main__":
    udp_listener.start()
    logger.info("HTTP server starting on http://%s:%s", config.HTTP_HOST, config.HTTP_PORT)
    try:
        socketio.run(app, host=config.HTTP_HOST, port=config.HTTP_PORT, debug=False,
                     allow_unsafe_werkzeug=True)
    finally:
        udp_listener.stop()
