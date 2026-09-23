"""Compatibility and coverage checks for OpenFOAM airflow interpolation."""
import argparse
import math
from pathlib import Path
import re
import struct

MARKER = 'thermalSimMappingCoverage'

def patches(case):
    text = (case/'constant/fluid/polyMesh/boundary').read_text()
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    result = {}
    for match in re.finditer(r'^\s*([\w.:-]+)\s*\{(.*?)^\s*\}', text, re.M | re.S):
        if not re.search(r'\bnFaces\s+\d+\s*;', match[2]):
            continue
        kind = re.search(r'\btype\s+(\w+)\s*;', match[2])
        if not kind:
            raise ValueError('Cannot read patch type: '+match[1])
        result[match[1]] = kind[1]
    if not result:
        raise ValueError('Cannot parse fluid boundary patches: '+str(case))
    return result

def preflight(source, target):
    a, b = patches(source), patches(target)
    if a != b:
        raise ValueError('Fluid patch names/types differ. Same physical layout is required. '
                         f'Source-only: {sorted(a.keys()-b.keys())}; target-only: {sorted(b.keys()-a.keys())}; '
                         f'changed types: {[k for k in a.keys() & b.keys() if a[k] != b[k]]}')

def marker(path, case, value):
    boundary = '\n'.join(f'    {name}\n    {{ type zeroGradient; }}' for name in patches(case))
    path.write_text('FoamFile\n{ version 2.0; format ascii; class volScalarField; '
                    f'object {MARKER}; }}\ndimensions [0 0 0 0 0 0 0];\n'
                    f'internalField uniform {value};\nboundaryField\n{{\n{boundary}\n}}\n')

def scalar_values(path):
    data = path.read_bytes()
    uniform = re.search(rb'internalField\s+uniform\s+([^;]+);', data)
    if uniform:
        yield float(uniform[1])
        return
    match = re.search(rb'internalField\s+nonuniform\s+List<scalar>\s+(\d+)\s*\(', data)
    if not match:
        raise ValueError('Cannot parse mapped coverage field')
    count = int(match[1])
    if count <= 0:
        raise ValueError('Empty coverage field')
    if re.search(rb'format\s+binary\s*;', data):
        arch = re.search(rb'arch\s+"([^"\n]+)"', data)
        if not arch:
            raise ValueError('Missing binary architecture')
        precision = re.search(rb'scalar=(32|64)', arch[1])
        if not precision:
            raise ValueError('Unsupported scalar precision')
        code = 'f' if precision[1] == b'32' else 'd'
        width = 4 if code == 'f' else 8
        order = '<' if b'LSB' in arch[1] else '>'
        raw = data[match.end():match.end()+count*width]
        if len(raw) != count*width:
            raise ValueError('Truncated coverage field')
        for (value,) in struct.iter_unpack(order+code, raw):
            yield value
    else:
        tokens = data[match.end():].split(b')', 1)[0].split()
        if len(tokens) != count:
            raise ValueError('Coverage field length mismatch')
        yield from map(float, tokens)

def verify(target):
    count = bad = 0
    for value in scalar_values(target/'0/fluid'/MARKER):
        count += 1
        if not math.isfinite(value) or abs(value-1.0) > 1e-6:
            bad += 1
    if not count or bad:
        raise ValueError(f'Mapping left {bad} of {count} coverage entries unmapped or invalid; qualification blocked.')
    print(f'Mapping coverage passed: {count} coverage entries, all equal to one.')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['preflight', 'seed', 'verify'])
    parser.add_argument('source', type=Path)
    parser.add_argument('target', type=Path)
    parser.add_argument('--time')
    args = parser.parse_args()
    if args.action == 'preflight':
        preflight(args.source, args.target)
    elif args.action == 'seed':
        if not args.time or not re.fullmatch(r'[0-9]+(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?', args.time):
            raise ValueError('Exact numeric source time required')
        marker(args.source/args.time/'fluid'/MARKER, args.source, 1)
        marker(args.target/'0/fluid'/MARKER, args.target, 0)
    else:
        verify(args.target)

if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
