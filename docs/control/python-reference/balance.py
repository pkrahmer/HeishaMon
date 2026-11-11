"""The energy balance: the house treated as a thermal battery.

    p_transmission = k_trans * (T_out - T_in)              >0 = house gains from outside
    p_model        = p_produced + p_solar + p_internal + p_transmission
    p_trim         = a_trim * clamp(T_in - T_set, +-L)     <0 when too cold => more heat
    p_net          = p_model + p_trim                      <0 = deficit, >0 = surplus

``p_model`` is the prediction, ``p_trim`` the correction for whatever the
prediction got wrong. They are summed in watts and integrated once -- never
blended or arbitrated. See docs/algorithm-plan.md 1.1.
"""

from __future__ import annotations

from .config import Config
from .types import Balance, Inputs

__all__ = ["energy_balance"]


def energy_balance(inputs: Inputs, cfg: Config) -> Balance:
    p_transmission = cfg.transmission_w_per_k * (inputs.t_outdoor_c - inputs.t_indoor_c)
    p_model = inputs.p_produced_w + inputs.p_solar_w + inputs.p_internal_w + p_transmission

    # The deviation is clamped, not the resulting power, so the limit stays readable in
    # kelvin and does not move when adjustment_w_per_k is retuned.
    deviation = inputs.t_indoor_c - cfg.desired_indoor_c
    limit = cfg.trim_limit_k
    if limit is not None:
        deviation = -limit if deviation < -limit else limit if deviation > limit else deviation
    p_trim = cfg.adjustment_w_per_k * deviation

    return Balance(
        p_transmission_w=p_transmission,
        p_model_w=p_model,
        p_trim_w=p_trim,
        p_net_w=p_model + p_trim,
    )
