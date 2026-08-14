"""Resolve the most recently generated Thermal Sim case without retyping it."""
from __future__ import annotations

import json
import os
from pathlib import Path

METADATA_NAME = ".thermal_sim_last_run.json"


def metadata_candidates() -> list[Path]:
    override = os.environ.get("THERMAL_SIM_RUN_METADATA")
    candidates = [Path(override).expanduser()] if override else []
    current = Path.cwd().resolve()
    candidates.extend(parent / METADATA_NAME for parent in (current, *current.parents))
    repository = Path(__file__).resolve().parents[1]
    candidates.append(repository / METADATA_NAME)
    result = []
    for candidate in candidates:
        candidate = candidate.resolve()
        if candidate not in result:
            result.append(candidate)
    return result


def load_last_run(metadata_path: Path | None = None) -> tuple[dict, Path]:
    candidates = [metadata_path.expanduser().resolve()] if metadata_path else metadata_candidates()
    for candidate in candidates:
        if not candidate.is_file():
            continue
        try:
            data = json.loads(candidate.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise ValueError(f"Invalid Thermal Sim run metadata {candidate}: {exc}") from exc
        if data.get("schema_version") != 1:
            raise ValueError(f"Unsupported Thermal Sim metadata schema in {candidate}")
        return data, candidate
    searched = "\n  ".join(str(path) for path in candidates)
    raise FileNotFoundError(
        f"No {METADATA_NAME} was found. Run model_runner first or provide --case."
        f"\nSearched:\n  {searched}"
    )


def resolve_case(explicit=None, metadata_path: Path | None = None) -> Path:
    """Use an explicit case unchanged, otherwise resolve the last OpenFOAM run."""
    if explicit:
        return Path(explicit).expanduser().resolve()
    data, source = load_last_run(metadata_path)
    value = data.get("case_directory", "")
    if not value:
        raise ValueError(
            f"The latest run in {source} used backend={data.get('backend', 'unknown')!r} "
            "and has no OpenFOAM case. Provide --case explicitly."
        )
    case = Path(value).expanduser().resolve()
    if not (case / "system" / "controlDict").is_file():
        raise FileNotFoundError(
            f"The last-run metadata points to a missing/invalid OpenFOAM case: {case}"
        )
    print(f"Using last OpenFOAM case from {source}: {case}")
    return case
