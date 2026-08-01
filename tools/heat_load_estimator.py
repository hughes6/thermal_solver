"""Estimate component watts for a Thermal Sim internal solid region.

Electrical input is normally the best rack heat estimate: almost all power
consumed by a server ultimately becomes heat in or near the rack. Temperature
can estimate watts only when thermal resistance or transient thermal mass is
also known. This utility reports each available method separately and prints a
copy-ready TOML snippet.
"""

from __future__ import annotations

import argparse
import statistics


def normalize_efficiency(value: float) -> float:
    efficiency = value / 100.0 if value > 1.0 else value
    if not 0.0 < efficiency <= 1.0:
        raise ValueError("Efficiency must be 0-1 or 0-100 percent.")
    return efficiency


def estimate_methods(args: argparse.Namespace) -> dict[str, float]:
    estimates: dict[str, float] = {}
    exported = args.exported_power_w

    if args.input_power_w is not None:
        estimates["electrical_input"] = args.input_power_w - exported

    if args.dc_load_w is not None:
        if args.efficiency is None:
            raise ValueError("--dc-load-w requires --efficiency.")
        efficiency = normalize_efficiency(args.efficiency)
        wall_power = args.dc_load_w / efficiency
        estimates["dc_load_and_efficiency"] = wall_power - exported

    if args.surface_c is not None:
        if args.ambient_c is None or args.thermal_resistance_k_per_w is None:
            raise ValueError(
                "--surface-c requires --ambient-c and "
                "--thermal-resistance-k-per-w."
            )
        if args.thermal_resistance_k_per_w <= 0:
            raise ValueError("Thermal resistance must be positive.")
        estimates["steady_temperature"] = (
            args.surface_c - args.ambient_c
        ) / args.thermal_resistance_k_per_w

    transient_values = (
        args.mass_kg, args.specific_heat_j_kg_k, args.start_c,
        args.end_c, args.duration_s,
    )
    if any(value is not None for value in transient_values):
        if any(value is None for value in transient_values):
            raise ValueError(
                "Transient estimation requires --mass-kg, "
                "--specific-heat-j-kg-k, --start-c, --end-c, and --duration-s."
            )
        if args.mass_kg <= 0 or args.specific_heat_j_kg_k <= 0 or args.duration_s <= 0:
            raise ValueError("Mass, specific heat, and duration must be positive.")
        stored_power = (
            args.mass_kg * args.specific_heat_j_kg_k
            * (args.end_c - args.start_c) / args.duration_s
        )
        loss_power = 0.0
        if args.thermal_resistance_k_per_w is not None:
            if args.thermal_resistance_k_per_w <= 0:
                raise ValueError("Thermal resistance must be positive.")
            if args.ambient_c is None:
                raise ValueError(
                    "Transient heat-loss correction requires --ambient-c."
                )
            average_temperature = 0.5 * (args.start_c + args.end_c)
            loss_power = (
                average_temperature - args.ambient_c
            ) / args.thermal_resistance_k_per_w
        estimates["transient_temperature"] = stored_power + loss_power

    for name, watts in estimates.items():
        if watts < 0:
            raise ValueError(f"{name} produced negative heat; check the inputs.")
    return estimates


def interactive_namespace() -> argparse.Namespace:
    print("Choose the information you have:")
    print("  1: measured wall/input electrical power")
    print("  2: DC load and power-supply efficiency")
    print("  3: steady surface/ambient temperature and thermal resistance")
    choice = input("Method [1/2/3]: ").strip()
    values = {
        "input_power_w": None, "dc_load_w": None, "efficiency": None,
        "exported_power_w": 0.0, "surface_c": None, "ambient_c": None,
        "thermal_resistance_k_per_w": None, "mass_kg": None,
        "specific_heat_j_kg_k": None, "start_c": None, "end_c": None,
        "duration_s": None, "name": "Estimated heat source", "method": None,
    }
    values["name"] = input("Internal-region name [Estimated heat source]: ").strip() \
        or values["name"]
    if choice == "1":
        values["input_power_w"] = float(input("Measured input power (W): "))
    elif choice == "2":
        values["dc_load_w"] = float(input("DC load delivered by PSU (W): "))
        values["efficiency"] = float(input("PSU efficiency (0-1 or percent): "))
    elif choice == "3":
        values["surface_c"] = float(input("Steady surface temperature (C): "))
        values["ambient_c"] = float(input("Ambient temperature (C): "))
        values["thermal_resistance_k_per_w"] = float(
            input("Measured/known thermal resistance (K/W): ")
        )
    else:
        raise ValueError("Choose 1, 2, or 3.")
    return argparse.Namespace(**values)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Estimate TOML component watts.")
    parser.add_argument("--name", default="Estimated heat source")
    parser.add_argument("--input-power-w", type=float,
                        help="Measured AC/DC power entering the modeled device")
    parser.add_argument("--dc-load-w", type=float,
                        help="PSU output delivered to internal electronics")
    parser.add_argument("--efficiency", type=float,
                        help="PSU efficiency as 0-1 or percent")
    parser.add_argument("--exported-power-w", type=float, default=0.0,
                        help="Power leaving as light, shaft work, cables, etc.")
    parser.add_argument("--surface-c", type=float)
    parser.add_argument("--ambient-c", type=float)
    parser.add_argument("--thermal-resistance-k-per-w", type=float)
    parser.add_argument("--mass-kg", type=float)
    parser.add_argument("--specific-heat-j-kg-k", type=float)
    parser.add_argument("--start-c", type=float)
    parser.add_argument("--end-c", type=float)
    parser.add_argument("--duration-s", type=float)
    parser.add_argument(
        "--method",
        choices=("electrical_input", "dc_load_and_efficiency",
                 "steady_temperature", "transient_temperature", "median"),
        help="Estimate to place in the TOML snippet"
    )
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    supplied = any(value is not None for value in (
        args.input_power_w, args.dc_load_w, args.surface_c, args.mass_kg
    ))
    if not supplied:
        args = interactive_namespace()

    estimates = estimate_methods(args)
    if not estimates:
        raise SystemExit("No complete estimation method was supplied.")

    print("\nAvailable heat-load estimates:")
    for method, watts in estimates.items():
        print(f"  {method}: {watts:.6g} W")

    selected_method = args.method
    if selected_method == "median":
        selected_watts = statistics.median(estimates.values())
    elif selected_method:
        if selected_method not in estimates:
            raise SystemExit(f"Method {selected_method!r} is unavailable from these inputs.")
        selected_watts = estimates[selected_method]
    else:
        priority = ("electrical_input", "dc_load_and_efficiency",
                    "steady_temperature", "transient_temperature")
        selected_method = next(method for method in priority if method in estimates)
        selected_watts = estimates[selected_method]

    print(f"\nSelected: {selected_method} = {selected_watts:.6g} W")
    print("\nCopy into a component's internal solid region:\n")
    print("[[internal_regions]]")
    print(f'name = "{args.name}"')
    print('state = "solid"')
    print(f"watts = {selected_watts:.9g}")
    print("# Add position, size, and material tables below this block.")
    print("\nNOTE: Do not reduce measured wall/input power by PSU efficiency. "
          "PSU losses and the DC load both normally become rack heat. Efficiency "
          "is used only when converting a known DC load to wall/input power.")
    print("Temperature alone cannot determine watts; it requires a calibrated "
          "thermal resistance or transient mass and specific heat.")


if __name__ == "__main__":
    main()
