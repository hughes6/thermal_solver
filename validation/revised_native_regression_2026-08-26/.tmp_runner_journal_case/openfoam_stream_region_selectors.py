#!/usr/bin/env python3
"""Project exported selector masks into split OpenFOAM regions with bounded RAM.

``splitMeshRegions`` reads every field in the root time directory at once.  A
large rack export contains many one-byte logical selector masks represented as
full scalar fields, which can dominate preparation memory.  This tool stages
only those generated selector fields outside ``0`` before the split and then
recreates each required region field by following OpenFOAM's exact
``cellRegionAddressing`` map.

The workflow is intentionally fail closed: selector dictionaries and generated
preparation commands must agree, every selected root cell must belong to the
declared target region, addressing lists must be in range and strictly
increasing, and output is replaced atomically only after all checks pass.
"""

from __future__ import annotations

import argparse
import array
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import sys
from typing import BinaryIO, Iterable, NamedTuple


SAFE_WORD = re.compile(r"^[A-Za-z0-9_]+$")
SPLIT_STATE_SCHEMA = "thermal-sim-openfoam-split-state-v1"
MATERIALIZATION_SCHEMA = "thermal-sim-openfoam-stream-region-selectors-v2"
SPLIT_ROOT_MESH_FILES = (
    "points",
    "faces",
    "owner",
    "neighbour",
    "boundary",
    "cellZones",
)
SPLIT_REGION_MESH_FILES = (
    "points",
    "faces",
    "owner",
    "neighbour",
    "boundary",
    "cellRegionAddressing",
    "faceRegionAddressing",
    "pointRegionAddressing",
)
SPLIT_REGION_HASHED_FILES = (
    "points",
    "cellRegionAddressing",
    "faceRegionAddressing",
    "pointRegionAddressing",
)
FIELD_SOURCE = re.compile(
    r"\bsource\s+fieldToCell\s*;(?:(?!\bsource\b).)*?"
    r"\bfield\s+([A-Za-z0-9_]+)\s*;",
    re.DOTALL,
)
RUN_TOPOSET = re.compile(
    r"^\s*run_toposet\s+-case\s+\"\$case_dir\"\s+"
    r"-region\s+([A-Za-z0-9_]+)\s+"
    r"(?:-time\s+0|-latestTime)\s+"
    r"-dict\s+\"\$case_dir/system/(topoSetDict_[A-Za-z0-9_]+)\"\s*$",
    re.MULTILINE,
)
DIRECT_TOPOSET = re.compile(
    r"^\s*\"\$foam_launcher\"\s+topoSet\s+"
    r"-case\s+\"\$case_dir\"\s+"
    r"-region\s+([A-Za-z0-9_]+)\s+"
    r"(?:-time\s+0|-latestTime)\s+"
    r"-dict\s+\"\$case_dir/system/(topoSetDict_[A-Za-z0-9_]+)\"\s*$",
    re.MULTILINE,
)


class SelectorError(RuntimeError):
    """A fail-closed selector staging or projection error."""


class SelectorMapping(NamedTuple):
    field: str
    region: str
    dictionary: str


def _safe_case(case: Path) -> Path:
    resolved = case.resolve()
    if not resolved.is_dir():
        raise SelectorError(f"OpenFOAM case directory does not exist: {resolved}")
    if not (resolved / "prepare_regions.sh").is_file():
        raise SelectorError(f"Missing generated prepare_regions.sh: {resolved}")
    return resolved


def discover_mappings(case: Path) -> list[SelectorMapping]:
    """Discover every fieldToCell selector and its generated target region."""

    case = _safe_case(case)
    prepare_text = (case / "prepare_regions.sh").read_text(
        encoding="utf-8", errors="strict"
    )
    invocations: dict[str, str] = {}
    generated_commands = (
        RUN_TOPOSET.findall(prepare_text)
        + DIRECT_TOPOSET.findall(prepare_text)
    )
    for region, dictionary in generated_commands:
        if dictionary in invocations:
            raise SelectorError(
                f"Duplicate generated topoSet invocation for {dictionary}"
            )
        invocations[dictionary] = region

    mappings: list[SelectorMapping] = []
    seen_fields: set[str] = set()
    selector_dicts: set[str] = set()
    for path in sorted((case / "system").glob("topoSetDict_*")):
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="strict")
        matches = FIELD_SOURCE.findall(text)
        if not matches:
            continue
        if len(set(matches)) != 1:
            raise SelectorError(
                f"Selector dictionary must reference exactly one field: {path}"
            )
        dictionary = path.name
        selector_dicts.add(dictionary)
        if dictionary not in invocations:
            raise SelectorError(
                f"No generated target-region invocation for {dictionary}"
            )
        field = matches[0]
        region = invocations[dictionary]
        if not SAFE_WORD.fullmatch(field) or not SAFE_WORD.fullmatch(region):
            raise SelectorError(
                f"Unsafe selector field or region name: {field!r}, {region!r}"
            )
        if field in seen_fields:
            raise SelectorError(f"Selector field is mapped more than once: {field}")
        seen_fields.add(field)
        mappings.append(SelectorMapping(field, region, dictionary))

    if not mappings:
        raise SelectorError("No generated fieldToCell selector mappings were found")
    unexpected = sorted(selector_dicts - invocations.keys())
    if unexpected:
        raise SelectorError(
            "Uninvoked selector dictionaries: " + ", ".join(unexpected)
        )
    return mappings


def _atomic_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp.{os.getpid()}")
    try:
        with temporary.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(payload, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def _create_once_json(path: Path, payload: object) -> None:
    """Publish JSON without replacing an existing evidence path."""

    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        raise SelectorError(f"Refusing to overwrite create-once evidence: {path}")
    temporary = path.with_name(f".{path.name}.tmp.{os.getpid()}")
    try:
        with temporary.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(payload, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        try:
            os.link(temporary, path)
        except FileExistsError as error:
            raise SelectorError(
                f"Refusing to overwrite create-once evidence: {path}"
            ) from error
    finally:
        if temporary.exists():
            temporary.unlink()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def _required_file_record(case: Path, relative: Path) -> dict[str, object]:
    path = case / relative
    if not path.is_file():
        raise SelectorError(f"Missing required split-mesh file: {path}")
    return {
        "path": relative.as_posix(),
        "bytes": path.stat().st_size,
        "sha256": _sha256_file(path),
    }


def expected_split_regions(case: Path) -> list[str]:
    """Read the exact generated fluid/solid inventory from regionProperties."""

    case = _safe_case(case)
    path = case / "constant" / "regionProperties"
    if not path.is_file():
        raise SelectorError(f"Missing generated regionProperties: {path}")
    text = path.read_text(encoding="utf-8", errors="strict")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\r\n]*", "", text)
    categories = re.findall(r"\b(fluid|solid)\s*\(([^()]*)\)", text)
    if len(categories) != 2 or {name for name, _body in categories} != {
        "fluid",
        "solid",
    }:
        raise SelectorError(
            f"regionProperties must contain exactly one fluid and one solid list: {path}"
        )
    regions: list[str] = []
    for _category, body in categories:
        for word in body.split():
            if not SAFE_WORD.fullmatch(word):
                raise SelectorError(f"Unsafe split-region name {word!r} in {path}")
            if word in regions:
                raise SelectorError(f"Duplicate split-region name {word!r} in {path}")
            regions.append(word)
    if "fluid" not in regions or len(regions) < 2:
        raise SelectorError(
            f"Bounded-memory split requires fluid and at least one solid region: {path}"
        )
    return regions


def current_split_state(case: Path) -> dict[str, object]:
    """Hash the split inputs plus the complete expected region-mesh inventory."""

    case = _safe_case(case)
    regions = expected_split_regions(case)
    root_records = [
        _required_file_record(case, Path("constant") / "polyMesh" / name)
        for name in SPLIT_ROOT_MESH_FILES
    ]
    region_inventory: dict[str, list[str]] = {}
    region_hashes: dict[str, list[dict[str, object]]] = {}
    for region in regions:
        inventory: list[str] = []
        for name in SPLIT_REGION_MESH_FILES:
            relative = Path("constant") / region / "polyMesh" / name
            path = case / relative
            if not path.is_file():
                raise SelectorError(f"Missing required split-mesh file: {path}")
            inventory.append(relative.as_posix())
        region_inventory[region] = inventory
        # createBaffles intentionally rewrites fluid faces/owners/boundaries
        # after the split. Bind the immutable points and exact split addressing,
        # while treating every mutable topology file as a required inventory
        # member that the subsequent all-region checkMesh must still accept.
        region_hashes[region] = [
            _required_file_record(
                case, Path("constant") / region / "polyMesh" / name
            )
            for name in SPLIT_REGION_HASHED_FILES
        ]
    return {
        "region_properties": _required_file_record(
            case, Path("constant") / "regionProperties"
        ),
        "regions": regions,
        "root_mesh": root_records,
        "region_mesh_inventory": region_inventory,
        "region_mesh_hashes": region_hashes,
    }


def split_state_path(case: Path) -> Path:
    return case / ".openfoam_prepare_checkpoints" / "splitMeshRegions.state.json"


def record_split_state(case: Path) -> dict[str, object]:
    """Atomically publish a complete, content-bound split checkpoint."""

    case = _safe_case(case)
    payload: dict[str, object] = {
        "schema": SPLIT_STATE_SCHEMA,
        "status": "PASS",
        "state": current_split_state(case),
    }
    _atomic_json(split_state_path(case), payload)
    return payload


def verify_split_state(case: Path) -> dict[str, object]:
    """Reject a missing, malformed, incomplete, or stale split checkpoint."""

    case = _safe_case(case)
    path = split_state_path(case)
    if not path.is_file():
        raise SelectorError(f"Missing content-bound split checkpoint: {path}")
    try:
        payload = json.loads(path.read_text(encoding="utf-8", errors="strict"))
    except (json.JSONDecodeError, UnicodeError) as error:
        raise SelectorError(f"Unreadable split checkpoint {path}: {error}") from error
    if payload.get("schema") != SPLIT_STATE_SCHEMA or payload.get("status") != "PASS":
        raise SelectorError(f"Unsupported or non-PASS split checkpoint: {path}")
    actual = current_split_state(case)
    if payload.get("state") != actual:
        raise SelectorError(
            "Split checkpoint does not match the current root mesh, "
            "regionProperties, addressing, or region-mesh inventory"
        )
    return payload


def stage_selectors(case: Path) -> dict[str, object]:
    """Move all discovered root selector fields out of time 0 transactionally."""

    case = _safe_case(case)
    mappings = discover_mappings(case)
    source_root = case / "0"
    staging_root = case / ".openfoam_selector_fields"

    states: list[tuple[SelectorMapping, Path, Path]] = []
    for mapping in mappings:
        source = source_root / mapping.field
        staged = staging_root / mapping.field
        source_exists = source.is_file()
        staged_exists = staged.is_file()
        if source_exists and staged_exists:
            raise SelectorError(
                "Ambiguous selector state (both root and staged copies exist): "
                f"{mapping.field}"
            )
        if not source_exists and not staged_exists:
            raise SelectorError(
                f"Missing root and staged selector field: {mapping.field}"
            )
        states.append((mapping, source, staged))

    staging_root.mkdir(parents=True, exist_ok=True)
    moved = 0
    for _mapping, source, staged in states:
        if source.is_file():
            os.replace(source, staged)
            moved += 1

    records = [
        {
            **mapping._asdict(),
            "bytes": staged.stat().st_size,
            "sha256": _sha256_file(staged),
        }
        for mapping, _source, staged in states
    ]
    prior_path = staging_root / "staging.json"
    if prior_path.is_file():
        try:
            prior = json.loads(prior_path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeError) as error:
            raise SelectorError(f"Unreadable prior selector staging audit: {error}")
        if prior.get("schema") != "thermal-sim-openfoam-selector-staging-v1":
            raise SelectorError("Prior selector staging audit has an unsupported schema")
        if prior.get("records") != records:
            raise SelectorError(
                "Staged selector bytes or target mappings changed since the prior audit"
            )

    payload: dict[str, object] = {
        "schema": "thermal-sim-openfoam-selector-staging-v1",
        "status": "PASS",
        "case": str(case),
        "selector_count": len(mappings),
        "moved_this_run": moved,
        "mappings": [mapping._asdict() for mapping in mappings],
        "records": records,
    }
    _atomic_json(prior_path, payload)
    return payload


def _foam_header(data: bytes, path: Path) -> bytes:
    match = re.search(rb"\bFoamFile\s*\{.*?\}", data, re.DOTALL)
    if not match:
        raise SelectorError(f"Missing FoamFile header: {path}")
    return match.group(0)


def read_cell_region_addressing(path: Path) -> tuple[array.array, str]:
    """Read an OpenFOAM ASCII/binary labelList and return exact cell addresses."""

    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    header_match = re.search(rb"\bFoamFile\s*\{.*?\}", data, re.DOTALL)
    if not header_match:
        raise SelectorError(f"Missing FoamFile header: {path}")
    header = header_match.group(0)
    if not re.search(rb"\bclass\s+labelList\s*;", header):
        raise SelectorError(f"Expected labelList addressing: {path}")
    object_match = re.search(rb"\bobject\s+([A-Za-z0-9_]+)\s*;", header)
    if not object_match or object_match.group(1) != b"cellRegionAddressing":
        raise SelectorError(f"Wrong addressing object in {path}")
    format_match = re.search(rb"\bformat\s+(ascii|binary)\s*;", header)
    if not format_match:
        raise SelectorError(f"Missing addressing format in {path}")
    list_match = re.search(
        rb"\s*([0-9]+)\s*\(", data[header_match.end() :]
    )
    if not list_match:
        raise SelectorError(f"Missing addressing list in {path}")
    count = int(list_match.group(1))
    payload_start = header_match.end() + list_match.end()
    labels: array.array

    if format_match.group(1) == b"binary":
        arch_match = re.search(
            rb"\barch\s+\"(LSB|MSB);label=(32|64);scalar=(32|64)\"\s*;",
            header,
        )
        if not arch_match:
            raise SelectorError(f"Unsupported or missing binary arch in {path}")
        label_bits = int(arch_match.group(2))
        typecode = "i" if label_bits == 32 else "q"
        width = label_bits // 8
        payload_end = payload_start + count * width
        if payload_end > len(data):
            raise SelectorError(f"Truncated binary addressing payload: {path}")
        labels = array.array(typecode)
        labels.frombytes(data[payload_start:payload_end])
        file_little = arch_match.group(1) == b"LSB"
        host_little = sys.byteorder == "little"
        if file_little != host_little:
            labels.byteswap()
        trailing = data[payload_end:].lstrip()
        if not trailing.startswith(b")"):
            raise SelectorError(f"Missing binary addressing terminator: {path}")
    else:
        close = data.find(b")", payload_start)
        if close < 0:
            raise SelectorError(f"Missing ASCII addressing terminator: {path}")
        labels = array.array("q")
        for token in re.finditer(rb"[+-]?[0-9]+", data[payload_start:close]):
            labels.append(int(token.group(0)))
        if len(labels) != count:
            raise SelectorError(
                f"Addressing count mismatch in {path}: declared {count}, read {len(labels)}"
            )

    if len(labels) != count:
        raise SelectorError(
            f"Addressing count mismatch in {path}: declared {count}, read {len(labels)}"
        )
    previous = -1
    for value in labels:
        if value < 0 or value <= previous:
            raise SelectorError(
                f"Addressing must be non-negative and strictly increasing: {path}"
            )
        previous = value
    return labels, digest


def _read_nonempty_line(stream: BinaryIO, digest: hashlib._Hash) -> bytes:
    while True:
        line = stream.readline()
        if not line:
            raise SelectorError("Unexpected end of selector field")
        digest.update(line)
        stripped = line.strip()
        if stripped:
            return stripped


def _write_bytes(stream: BinaryIO, digest: hashlib._Hash, data: bytes) -> None:
    stream.write(data)
    digest.update(data)


def project_selector(
    source: Path,
    destination: Path,
    field: str,
    region: str,
    addresses: Iterable[int],
) -> dict[str, object]:
    """Project one exported ASCII 0/1 field through region cell addressing."""

    if not SAFE_WORD.fullmatch(field) or not SAFE_WORD.fullmatch(region):
        raise SelectorError(f"Unsafe field or region: {field!r}, {region!r}")
    address_list = addresses if isinstance(addresses, array.array) else array.array("q", addresses)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.name}.tmp.{os.getpid()}")
    source_digest = hashlib.sha256()
    output_digest = hashlib.sha256()
    source_selected = 0
    region_selected = 0
    root_count = -1

    try:
        with source.open("rb") as input_stream, temporary.open("wb") as output_stream:
            header_lines: list[bytes] = []
            while True:
                line = input_stream.readline()
                if not line:
                    raise SelectorError(f"Missing internalField in {source}")
                source_digest.update(line)
                header_lines.append(line)
                if re.search(
                    rb"\binternalField\s+nonuniform\s+List<scalar>\s*$",
                    line.strip(),
                ):
                    break
            header = b"".join(header_lines)
            foam_header = _foam_header(header, source)
            if not re.search(rb"\bformat\s+ascii\s*;", foam_header):
                raise SelectorError(f"Selector source must be ASCII: {source}")
            if not re.search(rb"\bclass\s+volScalarField\s*;", foam_header):
                raise SelectorError(f"Selector source is not volScalarField: {source}")
            object_match = re.search(
                rb"\bobject\s+([A-Za-z0-9_]+)\s*;", foam_header
            )
            if not object_match or object_match.group(1).decode("ascii") != field:
                raise SelectorError(f"Selector object/filename mismatch: {source}")

            count_line = _read_nonempty_line(input_stream, source_digest)
            if not count_line.isdigit():
                raise SelectorError(f"Invalid selector cell count in {source}")
            root_count = int(count_line)
            if _read_nonempty_line(input_stream, source_digest) != b"(":
                raise SelectorError(f"Invalid selector list opener in {source}")
            if address_list and address_list[-1] >= root_count:
                raise SelectorError(
                    f"Region address {address_list[-1]} exceeds selector count {root_count}: {source}"
                )

            output_header = (
                "FoamFile\n"
                "{\n"
                "    format      ascii;\n"
                "    class       volScalarField;\n"
                f'    location    "0/{region}";\n'
                f"    object      {field};\n"
                "}\n\n"
                "dimensions      [0 0 0 0 0 0 0];\n"
                "internalField   nonuniform List<scalar>\n"
                f"{len(address_list)}\n(\n"
            ).encode("ascii")
            _write_bytes(output_stream, output_digest, output_header)

            address_index = 0
            next_address = address_list[0] if address_list else -1
            output_chunk = bytearray()
            for root_index in range(root_count):
                token = input_stream.readline()
                if not token:
                    raise SelectorError(f"Truncated selector values in {source}")
                source_digest.update(token)
                value = token.strip()
                if value not in (b"0", b"1"):
                    raise SelectorError(
                        f"Selector value {value!r} is not exactly 0 or 1 in {source}"
                    )
                selected = value == b"1"
                source_selected += int(selected)
                if root_index == next_address:
                    output_chunk.extend(b"1\n" if selected else b"0\n")
                    region_selected += int(selected)
                    address_index += 1
                    next_address = (
                        address_list[address_index]
                        if address_index < len(address_list)
                        else -1
                    )
                    if len(output_chunk) >= 1024 * 1024:
                        _write_bytes(output_stream, output_digest, bytes(output_chunk))
                        output_chunk.clear()
            if output_chunk:
                _write_bytes(output_stream, output_digest, bytes(output_chunk))
            if address_index != len(address_list):
                raise SelectorError(f"Not all region addresses were projected: {source}")

            if _read_nonempty_line(input_stream, source_digest) != b")":
                raise SelectorError(f"Invalid selector list terminator in {source}")
            remainder = input_stream.read()
            source_digest.update(remainder)
            if b"boundaryField" not in remainder:
                raise SelectorError(f"Selector source lacks boundaryField: {source}")
            if source_selected != region_selected:
                raise SelectorError(
                    f"Selector {field} has {source_selected} selected root cells but only "
                    f"{region_selected} in declared region {region}; refusing lossy projection"
                )

            output_tail = (
                ")\n;\n"
                "boundaryField\n"
                "{\n"
                '    ".*"\n'
                "    {\n"
                "        type  calculated;\n"
                "        value uniform 0;\n"
                "    }\n"
                "}\n"
            ).encode("ascii")
            _write_bytes(output_stream, output_digest, output_tail)
            output_stream.flush()
            os.fsync(output_stream.fileno())

        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            temporary.unlink()

    return {
        "field": field,
        "region": region,
        "root_cell_count": root_count,
        "region_cell_count": len(address_list),
        "selected_cell_count": source_selected,
        "source_sha256": source_digest.hexdigest(),
        "output_sha256": output_digest.hexdigest(),
    }


def materialize_selectors(case: Path, audit_path: Path | None = None) -> dict[str, object]:
    """Create every required region selector from staged root fields."""

    case = _safe_case(case)
    destination = audit_path or (case / "selector_mapping_audit.json")
    if not destination.is_absolute():
        destination = case / destination
    # Never allow an earlier PASS to survive a partial or failed rebuild.  The
    # derived files are trusted only when the new, complete audit is published.
    if destination.exists() or destination.is_symlink():
        destination.unlink()

    mappings = discover_mappings(case)
    staging_root = case / ".openfoam_selector_fields"
    for mapping in mappings:
        root_source = case / "0" / mapping.field
        staged_source = staging_root / mapping.field
        if root_source.exists():
            raise SelectorError(
                f"Root selector would be loaded by splitMeshRegions: {root_source}"
            )
        if not staged_source.is_file():
            raise SelectorError(f"Missing staged selector field: {staged_source}")

    addresses: dict[str, array.array] = {}
    addressing_hashes: dict[str, str] = {}
    for region in sorted({mapping.region for mapping in mappings}):
        path = case / "constant" / region / "polyMesh" / "cellRegionAddressing"
        if not path.is_file():
            raise SelectorError(f"Missing split-region addressing: {path}")
        labels, digest = read_cell_region_addressing(path)
        addresses[region] = labels
        addressing_hashes[region] = digest

    records: list[dict[str, object]] = []
    root_count: int | None = None
    for mapping in mappings:
        record = project_selector(
            staging_root / mapping.field,
            case / "0" / mapping.region / mapping.field,
            mapping.field,
            mapping.region,
            addresses[mapping.region],
        )
        record["dictionary"] = mapping.dictionary
        record["addressing_sha256"] = addressing_hashes[mapping.region]
        record["dictionary_sha256"] = _sha256_file(
            case / "system" / mapping.dictionary
        )
        record["source_path"] = (
            Path(".openfoam_selector_fields") / mapping.field
        ).as_posix()
        record["addressing_path"] = (
            Path("constant") / mapping.region / "polyMesh" /
            "cellRegionAddressing"
        ).as_posix()
        record["output_path"] = (
            Path("0") / mapping.region / mapping.field
        ).as_posix()
        current_root_count = int(record["root_cell_count"])
        if root_count is None:
            root_count = current_root_count
        elif current_root_count != root_count:
            raise SelectorError(
                f"Selector root cell counts disagree: {root_count} vs {current_root_count}"
            )
        records.append(record)

    payload: dict[str, object] = {
        "schema": MATERIALIZATION_SCHEMA,
        "status": "PASS",
        "case": str(case),
        "root_cell_count": root_count,
        "selector_count": len(records),
        "mappings": [mapping._asdict() for mapping in mappings],
        "maximum_addressing_bytes": max(
            (len(values) * values.itemsize for values in addresses.values()),
            default=0,
        ),
        "total_addressing_bytes": sum(
            len(values) * values.itemsize for values in addresses.values()
        ),
        "records": records,
    }
    _atomic_json(destination, payload)
    return payload


def read_logical_scalar_field(path: Path) -> tuple[bytes, str]:
    """Return exact 0/1 internal values from an ASCII or binary scalar field."""

    data = path.read_bytes()
    raw_digest = hashlib.sha256(data).hexdigest()
    header = _foam_header(data, path)
    if not re.search(rb"\bclass\s+volScalarField\s*;", header):
        raise SelectorError(f"Expected volScalarField: {path}")
    format_match = re.search(rb"\bformat\s+(ascii|binary)\s*;", header)
    if not format_match:
        raise SelectorError(f"Missing scalar-field format: {path}")
    payload_match = re.search(
        rb"\binternalField\s+nonuniform\s+List<scalar>\s*"
        rb"([0-9]+)\s*\(\s*",
        data,
    )
    if not payload_match:
        raise SelectorError(f"Missing nonuniform scalar payload: {path}")
    count = int(payload_match.group(1))
    start = payload_match.end()

    if format_match.group(1) == b"binary":
        arch_match = re.search(
            rb"\barch\s+\"(LSB|MSB);label=(32|64);scalar=(32|64)\"\s*;",
            header,
        )
        if not arch_match:
            raise SelectorError(f"Unsupported or missing binary arch in {path}")
        scalar_bits = int(arch_match.group(3))
        typecode = "f" if scalar_bits == 32 else "d"
        width = scalar_bits // 8
        end = start + count * width
        if end > len(data):
            raise SelectorError(f"Truncated binary scalar payload: {path}")
        values = array.array(typecode)
        values.frombytes(data[start:end])
        file_little = arch_match.group(1) == b"LSB"
        if file_little != (sys.byteorder == "little"):
            values.byteswap()
        trailing = data[end:].lstrip()
        if not trailing.startswith(b")"):
            raise SelectorError(f"Missing binary scalar terminator: {path}")
        logical = bytearray(count)
        for index, value in enumerate(values):
            if value == 0.0:
                logical[index] = 0
            elif value == 1.0:
                logical[index] = 1
            else:
                raise SelectorError(
                    f"Selector reference contains non-logical value {value}: {path}"
                )
    else:
        close = data.find(b")", start)
        if close < 0:
            raise SelectorError(f"Missing ASCII scalar terminator: {path}")
        logical = bytearray()
        for token in re.finditer(rb"[^\s]+", data[start:close]):
            value = token.group(0)
            if value == b"0":
                logical.append(0)
            elif value == b"1":
                logical.append(1)
            else:
                raise SelectorError(
                    f"Selector reference contains non-logical token {value!r}: {path}"
                )
    if len(logical) != count:
        raise SelectorError(
            f"Scalar count mismatch in {path}: declared {count}, read {len(logical)}"
        )
    return bytes(logical), raw_digest


def verify_materialized_selectors(
    case: Path, audit_path: Path | None = None
) -> dict[str, object]:
    """Recompute every materialization binding before topoSet may consume it."""

    case = _safe_case(case)
    destination = audit_path or (case / "selector_mapping_audit.json")
    if not destination.is_absolute():
        destination = case / destination
    if not destination.is_file():
        raise SelectorError(f"Missing complete selector mapping audit: {destination}")
    try:
        payload = json.loads(
            destination.read_text(encoding="utf-8", errors="strict")
        )
    except (json.JSONDecodeError, UnicodeError) as error:
        raise SelectorError(
            f"Unreadable selector mapping audit {destination}: {error}"
        ) from error
    if (
        payload.get("schema") != MATERIALIZATION_SCHEMA
        or payload.get("status") != "PASS"
    ):
        raise SelectorError(
            f"Unsupported or non-PASS selector mapping audit: {destination}"
        )

    mappings = discover_mappings(case)
    expected_mappings = [mapping._asdict() for mapping in mappings]
    if payload.get("mappings") != expected_mappings:
        raise SelectorError("Selector mapping audit does not match generated dictionaries")
    records = payload.get("records")
    if not isinstance(records, list) or len(records) != len(mappings):
        raise SelectorError("Selector mapping audit has an incomplete record inventory")

    for mapping, record in zip(mappings, records):
        if not isinstance(record, dict):
            raise SelectorError("Selector mapping audit contains a non-object record")
        identity = {
            "field": mapping.field,
            "region": mapping.region,
            "dictionary": mapping.dictionary,
        }
        if any(record.get(key) != value for key, value in identity.items()):
            raise SelectorError(
                f"Selector mapping audit record order/identity changed: {mapping.field}"
            )
        expected_paths = {
            "source_path": (
                Path(".openfoam_selector_fields") / mapping.field
            ).as_posix(),
            "addressing_path": (
                Path("constant") / mapping.region / "polyMesh" /
                "cellRegionAddressing"
            ).as_posix(),
            "output_path": (
                Path("0") / mapping.region / mapping.field
            ).as_posix(),
        }
        if any(record.get(key) != value for key, value in expected_paths.items()):
            raise SelectorError(
                f"Selector mapping audit paths changed: {mapping.field}"
            )
        source = case / expected_paths["source_path"]
        addressing = case / expected_paths["addressing_path"]
        output = case / expected_paths["output_path"]
        dictionary = case / "system" / mapping.dictionary
        for required in (source, addressing, output, dictionary):
            if not required.is_file():
                raise SelectorError(
                    f"Materialized selector binding is missing: {required}"
                )
        if (case / "0" / mapping.field).exists():
            raise SelectorError(
                f"Root selector reappeared after split: {case / '0' / mapping.field}"
            )
        bindings = {
            "source_sha256": _sha256_file(source),
            "addressing_sha256": _sha256_file(addressing),
            "output_sha256": _sha256_file(output),
            "dictionary_sha256": _sha256_file(dictionary),
        }
        for key, value in bindings.items():
            if record.get(key) != value:
                raise SelectorError(
                    f"Materialized selector {mapping.field} has stale {key}"
                )
        logical, _raw_hash = read_logical_scalar_field(output)
        if len(logical) != record.get("region_cell_count"):
            raise SelectorError(
                f"Materialized selector cell count changed: {mapping.field}"
            )
        if sum(logical) != record.get("selected_cell_count"):
            raise SelectorError(
                f"Materialized selector selected-cell count changed: {mapping.field}"
            )
    if payload.get("selector_count") != len(mappings):
        raise SelectorError("Selector mapping audit selector_count is inconsistent")
    return payload


def summarize_oom_log(path: Path) -> dict[str, object]:
    """Quantify selector payload named in a retained splitMeshRegions OOM log."""

    text = path.read_text(encoding="utf-8", errors="strict")
    field_line = next(
        (line for line in text.splitlines() if line.startswith("Reading volScalarField:")),
        None,
    )
    if field_line is None:
        raise SelectorError(f"OOM log has no volScalarField inventory: {path}")
    fields = field_line.split(":", 1)[1].split()
    if fields.count("cellToRegion") != 1:
        raise SelectorError(f"OOM log has an unexpected cellToRegion inventory: {path}")
    selectors = [field for field in fields if field != "cellToRegion"]
    if len(selectors) != len(set(selectors)):
        raise SelectorError(f"OOM log selector inventory contains duplicates: {path}")
    scalar_match = re.search(r'Arch\s+:\s+"[^"]*scalar=(32|64)"', text)
    if not scalar_match:
        raise SelectorError(f"OOM log has no scalar-width architecture: {path}")
    scalar_bits = int(scalar_match.group(1))
    region_counts = [
        int(match.group(1))
        for match in re.finditer(r"(?m)^\s*[0-9]+\s+([0-9]+)\s*$", text)
    ]
    if not region_counts:
        raise SelectorError(f"OOM log has no region cell-count table: {path}")
    root_cells = sum(region_counts)
    selector_bytes = len(selectors) * root_cells * (scalar_bits // 8)
    return {
        "path": str(path.resolve()),
        "sha256": _sha256_file(path),
        "vol_scalar_field_count": len(fields),
        "selector_field_count": len(selectors),
        "selector_names_unique": True,
        "root_cell_count_from_region_table": root_cells,
        "region_count": len(region_counts),
        "scalar_bits": scalar_bits,
        "selector_internal_scalar_payload_bytes": selector_bytes,
        "selector_internal_scalar_payload_gib": selector_bytes / (1024 ** 3),
        "scope_note": (
            "This is the internal scalar payload named in the log, not a measured "
            "or predicted RSS reduction and not proof that the residual mesh split fits."
        ),
    }


def verify_existing_selectors(
    case: Path,
    audit_path: Path,
    oom_log: Path | None = None,
) -> dict[str, object]:
    """Compare streamed projections with every existing OpenFOAM split field."""

    case = _safe_case(case)
    mappings = discover_mappings(case)
    address_cache: dict[str, array.array] = {}
    address_hashes: dict[str, str] = {}
    records: list[dict[str, object]] = []
    campaign_digest = hashlib.sha256()
    selected_total = 0
    region_counts: dict[str, int] = {}

    with tempfile.TemporaryDirectory(prefix="openfoam_selector_verify_") as directory:
        temporary_root = Path(directory)
        for mapping in mappings:
            root_source = case / "0" / mapping.field
            staged_source = case / ".openfoam_selector_fields" / mapping.field
            if root_source.is_file() and staged_source.is_file():
                raise SelectorError(
                    f"Ambiguous root/staged selector during read-only audit: {mapping.field}"
                )
            source = root_source if root_source.is_file() else staged_source
            if not source.is_file():
                raise SelectorError(f"Missing selector source during audit: {mapping.field}")
            if mapping.region not in address_cache:
                addressing_path = (
                    case / "constant" / mapping.region / "polyMesh" /
                    "cellRegionAddressing"
                )
                labels, digest = read_cell_region_addressing(addressing_path)
                address_cache[mapping.region] = labels
                address_hashes[mapping.region] = digest

            projected_path = temporary_root / mapping.field
            projection = project_selector(
                source,
                projected_path,
                mapping.field,
                mapping.region,
                address_cache[mapping.region],
            )
            projected, _projected_raw_hash = read_logical_scalar_field(projected_path)
            reference_path = case / "0" / mapping.region / mapping.field
            if not reference_path.is_file():
                raise SelectorError(f"Missing existing split selector: {reference_path}")
            reference, reference_raw_hash = read_logical_scalar_field(reference_path)
            if projected != reference:
                raise SelectorError(
                    f"Streamed selector differs from existing split field: {mapping.field}"
                )
            logical_hash = hashlib.sha256(projected).hexdigest()
            campaign_digest.update(mapping.field.encode("ascii") + b"\0")
            campaign_digest.update(projected)
            selected = sum(projected)
            selected_total += selected
            region_counts[mapping.region] = region_counts.get(mapping.region, 0) + 1
            records.append(
                {
                    "field": mapping.field,
                    "region": mapping.region,
                    "dictionary": mapping.dictionary,
                    "region_cell_count": len(projected),
                    "selected_cell_count": selected,
                    "logical_sha256": logical_hash,
                    "source_sha256": projection["source_sha256"],
                    "existing_split_field_sha256": reference_raw_hash,
                    "addressing_sha256": address_hashes[mapping.region],
                    "status": "IDENTICAL",
                }
            )

    case_hashes = {}
    for relative in ("geometry.txt", "prepare_regions.sh", "provenance/manifest.txt"):
        path = case / relative
        if path.is_file():
            case_hashes[relative] = _sha256_file(path)
    payload: dict[str, object] = {
        "schema": "thermal-sim-openfoam-existing-selector-equivalence-v1",
        "status": "PASS",
        "case": str(case),
        "case_file_sha256": case_hashes,
        "case_mutated": False,
        "selector_count": len(records),
        "region_count": len(region_counts),
        "region_selector_counts": dict(sorted(region_counts.items())),
        "selected_cell_total": selected_total,
        "campaign_logical_sha256": campaign_digest.hexdigest(),
        "records": records,
        "limitation": (
            "Existing-field equivalence proves selector fidelity only. It does not "
            "prove that the corrected 19 mm split fits memory or validate its mesh."
        ),
    }
    if oom_log is not None:
        payload["retained_19mm_oom_inventory"] = summarize_oom_log(oom_log)
    _create_once_json(audit_path.resolve(), payload)
    return payload


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action",
        choices=(
            "discover",
            "stage",
            "record-split",
            "verify-split",
            "materialize",
            "verify-materialized",
            "verify-existing",
        ),
    )
    parser.add_argument("--case", required=True, type=Path)
    parser.add_argument(
        "--audit",
        type=Path,
        help="materialize audit path (relative paths are resolved inside the case)",
    )
    parser.add_argument(
        "--oom-log",
        type=Path,
        help="retained splitMeshRegions OOM log to inventory during verify-existing",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.action == "discover":
            payload: object = [
                mapping._asdict() for mapping in discover_mappings(args.case)
            ]
        elif args.action == "stage":
            payload = stage_selectors(args.case)
        elif args.action == "record-split":
            payload = record_split_state(args.case)
        elif args.action == "verify-split":
            payload = verify_split_state(args.case)
        elif args.action == "materialize":
            payload = materialize_selectors(args.case, args.audit)
        elif args.action == "verify-materialized":
            payload = verify_materialized_selectors(args.case, args.audit)
        else:
            if args.audit is None:
                raise SelectorError("verify-existing requires a create-once --audit path")
            payload = verify_existing_selectors(args.case, args.audit, args.oom_log)
        print(json.dumps(payload, indent=2, sort_keys=True))
        return 0
    except (OSError, UnicodeError, SelectorError, ValueError) as error:
        print(f"openfoam_stream_region_selectors: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
