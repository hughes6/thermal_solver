"""Numerical checks for the 128-cell OpenFOAM reuse fixture (not a rack validation)."""
import json, math, re, struct, sys
from pathlib import Path

root=Path(sys.argv[1])
def latest(case):
    return max((p for p in case.iterdir() if p.is_dir() and re.fullmatch(r'[0-9]+(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?',p.name)),key=lambda p:float(p.name))
def field(p,n,components=1):
    b=p.read_bytes()
    uniform=re.search(rb'internalField\s+uniform\s+([^;]+);',b)
    if uniform:
        vals=[float(v) for v in uniform[1].strip(b'() ').split()]
        assert len(vals)==components
        return vals*n
    m=re.search(rb'internalField\s+nonuniform\s+List<(scalar|vector)>\s+(\d+)\s*\(',b)
    assert m, p
    count=int(m[2])*components
    assert int(m[2])==n,(p,int(m[2]),n)
    if re.search(rb'format\s+binary',b):
        assert b'LSB;label=32;scalar=64' in b
        vals=list(struct.unpack('<'+'d'*count,b[m.end():m.end()+8*count]))
    else:
        chunk=b[m.end():].split(b';',1)[0]
        vals=[float(v) for v in re.findall(rb'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?',chunk)][:count]
    assert len(vals)==count and all(math.isfinite(v) for v in vals),p
    return vals

ambient=293.15
checks={}
seed=root/'seed'; thermal=root/'thermal'; branch=root/'load60'; qualification=root/'load60.heated-flow-check'
for case,label in ((seed,'cold'),(qualification,'qualified')):
    t=(case/'.cold_flow_seed_manifest').read_text().split('seed_time ')[1].split()[0]
    temperature=[]
    for region,n in (('fluid',120),('heater_0',8)):
        temperature+=field(case/t/region/'T',n)
    drift=max(abs(v-ambient) for v in temperature)
    assert drift<0.002,(label,drift)
    checks[label+'_accepted_time']=float(t)
    checks[label+'_max_temperature_drift_K']=drift

for case,label,watts in ((thermal,'donor',float(sys.argv[2]) if len(sys.argv)>2 else 100),(branch,'branch',float(sys.argv[3]) if len(sys.argv)>3 else 60)):
    end=latest(case)
    for region,n in (('fluid',120),('heater_0',8)):
        t0=field(case/'0'/region/'T',n)
        assert max(abs(t-ambient) for t in t0)<1e-10,(label,region,'not ambient')
    u0=field(case/'0/fluid/U',120,3); u1=field(end/'fluid/U',120,3)
    delta=max(abs(a-b) for a,b in zip(u0,u1))
    assert delta<1e-12,(label,'held velocity changed',delta)
    ts=field(end/'heater_0/T',8)
    # 8 equal 0.05^3 m3 cells, rho_s=1000 kg/m3, Cp_s=1000 J/kg/K.
    solid_energy=sum(t-ambient for t in ts)*0.05**3*1000*1000
    expected=watts*float(end.name)
    assert solid_energy>0 and abs(solid_energy/expected-1)<0.02,(label,solid_energy,expected)
    checks[label+'_solid_energy_J']=solid_energy
    checks[label+'_input_energy_J']=expected
    checks[label+'_held_velocity_max_delta_m_s']=delta
    checks[label+'_max_solid_temperature_K']=max(ts)
assert (root/'donor.before.sha256').read_bytes()==(root/'donor.after.sha256').read_bytes()
checks['donor_unchanged']=True
checks['scope']='128-cell short-time energy and flow reuse regression; not production geometry/convergence validation'
print(json.dumps(checks,indent=2))
(root/'physics-results.json').write_text(json.dumps(checks,indent=2)+'\n')
