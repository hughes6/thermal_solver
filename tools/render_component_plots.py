"""Render and inventory every component plus the full rack geometry plot."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
from importlib import metadata
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys


COMPONENT_PATTERN = re.compile(r"^Component\s+\d+:\s*(.+?)\s*$")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def component_names(path: Path) -> list[str]:
    names = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = COMPONENT_PATTERN.match(line.strip())
        if match:
            names.append(match.group(1))
    if not names:
        raise ValueError(f"no component blocks found in {path}")
    return names


def filename_slug(name: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", name.casefold()).strip("_")
    return slug or "component"


def package_version(name: str) -> str | None:
    try:
        return metadata.version(name)
    except metadata.PackageNotFoundError:
        return None


def git_revision(project_root: Path) -> dict[str, object]:
    def git(*arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["git", *arguments],
            cwd=project_root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    head = git("rev-parse", "HEAD")
    status = git("status", "--porcelain")
    return {
        "head": head.stdout.strip() if head.returncode == 0 else None,
        "dirty": bool(status.stdout.strip()) if status.returncode == 0 else None,
    }


def run_plot(command: list[str], project_root: Path) -> str:
    environment = os.environ.copy()
    environment["MPLBACKEND"] = "Agg"
    environment["PYTHONHASHSEED"] = "0"
    result = subprocess.run(
        command,
        cwd=project_root,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        rendered = subprocess.list2cmdline(command)
        raise RuntimeError(
            f"plot command failed with exit {result.returncode}:\n"
            f"{rendered}\n{result.stdout}"
        )
    return result.stdout.strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("geometry", type=Path, help="exported geometry.txt/output.txt")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--no-rack",
        action="store_true",
        help="render only the component plots",
    )
    parser.add_argument(
        "--resume",
        action="store_true",
        help="reuse existing nonempty PNGs and render only missing outputs",
    )
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parents[1]
    source_geometry = args.geometry.expanduser().resolve()
    if not source_geometry.is_file():
        parser.error(f"geometry input does not exist: {source_geometry}")
    output_dir = args.output_dir.expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    preserved_geometry = output_dir / "geometry_input.txt"
    if source_geometry != preserved_geometry:
        shutil.copyfile(source_geometry, preserved_geometry)
    names = component_names(preserved_geometry)

    component_plotter = project_root / "plot" / "plot_component.py"
    rack_plotter = project_root / "plot" / "plot.py"
    outputs = []
    command_log = []
    for index, name in enumerate(names, start=1):
        output = output_dir / f"component_{index:02d}_{filename_slug(name)}.png"
        command = [
            sys.executable,
            str(component_plotter),
            "--input",
            str(preserved_geometry),
            "--component-index",
            str(index),
            "--save",
            "--output",
            str(output),
        ]
        reused = args.resume and output.is_file() and output.stat().st_size > 0
        stdout = "" if reused else run_plot(command, project_root)
        command_log.append(
            {"command": command, "stdout": stdout, "reused": reused}
        )
        outputs.append(
            {
                "kind": "component",
                "component_index": index,
                "component_name": name,
                "file": output.name,
                "bytes": output.stat().st_size,
                "sha256": sha256(output),
            }
        )

    if not args.no_rack:
        output = output_dir / "rack.png"
        command = [
            sys.executable,
            str(rack_plotter),
            "--input",
            str(preserved_geometry),
            "--save",
            "--output",
            str(output),
        ]
        reused = args.resume and output.is_file() and output.stat().st_size > 0
        stdout = "" if reused else run_plot(command, project_root)
        command_log.append(
            {"command": command, "stdout": stdout, "reused": reused}
        )
        outputs.append(
            {
                "kind": "rack",
                "file": output.name,
                "bytes": output.stat().st_size,
                "sha256": sha256(output),
            }
        )

    manifest = {
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "geometry": {
            "source_path": str(source_geometry),
            "preserved_file": preserved_geometry.name,
            "bytes": preserved_geometry.stat().st_size,
            "sha256": sha256(preserved_geometry),
            "component_count": len(names),
        },
        "plotters": {
            "component": {
                "path": str(component_plotter.relative_to(project_root)),
                "sha256": sha256(component_plotter),
            },
            "rack": {
                "path": str(rack_plotter.relative_to(project_root)),
                "sha256": sha256(rack_plotter),
            },
            "batch": {
                "path": str(Path(__file__).resolve().relative_to(project_root)),
                "sha256": sha256(Path(__file__).resolve()),
            },
        },
        "environment": {
            "python": platform.python_version(),
            "platform": platform.platform(),
            "matplotlib": package_version("matplotlib"),
            "numpy": package_version("numpy"),
            "pandas": package_version("pandas"),
        },
        "source_revision": git_revision(project_root),
        "commands": command_log,
        "outputs": outputs,
    }
    manifest_path = output_dir / "PLOT_MANIFEST.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    readme = [
        "# Updated component plots",
        "",
        "These files were rendered from the preserved `geometry_input.txt`.",
        "All component air regions use `tab:cyan` in both fill and edge color.",
        "`PLOT_MANIFEST.json` records the input, plotter and output hashes,",
        "tool versions, source revision, and exact commands.",
        "",
        "Re-render from the project root with:",
        "",
        "```powershell",
        (
            "python tools/render_component_plots.py "
            f"\"{preserved_geometry}\" --output-dir \"{output_dir}\""
        ),
        "```",
        "",
        f"Component count: {len(names)}",
        "",
    ]
    (output_dir / "README.md").write_text("\n".join(readme), encoding="utf-8")
    print(f"Rendered {len(outputs)} plots in {output_dir}")
    print(f"Manifest: {manifest_path}")


if __name__ == "__main__":
    main()
