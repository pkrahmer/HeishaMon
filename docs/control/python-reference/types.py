"""Value objects for the control core.

Everything here is immutable and free of behaviour beyond trivial derivation.
No module in ``heatnrgx.core`` may import I/O, a clock, or a logger.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from enum import Enum
from typing import Final

__all__ = [
    "Demand",
    "Phase",
    "Reason",
    "Signal",
    "ALL_DEMANDS",
    "REQUIRED_SIGNALS",
    "OPTIONAL_SIGNALS",
    "Reading",
    "Inputs",
    "Balance",
    "Decision",
    "EnergyLedger",
    "StateSnapshot",
    "SNAPSHOT_VERSION",
]


class Demand(Enum):
    """The controller's tri-state output."""

    HEATING = "heating"
    COOLING = "cooling"
    NO_DEMAND = "no_demand"


class Phase(Enum):
    """Controller lifecycle. See docs/algorithm-plan.md 1.6/1.7."""

    UNINITIALIZED = "uninitialized"  # waiting for the first complete set of required inputs
    RUNNING = "running"  # inputs healthy, integrating
    HOLD = "hold"  # inputs stale, last demand held, integral frozen
    FAULT = "fault"  # inputs stale beyond input_hold_s, output forced off


class Reason(Enum):
    """Why a decision came out the way it did. Diagnostics only; never control flow."""

    NORMAL = "normal"
    SEEDED = "seeded"
    WAITING_FOR_INPUTS = "waiting_for_inputs"
    MASKED = "masked"
    HELD_STALE_INPUT = "held_stale_input"
    FAULT_TIMEOUT = "fault_timeout"
    FAULT_RECOVERED = "fault_recovered"

    #: The caller passed a non-finite instant. The call was ignored entirely and no state
    #: changed. Always an adapter bug -- log it as an error.
    INVALID_TIME = "invalid_time"


class Signal(Enum):
    """The five measured inputs. Each arrives independently, at its own cadence."""

    T_INDOOR = "t_indoor_c"
    T_OUTDOOR = "t_outdoor_c"
    P_PRODUCED = "p_produced_w"
    P_SOLAR = "p_solar_w"
    P_INTERNAL = "p_internal_w"


ALL_DEMANDS: Final[frozenset[Demand]] = frozenset(Demand)

#: Missing or stale => the loop cannot integrate and cannot decide. See [A-20].
REQUIRED_SIGNALS: Final[tuple[Signal, ...]] = (
    Signal.T_INDOOR,
    Signal.T_OUTDOOR,
    Signal.P_PRODUCED,
)

#: Missing or stale => substituted with 0 W; the trim term repays the bias. See [A-20].
OPTIONAL_SIGNALS: Final[tuple[Signal, ...]] = (Signal.P_SOLAR, Signal.P_INTERNAL)

SNAPSHOT_VERSION: Final[int] = 1


@dataclass(frozen=True, slots=True)
class Reading:
    """One measurement of one signal at one instant.

    ``t`` is monotonic seconds and is the instant the value became true — not the
    instant it was delivered. Transport latency must not be folded into it.
    """

    signal: Signal
    value: float
    t: float


@dataclass(frozen=True, slots=True)
class Inputs:
    """All five signals resolved to a single instant by zero-order hold."""

    t_indoor_c: float
    t_outdoor_c: float
    p_produced_w: float
    p_solar_w: float
    p_internal_w: float
    substituted: frozenset[Signal] = frozenset()


@dataclass(frozen=True, slots=True)
class Balance:
    """The instantaneous energy balance, in watts. Negative net = deficit = wants heat."""

    p_transmission_w: float
    p_model_w: float
    p_trim_w: float
    p_net_w: float


@dataclass(frozen=True, slots=True)
class EnergyLedger:
    """Lifetime Wh contributed by each term of the balance.

    The legacy system keeps exactly this breakdown and it is the instrumentation
    you need to identify ``transmission_w_per_k`` and the gain factors from field
    data: over a long window at steady temperature, the terms must sum to roughly
    zero, and how they fail to is the model error. ``total`` tracks the
    accumulator before clamping, so a persistent gap between ``total`` and the
    reported ``energy_wh`` means the anti-windup clamp is biting.
    """

    produced_wh: float = 0.0
    solar_wh: float = 0.0
    internal_wh: float = 0.0
    transmission_wh: float = 0.0
    trim_wh: float = 0.0

    @property
    def total_wh(self) -> float:
        return (
            self.produced_wh
            + self.solar_wh
            + self.internal_wh
            + self.transmission_wh
            + self.trim_wh
        )

    def accumulate(self, inputs: "Inputs", balance: "Balance", hours: float) -> "EnergyLedger":
        return EnergyLedger(
            produced_wh=self.produced_wh + inputs.p_produced_w * hours,
            solar_wh=self.solar_wh + inputs.p_solar_w * hours,
            internal_wh=self.internal_wh + inputs.p_internal_w * hours,
            transmission_wh=self.transmission_wh + balance.p_transmission_w * hours,
            trim_wh=self.trim_wh + balance.p_trim_w * hours,
        )

    def as_dict(self) -> dict[str, float]:
        return {
            "produced": self.produced_wh,
            "solar": self.solar_wh,
            "internal": self.internal_wh,
            "transmission": self.transmission_wh,
            "trim": self.trim_wh,
        }


@dataclass(frozen=True, slots=True)
class Decision:
    """The controller's output plus everything needed to explain it at 2am."""

    t: float
    demand: Demand
    reason: Reason
    phase: Phase
    energy_wh: float
    hysteresis_wh: float
    dt_s: float = 0.0  # interval closed by this call alone

    #: Lifetime totals, not per-call: seconds of elapsed time that were integrated,
    #: and seconds that were advanced over without integrating. Per-call deltas are
    #: meaningless because readings close intervals out of band, between ticks.
    #: ``skipped_s`` rising during normal operation means the heartbeat is too slow
    #: or a source is failing.
    integrated_s: float = 0.0
    skipped_s: float = 0.0

    balance: Balance | None = None
    inputs: Inputs | None = None
    ledger: "EnergyLedger | None" = None
    clamped: bool = False  # accumulator pinned at its anti-windup bound
    unhealthy: frozenset[Signal] = frozenset()
    substituted: frozenset[Signal] = frozenset()

    @property
    def engage_heating_at_wh(self) -> float:
        return -self.hysteresis_wh

    @property
    def engage_cooling_at_wh(self) -> float:
        return self.hysteresis_wh

    @property
    def release_at_wh(self) -> float:
        return 0.0

    def describe(self) -> str:
        """One-line log form."""
        net = f"{self.balance.p_net_w:+.0f}W" if self.balance else "n/a"
        skip = f" skipped={self.skipped_s:.0f}s" if self.skipped_s else ""
        pin = " CLAMPED" if self.clamped else ""
        bad = f" stale={sorted(s.name for s in self.unhealthy)}" if self.unhealthy else ""
        sub = f" sub={sorted(s.name for s in self.substituted)}" if self.substituted else ""
        return (
            f"t={self.t:.1f} {self.demand.name} ({self.reason.value}/{self.phase.value}) "
            f"E={self.energy_wh:+.1f}Wh of +-{self.hysteresis_wh:.0f} "
            f"net={net} dt={self.dt_s:.1f}s{skip}{pin}{bad}{sub}"
        )


@dataclass(frozen=True, slots=True)
class StateSnapshot:
    """The only thing that survives a restart. ``saved_at`` is WALL time, not monotonic."""

    energy_wh: float
    demand: Demand
    saved_at: float
    version: int = SNAPSHOT_VERSION

    def is_valid(self) -> bool:
        return (
            self.version == SNAPSHOT_VERSION
            and math.isfinite(self.energy_wh)
            and math.isfinite(self.saved_at)
        )
