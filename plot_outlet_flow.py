from pathlib import Path
import matplotlib.pyplot as plt

# Change this to your actual OpenFOAM case.
CASE = Path(r"C:\OpenFOAM\thermal_sim_v2\validation_fan_rack")

# TOML name "Validation outlet" becomes Validation_outlet.
REPORT = '\fluid\ambient_net_mass_flow'

# Use the same density as the simulation for conversion to volumetric flow.
RHO = 1.225  # kg/m^3
M3S_TO_CFM = 2118.880003

report_root = CASE / "postProcessing" / REPORT

if not report_root.is_dir():
    available = []
    post = CASE / "postProcessing"

    if post.is_dir():
        available = sorted(
            item.name
            for item in post.iterdir()
            if item.is_dir() and "mass_flow" in item.name
        )

    raise SystemExit(
        f"Could not find:\n  {report_root}\n\n"
        f"Available mass-flow reports:\n  "
        + "\n  ".join(available)
    )

# A restarted run can create several numbered output directories.
files = list(report_root.glob("*/surfaceFieldValue.dat"))

if not files:
    raise SystemExit(
        f"No surfaceFieldValue.dat files found below:\n  {report_root}"
    )

# Keying by time automatically replaces duplicate restart samples.
samples = {}

for path in files:
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            line = line.strip()

            if not line or line.startswith("#"):
                continue

            columns = line.replace("(", " ").replace(")", " ").split()

            try:
                time = float(columns[0])
                phi = float(columns[1])
            except (ValueError, IndexError):
                continue

            samples[time] = phi

if not samples:
    raise SystemExit("The report files contained no readable flow samples.")

times = sorted(samples)
signed_mass_flow = [samples[t] for t in times]
outlet_mass_flow = [abs(value) for value in signed_mass_flow]
volume_flow = [value / RHO for value in outlet_mass_flow]
cfm = [value * M3S_TO_CFM for value in volume_flow]
velocity = [value / 0.15 for value in volume_flow]

print(f"Latest result time:       {times[-1]:.6g} s")
print(f"Signed phi:               {signed_mass_flow[-1]:.8g} kg/s")
print(f"Outlet mass flow:         {outlet_mass_flow[-1]:.8g} kg/s")
print(f"Outlet volume flow:       {volume_flow[-1]:.8g} m^3/s")
print(f"Outlet flow:              {cfm[-1]:.4f} CFM")
print(f"Area-average equivalent:  {velocity[-1]:.6g} m/s")

fig, left = plt.subplots(figsize=(9, 5))

left.plot(times, outlet_mass_flow, color="tab:blue", linewidth=2)
left.axhline(
    0.0091875,
    color="black",
    linestyle="--",
    linewidth=1,
    label="Expected: 0.0091875 kg/s",
)
left.set_xlabel("Simulation time (s)")
left.set_ylabel("Outlet mass flow (kg/s)", color="tab:blue")
left.tick_params(axis="y", labelcolor="tab:blue")
left.grid(True, alpha=0.3)
left.legend()

right = left.twinx()
right.plot(times, cfm, color="tab:orange", linewidth=1.5, alpha=0.8)
right.set_ylabel("Outlet flow (CFM)", color="tab:orange")
right.tick_params(axis="y", labelcolor="tab:orange")

plt.title("Validation Outlet Flow")
fig.tight_layout()
plt.show()