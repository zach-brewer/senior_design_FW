"""
Time-Difference-of-Arrival (TDOA) localization for a multi-microphone array
(currently 6 mics, 2 rows of 3 -- see config.MIC_POSITIONS_M), plus
target-ring scoring.

Geometry
--------
Mics sit at known (x, y) positions. A sound source at unknown position
S = (x, y) produces an arrival time t_i at mic i:

    t_i = t_0 + |S - M_i| / c

where t_0 is the (unknown) emission time and c is the speed of sound. Only
*differences* between arrival times are usable without knowing t_0:

    (t_i - t_ref) * c = |S - M_i| - |S - M_ref|

Each such equation defines a branch of a hyperbola with foci M_i, M_ref,
where M_ref is a designated reference ("trigger") mic -- whichever mic
reports the earliest time for a given shot (see server.py's
_on_correlated_event). With N mics that's N-1 equations for 2 unknowns
(x, y): exactly determined at N=3, over-determined (and more robust to
noise) for N>3. The equations are nonlinear, so we solve them with
nonlinear least squares from several initial guesses and keep the
best-fitting, physically sensible solution (target side, y > 0).

Note that `times_us` below doesn't need to be an absolute, synchronized
clock reading -- only relative differences between mics are ever used. In
this system, each mic's firmware already reports an elapsed delta since
whichever mic triggered first (see udp_listener.py), so `times_us[ref_id]`
is typically already 0; the subtraction here is harmless either way.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

import numpy as np
from scipy.optimize import least_squares


@dataclass
class TdoaResult:
    x: float
    y: float
    residual: float           # RMS residual of the fit, in meters (fit quality)
    converged: bool
    reference_mic_id: int
    tdoa_s: Dict[str, float]        # pairwise TDOA values actually used, for display
    relative_us: Dict[int, int]     # every mic's arrival time relative to the reference mic (0 for the reference itself)


def _residuals(point: np.ndarray, ref_id: int, other_ids: List[int],
               mic_pos: Dict[int, Tuple[float, float]],
               times_s: Dict[int, float], speed_of_sound_mps: float) -> np.ndarray:
    x, y = point
    ref_pos = np.array(mic_pos[ref_id])
    ref_t = times_s[ref_id]
    r_ref = np.hypot(x - ref_pos[0], y - ref_pos[1])

    out = []
    for mid in other_ids:
        pos = np.array(mic_pos[mid])
        r = np.hypot(x - pos[0], y - pos[1])
        predicted_diff = r - r_ref
        measured_diff = speed_of_sound_mps * (times_s[mid] - ref_t)
        out.append(predicted_diff - measured_diff)
    return np.array(out)


def solve_tdoa(
    times_us: Dict[int, int],
    mic_positions_m: Dict[int, Tuple[float, float]],
    speed_of_sound_mps: float,
    target_center_m: Tuple[float, float] = (0.0, 1.0),
    reference_mic_id: Optional[int] = None,
) -> TdoaResult:
    """
    Solve for the (x, y) source location given arrival times (microseconds,
    integer) at each mic id. These may be absolute (same clock reference
    across mics) or, as this system actually sends them, already-relative
    deltas from a per-shot trigger mic -- only the differences between mics
    matter to the fit.

    Requires at least 3 mics with distinct positions (not all collinear with
    a shared axis in a degenerate way) to resolve a unique 2D point. If the
    array is purely linear (all mics on one line), we additionally assume
    the source is on the target side (y > 0), since a linear array can't
    acoustically distinguish the two mirror-image locations on either side
    of the mic line; a multi-row array resolves this on its own.

    `reference_mic_id` picks which mic every other arrival time is reported
    relative to (defaults to config.REFERENCE_MIC_ID's usual role: whichever
    mic id is lowest, if not given). It only affects display/reporting
    (`tdoa_s`, `relative_us`) -- the fit itself is independent of that choice.
    """
    mic_ids = sorted(times_us.keys())
    if len(mic_ids) < 3:
        raise ValueError("Need readings from at least 3 mics to solve for (x, y)")

    ref_id = reference_mic_id if reference_mic_id in times_us else mic_ids[0]
    other_ids = [m for m in mic_ids if m != ref_id]

    times_s = {mid: t_us * 1e-6 for mid, t_us in times_us.items()}

    # A handful of initial guesses spread across the plausible target area,
    # so the nonlinear solver doesn't get stuck in a bad local minimum.
    cx, cy = target_center_m
    initial_guesses = [
        (cx, cy),
        (cx - 0.3, cy),
        (cx + 0.3, cy),
        (cx, cy + 0.3),
        (cx, cy - 0.3),
        (0.0, 0.5),
        (0.0, 2.0),
    ]

    best = None
    for guess in initial_guesses:
        try:
            sol = least_squares(
                _residuals,
                x0=np.array(guess, dtype=float),
                args=(ref_id, other_ids, mic_positions_m, times_s, speed_of_sound_mps),
                method="lm",
                max_nfev=2000,
            )
        except Exception:
            continue

        residual_rms = float(np.sqrt(np.mean(sol.fun ** 2))) if sol.fun.size else float("inf")
        candidate = (sol.x[0], sol.x[1], residual_rms, sol.success)

        if best is None:
            best = candidate
            continue

        # Prefer solutions on the target side (y > 0); among those, prefer
        # lower residual. If nothing lands on the target side, fall back to
        # the overall best residual.
        best_on_target = best[1] > 0
        cand_on_target = candidate[1] > 0
        if cand_on_target and not best_on_target:
            best = candidate
        elif cand_on_target == best_on_target and candidate[2] < best[2]:
            best = candidate

    if best is None:
        raise RuntimeError("TDOA solver failed to converge from any initial guess")

    x, y, residual_rms, converged = best

    relative_us = {mid: times_us[mid] - times_us[ref_id] for mid in mic_ids}
    tdoa_display = {f"t{mid}-t{ref_id}_us": relative_us[mid] for mid in other_ids}

    return TdoaResult(
        x=x, y=y, residual=residual_rms, converged=bool(converged),
        reference_mic_id=ref_id, tdoa_s=tdoa_display, relative_us=relative_us,
    )


def score_for_point(x: float, y: float, target_center_m: Tuple[float, float],
                     ring_radii_m: List[float]) -> Tuple[int, float]:
    """
    Return (score, distance_from_center_m). Score is len(ring_radii_m) for a
    hit inside the innermost ring, down to 1 for the outermost ring, 0 for a
    miss outside every ring.
    """
    cx, cy = target_center_m
    dist = float(np.hypot(x - cx, y - cy))
    max_score = len(ring_radii_m)
    for i, radius in enumerate(ring_radii_m):
        if dist <= radius:
            return max_score - i, dist
    return 0, dist


def speed_of_sound_from_temperature(temperature_c: float) -> float:
    """Standard dry-air approximation, m/s."""
    return 331.3 + 0.606 * temperature_c
