"""Convert exported internal fanMomentumSource entries to cyclic fan jumps.

This is an explicit experimental case conversion, not an in-place exporter
default.  Run it after ``prepare_regions.sh`` has created the internal fan face
zones and before decomposition.  Original fvOptions files are retained with a
``.momentumSource`` suffix.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path


HEADER = """FoamFile
{
    format      ascii;
    class       dictionary;
    location    \"system\";
    object      createBafflesDict_internal_fan_pressure_jumps;
}

internalFacesOnly true;

baffles
{
"""


def matching_brace(text: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("unterminated OpenFOAM dictionary block")


def fan_blocks(text: str) -> list[tuple[str, int, int, str]]:
    blocks = []
    pattern = re.compile(
        r"(?m)^([A-Za-z_][A-Za-z0-9_]*)\s*\n\s*\{\s*\n"
        r"\s*type\s+fanMomentumSource\s*;"
    )
    for match in pattern.finditer(text):
        opening = text.find("{", match.start())
        closing = matching_brace(text, opening)
        blocks.append((match.group(1), match.start(), closing + 1,
                       text[opening:closing + 1]))
    return blocks


def curve_values(block: str) -> str:
    curve = re.search(r"\bfanCurve\s*\{", block)
    if not curve:
        raise ValueError("fanMomentumSource has no fanCurve block")
    opening = block.find("{", curve.start())
    body = block[opening:matching_brace(block, opening) + 1]
    values = re.search(r"\bvalues\s*(\(.*?\))\s*;", body, re.DOTALL)
    if not values:
        raise ValueError("fanCurve has no values table")
    return values.group(1)


def strip_fans(text: str, blocks: list[tuple[str, int, int, str]]) -> str:
    for _, start, end, _ in reversed(blocks):
        text = text[:start] + text[end:]
    return text


def baffle_entry(name: str, values: str) -> str:
    return f"""    {name}_pressure_jump
    {{
        type faceZone;
        zoneName {name}_faces;
        patchPairs
        {{
            type cyclic;
            patchFields
            {{
                p_rgh
                {{
                    type fan;
                    patchType cyclic;
                    mode volumeFlowRate;
                    phi phi;
                    rho rho;
                    jump uniform 0;
                    value uniform 101325;
                    jumpTable
                    {{
                        type table;
                        outOfBounds clamp;
                        values
                        {values};
                    }}
                }}
            }}
        }}
    }}
"""


def convert(case: Path) -> list[str]:
    fluid = case / "constant" / "fluid"
    source = fluid / "fvOptions.fullFan"
    if not source.is_file():
        source = fluid / "fvOptions"
    text = source.read_text(encoding="utf-8", errors="replace")
    blocks = fan_blocks(text)
    if not blocks:
        raise ValueError(f"no fanMomentumSource entries found in {source}")
    names = [name for name, *_ in blocks]
    curves = {
        name: [[float(q), float(dp)] for q, dp in re.findall(
            r"\(\s*([+\-0-9.eE]+)\s+([+\-0-9.eE]+)\s*\)",
            curve_values(block),
        )]
        for name, _, _, block in blocks
    }

    dictionary = HEADER
    for name, _, _, block in blocks:
        dictionary += baffle_entry(name, curve_values(block))
    dictionary += "}\n"
    output = case / "system" / "fluid" / "createBafflesDict"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(dictionary, encoding="utf-8", newline="\n")
    (fluid / "pressureJumpFanCurves.json").write_text(
        json.dumps({"fans": curves}, indent=2) + "\n", encoding="utf-8"
    )
    scaler_source = Path(__file__).with_name("scale_pressure_jump_fans.py")
    shutil.copy2(scaler_source, case / "scale_pressure_jump_fans.py")
    apply_script = case / "apply_internal_fan_pressure_jumps.sh"
    apply_script.write_text(
        """#!/usr/bin/env bash
set -euo pipefail
case_dir="$(cd "$(dirname "$0")" && pwd)"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"
"$foam_launcher" createBaffles -case "$case_dir" -region fluid -overwrite
# createBaffles clears face sets.  Rebuild the CHT interface set required by
# the decomposition constraint before running the generated parallel runner.
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 \\
    -dict "$case_dir/system/topoSetDict_fluid_interfaces"
"$foam_launcher" checkMesh -case "$case_dir" -allRegions \\
    -allGeometry -allTopology
""",
        encoding="utf-8", newline="\n")

    for path in (fluid / "fvOptions", fluid / "fvOptions.fullFan",
                 fluid / "fvOptions.flowOnly"):
        if not path.is_file():
            continue
        original = path.with_name(path.name + ".momentumSource")
        if original.exists():
            raise FileExistsError(f"refusing to overwrite backup {original}")
        shutil.copy2(path, original)
        current = path.read_text(encoding="utf-8", errors="replace")
        path.write_text(strip_fans(current, fan_blocks(current)),
                        encoding="utf-8", newline="\n")
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    args = parser.parse_args()
    names = convert(args.case.resolve())
    print(f"Converted {len(names)} internal fans: {', '.join(names)}")
    print("Next run bash apply_internal_fan_pressure_jumps.sh before "
          "decomposition.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
