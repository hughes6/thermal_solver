"""Small file-handoff regression test. Mock utilities do NOT validate CFD."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import sys

tool = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='cold-seed-reuse-') as tmp:
    root = Path(tmp)
    seed, target = root/'seed', root/'thermal'
    def put(case, name, text):
        p = case/name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    for case in (seed, target):
        for name in ('constant/regionProperties', 'constant/g', 'system/decomposeParDict',
                     'system/controlDict', 'constant/fluid/polyMesh/points',
                     'constant/fluid/polyMesh/boundary','constant/fluid/polyMesh/faces',
                     'constant/fluid/polyMesh/owner','constant/fluid/polyMesh/neighbour'):
            put(case, name, 'identical mesh/config\n')
        put(case, '0/fluid/T', 'ambient fluid 293.15\n')
        put(case, '0/solid/T', 'ambient solid 293.15\n')
        put(case, 'constant/solid/fvOptions', 'target watts 60\n')
    fields = ('U','p','p_rgh','phi','rho','k','omega','nut','alphat')
    for name in fields:
        put(seed, '1/fluid/'+name, 'developed '+name+'\n')
        put(target, '0/fluid/'+name, 'initial '+name+'\n')
    # Version 2 uses case-relative filenames and excludes decomposition count.
    names = ['constant/regionProperties','constant/g'] + [
        'constant/fluid/polyMesh/'+f for f in ('points','boundary','faces','owner','neighbour')]
    lines = ''.join(hashlib.sha256((seed/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names)
    digest = hashlib.sha256(lines.encode()).hexdigest()
    put(seed,'.cold_flow_seed_manifest',f'version 2\nseed_time 1\ngeometry_sha256 {digest}\nprocesses 2\n')
    put(seed,'.cold_flow_seed_complete','')
    fake = root/'launcher'
    fake.write_text('''#!/usr/bin/env python3
import sys, pathlib, shutil
a=sys.argv[1:]
if a[0]=='decomposePar':
 c=pathlib.Path(a[a.index('-case')+1])
 for r in range(4):
  shutil.copytree(c/'0',c/('processor'+str(r))/'0')
elif a[0]!='foamDictionary':
 raise SystemExit('Unexpected mock command '+str(a))
''')
    fake.chmod(0o755)
    env=dict(os.environ,OPENFOAM_LAUNCHER=str(fake))
    result=subprocess.run(['bash',str(tool),str(seed),str(target),'4'],env=env,text=True,capture_output=True)
    print(result.stdout,result.stderr)
    assert result.returncode==0, f'import failed: {result.returncode}'
    for rank in range(4):
        for f in fields:
            assert (target/f'processor{rank}/0/fluid/{f}').read_bytes()==(seed/f'1/fluid/{f}').read_bytes()
        assert (target/f'processor{rank}/0/fluid/T').read_text()=='ambient fluid 293.15\n'
        assert (target/f'processor{rank}/0/solid/T').read_text()=='ambient solid 293.15\n'
    assert (target/'constant/solid/fvOptions').read_text()=='target watts 60\n'
    assert (target/'.fan_ramp_complete').exists(), 'import would restart the fan ramp'
    before={str(p.relative_to(target)):p.read_bytes() for p in target.rglob('*') if p.is_file()}
    rejected=subprocess.run(['bash',str(tool),str(seed),str(target),'4'],env=env,capture_output=True)
    assert rejected.returncode!=0, 'used target must be refused'
    after={str(p.relative_to(target)):p.read_bytes() for p in target.rglob('*') if p.is_file()}
    assert before==after, 'refusal modified target data'
    rejected=subprocess.run(['bash',str(tool),str(seed),str(seed),'4'],env=env,capture_output=True)
    assert rejected.returncode!=0, 'source=target must be refused'
    print('PASS: 2-rank seed to 4-rank fixture; fields preserved; ambient T and target watts preserved; fan ramp skipped; existing target and self-import rejected.')
