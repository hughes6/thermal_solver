"""Check the tiny 0.025 m fine-grid thermal branch after real mapping."""
from pathlib import Path
import sys, re, struct, json, math
root=Path(sys.argv[1]); case=root/'fine60'
def field(path,n,components=1):
    b=path.read_bytes()
    m=re.search(rb'internalField\s+uniform\s+([^;]+);',b)
    if m:
        values=[float(v) for v in m[1].strip(b'() ').split()]*n
    else:
        m=re.search(rb'internalField\s+nonuniform\s+List<(?:scalar|vector)>\s+(\d+)\s*\(',b)
        assert m and int(m[1])==n,path
        if re.search(rb'format\s+binary',b):
            assert b'LSB;label=32;scalar=64' in b
            values=list(struct.unpack('<'+'d'*(n*components),b[m.end():m.end()+8*n*components]))
        else:
            values=[float(v) for v in re.findall(rb'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?',b[m.end():].split(b';',1)[0])][:n*components]
    assert len(values)==n*components and all(math.isfinite(v) for v in values),path
    return values
times=[p for p in case.iterdir() if p.is_dir() and re.fullmatch(r'[0-9]+(?:\.[0-9]+)?',p.name)]
end=max(times,key=lambda p:float(p.name))
for region,n in [('fluid',960),('heater_0',64)]:
    assert max(abs(t-293.15) for t in field(case/'0'/region/'T',n))<1e-10
u0=field(case/'0/fluid/U',960,3); u1=field(end/'fluid/U',960,3)
delta=max(abs(a-b) for a,b in zip(u0,u1)); assert delta<1e-12
energy=sum(t-293.15 for t in field(end/'heater_0/T',64))*0.025**3*1000*1000
expected=60*float(end.name); assert abs(energy/expected-1)<0.02
result={'ambient_initial_T':True,'velocity_delta':delta,'solid_energy_J':energy,'expected_J':expected,'fine_fluid_cells':960,'thermal_end_time':float(end.name)}
print(json.dumps(result,indent=2))
(root/'mapping-physics-results.json').write_text(json.dumps(result,indent=2)+'\n')
