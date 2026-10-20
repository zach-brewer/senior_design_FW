# Acoustic Shot Locator (TDOA, 6-mic array)

A small full-stack system that takes arrival-time packets from 6 microphones
(2 rows of 3) over Ethernet, solves for the 2D source location by
time-difference-of-arrival (TDOA), scores it against a target's rings, and
shows it live on an interactive, target-shaped web chart.

```
tdoa-target-system/
├── backend/
│   ├── server.py         Flask + Flask-SocketIO app: UDP listener, REST API, live push
│   ├── config.py         Mic geometry, speed of sound, target rings, ports
│   ├── tdoa.py           TDOA math + ring scoring
│   ├── geometry.py       Minimum enclosing circle + extreme spread, for shot groups
│   ├── correlator.py     Groups raw mic readings into one shot event (requires all 6 mics)
│   ├── udp_listener.py   Raw UDP socket -> parsed (mic_id, delta_us)
│   └── requirements.txt
├── frontend/
│   ├── index.html
│   ├── style.css
│   ├── app.js            Plotly target chart + live mic readout, over Socket.IO
│   └── targets/          Target background images (see "Target images" below)
├── tools/
│   └── simulate_udp_sender.py   Sends real UDP test packets (no hardware needed)
├── arduino/
│   └── fake_mic_sender/
│       └── fake_mic_sender.ino   Arduino GIGA R1 + W5500 shield: sends fake mic UDP packets
└── README.md
```

## Quick start

```bash
cd backend
python -m venv .venv && source .venv/bin/activate   # optional but recommended
pip install -r requirements.txt
python server.py
```

Open **http://localhost:8000**. You'll see the target, 6 empty mic time
readouts, and an "Arm system" button. The system ignores everything until
armed — click it, then "Simulate shot" exercises the whole pipeline without
any hardware. Disarm when you're done firing to close out the shot group
(see **Arm/disarm & shot groups** below).

Plotly and Socket.IO's client library are vendored under `frontend/vendor/`
(not loaded from a CDN), so the page works with no internet access at all —
handy on a locked-down lab or range network. Swap those two files if you
want a newer release.

To test the *actual* network path (real UDP packets, from another machine or
process), in a second terminal:

```bash
cd tools
python simulate_udp_sender.py --host 127.0.0.1 --port 5005 --x 0.02 --y 1.18
# or fire a burst of random shots:
python simulate_udp_sender.py --random --count 10 --interval 2 --jitter-us 8
```

## How it works

**Coordinate system.** 6 mics in 2 rows of 3. The front row (mics 1-3) sits
on a line at `y = 0`, 1 meter apart, centered on `x = 0`: mic 1 at `(-1, 0)`,
mic 2 at `(0, 0)`, mic 3 at `(1, 0)`. The back row (mics 4-6) is the same
layout, `ROW_SPACING_M` behind it (default 0.1524 m, i.e. ~0.5 ft): mic 4 at
`(-1, -0.1524)`, mic 5 at `(0, -0.1524)`, mic 6 at `(1, -0.1524)`. The target
face is above both rows at positive `y` (default center `(0, 1.2)` — edit
`TARGET_CENTER_M` in `backend/config.py` to match your physical setup). All
of this — mic spacing, row spacing, target center, ring radii, speed of
sound — is defined once in `backend/config.py`; if your hardware numbers or
places the 6 mics differently, just edit `MIC_POSITIONS_M` to match.

**Trigger / reference mic.** There's no fixed reference mic, and no shared
absolute clock either. Each mic's own hardware reports its reading as an
*elapsed delta since whichever mic triggered first* for that shot (see
"Network protocol" below) — so the trigger mic is simply whichever one
reports `delta_us = 0`, and that's identified in `server.py` with
`earliest_mic_id = min(times_us, key=times_us.get)`. Every other mic's value
is already `>= 0` by construction — never negative, since nothing can arrive
before whichever mic saw it first. Concretely: if the source is closer to
mic 5 than any other mic, mic 5 is that shot's trigger and gets the
"TRIGGER" badge; the next shot might be closer to mic 2 instead, and the
badge moves there. The UI, REST API, and CSV export all show these
already-relative values directly (as milliseconds) — there's no absolute
epoch timestamp anywhere in this system to begin with.

**TDOA solve.** Given each mic's elapsed time relative to the trigger mic,
that time constrains the source to a branch of a hyperbola with that mic and
the trigger as foci. `backend/tdoa.py` solves the resulting nonlinear system
(5 equations, 2 unknowns — over-determined, which makes the fit more robust
to per-mic timing noise than an exactly-determined 3-mic case) with
`scipy.optimize.least_squares`, retried from several starting points,
keeping the best-fitting solution on the target side (`y > 0`).

**Event correlation.** Mics report independently, one UDP packet per
detection — but only once a mic's own voltage threshold has actually been
crossed. That gating happens entirely on the downstream **Nucleo board**:
it holds a 0.2V threshold per mic and simply never sends a packet for a mic
that hasn't crossed it, so the backend never sees voltage and doesn't need
to. `backend/correlator.py` buffers the latest reading per mic and fires a
"shot event" only once **all 6** have reported within
`EVENT_CORRELATION_WINDOW_S` (default 0.5 s of wall-clock arrival at the
server — generous for network jitter, since it's unrelated to the acoustic
travel time itself, which is only a few milliseconds at this scale). A mic
that's dead, unplugged, or whose signal never crosses its threshold for a
given shot simply blocks that shot from ever correlating — see
**Limitations** below if you'd rather relax that to "best N of 6."

**Scoring.** The computed point's distance from the target center is compared
against `RING_RADII_M` in `config.py` to produce a 10-down-to-1 (or 0 for a
miss) score, the way a scored paper target works. Adjust the ring list to
match your target.

## Arm/disarm & shot groups

The system doesn't passively listen all the time. It starts **idle**: the
UDP listener still receives packets at the socket level, but
`handle_mic_reading` throws them away immediately if the system isn't armed
— no mic status update, no correlation, nothing reaches the frontend. `POST
/api/arm` flips it to **armed** and opens a new shot group; `POST
/api/disarm` flips it back to idle and closes that group. The frontend's
"Arm system" / "Disarm system" button drives this, and `GET /api/status`
reports the current state (handy if you refresh the page mid-session).

Every shot fired while armed is tagged with the currently-open group's id.
When you disarm, the backend takes every shot in that group and computes the
**minimum enclosing circle** — the smallest circle containing every shot in
the group (`geometry.py`) — plus the **extreme spread** (the largest
distance between any two shots in the group, the other common way to
describe a shot group's size). If a group has zero shots (you armed and
disarmed without firing), it's just discarded rather than saved. `GET
/api/groups` lists every closed group; `DELETE /api/events` clears both the
shot log and the group list together.

The frontend's "Shot groups" panel lists each closed group, draws its
enclosing circle on the target, and lets you click a group to isolate it:
that group's shots and circle stay at full strength, and every other shot
and circle fades to near-transparent, so one string of fire is easy to pick
out of a long session. Click "Show all" (or the same group again) to go back
to the normal, undimmed view.

Since arming starts a brand-new group and disarming closes it, `/api/simulate`
also requires the system to be armed (real hardware obviously can't send
data while the backend isn't listening either), and the frontend grays out
"Simulate shot" / "Click target to place shot" while idle.

## Network protocol (what your mic hardware needs to send)

The backend listens for **UDP** datagrams on port `5005` by default
(`UDP_PORT` in `config.py`). Each datagram is one UTF-8 JSON object:

```json
{"mic_id": 1, "delta_us": 0}
```

- `mic_id`: integer `1` through `6` (must match `MIC_POSITIONS_M` in `config.py`).
- `delta_us`: integer microseconds **elapsed since whichever mic in this shot
  triggered first** — not an absolute timestamp. The mic that triggered
  first sends `0`; every other mic sends how much later it crossed its own
  threshold. Send one packet per mic per detected acoustic event (e.g. per
  bullet impact / gunshot).

There's no voltage field, and the backend doesn't want one. Each mic's 0.2V
trigger threshold is applied entirely on the downstream **Nucleo board** —
it only emits a `delta_us` reading for a mic once that mic's analog signal
has actually crossed threshold, so a below-threshold mic simply never sends
a packet at all. The backend requires a reading from **all 6 mics** before
it will solve a shot (see "Event correlation" above); if your Nucleo logic
or wiring changes (e.g. a different threshold, or fewer/more mics), update
`MIC_POSITIONS_M` in `config.py` to match, but no other backend change is
needed.

**Timing requirement, and why it's simpler than it sounds.** Because
`delta_us` is already relative to a per-shot trigger, there's no shared
*absolute* clock to maintain across mics — you do not need GPS/PPS-
disciplined clocks or PTP. What you do need is a way for every mic to know
*when the first mic triggered* (typically a shared hardware trigger/fan-out
line that every mic's detector board watches alongside its own analog
threshold comparator) and an accurate **local** elapsed-time count from that
shared edge to its own threshold crossing. At 1 m mic spacing, 1 µs of local
timing error is worth about 0.3 mm of position error, which is generally
fine; a few milliseconds of error will visibly wreck the solve. If your mics
are ESP32/Arduino-class boards, a shared trigger line into an interrupt pin
on each board, paired with a free-running hardware timer/counter for the
local elapsed-time measurement, is the simplest robust option. (This is a
different thing from arm/disarm, which is about when the system accepts
data at all, not about this per-shot triggering.)

If you need a different transport (TCP instead of UDP, a different payload
shape, MQTT, etc.), the only file that needs to change is
`backend/udp_listener.py` — it just needs to end up calling
`handle_mic_reading(mic_id, delta_us)` in `server.py` for every reading.

## Nucleo threshold push (sending the 0.2V setting *to* the hardware)

The Nucleo board — not this backend — owns the actual per-mic trigger
threshold described above (default 0.2V) and applies it locally; this
backend never receives voltage readings, and that part isn't changing. But
the Nucleo's threshold still needs to be *settable* from the website, so you
don't have to reflash it every time you want to try a different value.

The website's arm bar has a **"Mic trigger threshold"** field and a **"Send
to Nucleo"** button (press Enter in the field works too). Clicking it does
one thing: the backend fires a single one-way UDP packet —

```json
{"voltage_threshold_v": 0.35}
```

— at `NUCLEO_HOST:NUCLEO_PORT` (set these in `backend/config.py` to your
Nucleo board's actual listening IP/port; they default to placeholder values
that won't reach real hardware). This is **fire-and-forget**: nothing is
read back from the Nucleo, so a successful response (`"sent": true`) only
means the UDP packet left this machine without a local socket error — not
that the Nucleo actually received it, parsed it, or applied it. UDP `sendto`
generally succeeds even if nothing is listening on the other end at all, so
don't treat `"sent": true` as delivery confirmation; check the Nucleo side
directly if you need to be sure.

The exact packet format above is a starting point, not a fixed protocol —
since the Nucleo-side firmware is yours to write, adjust the JSON shape (or
drop JSON entirely for a raw value) in `_send_voltage_threshold_to_nucleo()`
in `backend/server.py` to match whatever your Nucleo's UDP listener actually
expects to parse.

The website's field just remembers the last value *it* sent (or
`DEFAULT_VOLTAGE_THRESHOLD_V` from `config.py`, before anything's been sent
this session) — it is a convenience, not a live mirror of the Nucleo's
actual current threshold. If someone changes the threshold directly on the
board, the website has no way to know and will happily show a stale value
until someone sends a new one from the UI.

## Target images (showing the real target instead of abstract rings)

The website can show an actual picture of the physical target sheet behind
the shot plot, instead of (or underneath) the plain scoring-ring circles, so
bullet holes are easier to compare against the real target. Two are included
out of the box: **530-O** (a 5-bull qualification sheet) and **B-27 PRO-G**
(a silhouette qualification target), both 23"x35". A **"Target"** dropdown
above the chart switches between them and plain rings; switching refits the
view to the new target's full extent.

**This is purely visual — it does not change scoring.** Whichever target
image is showing (or none), scoring always uses `RING_RADII_M` /
`TARGET_CENTER_M` in `config.py`, same as before this feature existed. When
an image is active, those same rings are drawn as thin dotted outlines on
top of the photo (rather than the opaque banded discs used in plain-rings
mode, which would otherwise completely hide the image underneath). Matching
each target's own real printed scoring zones (e.g. the B-27's actual
X/10/9/8/7 rings and point values, or the fact that the 530-O has no scoring
rings printed on it at all) is a reasonable next step but isn't implemented
here — see **Limitations** below.

**Adding your own target images.** Drop a `.webp`, `.png`, or `.jpg` file
into `frontend/targets/`, then add an entry to `TARGET_TYPES` in
`backend/config.py`:

```python
"my-target": {
    "label": "My Target — whatever you want shown in the dropdown",
    "image_url": "targets/my-target.webp",
    "width_m": 23.0 * IN_TO_M,   # the sheet's real physical width
    "height_m": 35.0 * IN_TO_M,  # and height, in inches here (IN_TO_M converts)
},
```

Also add its key to `TARGET_TYPE_ORDER` (controls the dropdown's order —
needed because JSON object key order isn't reliable, and this backend's
`jsonify` sorts keys alphabetically anyway). The image is placed centered on
`TARGET_CENTER_M` and scaled to exactly `width_m` x `height_m`, so getting
those two numbers right (matching the sheet's real printed dimensions) is
what keeps it correctly scaled against the mic geometry — an image with the
wrong aspect ratio for its `width_m`/`height_m` will appear stretched.

## Testing the network path with an Arduino

`arduino/fake_mic_sender/fake_mic_sender.ino` is a sketch for an **Arduino
GIGA R1 WiFi** that sends fake mic packets over wired Ethernet, so you can
confirm the backend's IP/port/UDP handling, arm/disarm gating, and the live
website all work before any real mic hardware is involved.

The GIGA R1 WiFi has no built-in wired Ethernet (only onboard Wi-Fi), so this
sketch targets a **W5500-based Ethernet shield or breakout** wired over SPI,
using the classic Arduino `Ethernet` library (Library Manager → "Ethernet" by
Arduino, v2.0.0+). It auto-detects the W5500 chip.

What it does: every few seconds it invents a random impact point near the
target, computes the true acoustic travel time from that point to each of the
6 configured mic positions (same geometry + speed of sound as
`backend/config.py`), subtracts the smallest one so every mic's value is a
delta since the first to trigger, adds a little timing jitter, and sends one
UDP JSON packet per mic — exactly the `{"mic_id": ..., "delta_us": ...}`
format above — to the backend. It's the on-hardware equivalent of
`tools/simulate_udp_sender.py --random`. It always sends all 6 mics, since
the real system never would have gotten a packet from a mic in the first
place if that mic hadn't crossed its threshold on the Nucleo board.

Before uploading, edit the block near the top of the `.ino` marked
`EDIT THESE`: your Arduino's static IP, the backend PC's actual LAN IP
(not `127.0.0.1`/`0.0.0.0`), the UDP port (must match `UDP_PORT`), and the
Ethernet chip-select pin if your wiring doesn't use the default (pin 10).

**Arm the website before powering on the sketch** — the backend silently
drops all UDP packets while disarmed, so if you forget this step it'll look
like nothing is working when the network is actually fine.

## REST API

| Method & path | Purpose |
|---|---|
| `GET /api/config` | Mic positions, target center/rings, current speed of sound, last-sent voltage threshold, Nucleo host/port, UDP port, available target images |
| `GET /api/status` | `{armed, current_group_id, current_group_started_at, current_group_shot_count}` |
| `POST /api/arm` | Start accepting data; opens a new shot group. 409 if already armed. |
| `POST /api/disarm` | Stop accepting data; closes the open group (computing its circle) if it has shots. 409 if already idle. |
| `GET /api/groups` | Every closed shot group (id, shot ids, enclosing circle, extreme spread, timestamps) |
| `GET /api/mics/status` | Last `delta_us` + age per mic |
| `GET /api/events?limit=200` | Recent computed shot events (each tagged with its `group_id`) |
| `DELETE /api/events` | Clear the shot log *and* the group list |
| `PATCH /api/settings` | `{"speed_of_sound_mps": 344}` or `{"temperature_c": 22}` — no longer exposed in the UI (see **Frontend**), but still here if you want to script a change |
| `POST /api/simulate` | `{}` for a random point, or `{"x_m":0.02,"y_m":1.18,"noise_us":5}`. Requires the system to be armed; 409 otherwise. |
| `POST /api/nucleo/voltage-threshold` | `{"voltage_threshold_v": 0.35}` — fires a one-way UDP packet at the Nucleo board (see **Nucleo threshold push**). `{"sent": true/false}` reflects only whether the local socket call succeeded, not delivery. |

Live updates (`mic_reading`, `shot_event`, `armed_changed`, `group_closed`,
`settings_updated`, `voltage_threshold_sent`, `events_cleared`) are pushed
over Socket.IO so the page never has to poll.

## Frontend

Open `frontend/index.html` via the Flask server (it's served at `/`, not
meant to be opened as a standalone file, since it needs the API + WebSocket).
It shows:

- An arm bar at the top: one button to arm/disarm, a live status line
  ("Armed — group #4, 3 shots" / "Idle — not receiving"), and a status dot.
  "Simulate shot" and "Click target to place shot" are disabled while idle.
- A settings bar just below it with the **"Mic trigger threshold"** field and
  "Send to Nucleo" button described in **Nucleo threshold push** above. This
  is a send-only control — the website never receives or displays a mic's
  actual voltage, only the threshold value it last told the Nucleo to use.
- A **"Target"** dropdown above the chart (see **Target images** above) to
  switch the chart's background between plain scoring rings and an actual
  target photo. This choice is local to the browser tab/page load — it
  resets to `DEFAULT_TARGET_TYPE` on refresh and isn't synced between
  multiple open tabs, since it's a display preference, not shared state.
- Six always-visible mic cards (2 rows of 3, matching the physical array),
  each showing that mic's **last arrival time relative to that shot's own
  trigger mic** in large monospace type (e.g. `0.602 ms`, or `0.000 ms` on
  whichever mic was first, badged "TRIGGER" — the badge moves to a different
  card from shot to shot), plus a live "how long ago" indicator.
- An interactive Plotly target chart — scroll to zoom, drag to pan, click a
  shot marker to inspect its exact numbers, or use "Click target to place
  shot" to fire a simulated shot at any point you click. The mic markers on
  the chart are drawn at their real 2D positions, so the two rows show up
  exactly as they're physically mounted. Closed shot groups are drawn as
  circles on the chart.
- A "Shot groups" panel listing every closed group (shot count, spread,
  when it ended); click one to isolate it — its shots and circle stay at
  full strength, everything else fades to near-transparent — or "Show all"
  to go back to the normal view.
- A shot log with group, score, position, and time for every event; click a
  row to select it; a detail panel showing each mic's arrival time (relative
  to that shot's trigger mic, same as the live cards) for the selected shot;
  and a CSV export with the group id, trigger mic id, and both the
  relative-ms and raw delta-microsecond values per mic.

There's no speed-of-sound control in the UI — `DEFAULT_SPEED_OF_SOUND_MPS`
in `config.py` is fixed for this installation. Edit that constant (or use
`PATCH /api/settings`, see the REST API table) if your conditions ever
change.

## Limitations / next steps

- **Single-shot-at-a-time correlation.** The correlator matches "the latest
  reading per mic," so two real shots landing within the correlation window
  of each other could be mismatched. If you need rapid-fire support, extend
  `correlator.py` to match by a shared `event_id`/sequence number your mic
  firmware assigns, or narrow `EVENT_CORRELATION_WINDOW_S` to match your
  actual network jitter.
- **Mirror-image ambiguity** was a real problem with the original single-row,
  3-mic array (acoustically identical to its reflection across the mic
  line) and is now mostly resolved by having mics off-axis in a second row —
  the solver still prefers the target-side (`y > 0`) solution as a
  tie-breaker, but with 6 non-collinear mics it rarely needs to.
- The dev server (`socketio.run(...)`) is fine for a lab/range setup on a
  trusted network; put it behind a real WSGI/ASGI server (gunicorn +
  eventlet, or uvicorn with an ASGI shim) if you need to expose it more
  broadly.
- **Target images use generic scoring, not each target's real scoring
  zones.** The B-27 PRO-G's printed rings (X/10/9/8/7, worth 5/5/5/5/4/3/0
  points) and the 530-O's complete lack of scoring rings aren't reflected —
  every target type scores against the same generic `RING_RADII_M` list.
  Making scoring target-specific would mean adding a per-target-type ring
  list (and point values) in `config.py` and having `tdoa.py`'s scoring step
  key off the selected target type instead of a single global list; since
  target type is currently a frontend-only, unsynced display choice (see
  **Frontend**), it would also need to become real shared state the backend
  knows about, not just a local dropdown.
- **The 530-O's 5 bulls aren't individually trackable.** All 5 are shown on
  one sheet, but a shot's score/position is still computed against the one
  `TARGET_CENTER_M` for the whole system — there's no way to say "this shot
  belongs to bull #3." Fine for eyeballing where impacts land relative to
  the sheet, but if you need per-bull stats, that's a bigger change (likely:
  pick the nearest bull center to each shot, which would need each bull's
  real position on the sheet added to config).

## Troubleshooting

**`pip install -r requirements.txt` tries to compile scipy and fails**
(commonly on Windows, with errors about `meson`/`ifort`/`gfortran` not being
found). This happens when your Python version is newer than any published
precompiled ("wheel") build of the exact numpy/scipy versions pinned here, so
pip falls back to building from source — which needs a Fortran compiler you
probably don't have. `requirements.txt` pins numpy/scipy with `>=` rather
than `==` for this reason, so a plain `pip install -r requirements.txt` should
resolve to a newer release with a ready-made wheel. If it still tries to
build from source (e.g. on a very new Python release before wheels catch up),
and you're using conda/miniconda, install those two packages from conda
first, then let pip handle the rest:

```bash
conda install -y numpy scipy
pip install -r requirements.txt
```
