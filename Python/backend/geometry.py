"""
Small geometry helpers for describing a "shot group" -- all the shots fired
during one arm/disarm cycle -- as a circle on the target.

The minimum enclosing circle (the smallest circle containing every point in
the group) is the standard way to draw a shot group and report its size: its
diameter is what most people mean by "group size." The optimal circle is
always determined by either 2 points (as its diameter) or 3 points (as its
circumcircle), so checking every pair and triple and keeping the smallest
one that contains all the points is guaranteed to find the true minimum --
this is the classic O(n^4) approach rather than the optimal O(n) randomized
algorithm (Welzl's), traded here for simplicity, since shot groups are small
(a handful to a few dozen rounds), not a performance-critical path.
"""
from __future__ import annotations

import math
from typing import List, Optional, Tuple

Point = Tuple[float, float]


def _dist(a: Point, b: Point) -> float:
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _circle_from_two(a: Point, b: Point) -> Tuple[float, float, float]:
    cx, cy = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    return cx, cy, _dist(a, b) / 2


def _circle_from_three(a: Point, b: Point, c: Point) -> Optional[Tuple[float, float, float]]:
    ax, ay = a
    bx, by = b
    cx, cy = c
    d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by))
    if abs(d) < 1e-9:
        return None  # collinear -- no finite circumcircle
    ux = ((ax**2 + ay**2) * (by - cy) + (bx**2 + by**2) * (cy - ay) + (cx**2 + cy**2) * (ay - by)) / d
    uy = ((ax**2 + ay**2) * (cx - bx) + (bx**2 + by**2) * (ax - cx) + (cx**2 + cy**2) * (bx - ax)) / d
    return ux, uy, _dist((ux, uy), a)


def _contains_all(circle: Tuple[float, float, float], points: List[Point], eps: float = 1e-7) -> bool:
    cx, cy, r = circle
    return all(_dist((cx, cy), p) <= r + eps for p in points)


def minimal_enclosing_circle(points: List[Point]) -> Optional[Tuple[float, float, float]]:
    """Return (center_x, center_y, radius) for the smallest circle containing
    every point, or None for an empty input."""
    pts = list(points)
    n = len(pts)
    if n == 0:
        return None
    if n == 1:
        return pts[0][0], pts[0][1], 0.0

    best: Optional[Tuple[float, float, float]] = None

    for i in range(n):
        for j in range(i + 1, n):
            c = _circle_from_two(pts[i], pts[j])
            if _contains_all(c, pts) and (best is None or c[2] < best[2]):
                best = c

    for i in range(n):
        for j in range(i + 1, n):
            for k in range(j + 1, n):
                c = _circle_from_three(pts[i], pts[j], pts[k])
                if c and _contains_all(c, pts) and (best is None or c[2] < best[2]):
                    best = c

    return best


def extreme_spread(points: List[Point]) -> float:
    """Max pairwise distance between any two points -- the other common
    ballistic "group size" metric, independent of the enclosing circle."""
    pts = list(points)
    if len(pts) < 2:
        return 0.0
    return max(_dist(pts[i], pts[j]) for i in range(len(pts)) for j in range(i + 1, len(pts)))
