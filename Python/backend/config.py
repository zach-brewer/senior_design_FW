"""
Central configuration for the TDOA acoustic shot-locating system.

Everything a physical installer would need to tune lives here: microphone
geometry, the speed of sound, the target's position relative to the mic
array, and the scoring rings drawn on it.
"""

# ---------------------------------------------------------------------------
# Microphone array
# ---------------------------------------------------------------------------
# Six microphones in two rows of three. Each row is a 1-meter-spaced line
# (like the original 3-mic array); the two rows sit ROW_SPACING_M apart.
# Coordinates are meters, in a 2D plane: x = horizontal position, y = height
# above the *front* row. The front row (mics 1-3) sits at y = 0, directly
# under the target; the back row (mics 4-6) sits behind it at negative y.
# The target face is above both rows, at positive y.
#
# Numbering: 1/2/3 are the front row, left to right; 4/5/6 are the back row,
# left to right, aligned with 1/2/3. Wire your hardware's mic_id to match
# this layout (or just edit the coordinates below to match however you
# actually number and place them).
#
#      target (above, at TARGET_CENTER_M)
#          ^
#   4   5   6     <- back row  (y = -ROW_SPACING_M)
#   1   2   3     <- front row (y = 0)
#
# mic_id MUST match the "mic_id" field in incoming packets.
MIC_SPACING_M = 1.0        # spacing between mics within a row
ROW_SPACING_M = 0.1524     # ~0.5 ft between the front and back rows

MIC_POSITIONS_M = {
    1: (-MIC_SPACING_M, 0.0),
    2: (0.0, 0.0),
    3: (MIC_SPACING_M, 0.0),
    4: (-MIC_SPACING_M, -ROW_SPACING_M),
    5: (0.0, -ROW_SPACING_M),
    6: (MIC_SPACING_M, -ROW_SPACING_M),
}

# NOTE on the "trigger" mic: there is no fixed reference mic id. Each mic's
# own firmware reports its reading as an elapsed time *since whichever mic
# triggered first* (see "Network protocol" below) -- so the trigger mic is
# simply whichever one reports 0, and that can differ from shot to shot
# depending on where the source was.
#
# NOTE on voltage thresholding: each mic's analog front end is gated by a
# 0.2V trigger threshold entirely on the downstream Nucleo board -- a mic
# that doesn't cross it simply never sends a packet at all. This backend
# never sees raw voltage and doesn't need to: every packet it receives has,
# by construction, already cleared the threshold. See "Network protocol" and
# "Event correlation" below -- a shot isn't solved unless all 6 mics report.

# ---------------------------------------------------------------------------
# Nucleo (mic trigger threshold push)
# ---------------------------------------------------------------------------
# The website can push a new trigger threshold to the Nucleo board (the
# "Mic trigger threshold" control + POST /api/nucleo/voltage-threshold) so
# it's adjustable without reflashing. This is a one-way UDP packet to the
# Nucleo's own listening address -- edit these to match your setup.
NUCLEO_HOST = "192.168.1.60"   # <-- set to your Nucleo board's IP
NUCLEO_PORT = 5006             # <-- whatever port its listener binds

# Default/initial value shown in the website's threshold control before you
# change it. This is a UI convenience only -- nothing reads the Nucleo's
# actual current threshold back, so there's no way for the backend to know
# if it's drifted from this (e.g. someone changed it directly on the board).
DEFAULT_VOLTAGE_THRESHOLD_V = 0.2

# ---------------------------------------------------------------------------
# Target images (visual background only -- does NOT affect scoring)
# ---------------------------------------------------------------------------
# Lets the website show an actual picture of the physical target sheet behind
# the shot plot, instead of (or underneath) the abstract scoring rings below,
# so bullet holes are easier to compare against the real target. This is
# purely visual: scoring always uses RING_RADII_M / TARGET_CENTER_M below, no
# matter which target image (if any) is selected on the website. Matching a
# target's own real printed scoring zones per target type is a possible
# future enhancement, not implemented here.
#
# Each image is drawn centered on TARGET_CENTER_M, scaled to its real-world
# physical size (width_m/height_m) so it lines up correctly against the mic
# geometry. Images live in frontend/targets/ and are served as static files
# (Flask serves all of frontend/ at the site root -- see server.py).
IN_TO_M = 0.0254

TARGET_TYPES = {
    "rings": {
        "label": "Scoring rings only (no target image)",
        "image_url": None,
    },
    "530-o": {
        "label": "530-O — 5-bull qualification sheet",
        "image_url": "targets/530-O.webp",
        "width_m": 23.0 * IN_TO_M,
        "height_m": 35.0 * IN_TO_M,
    },
    "b27-pro-g": {
        "label": "B-27 PRO-G — silhouette qualification target",
        "image_url": "targets/B-27-PRO-G.webp",
        "width_m": 23.0 * IN_TO_M,
        "height_m": 35.0 * IN_TO_M,
    },
}

# The order the website's target dropdown lists these in. Kept as an
# explicit list (rather than relying on TARGET_TYPES' key order) because
# Flask's jsonify sorts object keys alphabetically by default, which would
# otherwise scramble the dropdown order ("530-o" / "b27-pro-g" / "rings").
TARGET_TYPE_ORDER = ["rings", "530-o", "b27-pro-g"]

# Shown on first page load, before anyone's picked something from the
# dropdown. This is a UI default only -- nothing is persisted server-side,
# so a page refresh always starts back here (see "Target images" in the
# README for why that's intentional).
DEFAULT_TARGET_TYPE = "rings"

# ---------------------------------------------------------------------------
# Acoustics
# ---------------------------------------------------------------------------
# Speed of sound in air, m/s. Fixed for this installation -- there's no UI
# control for it since it doesn't change for this setup. Edit this constant
# if your conditions change; PATCH /api/settings still exists if you ever
# want to script a change (e.g. from a temperature sensor) without a restart.
DEFAULT_SPEED_OF_SOUND_MPS = 343.0

# ---------------------------------------------------------------------------
# Target geometry (for plotting + scoring)
# ---------------------------------------------------------------------------
# Center of the target face, in the same (x, y) frame as the mics.
TARGET_CENTER_M = (0.0, 1.2)

# Ring radii, meters, innermost first. Score N (10-ring target here) is
# awarded for a hit inside RING_RADII_M[0], score N-1 for inside
# RING_RADII_M[1], and so on down to 1; anything outside the last ring is a
# miss (score 0). Tune these to match your physical target.
RING_RADII_M = [
    0.025, 0.050, 0.075, 0.100, 0.125,
    0.150, 0.175, 0.200, 0.250, 0.300,
]

# ---------------------------------------------------------------------------
# Networking
# ---------------------------------------------------------------------------
UDP_HOST = "0.0.0.0"
UDP_PORT = 5005

HTTP_HOST = "0.0.0.0"
HTTP_PORT = 8000

# ---------------------------------------------------------------------------
# Event correlation
# ---------------------------------------------------------------------------
# Readings from all 6 mics are grouped into one "event" (one shot) if they
# arrive at the *server* within this many seconds of each other. This just
# needs to be bigger than network jitter and the max real TDOA (a few
# milliseconds at this scale) -- it is NOT the acoustic delay itself.
#
# All 6 mics are required -- there's no partial/below-threshold case to
# tolerate here, since the Nucleo board only ever sends a reading for a mic
# that already crossed the 0.2V threshold. One dead/unplugged mic will
# simply block every shot event; see correlator.py / README "Limitations"
# if you need to relax that.
EVENT_CORRELATION_WINDOW_S = 0.5

# A pending reading older than this (wall-clock, at the server) is dropped
# rather than matched, so a mic that never reports back doesn't silently
# glue a later, unrelated reading into a stale event.
PENDING_READING_TTL_S = 2.0

# How many computed shot events to keep in memory / send to the frontend.
MAX_EVENT_HISTORY = 500

# How many closed shot groups (one arm/disarm cycle each) to keep.
MAX_GROUP_HISTORY = 200
