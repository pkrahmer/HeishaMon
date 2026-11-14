"""Controller configuration.

Five parameters come from the concept brief; the rest exist to make the time
dimension safe and are defaulted to be near-inert. See docs/control/core.md.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from types import MappingProxyType
from typing import ClassVar, Final, Mapping

from .types import OPTIONAL_SIGNALS, REQUIRED_SIGNALS, Signal

__all__ = ["Config", "DEFAULT_MAX_AGE_S"]


#: Zero-order-hold validity per signal. A uniform 600 s on every sensed signal, which is
#: what the legacy system used on every cached branch for four years. Against the real
#: cadences (indoor average 60 s, HeishaMon ~30 s, Victron ~1 s) that is 10-500x headroom.
#: Do not tighten toward the 3-5x rule of thumb without accounting for packet loss: at 5x
#: a 60 s signal, three consecutive dropped readings put the whole loop into HOLD.
DEFAULT_MAX_AGE_S: Final[Mapping[Signal, float]] = MappingProxyType(
    {
        Signal.T_INDOOR: 600.0,
        Signal.T_OUTDOOR: 600.0,
        Signal.P_PRODUCED: 600.0,
        Signal.P_SOLAR: 600.0,
        # Internal gains are commonly a static estimate rather than a measurement.
        # A live source should override this with a finite age.
        Signal.P_INTERNAL: math.inf,
    }
)

#: Value substituted for an optional signal that is absent or stale. Not zero:
#: the legacy system contributes a constant 550 W of internal gains unconditionally,
#: and dropping to 0 W would bias the balance by that much whenever the source is quiet.
DEFAULT_FALLBACK_W: Final[Mapping[Signal, float]] = MappingProxyType(
    {
        Signal.P_SOLAR: 0.0,
        Signal.P_INTERNAL: 550.0,
    }
)


@dataclass(frozen=True, slots=True)
class Config:
    """Immutable within a call. Reloading config means constructing a new value."""

    ENERGY_UNIT: ClassVar[str] = "Wh"

    # --- from the brief, valued from the legacy system's live tuning -----
    desired_indoor_c: float = 22.0
    transmission_w_per_k: float = 155.0

    #: 805 = the legacy's tuned 650 W/K plus transmission_w_per_k. The legacy references
    #: transmission to the *target* temperature, so measured indoor appears only in its
    #: trim term; we reference it to *measured* indoor, which partially cancels the trim.
    #: Adding k back preserves the legacy's effective stiffness exactly. See plan 1.9.
    adjustment_w_per_k: float = 805.0

    #: The legacy engages heating at a 1485 Wh deficit (its 0-100% gauge, after rounding).
    #: Release is at 0 rather than its +1485 Wh surplus: in a tri-state loop, releasing at
    #: +H is exactly where cooling engages. Gives ~1 cycle/h.
    hysteresis_wh: float = 1500.0

    #: Clamp on the deviation feeding the trim, in kelvin. None disables it. The output is
    #: bang-bang, so beyond the engage threshold extra trim magnitude changes nothing except
    #: how fast E gets there -- but it does bound a stuck sensor, which unclamped would
    #: inject 805 W/K x tens of kelvin of phantom demand. Legacy uses 3 K.
    trim_limit_k: float | None = 3.0

    #: Applied by the SensorSource adapter to PV **DC** power to estimate solar gain into
    #: the house -- DC rather than inverter AC, so that charging and discharging the
    #: battery cannot move a signal that is supposed to track the weather. The core never
    #: reads this -- it receives P_SOLAR already in watts -- but it belongs with the other
    #: four field-tuned constants.
    solar_gain_factor: float = 0.02

    # --- time and safety ------------------------------------------------
    #: E limit = factor * hysteresis_wh. The legacy's runaway bound is 5x its engage
    #: threshold, but it *resets* the ledger there while we *hold* the debt -- so 5x
    #: would carry 7500 Wh of debt into a recovery and dump it. Measured overshoot after
    #: a 6 h cold snap on an undersized pump: 0.8 K at 1x, 1.5 K at 2x, 2.8 K at 5x.
    #: 2x never binds in normal operation (excursions stay within H) and bounds the dump.
    energy_clamp_factor: float = 2.0
    max_step_s: float = 300.0  # longest interval still trusted for integration
    tick_interval_s: float = 60.0  # decision heartbeat; must be well under max_step_s
    integration_interval_s: float = 10.0  # advance-only tick; see Controller.advance
    input_hold_s: float = 900.0  # hold last demand across a dropout, then fail off
    state_max_age_s: float = 900.0  # snapshot freshness window
    persist_interval_s: float = 60.0

    max_age_s: Mapping[Signal, float] = DEFAULT_MAX_AGE_S
    fallback_w: Mapping[Signal, float] = DEFAULT_FALLBACK_W

    def __post_init__(self) -> None:
        # Snapshot caller-supplied mappings. Without this a plain dict passed in stays
        # aliased, and mutating it later would change controller behaviour mid-run with
        # no revalidation -- the one way this "frozen" value object could be mutated.
        for field in ("max_age_s", "fallback_w"):
            value = getattr(self, field)
            if not isinstance(value, MappingProxyType):
                object.__setattr__(self, field, MappingProxyType(dict(value)))

    @property
    def energy_limit_wh(self) -> float:
        """Anti-windup bound on |E| when both directions are permitted."""
        return self.energy_clamp_factor * self.hysteresis_wh

    def max_age(self, signal: Signal) -> float:
        return self.max_age_s.get(signal, DEFAULT_MAX_AGE_S[signal])

    def fallback(self, signal: Signal) -> float:
        """Value used for an optional signal that is absent or stale."""
        return self.fallback_w.get(signal, DEFAULT_FALLBACK_W.get(signal, 0.0))

    def validate(self) -> list[str]:
        """Return human-readable problems. Never raises; an empty list means valid."""
        problems: list[str] = []
        finite = {
            "desired_indoor_c": self.desired_indoor_c,
            "transmission_w_per_k": self.transmission_w_per_k,
            "adjustment_w_per_k": self.adjustment_w_per_k,
            "hysteresis_wh": self.hysteresis_wh,
            "energy_clamp_factor": self.energy_clamp_factor,
            "solar_gain_factor": self.solar_gain_factor,
            "max_step_s": self.max_step_s,
            "tick_interval_s": self.tick_interval_s,
            "integration_interval_s": self.integration_interval_s,
            "input_hold_s": self.input_hold_s,
            "state_max_age_s": self.state_max_age_s,
            "persist_interval_s": self.persist_interval_s,
        }
        for name, value in finite.items():
            if not math.isfinite(value):
                problems.append(f"{name} must be finite, got {value!r}")

        if not 5.0 <= self.desired_indoor_c <= 30.0:
            problems.append(f"desired_indoor_c {self.desired_indoor_c} outside 5..30 degC")
        if self.transmission_w_per_k <= 0.0:
            problems.append("transmission_w_per_k must be > 0")
        if self.adjustment_w_per_k < 0.0:
            problems.append("adjustment_w_per_k must be >= 0")
        if self.hysteresis_wh <= 0.0:
            problems.append("hysteresis_wh must be > 0")
        if self.energy_clamp_factor < 1.0:
            problems.append("energy_clamp_factor must be >= 1 (clamp must not bite inside the band)")
        if self.max_step_s <= 0.0:
            problems.append("max_step_s must be > 0")
        if self.tick_interval_s <= 0.0:
            problems.append("tick_interval_s must be > 0")
        elif self.tick_interval_s > self.max_step_s / 2.0:
            problems.append(
                f"tick_interval_s {self.tick_interval_s} must be <= max_step_s/2 "
                f"({self.max_step_s / 2.0}) or normal intervals will be skipped"
            )
        if self.integration_interval_s <= 0.0:
            problems.append("integration_interval_s must be > 0")
        elif self.integration_interval_s > self.tick_interval_s:
            problems.append(
                "integration_interval_s must be <= tick_interval_s (it exists to integrate "
                "MORE often than the loop decides, not less)"
            )
        if self.trim_limit_k is not None and (
            not math.isfinite(self.trim_limit_k) or self.trim_limit_k <= 0.0
        ):
            problems.append("trim_limit_k must be finite and > 0, or None to disable")
        if self.solar_gain_factor < 0.0:
            problems.append("solar_gain_factor must be >= 0")
        for signal, value in self.fallback_w.items():
            if not math.isfinite(value):
                problems.append(f"fallback_w[{signal.name}] must be finite, got {value!r}")
        if self.input_hold_s < 0.0:
            problems.append("input_hold_s must be >= 0")
        if self.state_max_age_s < 0.0:
            problems.append("state_max_age_s must be >= 0")
        if self.persist_interval_s <= 0.0:
            problems.append("persist_interval_s must be > 0")

        for signal in (*REQUIRED_SIGNALS, *OPTIONAL_SIGNALS):
            age = self.max_age(signal)
            if math.isnan(age) or age <= 0.0:
                problems.append(f"max_age_s[{signal.name}] must be > 0, got {age!r}")
            elif signal in REQUIRED_SIGNALS and not math.isfinite(age):
                problems.append(
                    f"max_age_s[{signal.name}] must be finite: a required signal that never "
                    "expires cannot be detected as dead"
                )
        return problems
