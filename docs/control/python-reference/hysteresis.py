"""Tri-state relay on the accumulated energy deviation, plus its anti-windup clamp.

Engage at -+H, release at 0. The deadband is 2H wide, so swapping direction costs
2H of energy travel and there is no HEATING -> COOLING edge: both directions route
through NO_DEMAND. See docs/algorithm-plan.md 1.3/1.4.

Cycle sizing:  t_on ~ H / (P_delivered - P_loss),  t_off ~ H / P_loss.
"""

from __future__ import annotations

from typing import AbstractSet

from .config import Config
from .types import Demand

__all__ = ["relay", "clamp_bounds", "clamp"]


def relay(energy_wh: float, previous: Demand, hysteresis_wh: float) -> Demand:
    """One transition at most. ``previous`` must be the demand actually emitted last."""
    if previous is Demand.HEATING:
        return Demand.NO_DEMAND if energy_wh >= 0.0 else Demand.HEATING
    if previous is Demand.COOLING:
        return Demand.NO_DEMAND if energy_wh <= 0.0 else Demand.COOLING
    if energy_wh <= -hysteresis_wh:
        return Demand.HEATING
    if energy_wh >= hysteresis_wh:
        return Demand.COOLING
    return Demand.NO_DEMAND


def clamp_bounds(allowed: AbstractSet[Demand], cfg: Config) -> tuple[float, float]:
    """Anti-windup bounds on E, tightened on any direction the supervisor has masked.

    A masked direction clamps at exactly its engage threshold, so unmasking gives an
    immediate but bounded response instead of releasing hours of stored windup.
    """
    limit = cfg.energy_limit_wh
    lo = -cfg.hysteresis_wh if Demand.HEATING not in allowed else -limit
    hi = cfg.hysteresis_wh if Demand.COOLING not in allowed else limit
    return lo, hi


def clamp(value: float, lo: float, hi: float) -> float:
    return lo if value < lo else hi if value > hi else value
