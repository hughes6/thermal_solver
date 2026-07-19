"""Plot the 3-D velocity and local convection-coefficient fields.

Usage:
  python plot_flow_fields.py --sim simulation.csv --rack output.txt --step 0
  python plot_flow_fields.py --sim simulation.csv --rack output.txt --step -1
"""
from __future__ import annotations

import argparse
import re
from dataclasses import dataclass, field
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np
import pandas as pd


@dataclass
class Region:
    kind: str
    origin: tuple[float, float, float]
    size: tuple[float, float, float]


@dataclass
class Component:
    name: str
    origin: tuple[float, float, float]
    size: tuple[float, float, float]
    regions: list[Region] = field(default_factory=list)


@dataclass
class Rack:
    width: float
    depth: float
    height: float
    components: list[Component] = field(default_factory=list)


def floats(text: str) -> list[float]:
    return [float(v) for v in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", text)]


def parse_rack(path: str) -> Rack:
    lines = [line.strip() for line in Path(path).read_text().splitlines() if line.strip()]
    width = depth = height = None
    for line in lines:
        if line.startswith("width:"): width = floats(line)[0]
        elif line.startswith("depth:"): depth = floats(line)[0]
        elif line.startswith("height:"): height = floats(line)[0]
    if None in (width, depth, height):
        raise ValueError("Rack dimensions were not found in output.txt")

    rack = Rack(width, depth, height)
    i = 0
    while i < len(lines):
        match = re.match(r"^Component\s+\d+:\s*(.*)$", lines[i])
        if not match:
            i += 1
            continue
        name = match.group(1)
        values = {}
        regions = []
        i += 1
        while i < len(lines) and not re.match(r"^(Component|Fan|Vent)\s+\d+:", lines[i]):
            if re.match(r"^Internal Region\s+\d+:$", lines[i]):
                rv = {}
                i += 1
                while i < len(lines) and not re.match(r"^(Internal Region\s+\d+:|Component\s+\d+:|Fan\s+\d+:|Vent\s+\d+:)", lines[i]):
                    if ":" in lines[i]:
                        k, v = lines[i].split(":", 1); rv[k.strip().lower()] = v.strip()
                    i += 1
                if {"type", "size", "global_position"} <= rv.keys():
                    regions.append(Region(rv["type"], tuple(floats(rv["global_position"])[:3]), tuple(floats(rv["size"])[:3])))
                continue
            if ":" in lines[i]:
                k, v = lines[i].split(":", 1); values[k.strip().lower()] = v.strip()
            i += 1
        dims = floats(values["dimensions"])
        if len(dims) < 3:
            raise ValueError(f"Component {name} dimensions must contain width, depth, height")
        rack.components.append(Component(
            name,
            tuple(floats(values["coordinates"])[:3]),
            tuple(dims[:3]),  # width, depth, height
            regions,
        ))
    return rack


def read_spacing(path: str) -> tuple[float, float, float]:
    with open(path, "r", encoding="utf-8") as f:
        f.readline(); parts = f.readline().strip().split(",")
    return float(parts[1]), float(parts[3]), float(parts[5])


def box_edges(ax, origin, size, **kwargs):
    x, y, z = origin; sx, sy, sz = size
    p = np.array([[x,y,z],[x+sx,y,z],[x+sx,y+sy,z],[x,y+sy,z],
                  [x,y,z+sz],[x+sx,y,z+sz],[x+sx,y+sy,z+sz],[x,y+sy,z+sz]])
    for a,b in [(0,1),(1,2),(2,3),(3,0),(4,5),(5,6),(6,7),(7,4),(0,4),(1,5),(2,6),(3,7)]:
        ax.plot(*zip(p[a], p[b]), **kwargs)


def draw_geometry(ax, rack: Rack):
    box_edges(ax, (0,0,0), (rack.width,rack.depth,rack.height), color="black", linewidth=1.5)
    for comp in rack.components:
        box_edges(ax, comp.origin, comp.size, color="dimgray", linewidth=1.5)
        for region in comp.regions:
            color = "deepskyblue" if region.kind.lower() == "air" else "orangered"
            box_edges(ax, region.origin, region.size, color=color, linewidth=2, linestyle="--")


def set_rack_axes(ax, rack: Rack):
    ax.set_xlim(0, rack.width); ax.set_ylim(0, rack.depth); ax.set_zlim(0, rack.height)
    ax.set_box_aspect((rack.width, rack.depth, rack.height))
    ax.set_xlabel("Width (m)"); ax.set_ylabel("Depth (m)"); ax.set_zlabel("Height (m)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sim", default="simulation.csv")
    parser.add_argument("--rack", default="output.txt")
    parser.add_argument("--step", type=int, default=0, help="Step value; -1 selects final step")
    parser.add_argument("--stride", type=int, default=1, help="Plot every Nth cell in each direction")
    args = parser.parse_args()

    dx, dy, dz = read_spacing(args.sim)
    df = pd.read_csv(args.sim, skiprows=[1])
    required = {"step","x","y","z","vx","vy","vz","h","is_component"}
    missing = required - set(df.columns)
    if missing:
        raise SystemExit(f"simulation.csv is missing columns: {sorted(missing)}")

    df = df.drop_duplicates(subset=["step","x","y","z"], keep="last")
    steps = sorted(df["step"].unique())
    step = steps[-1] if args.step == -1 else args.step
    if step not in steps:
        raise SystemExit(f"Step {step} is not present. Available range: {steps[0]} to {steps[-1]}")
    frame = df[df["step"] == step].copy()

    try:
        rack = parse_rack(args.rack)
    except (FileNotFoundError, ValueError) as exc:
        print(f"Warning: {exc}; using mesh extents")
        rack = Rack((int(df.x.max())+1)*dx, (int(df.y.max())+1)*dy, (int(df.z.max())+1)*dz)

    stride = max(1, args.stride)
    spatial = ((frame.x % stride == 0) & (frame.y % stride == 0) & (frame.z % stride == 0))
    fluid = frame.is_component.astype(int) == 0
    moving = np.sqrt(frame.vx**2 + frame.vy**2 + frame.vz**2) > 0
    arrows = frame[spatial & fluid & moving].copy()
    arrows["speed"] = np.sqrt(arrows.vx**2 + arrows.vy**2 + arrows.vz**2)

    fig1 = plt.figure(figsize=(11, 8)); ax1 = fig1.add_subplot(111, projection="3d")
    draw_geometry(ax1, rack)
    if not arrows.empty:
        x=(arrows.x.to_numpy()+0.5)*dx; y=(arrows.y.to_numpy()+0.5)*dy; z=(arrows.z.to_numpy()+0.5)*dz
        speed=arrows.speed.to_numpy(); vmax=max(float(speed.max()), 1e-12)
        target=0.65*min(dx,dy,dz)
        u=arrows.vx.to_numpy()*target/vmax; v=arrows.vy.to_numpy()*target/vmax; w=arrows.vz.to_numpy()*target/vmax
        norm=plt.Normalize(0, vmax); colors=np.repeat(plt.cm.viridis(norm(speed)), 3, axis=0)
        ax1.quiver(x,y,z,u,v,w,colors=colors,length=1.0,normalize=False,arrow_length_ratio=0.35,linewidth=1.1)
        fig1.colorbar(plt.cm.ScalarMappable(norm=norm,cmap="viridis"), ax=ax1, shrink=0.62, pad=0.08, label="|v| (m/s)")
    else:
        ax1.text2D(0.03,0.95,"No nonzero fluid velocities at this step",transform=ax1.transAxes)
    set_rack_axes(ax1,rack); ax1.set_title(f"Velocity field, step={step}"); ax1.view_init(elev=24,azim=35)

    hframe = frame[spatial & (frame.h > 0)].copy()
    fig2 = plt.figure(figsize=(11, 8)); ax2 = fig2.add_subplot(111, projection="3d")
    draw_geometry(ax2,rack)
    if not hframe.empty:
        x=(hframe.x.to_numpy()+0.5)*dx; y=(hframe.y.to_numpy()+0.5)*dy; z=(hframe.z.to_numpy()+0.5)*dz
        h=hframe.h.to_numpy(); hmax=max(float(h.max()),1e-12)
        sc=ax2.scatter(x,y,z,c=h,cmap="inferno",vmin=0,vmax=hmax,s=42,marker="s",alpha=0.9,edgecolors="none")
        fig2.colorbar(sc,ax=ax2,shrink=0.62,pad=0.08,label="h (W/m²·K)")
    else:
        ax2.text2D(0.03,0.95,"No positive convection coefficients at this step",transform=ax2.transAxes)
    set_rack_axes(ax2,rack); ax2.set_title(f"Local convection coefficient h, step={step}"); ax2.view_init(elev=24,azim=35)
    plt.show()


if __name__ == "__main__":
    main()