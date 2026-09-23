"""Coverage parser/refusal tests and incomplete-preparation runner guard."""
from pathlib import Path
import importlib.util
import subprocess
import sys
import tempfile
import shutil
import struct

checker=Path(sys.argv[1]).resolve()
spec=importlib.util.spec_from_file_location('coverage_checks',checker)
checks=importlib.util.module_from_spec(spec); spec.loader.exec_module(checks)
with tempfile.TemporaryDirectory(prefix='mapped-checks-') as tmp:
    root=Path(tmp)
    for name in ('source','target'):
        case=root/name
        (case/'constant/fluid/polyMesh').mkdir(parents=True)
        (case/'0/fluid').mkdir(parents=True)
        (case/'constant/fluid/polyMesh/boundary').write_text('1\n(\ninlet\n{\n type patch;\n nFaces 2;\n startFace 0;\n}\n)\n')
    source,target=root/'source',root/'target'
    checks.preflight(source,target)
    boundary=target/'constant/fluid/polyMesh/boundary'
    boundary.write_text(boundary.read_text().replace('inlet','changed_inlet'))
    try: checks.preflight(source,target)
    except ValueError: pass
    else: raise AssertionError('Changed patch accepted')
    path=target/'0/fluid'/checks.MARKER
    for values,accepted in [([1.,1.],True),([1.,0.],False),([float('nan'),1.],False)]:
        path.write_bytes(b'FoamFile { format binary; arch "LSB;label=32;scalar=64"; }\ninternalField nonuniform List<scalar>\n2\n('+struct.pack('<2d',*values)+b');\n')
        try: checks.verify(target)
        except ValueError:
            assert not accepted
        else: assert accepted
    if len(sys.argv)>2:
        runner=root/'run_parallel.sh'
        shutil.copyfile(sys.argv[2],runner)
        (root/'.airflow_reuse_preparation_pending').touch()
        result=subprocess.run(['bash',str(runner),'2','--cold-flow-seed','0.2'],capture_output=True,text=True)
        assert result.returncode!=0 and 'preparation is incomplete' in result.stderr, result
print('PASS: changed patches, missing coverage, NaN coverage and incomplete preparation refused')
