"""Heating/cooling decision core.

Pure, total, and free of I/O: the only things that cross this boundary are
timestamped scalar readings in, and a Decision out. Nothing here knows whether a
temperature came from 1-Wire, Modbus, MQTT, or a CSV replay, nor whether HEATING
becomes a relay contact or a log line.

    from heatnrgx.core import Config, Controller, Demand, Signal

    ctrl = Controller(Config())
    ctrl.observe(Signal.T_INDOOR, 20.5, t=0.0)
    ...
    decision = ctrl.evaluate(t=60.0)
"""

from .balance import energy_balance
from .config import DEFAULT_FALLBACK_W, DEFAULT_MAX_AGE_S, Config
from .controller import (
    Controller,
    ControllerState,
    advance_to,
    evaluate,
    initial_state,
    observe,
    observe_all,
    restore,
    set_allowed,
    to_snapshot,
)
from .hysteresis import clamp, clamp_bounds, relay
from .signals import Held, SignalTable, resolve
from .types import (
    ALL_DEMANDS,
    OPTIONAL_SIGNALS,
    REQUIRED_SIGNALS,
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
    "ALL_DEMANDS",
    "DEFAULT_FALLBACK_W",
    "DEFAULT_MAX_AGE_S",
    "OPTIONAL_SIGNALS",
    "REQUIRED_SIGNALS",
    "SNAPSHOT_VERSION",
    "Balance",
    "Config",
    "Controller",
    "ControllerState",
    "Decision",
    "Demand",
    "EnergyLedger",
    "Held",
    "Inputs",
    "Phase",
    "Reading",
    "Reason",
    "Signal",
    "SignalTable",
    "StateSnapshot",
    "advance_to",
    "clamp",
    "clamp_bounds",
    "energy_balance",
    "evaluate",
    "initial_state",
    "observe",
    "observe_all",
    "relay",
    "resolve",
    "restore",
    "set_allowed",
    "to_snapshot",
]
