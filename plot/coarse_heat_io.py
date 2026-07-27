"""Dependency-free input validation helpers for coarse_heat_animation.py."""
from __future__ import annotations

import math
from collections.abc import Iterable


REQUIRED_COLUMNS = frozenset({
    "step", "time", "x", "y", "z", "T", "is_component",
    "vx", "vy", "vz",
})


def read_spacing(filename: str) -> tuple[float, float, float]:
    with open(filename, "r", encoding="utf-8") as stream:
        stream.readline()
        parts = stream.readline().strip().split(",")
    if (len(parts) < 6 or parts[0] != "dx"
            or parts[2] != "dy" or parts[4] != "dz"):
        raise ValueError(
            "Second CSV line must contain dx,value,dy,value,dz,value")
    spacing = float(parts[1]), float(parts[3]), float(parts[5])
    if any(not math.isfinite(value) or value <= 0.0 for value in spacing):
        raise ValueError("CSV cell spacings must be finite and positive")
    return spacing


def validate_columns(columns: Iterable[str]) -> None:
    missing = REQUIRED_COLUMNS.difference(columns)
    if missing:
        raise ValueError(f"Simulation CSV is missing columns: {sorted(missing)}")


def temperature_limits(
    values: Iterable[float],
    ambient: float | None = None,
) -> tuple[float, float]:
    finite = [float(value) for value in values if math.isfinite(float(value))]
    if not finite:
        raise ValueError("Simulation CSV contains no finite temperatures")
    low = min(finite) if ambient is None else float(ambient)
    high = max(finite)
    if not math.isfinite(low):
        raise ValueError("Ambient temperature must be finite")
    if high <= low + 1e-9:
        high = low + 1.0
    return low, high
