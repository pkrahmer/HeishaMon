"""The decision core: pure, total, clock-free.

Time handling *is* the algorithm here, so it is concentrated in one function.
``advance_to`` is the only place the time reference moves and the only place the
energy integral changes; every other entry point calls it first and does nothing
time-related itself.

The ingest contract is **integrate-then-update**: a new reading describes the
world from its own timestamp onward and never before it, so the open interval is
closed with the *old* held values before the new value takes effect. Reversing
those two steps is the classic asynchronous-integration bug.

See docs/algorithm-plan.md 1.2-1.7.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field, replace
from typing import AbstractSet, Iterable

from .balance import energy_balance
from .config import Config
from .hysteresis import clamp, clamp_bounds, relay
from .signals import SignalTable, resolve
from .types import (
    ALL_DEMANDS,
    SNAPSHOT_VERSION,
    Balance,
    Decision,
    Demand,
    EnergyLedger,
    Inputs,
    Phase,
    Reading,
    Reason,
    Signal,
    StateSnapshot,
)

__all__ = [
    "ControllerState",
    "initial_state",
    "restore",
    "to_snapshot",
    "advance_to",
    "observe",
    "observe_all",
    "set_allowed",
    "evaluate",
    "Controller",
]

_SECONDS_PER_HOUR = 3600.0


@dataclass(frozen=True, slots=True)
class ControllerState:
    """Everything the loop remembers. Threaded through the pure functions, never mutated.

    Only ``energy_wh`` and ``demand`` are worth persisting across a restart; see
    docs/algorithm-plan.md 3.
    """

    phase: Phase = Phase.UNINITIALIZED
    energy_wh: float = 0.0
    demand: Demand = Demand.NO_DEMAND
    allowed: frozenset[Demand] = ALL_DEMANDS
    t_last: float | None = None  # integration reference (monotonic)
    t_healthy: float | None = None  # last instant all required signals resolved
    signals: SignalTable = field(default_factory=SignalTable)

    #: Coverage accounting: elapsed seconds that were integrated, and seconds that
    #: were advanced over without integrating. Diagnostics only -- never control flow.
    integrated_s: float = 0.0
    skipped_s: float = 0.0

    #: Lifetime Wh per balance term, and whether the last integration hit the
    #: anti-windup bound. Diagnostics only.
    ledger: EnergyLedger = EnergyLedger()
    clamped: bool = False


# ---------------------------------------------------------------- lifecycle


def initial_state() -> ControllerState:
    return ControllerState()


def restore(snapshot: StateSnapshot | None, now_wall: float, cfg: Config) -> ControllerState:
    """Warm start from a snapshot, or cold start if it is absent, damaged, or stale.

    Always returns ``phase = UNINITIALIZED``: the energy and relay phase may be
    restored, but the *clock* is re-seeded by the first observation, and held
    sensor values are deliberately not resurrected ([A-21]).
    """
    if snapshot is None or not snapshot.is_valid() or snapshot.version != SNAPSHOT_VERSION:
        return initial_state()
    age = now_wall - snapshot.saved_at
    if not math.isfinite(age) or age < 0.0 or age > cfg.state_max_age_s:
        return initial_state()
    lo, hi = clamp_bounds(ALL_DEMANDS, cfg)
    return ControllerState(
        energy_wh=clamp(snapshot.energy_wh, lo, hi),
        demand=snapshot.demand,
    )


def to_snapshot(state: ControllerState, now_wall: float) -> StateSnapshot:
    return StateSnapshot(energy_wh=state.energy_wh, demand=state.demand, saved_at=now_wall)


# ------------------------------------------------------------ time & ingest


def advance_to(
    state: ControllerState, t: float, cfg: Config
) -> tuple[ControllerState, float, bool]:
    """Move the time reference to ``t``, integrating the interval that just closed.

    Returns ``(state, dt_s, integrated)``. The interval is integrated only if it is
    *trustworthy*: short enough to have been observed (``dt <= max_step_s``) and
    covered by fresh required inputs **at its start**, which is what zero-order hold
    says was true throughout it. An untrustworthy interval contributes zero energy
    and the reference still advances -- never invent energy for time nobody observed
    ([A-19]).
    """
    if not math.isfinite(t):
        return state, 0.0, False
    if state.t_last is None:
        return replace(state, t_last=t), 0.0, False

    dt = t - state.t_last
    if dt <= 0.0:  # duplicate instant, or a clock that went backwards; no time travel
        return state, 0.0, False

    inputs = resolve(state.signals, state.t_last, cfg)
    if inputs is None or dt > cfg.max_step_s:
        return replace(state, t_last=t, skipped_s=state.skipped_s + dt), dt, False

    balance = energy_balance(inputs, cfg)
    hours = dt / _SECONDS_PER_HOUR
    lo, hi = clamp_bounds(state.allowed, cfg)
    raw = state.energy_wh + balance.p_net_w * hours
    energy = clamp(raw, lo, hi)
    return (
        replace(
            state,
            t_last=t,
            energy_wh=energy,
            integrated_s=state.integrated_s + dt,
            ledger=state.ledger.accumulate(inputs, balance, hours),
            clamped=energy != raw,
        ),
        dt,
        True,
    )


def observe(state: ControllerState, reading: Reading, cfg: Config) -> ControllerState:
    """Ingest one asynchronous reading.

    Rejected silently: non-finite values or timestamps, and readings older than the
    timestamp already held for the same signal (a stale duplicate). A reading that
    predates ``t_last`` but postdates its own signal's last value is applied without
    rewinding the integral ([A-18]).
    """
    if not math.isfinite(reading.value) or not math.isfinite(reading.t):
        return state

    held = state.signals.held(reading.signal)
    if held is not None and reading.t < held.t:
        return state

    # Close the open interval with the OLD value, then let the new one take effect.
    state, _dt, _integrated = advance_to(state, reading.t, cfg)
    return replace(
        state, signals=state.signals.with_reading(reading.signal, reading.value, reading.t)
    )


def observe_all(
    state: ControllerState, readings: Iterable[Reading], cfg: Config
) -> ControllerState:
    """Ingest readings in delivery order. Ordering across signals is handled per reading."""
    for reading in readings:
        state = observe(state, reading, cfg)
    return state


def set_allowed(
    state: ControllerState, allowed: AbstractSet[Demand], t: float, cfg: Config
) -> ControllerState:
    """Change the permitted directions. The mask is an asynchronous input like any other.

    ``NO_DEMAND`` is always permitted. Tightening the mask re-clamps the accumulator
    immediately so a masked direction cannot bank windup.
    """
    new_allowed = frozenset(allowed) | {Demand.NO_DEMAND}
    # Advance unconditionally, even when the mask is unchanged: the contract is that this
    # entry point closes the open interval, and a caller re-asserting the same mask on a
    # tick should not be a silently different code path from one that changes it.
    state, _dt, _integrated = advance_to(state, t, cfg)
    if new_allowed == state.allowed:
        return state
    lo, hi = clamp_bounds(new_allowed, cfg)
    return replace(state, allowed=new_allowed, energy_wh=clamp(state.energy_wh, lo, hi))


# ------------------------------------------------------------------ decide


def evaluate(state: ControllerState, t: float, cfg: Config) -> tuple[ControllerState, Decision]:
    """Advance to ``t`` and decide. The only function that changes ``demand``.

    Must be called on a fixed heartbeat even when nothing has been observed, or a
    quiet system never advances time and never engages.
    """
    if not math.isfinite(t):
        # Ignore the call outright rather than letting NaN through. Left unguarded it
        # would set t_healthy to NaN, which silently voids the HOLD grace period and
        # sends the very next stale evaluate straight to FAULT.
        return state, Decision(
            t=t,
            demand=state.demand,
            reason=Reason.INVALID_TIME,
            phase=state.phase,
            energy_wh=state.energy_wh,
            hysteresis_wh=cfg.hysteresis_wh,
            integrated_s=state.integrated_s,
            skipped_s=state.skipped_s,
            ledger=state.ledger,
            clamped=state.clamped,
        )

    state, dt, _integrated = advance_to(state, t, cfg)

    inputs: Inputs | None = resolve(state.signals, t, cfg)
    unhealthy = state.signals.unhealthy(t, cfg)
    balance: Balance | None = energy_balance(inputs, cfg) if inputs is not None else None
    substituted: frozenset[Signal] = inputs.substituted if inputs is not None else frozenset()

    def emit(new_state: ControllerState, demand: Demand, reason: Reason) -> tuple[
        ControllerState, Decision
    ]:
        return new_state, Decision(
            t=t,
            demand=demand,
            reason=reason,
            phase=new_state.phase,
            energy_wh=new_state.energy_wh,
            hysteresis_wh=cfg.hysteresis_wh,
            dt_s=dt,
            integrated_s=new_state.integrated_s,
            skipped_s=new_state.skipped_s,
            balance=balance,
            inputs=inputs,
            ledger=new_state.ledger,
            clamped=new_state.clamped,
            unhealthy=unhealthy,
            substituted=substituted,
        )

    # 1. Not started yet: emit NO_DEMAND until every required signal has been seen.
    if state.phase is Phase.UNINITIALIZED:
        if inputs is None:
            return emit(state, Demand.NO_DEMAND, Reason.WAITING_FOR_INPUTS)
        seeded = replace(state, phase=Phase.RUNNING, t_healthy=t)
        # Cold start emits NO_DEMAND; a warm start emits the restored demand.
        return emit(seeded, seeded.demand, Reason.SEEDED)

    # 2. Recovering from a long blind period: the accumulated charge is not
    #    trustworthy, so start again from the neutral prior ([A-11]).
    if state.phase is Phase.FAULT and inputs is not None:
        recovered = replace(
            state,
            phase=Phase.RUNNING,
            energy_wh=0.0,
            demand=Demand.NO_DEMAND,
            t_healthy=t,
        )
        return emit(recovered, Demand.NO_DEMAND, Reason.FAULT_RECOVERED)

    # 3. Required inputs unusable: coast on the last demand, then fail off.
    if inputs is None:
        since_healthy = t - state.t_healthy if state.t_healthy is not None else math.inf
        if state.phase is not Phase.FAULT and since_healthy <= cfg.input_hold_s:
            held = replace(state, phase=Phase.HOLD)
            return emit(held, held.demand, Reason.HELD_STALE_INPUT)
        faulted = replace(state, phase=Phase.FAULT, demand=Demand.NO_DEMAND)
        return emit(faulted, Demand.NO_DEMAND, Reason.FAULT_TIMEOUT)

    # 4. Normal: relay on the accumulator, then apply the supervisor's mask.
    demand = relay(state.energy_wh, state.demand, cfg.hysteresis_wh)
    reason = Reason.NORMAL
    if demand not in state.allowed:
        demand, reason = Demand.NO_DEMAND, Reason.MASKED
    # Store the masked result so the relay's `previous` is always what was emitted.
    running = replace(state, phase=Phase.RUNNING, demand=demand, t_healthy=t)
    return emit(running, demand, reason)


# ------------------------------------------------------------------ facade


class Controller:
    """Thin mutable wrapper over the pure functions, for the runtime layer.

    Holds one state cell and nothing else: no clock, no I/O, no logging. Time is
    still supplied by the caller on every call.
    """

    __slots__ = ("_cfg", "_state")

    def __init__(self, cfg: Config, state: ControllerState | None = None) -> None:
        self._cfg = cfg
        self._state = state if state is not None else initial_state()

    @property
    def config(self) -> Config:
        return self._cfg

    @property
    def state(self) -> ControllerState:
        return self._state

    @property
    def energy_wh(self) -> float:
        return self._state.energy_wh

    @property
    def demand(self) -> Demand:
        return self._state.demand

    @property
    def phase(self) -> Phase:
        return self._state.phase

    @property
    def ledger(self) -> EnergyLedger:
        return self._state.ledger

    def advance(self, t: float) -> None:
        """Integrate up to ``t`` without deciding.

        Lets the runtime integrate on a fast tick (``integration_interval_s``, 10 s)
        while deciding on a slow one (``tick_interval_s``, 60 s) -- the split the
        legacy system uses. It costs nothing: the integral is exact either way, but
        a finer grid resolves a step in pump power six times sooner.
        """
        self._state, _dt, _integrated = advance_to(self._state, t, self._cfg)

    def observe(self, signal: Signal, value: float, t: float) -> None:
        self._state = observe(self._state, Reading(signal, value, t), self._cfg)

    def observe_reading(self, reading: Reading) -> None:
        self._state = observe(self._state, reading, self._cfg)

    def observe_all(self, readings: Iterable[Reading]) -> None:
        self._state = observe_all(self._state, readings, self._cfg)

    def set_allowed(self, allowed: AbstractSet[Demand], t: float) -> None:
        self._state = set_allowed(self._state, allowed, t, self._cfg)

    def evaluate(self, t: float) -> Decision:
        self._state, decision = evaluate(self._state, t, self._cfg)
        return decision

    def snapshot(self, now_wall: float) -> StateSnapshot:
        return to_snapshot(self._state, now_wall)
