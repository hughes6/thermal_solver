# Mapping verification, 2026-09-25

Tested v2.5 commit 047ef7b with real OpenFOAM 2606. Production cases were not
modified. Evidence directory in WSL:
`/home/hconner158/OpenFOAM/cases/mapping_validation_20260925`.

The heated coarse donor had 120 fluid cells; the new fine target had 960
fluid cells (128 to 1024 total cells). The fixture uses temperature-dependent
air, gravity and a 60 W solid heater in the target.

Results:

- Mapping completed with full coverage, and all donor file hashes matched.
- Initial target fluid and solid T files and fluid p/p_rgh matched the
  qualification case byte-for-byte after mapping.
- Fine airflow qualified at 0.25000000000000017 seconds: maximum Co
  0.0479365 against limit 0.5, relative velocity RMS change 0.0088602,
  direction and fan-domain checks passed.
- Thermal branch initial T was 293.15 K. At 0.05 seconds, solid energy gain
  was 2.99999301036 J against 3 J input; held velocity change was zero.
- A translated, disjoint target was rejected by the coverage check.
- Relaunching at the completed 0.05 endpoint refused to run another stage.
- Continuing to 0.1 seconds succeeded. The normal multirate runner correctly
  retained `airflow_refresh_pending` at that endpoint; this is not a claim
  that the final heated state is converged.

The original mapping/thermal probes and verify_mapping.py produced these
results. To repeat the initial mapping and thermal checks, use a new empty
test directory, following MAPPED_AIRFLOW_REUSE_STATUS.md. Do not rerun import
over this completed fixture. For a forward continuation from its current
state, load OpenFOAM and run:

```bash
cd /home/hconner158/OpenFOAM/cases/mapping_validation_20260925/fine60
THERMAL_SOLVER_OPENFOAM_ENV_READY=1 OPENFOAM_LAUNCHER=env \
  bash ./run_parallel.sh 2 --multirate 0.15
```

Scope: this verifies field transfer, thermal initialization, short-time energy
response, refusal of uncovered mapping, and restart bookkeeping. It does not
validate the user's rack heat-transfer accuracy or predict qualification time.
Matching patch names/types and mapping coverage cannot establish identical
fan curves, materials or physical geometry; those remain required inputs.
The fine mesh still needs its own flow adjustment and acceptance for the
qualified workflow. Unqualified thermal mode is a separate explicit option.
