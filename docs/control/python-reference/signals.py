"""Zero-order-hold reconstruction of asynchronous signals.

Each signal holds its last observed value until the next observation *for that
signal*. That makes all five inputs defined at every instant, and makes p_net a
piecewise-constant function whose breakpoints are exactly the observation
instants -- so integrating it over an observation-free interval is exact rather
than approximate. See docs/algorithm-plan.md 1.2 and [A-17].
"""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import Mapping

from .config import Config
from .types import OPTIONAL_SIGNALS, REQUIRED_SIGNALS, Inputs, Signal

__all__ = ["Held", "SignalTable", "resolve"]

_EMPTY: Mapping[Signal, "Held"] = MappingProxyType({})


@dataclass(frozen=True, slots=True)
class Held:
    """The last observed value of one signal, and when it became true."""

    value: float
    t: float


@dataclass(frozen=True, slots=True)
class SignalTable:
    """Immutable map of Signal -> Held. Copy-on-write; at most five entries."""

    _held: Mapping[Signal, Held] = _EMPTY

    def with_reading(self, signal: Signal, value: float, t: float) -> "SignalTable":
        return SignalTable(MappingProxyType({**self._held, signal: Held(value, t)}))

    def held(self, signal: Signal) -> Held | None:
        return self._held.get(signal)

    def age(self, signal: Signal, at: float) -> float:
        """Seconds since this signal was last observed; +inf if never."""
        entry = self._held.get(signal)
        if entry is None:
            return float("inf")
        return at - entry.t

    def is_fresh(self, signal: Signal, at: float, cfg: Config) -> bool:
        entry = self._held.get(signal)
        if entry is None:
            return False
        return (at - entry.t) <= cfg.max_age(signal)

    def unhealthy(self, at: float, cfg: Config) -> frozenset[Signal]:
        """Required signals that are missing or stale at ``at``."""
        return frozenset(s for s in REQUIRED_SIGNALS if not self.is_fresh(s, at, cfg))

    def stale_optional(self, at: float, cfg: Config) -> frozenset[Signal]:
        return frozenset(s for s in OPTIONAL_SIGNALS if not self.is_fresh(s, at, cfg))

    def observed(self) -> frozenset[Signal]:
        return frozenset(self._held)


def resolve(signals: SignalTable, at: float, cfg: Config) -> Inputs | None:
    """Resolve all five signals to the instant ``at``.

    Returns ``None`` if any *required* signal is missing or stale -- the caller
    must then neither integrate nor decide. Stale *optional* gains fall back to
    ``cfg.fallback(signal)`` and are reported in ``Inputs.substituted``.
    """
    # Phrased as `not (age <= max_age)` rather than `age > max_age` so a non-finite
    # `at` counts as stale. The two are not equivalent under NaN, and the inverted form
    # would silently disagree with SignalTable.is_fresh.
    def stale(signal: Signal, entry: Held | None) -> bool:
        return entry is None or not ((at - entry.t) <= cfg.max_age(signal))

    values: dict[Signal, float] = {}
    for signal in REQUIRED_SIGNALS:
        entry = signals.held(signal)
        if stale(signal, entry):
            return None
        values[signal] = entry.value

    substituted: list[Signal] = []
    for signal in OPTIONAL_SIGNALS:
        entry = signals.held(signal)
        if stale(signal, entry):
            values[signal] = cfg.fallback(signal)
            substituted.append(signal)
        else:
            values[signal] = entry.value

    return Inputs(
        t_indoor_c=values[Signal.T_INDOOR],
        t_outdoor_c=values[Signal.T_OUTDOOR],
        p_produced_w=values[Signal.P_PRODUCED],
        p_solar_w=values[Signal.P_SOLAR],
        p_internal_w=values[Signal.P_INTERNAL],
        substituted=frozenset(substituted),
    )
