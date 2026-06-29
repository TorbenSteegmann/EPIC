"""In-memory diagnostic series derived from exported solver columns.

The functions in this module never write to a result folder.  They augment the
loaded :class:`ModeDiagnostics` objects only for the lifetime of the viewer.
"""

from __future__ import annotations

import math
from dataclasses import replace
from typing import Any, Callable, Mapping

from diagnostics_loader import ModeDiagnostics


PREFIX = "derived:"


LABELS = {
    "normalized_kinetic_energy": "Normalised kinetic energy  E_k(t) / E_k(0)",
    "kinetic_energy_loss_fraction": "Kinetic-energy loss  1 - E_k(t) / E_k(0)",
    "normalized_total_energy": "Normalised total energy  E(t) / E(0)",
    "total_energy_loss_fraction": "Total-energy loss  1 - E(t) / E(0)",
    "fluid_measure_retention": "Fluid measure retention  V(t) / V(0)",
    "fluid_measure_drift": "Fluid measure drift  V(t) / V(0) - 1",
    "orbital_angular_momentum_retention": "Orbital angular-momentum retention",
    "represented_angular_momentum_retention": "Represented angular-momentum retention",
    "core_angular_momentum_z_retention": "Core angular-momentum retention  L_z(t) / L_z(0)",
    "total_momentum_magnitude": "Total momentum magnitude  |P|",
    "orbital_angular_momentum_magnitude": "Orbital angular-momentum magnitude  |L_orb|",
    "represented_angular_momentum_magnitude": "Represented angular-momentum magnitude  |L_repr|",
    "center_of_mass_displacement": "Centre-of-mass displacement  |x_c(t) - x_c(0)|",
    "subgrid_energy": "Sub-grid energy  E_k,p - E_k,g after P2G",
    "p2g_energy_change": "P2G kinetic-energy change",
    "g2p_energy_change": "G2P kinetic-energy change",
    "transfer_energy_change": "Combined transfer kinetic-energy change",
    "projection_energy_change": "Projection kinetic-energy change",
    "subgrid_energy_over_initial_total": "Sub-grid energy / |E(0)|",
    "g2p_energy_change_over_initial_total": "G2P energy change / |E(0)|",
    "cumulative_transfer_energy_change_over_initial": "Cumulative transfer energy change / E(0)",
    "grid_residual_energy": "Absolute grid-residual energy  E_res",
    "scene_normalized_residual_energy": "Scene-normalised residual energy  E_res / E_ref",
    "kinetic_energy_per_mass": "Kinetic energy per unit mass",
    "g2p_energy_gap_fraction": "Positive G2P particle-grid energy gap fraction",
    "grid_residual_delta_energy_dimensionless": "Residual-delta energy / (g dx)",
    "grid_residual_reversal_energy_dimensionless": "Residual-reversal energy / (g dx)",
    "effective_numerical_viscosity": "Fitted effective numerical viscosity",
    "energy_decay_fit_r_squared": "Energy-decay fit R²",
}


def key(name: str) -> str:
    return f"{PREFIX}{name}"


def is_derived_series(series_name: str) -> bool:
    return series_name.startswith(PREFIX)


def series_label(series_name: str) -> str:
    name = series_name[len(PREFIX) :] if is_derived_series(series_name) else series_name
    return LABELS.get(name, name.replace("_", " ").strip().capitalize())


def dropdown_label(series_name: str) -> str:
    label = series_label(series_name)
    return f"Calculated · {label}" if is_derived_series(series_name) else series_name


def _finite_reference(*series: tuple[float, ...] | None) -> float | None:
    for values in series:
        if not values:
            continue
        value = values[0]
        if math.isfinite(value) and abs(value) > 1.0e-30:
            return value
    return None


def _unary(values: tuple[float, ...], operation: Callable[[float], float]) -> tuple[float, ...]:
    return tuple(operation(value) if math.isfinite(value) else math.nan for value in values)


def _combine(
    series: Mapping[str, tuple[float, ...]],
    names: tuple[str, ...],
    operation: Callable[..., float],
) -> tuple[float, ...] | None:
    columns = [series.get(name) for name in names]
    if any(column is None for column in columns):
        return None
    return tuple(
        operation(*values) if all(math.isfinite(value) for value in values) else math.nan
        for values in zip(*columns, strict=True)
    )


def _add(target: dict[str, tuple[float, ...]], name: str, values: tuple[float, ...] | None) -> None:
    if values and any(math.isfinite(value) for value in values):
        target[key(name)] = values


def _ratio(target: dict[str, tuple[float, ...]], name: str, values: tuple[float, ...], reference: float | None) -> None:
    if reference is not None:
        _add(target, name, _unary(values, lambda value: value / reference))


def _vector_magnitude(series: Mapping[str, tuple[float, ...]], prefix: str) -> tuple[float, ...] | None:
    return _combine(series, tuple(f"{prefix}_{axis}" for axis in "xyz"), lambda x, y, z: math.sqrt(x*x + y*y + z*z))


def _vector_retention(
    series: Mapping[str, tuple[float, ...]],
    prefix: str,
    initial_prefix: str | None = None,
) -> tuple[float, ...] | None:
    names = tuple(f"{prefix}_{axis}" for axis in "xyz")
    columns = [series.get(name) for name in names]
    if any(column is None or not column for column in columns):
        return None
    initial_columns = [series.get(f"{initial_prefix}_{axis}") for axis in "xyz"] if initial_prefix else columns
    if any(column is None or not column for column in initial_columns):
        initial_columns = columns
    initial = tuple(column[0] for column in initial_columns if column is not None)
    norm_squared = sum(value * value for value in initial if math.isfinite(value))
    if len(initial) != 3 or not all(math.isfinite(value) for value in initial) or norm_squared <= 1.0e-30:
        return None
    return _combine(series, names, lambda x, y, z: (x*initial[0] + y*initial[1] + z*initial[2]) / norm_squared)


def _linear_fit(x_values: tuple[float, ...], y_values: tuple[float, ...]) -> tuple[float, float] | None:
    pairs = [(x, y) for x, y in zip(x_values, y_values, strict=True) if math.isfinite(x) and math.isfinite(y) and y > 1.0e-14]
    if len(pairs) < 2:
        return None
    xs = [pair[0] for pair in pairs]
    ys = [math.log(pair[1]) for pair in pairs]
    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    denominator = sum((x - mean_x) ** 2 for x in xs)
    if denominator <= 0.0:
        return None
    slope = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys, strict=True)) / denominator
    intercept = mean_y - slope * mean_x
    residual = sum((y - (slope*x + intercept)) ** 2 for x, y in zip(xs, ys, strict=True))
    total = sum((y - mean_y) ** 2 for y in ys)
    return slope, (1.0 - residual / total if total > 0.0 else 1.0)


def _per_mode(mode: ModeDiagnostics, metadata: Mapping[str, Any]) -> ModeDiagnostics:
    raw = mode.series
    derived: dict[str, tuple[float, ...]] = {}

    kinetic = raw.get("kinetic_energy")
    if kinetic:
        kinetic_reference = _finite_reference(raw.get("stage_before_step_particle_kinetic_energy"), kinetic)
        _ratio(derived, "normalized_kinetic_energy", kinetic, kinetic_reference)
        normalized = derived.get(key("normalized_kinetic_energy"))
        if normalized:
            _add(derived, "kinetic_energy_loss_fraction", _unary(normalized, lambda value: 1.0 - value))

    total = raw.get("total_energy")
    total_reference = _finite_reference(raw.get("stage_before_step_particle_energy"), total)
    if total:
        _ratio(derived, "normalized_total_energy", total, total_reference)
        normalized = derived.get(key("normalized_total_energy"))
        if normalized:
            _add(derived, "total_energy_loss_fraction", _unary(normalized, lambda value: 1.0 - value))

    dimension = _finite_reference(raw.get("fluid_measure_dimension"))
    measure_name = "fluid_area_estimate" if dimension == 2 else "fluid_volume_estimate"
    measure = raw.get(measure_name)
    if measure:
        measure_reference = _finite_reference(measure)
        _ratio(derived, "fluid_measure_retention", measure, measure_reference)
        retention = derived.get(key("fluid_measure_retention"))
        if retention:
            _add(derived, "fluid_measure_drift", _unary(retention, lambda value: value - 1.0))

    for prefix in ("total_momentum", "orbital_angular_momentum", "represented_angular_momentum"):
        _add(derived, f"{prefix}_magnitude", _vector_magnitude(raw, prefix))
    _add(derived, "orbital_angular_momentum_retention", _vector_retention(raw, "orbital_angular_momentum"))
    _add(
        derived,
        "represented_angular_momentum_retention",
        _vector_retention(raw, "represented_angular_momentum", "stage_before_step_particle_angular_momentum"),
    )

    core = raw.get("core_angular_momentum_z")
    if core:
        _ratio(derived, "core_angular_momentum_z_retention", core, _finite_reference(core))

    center_names = tuple(f"center_of_mass_{axis}" for axis in "xyz")
    center_columns = [raw.get(name) for name in center_names]
    if all(column for column in center_columns):
        origin = tuple(column[0] for column in center_columns if column)
        if len(origin) == 3 and all(math.isfinite(value) for value in origin):
            _add(derived, "center_of_mass_displacement", _combine(raw, center_names, lambda x, y, z: math.sqrt((x-origin[0])**2 + (y-origin[1])**2 + (z-origin[2])**2)))

    subgrid = _combine(raw, ("stage_before_step_particle_kinetic_energy", "stage_after_p2g_grid_kinetic_energy"), lambda particle, grid: particle - grid)
    p2g = _combine(raw, ("stage_after_p2g_grid_kinetic_energy", "stage_before_step_particle_kinetic_energy"), lambda grid, particle: grid - particle)
    g2p = _combine(raw, ("stage_after_g2p_particle_kinetic_energy", "stage_after_second_extrapolate_grid_kinetic_energy"), lambda particle, grid: particle - grid)
    projection = _combine(raw, ("stage_after_projection_grid_kinetic_energy", "stage_after_boundary_grid_kinetic_energy"), lambda after, before: after - before)
    transfer = tuple(a + b if math.isfinite(a) and math.isfinite(b) else math.nan for a, b in zip(p2g, g2p, strict=True)) if p2g and g2p else None
    _add(derived, "subgrid_energy", subgrid)
    _add(derived, "p2g_energy_change", p2g)
    _add(derived, "g2p_energy_change", g2p)
    _add(derived, "transfer_energy_change", transfer)
    _add(derived, "projection_energy_change", projection)
    if total_reference is not None:
        scale = abs(total_reference)
        if subgrid:
            _add(derived, "subgrid_energy_over_initial_total", _unary(subgrid, lambda value: value / scale))
        if g2p:
            _add(derived, "g2p_energy_change_over_initial_total", _unary(g2p, lambda value: value / scale))
        if transfer:
            cumulative = 0.0
            values: list[float] = []
            for value in transfer:
                if math.isfinite(value):
                    cumulative += value
                    values.append(cumulative / total_reference)
                else:
                    values.append(math.nan)
            _add(derived, "cumulative_transfer_energy_change_over_initial", tuple(values))

    residual = _combine(raw, ("stage_after_advect_grid_residual_energy_per_mass", "total_mass"), lambda per_mass, mass: per_mass * mass)
    _add(derived, "grid_residual_energy", residual)
    _add(derived, "kinetic_energy_per_mass", _combine(raw, ("kinetic_energy", "total_mass"), lambda energy, mass: energy / mass if abs(mass) > 1.0e-30 else math.nan))
    _add(derived, "g2p_energy_gap_fraction", _combine(raw, ("stage_after_g2p_particle_kinetic_energy", "stage_after_second_extrapolate_grid_kinetic_energy"), lambda particle, grid: max(0.0, particle-grid) / max(particle, 1.0e-30)))

    try:
        gravity_scale = 9.81 * float(metadata["dx"])
    except (KeyError, TypeError, ValueError):
        gravity_scale = 0.0
    if gravity_scale > 0.0:
        delta = raw.get("stage_after_advect_grid_residual_delta_energy_per_mass")
        reversal = raw.get("stage_after_advect_grid_residual_reversal_energy_per_mass")
        if delta:
            _add(derived, "grid_residual_delta_energy_dimensionless", _unary(delta, lambda value: value / gravity_scale))
        if reversal:
            _add(derived, "grid_residual_reversal_energy_dimensionless", _unary(reversal, lambda value: value / gravity_scale))

    if metadata.get("scene") == "taylor_green_vortex" and kinetic and raw.get("time"):
        try:
            extent = (float(metadata["domain"]) - 2.0) * float(metadata["dx"])
            wave_number = 2.0 * math.pi / extent
        except (KeyError, TypeError, ValueError, ZeroDivisionError):
            wave_number = 0.0
        normalized = derived.get(key("normalized_kinetic_energy"))
        fit = _linear_fit((0.0, *raw["time"]), (1.0, *normalized)) if normalized else None
        if fit and wave_number > 0.0:
            viscosity = -fit[0] / (4.0 * wave_number * wave_number)
            _add(derived, "effective_numerical_viscosity", tuple(viscosity for _ in kinetic))
            _add(derived, "energy_decay_fit_r_squared", tuple(fit[1] for _ in kinetic))

    return replace(mode, series={**raw, **derived})


def add_derived_series(
    modes: Mapping[str, ModeDiagnostics], metadata: Mapping[str, Any] | None = None
) -> dict[str, ModeDiagnostics]:
    """Return new modes containing every derivation supported by their columns."""

    metadata = metadata or {}
    augmented = {name: _per_mode(mode, metadata) for name, mode in modes.items()}
    reference = max(
        (
            value
            for mode in augmented.values()
            for value in mode.series.get("stage_after_advect_particle_kinetic_energy", ())
            if math.isfinite(value)
        ),
        default=0.0,
    )
    if reference > 0.0:
        for name, mode in tuple(augmented.items()):
            residual = mode.series.get(key("grid_residual_energy"))
            if residual:
                scene_normalized = tuple((value + 1.0e-12) / reference if math.isfinite(value) else math.nan for value in residual)
                augmented[name] = replace(mode, series={**mode.series, key("scene_normalized_residual_energy"): scene_normalized})
    return augmented
