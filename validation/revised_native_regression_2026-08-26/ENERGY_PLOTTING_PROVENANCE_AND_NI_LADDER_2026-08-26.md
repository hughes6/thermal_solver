# Energy, plotting, provenance, and NI-ladder continuation — 2026-08-26

## Decision

The native source snapshot captured by this dated continuation has materially
stronger numerical-regression evidence, and the plotting and OpenFOAM launch
paths fail more clearly. It is still **not industry-ready or physically
validated**. Later runner-state evidence supersedes this document for
current-source identity; the binaries and component campaigns named here are
pinned historical artifacts.

The native energy fixture closes the exact explicit update algebra to about
`1e-11 J`, and the complete harness for that source snapshot passes. Those are discrete
software checks. They do not establish long-transient accuracy, full-rack heat
removal, calibrated fan behavior, mesh independence, or agreement with
measurements. The inactive NI separator sensitivity still cannot be stamped
without changing an existing wall. The installed OpenFOAM solver is
definitively stale, the hardened source has not been compiled, and the host
does not meet the standing memory gate for a valid heavy build or solve.

The established runtime policy is unchanged: retain the `0.0005 s` live-flow
cap and three live outer passes; keep `24 s` only as an exploratory thermal-only
cap for the exact retained pre-correction 22.5 mm case; keep the canonical
19 mm thermal-only cap at `20 s`; and treat two thermal-only outer passes as an
unpromoted screening candidate.

## Native frozen-capacity energy ledger

`tests/native_thermal_energy_ledger_test.cpp` advances a forced-flow
`4 x 2 x 1` air/solid channel for four sequential `0.002 s` steps on both a
uniform and an adaptive eight-cell mesh. Before each step, the test reconstructs
every cell's heat generation, same-phase conduction, solid/air convection,
upwind internal face-flux advection, and ambient inlet/outlet enthalpy from the
pre-step state and the exact flow fields published by the solver. Storage uses
the solver's explicit frozen-capacity definition,
`rho_old * cp_old * V * (T_new - T_old)`.

| Mesh | Source (J) | Inlet (J) | Outlet (J) | Net boundary (J) | Storage (J) | Maximum cell residual (J) | Maximum global residual (J) |
|---|---:|---:|---:|---:|---:|---:|---:|
| Uniform | 0.336000 | 0.0569496 | 0.0954067 | -0.0384571 | 0.297543 | `1.27694e-11` | `1.24090e-11` |
| Adaptive | 0.370480 | 0.0569496 | 0.0954020 | -0.0384525 | 0.332028 | `1.48202e-11` | `8.46777e-12` |

The test makes the following assertions independently of the solver's thermal
assembly:

- for every fluid cell and step, it reconstructs continuity from the published
  x/y/z face fluxes plus ambient exchange, requires the reconstructed maximum
  to equal the solver-published maximum within `1e-15 m^3/s`, and requires the
  maximum to remain at or below `1e-10 m^3/s`;
- every cell's frozen-capacity storage must equal the independently assembled
  conduction, convection, advection, ambient, and generation update within a
  scale-aware `2e-9` tolerance;
- internal conduction, convection, and face-advection rows must each cancel
  globally within `1e-12 W`;
- every step must close globally within `2e-9 J`, and each four-step cumulative
  source-plus-boundary ledger must close within `5e-9 J`; and
- both meshes must actually exercise nonzero solid conduction, fluid
  conduction, solid/air convection, internal advection, generation, ambient
  inflow, and ambient outflow.

This is an independent reconstruction of residuals from solver-published
fluxes, not a second flow solution. It validates the native code's discrete
beginning-of-step `rho*cp*V` semantics. It does **not** validate endpoint-density
thermodynamic energy, property-linearization error, timestep independence,
radiation, face-wall/full-rack integration, a long thermal soak, OpenFOAM
energy conservation, or experiment-to-model agreement.

Evidence:

- ledger log: 19,657 bytes, SHA-256
  `48411a50929c52a7c18b758c2dfb20ef02f461d9027805c8d3d03bbff3f17464`;
- test source: 20,370 bytes, SHA-256
  `e95594cb743de223b6e6fe5880ed446b0205ccb280408905f3b0dbb88980f268`.

## NI separator mesh ladder and exact topology cause

The isolated ladder tests the canonical NI component and the inactive
minimum-separator variant at `7.5, 8.8, 9, 10, 15, 19, and 20 mm` fine spacing.
At each rung it creates six sequential meshes: each source on its own plan,
each with the top-right vent omitted, and each source on the other source's
plan. The largest case is 160,034 cells and reports 71,695,232 bytes of cell
storage; a 175,000-cell pre-allocation guard remains active.

The canonical wall 1 loses `0 m^3` with or without the vent on either plan.
The provisional component loses exactly `0.000018 m^3` from wall 1 at every
spacing and also when stamped on the canonical plan. Omitting the top-right
vent restores that volume. Thus neither refinement nor plan selection causes
the primary loss, and no tested mesh preserves the final provisional stamping.

The cause is the native vent-tunnel topology. Canonical interior air begins at
local `x=5 mm`, leaving a fluid stop before wall 1 begins at `x=10 mm`. The
provisional wall 3 fills that `x=5..10 mm` corridor. The top-right vent tunnel
therefore continues to the next fluid cell and carves through wall 1 over
`240 x 5 x 15 mm = 18,000 mm^3 = 0.000018 m^3`. There is no direct
source-volume intersection between the zero-thickness exterior vent and wall
1; the loss is created during stamping only after the provisional wall removes
the canonical fluid stop. A separate invariant `0.000010185 m^3` is removed
from provisional wall 3 relative to its vent-omission control.

For representation alone, every NI source-coordinate cut survives only through
`8.8 mm`, the 2.5 mm vent-edge/wall-top pair survives through `10 mm`, and all
tested 5 mm wall cuts survive through `20 mm`. These resolution facts cannot
repair the topology conflict. The variant therefore remains inactive and must
not be solved or promoted until measured separator gauge/termination and an
explicit vent-to-plenum/card-air drawing are supplied.

Detailed evidence is in `NI_SEPARATOR_MESH_LADDER_2026-08-26.md`; the final
ladder log is 5,105 bytes with SHA-256
`b56091b77c61f0f622591dbf0ab9a7f150307165d2f4cc7d3634a1a6393a1c34`.

## Plotting recovery

The already-installed ANSYS Student Python runtime was used without installing
or downloading packages:

- Python 3.12.11;
- Matplotlib 3.10.0;
- NumPy 1.26.4; and
- pandas 2.2.2.

The complete plotting suite passed 10/10, including the artist-level checks.
The actual provisional NI component export produced 17 region artists. Exactly
two were `Air`, and both fill and edge colors equal `tab:cyan`, RGBA
`(0.0901960784, 0.7450980392, 0.8117647059, 1.0)`. The rendered component PNG
is 463,599 bytes with SHA-256
`8d6a7549d8d88ba8031025060b04e202988d964cb82c75a1555be7d75f25116c`;
the rack PNG is 367,959 bytes with SHA-256
`eb583d097ddf79cb1b14a0319a4411bbf1e64e8b50bf6c8dbe272ff238d4b26f`.
The plot manifest SHA-256 is
`f391d5b74095755ca45153804fadfe0cfcd8f0045615a123f466a4c7f75352e8`.

This proves color handling and nominal geometry rendering. It does not negate
the native stamping rejection, prove that the two NI air regions are isolated,
or validate a flow field.

## OpenFOAM binary provenance and fail-closed launch gate

The installed WSL executable is not an unknown build; it is demonstrably the
old policy:

```text
/home/hconner158/OpenFOAM/hconner158-v2606/platforms/linux64GccDPInt32Opt/bin/semiFrozenChtMultiRegionFoam
bytes=1397192
sha256=4dad80e5f4c3b4a9a37599d06291dd8c93cdf9a49053c9a2582f90cd5a5c2e6d
```

Its embedded strings include `Solving isothermal airflow region`,
`Pressure-correcting isothermal airflow region`, `thermalOnlyFlow`, and
`isothermalAirflow`. It does not contain the hardened policy marker. It must
not be used for this campaign.

The reviewed source now prints
`THERMAL_SIM_SEMIFROZEN_MODE_POLICY_V1` before time creation. Exported runners
resolve the deployed executable after OpenFOAM environment setup and verify
that exact marker before acquiring the case lock or writing case state. The
focused executable regression proves both missing and stale binaries exit 14,
with two environment setups and zero case-tree writes. The focused evidence
also records:

- 10/10 semi-frozen source-policy tests passed;
- `openfoam_export_test` passed;
- the generated-runner provenance test passed for both rejection paths; and
- the generated thermal-outer guard test passed, including pre-launch rejection
  and restoration to three live outer correctors.

The focused log is 2,722 bytes with SHA-256
`1df20347f07683a9d22270f86faed4f28a4087767b801c8d0199308147f33dca`.
The full harness for this pinned source snapshot also passed. The marker proves that a deployed
binary contains this policy family; it is not a cryptographic attestation of
the exact source, compiler, ABI, or linked libraries. A clean build, binary
hash capture, and runtime microcase are still mandatory before any OpenFOAM
behavioral claim.

## Pinned regression, template campaign, and historical artifacts

The recorded added-feature harness finished with `All added-feature tests
passed.` for its then-current source snapshot. Its 327,008-byte log has SHA-256
`1c1dc85928631b4713ab401309f0f4206e11b653bca989c803d5576c2ea76f12`.
All invoked checks passed. The log still explicitly records optional-dependency
skips, including seven individual skipped cases, NumPy/PyVista-gated modules,
and the unavailable real-WSL flock lifetime test; the synthetic archive/lock
workflow passed. It is therefore full for the configured local harness, not a
claim that unavailable integrations executed.

The pinned O2 component campaign exited successfully with:

```text
11 functional passes, 1 expected geometry rejections, 12 selected templates,
2 Meanwell placements, 2315 W rack inventory, elapsed=70.7681 s
```

The expected rejection is the inactive NI separator variant. Canonical NI
still reports 42 stalled fan interfaces at the physical lower bound and a
22.7193 m/s peak, so its functional pass is not a realism pass. The 58,145-byte
campaign log has SHA-256
`6005a082afabfb60549b2904bc359db82cccf226d6b23c2393cf259f8a99637d`;
its 1,086,574-byte executable has SHA-256
`63f3c3e64ca4509941ff397cdfcea0bb4effbfb9beeba16343bd27fa92a0ef51`.

The two sequential historical campaign logs both contain 629 lines. Lines 1--628
are byte-for-byte equivalent; only the final elapsed value changed from
68.2119 to 70.7681 s, an increase of 3.747%. One sequential timing pair under
changing host conditions is not a performance regression measurement, while
the identical functional, iteration, residual, flow-domain, and geometry rows
show no observed numerical drift. The 3,976-byte comparison log has SHA-256
`d6300519a272dc4799b3fb21fa2b18f17742fadd1bf96790c847d11336afc526`.

The then-designated production rebuild and canonical geometry-only export are
retained as historical artifacts:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `model.exe` | 1,771,040 | `2ea1b5af3834923c9585a19533f33ca599c5b05cb1c903b9f9fb79488f09e9dc` |
| `output.txt` | 39,808 | `dbcaaa237fb8bf59c4841cf4e3a8b0a226d28208d23560c72900ce113d2ff2ea` |

The geometry hash remains identical to the preserved plot lineage. The
executable hash changed at that time because the published native
flow/continuity diagnostics were added to the then-designated production
build. This `model.exe` is not a current-source exporter and must not be used
to create a corrected case.

## Resource gate and remaining validation work

The pre-regression Windows snapshot reported 1,457 MiB available against the
standing 5,120 MiB continuous pre-launch requirement, and WSL reported
3,245 MiB available. The post-regression Windows recheck was lower at
1,442 MiB. No Fluent, OpenFOAM, or MPI solve was running, but the gate still
failed, so no heavy `wmake` or OpenFOAM solve was launched. The 174-byte
post-regression log has SHA-256
`3114e9b8e8cbfa2bf7c3bec0d8ce4e10d4dae8c1170ccf7cae28681f3486191e`;
the earlier 295-byte launch snapshot has SHA-256
`e7a6cdacc2c5ca1d3d05ca243be78e46d46007e2bebc216b504c4a6150ae203f`.

Industry-readiness still requires, at minimum:

1. measured NI separator and rail-2 geometry, followed by clean static,
   stamping, connectivity, and mesh-ladder passes;
2. measured or uncertainty-bounded heat loads and fan curves;
3. a resource-valid clean OpenFOAM build whose exact source/build/binary
   provenance is captured and whose runner gate and runtime policy microcases
   pass;
4. a current-geometry full-rack mesh-quality and mesh-independence study;
5. a sufficiently developed live-flow field, a validated frozen-flow
   transition, and a gap-free transient first-law ledger; and
6. calibrated experimental airflow and temperature comparisons over a thermal
   soak.

Until those are complete, the defensible result is **robust discrete native
screening with known geometry and OpenFOAM deployment blockers**, not a
validated prediction of the research-lab rack.
