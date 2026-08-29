# Actual-case audits at 1.600 s

These are lightweight, read-only audits of the reconstructed checkpoint at
`1.6000000000000001` in:

`C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases\new_model_updated_openfoam_export_test`

No transient solver was launched. The solved case owns its embedded
`provenance/model.toml` snapshot, SHA-256
`88f1d230efa6a2fb7d24c2f1a906a22c6f7f77e6a61ae6cd118b6095ed0d50cb`;
the later commented working fixture must not be substituted for that lineage.
The original audit tools' 21 focused unit tests passed before the case was
inspected. The later fan-domain supplement passed its seven focused tests.

## Findings

| Audit | Result | Evidence-backed interpretation |
|---|---|---|
| Heat sources | PASS | All 47 exported sources have non-empty, mutually non-overlapping cell sets within their solver regions; active absolute-watt sources match metadata exactly. Total power is 2,315 W across 37,926 selected cells. |
| Thermal mass | PASS as bookkeeping | Thirteen solids total 0.070540628 m3, 190.46 kg, and 171,414 J/K under the exported homogenized properties. The 47 sources reconcile to 2,315 W. These values do not establish that the homogenized materials are physically faithful. |
| Material fidelity | MATERIAL LIMITATION | Nine of 13 component instances contain differing internal solid materials that the one-region/component export homogenizes. Fifty-five internal solid regions are affected; the source-geometry aggregate OpenFOAM-minus-defined deltas are +8.99054 kg and +14,811.4 J/K. Conductivity differences also remain even where mass/capacity deltas are zero. This is a source-model audit, not a measurement of the solved field. |
| Fluid-boundary connectivity | PASS | The expected one connected fluid region was found. All 24 physical opening patches map exclusively to region 0; the 13 mapped solid interfaces and rack walls also map to region 0. |
| Component temperature and power | PASS as extraction only | OpenFOAM `postProcess` returned volume-weighted temperatures for all 13 solids and reconciled 2,315 W. At this 1.600 s cold-start checkpoint, the highest solid temperature is 294.462452 K in `Keysight_N5766A_PS_2` (1.312452 K above 293.15 K), followed by 294.392803 K in `Keysight_N6701C_PS_3`. These short-time values are not a thermal-soak or thermal-design pass. |
| Fan positive-pressure domain | **FAIL** | The curves actually exported in `fvOptions` and `0/fluid/p_rgh` were checked against runtime flow. Ten of 47 fans are at or beyond their first zero-pressure point, eight are at or above 90%, and 29 pass. Beyond the crossing the exported table is clamped to zero pressure, so positive direction alone does not validate those operating points. See `fan_operating_domain_1p60.csv` and `.md`. |
| Mesh determinant | **FAIL for quantitative sign-off** | `checkMesh.prepare.log` reports 39,878 cells below determinant 0.001 in the fluid plus 12 solids, including 5,082 fluid cells. Only the storage-shelf solid reports `Mesh OK`. Other geometry/topology checks pass, but they do not cancel this explicit failure. See `MESH_QUALITY_AUDIT.md`. |
| Cyclic pressure-jump fans | UNSUPPORTED FOR THIS CASE | The dedicated tool correctly rejected the case because no `_pressure_jump_master` cyclic patches exist. Current internal fans are `fanMomentumSource` cell zones, so `fan_flow_through_1p60.md` is the applicable flow audit; a cyclic pressure-jump result was not invented. |
| Wall y+ | PASS as report extraction, WALL-MODEL WARNING | Direct case selection was rejected because a rejected 1.610 benchmark left a stale `yPlus_1.6000000000000001.dat` sibling containing 1.610 rows. The intact authoritative `yPlus.dat` contains 1.600 rows and was audited through an exact SHA-256-matched snapshot (`ac0e9b2a77718a96bc1031605997b0025f8a8ff5e8bf5bc1088648beec1fa42e`). All 14 wall/interface patches span mixed/buffer-layer y+; the global range is 0.5790083 to 1,017.789. Near-wall shear and heat transfer are therefore wall-model sensitive and not grid-independent. |

Future default, in-depth, and validation exports reject determinant failures
before the prepared-case marker is created. Screening can opt in only as an
explicitly exploratory exception. The active case was deliberately left
unchanged to preserve its restart, logs, post-processing, and evidence hashes;
this external audit therefore remains its governing quantitative gate.

The before/after manifests cover 82 actual case inputs used by these audits,
including all reconstructed 1.600 s fields. They are byte-identical and share
SHA-256
`50448407fde43c67113f5522e249e7f9661efd209f5e3aad0d278a3f50934fc1`.
This confirms the audited input set was not mutated. The failed direct y+ and
pressure-jump attempts, plus the initial sandbox-denied WSL attempt, are
retained as logs rather than silently omitted.

## Reproduction commands

Run from the repository root in PowerShell. These variables expand to the
exact runtime, case, and evidence directory used here:

```powershell
$python = 'C:\Users\hconn\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$case = 'C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases\new_model_updated_openfoam_export_test'
$out = 'validation\revised_openfoam_22p5mm_2026-08-25'

& $python -m unittest tests.openfoam_heat_source_audit_test tests.openfoam_thermal_mass_audit_test tests.openfoam_material_fidelity_audit_test tests.openfoam_boundary_connectivity_audit_test tests.openfoam_pressure_jump_fan_audit_test tests.openfoam_yplus_report_test tests.openfoam_component_report_test -v 2>&1 | Tee-Object -FilePath "$out\actual_case_audit_tool_tests_2026-08-26.stdout.log"

& $python tools\openfoam_heat_source_audit.py $case --json "$out\actual_case_heat_source_1p60.json" --markdown "$out\actual_case_heat_source_1p60.md" 2>&1 | Tee-Object -FilePath "$out\actual_case_heat_source_1p60.stdout.log"
& $python tools\openfoam_thermal_mass_audit.py $case --markdown "$out\actual_case_thermal_mass_1p60.md" 2>&1 | Tee-Object -FilePath "$out\actual_case_thermal_mass_1p60.stdout.log"
& $python tools\openfoam_material_fidelity_audit.py library\models\new_model_updated_openfoam_export_test.toml 2>&1 | Tee-Object -FilePath "$out\actual_case_material_fidelity_source_model.stdout.log"
& $python tools\openfoam_boundary_connectivity_audit.py $case --expect-regions 1 --opening-region 0 2>&1 | Tee-Object -FilePath "$out\actual_case_boundary_connectivity_1p60.stdout.log"
& $python tools\openfoam_pressure_jump_fan_audit.py $case --markdown "$out\actual_case_pressure_jump_fans_1p60.md" 2>&1 | Tee-Object -FilePath "$out\actual_case_pressure_jump_fans_1p60.stdout.log"
& $python tools\openfoam_yplus_report.py $case --json "$out\actual_case_yplus_1p60.json" --markdown "$out\actual_case_yplus_1p60.md" 2>&1 | Tee-Object -FilePath "$out\actual_case_yplus_1p60.stdout.log"
& $python tools\openfoam_component_report.py $case --time 1.6000000000000001 --csv "$out\actual_case_component_thermal_1p60.csv" --json "$out\actual_case_component_thermal_1p60.json" --markdown "$out\actual_case_component_thermal_1p60.md" 2>&1 | Tee-Object -FilePath "$out\actual_case_component_thermal_1p60.stdout.log"
& $python -m unittest tests.openfoam_fan_operating_domain_audit_test -v
& $python tools\openfoam_fan_operating_domain_audit.py $case --density 0.9833 --csv "$out\fan_operating_domain_1p60.csv" --markdown "$out\fan_operating_domain_1p60.md"
```

The direct y+ command above intentionally reproduces the stale-sibling
rejection. The successful y+ evidence was produced after copying only the
authoritative 1.600 s `yPlus.dat` into
`actual_case_yplus_input_snapshot/postProcessing/fluid/fluid_y_plus/1.6000000000000001/`
and then running:

```powershell
$snapshot = "$out\actual_case_yplus_input_snapshot\postProcessing\fluid\fluid_y_plus\1.6000000000000001"
New-Item -ItemType Directory -Path $snapshot -Force | Out-Null
Copy-Item -LiteralPath "$case\postProcessing\fluid\fluid_y_plus\1.6000000000000001\yPlus.dat" -Destination "$snapshot\yPlus.dat"
& $python tools\openfoam_yplus_report.py "$out\actual_case_yplus_input_snapshot" --json "$out\actual_case_yplus_1p60.json" --markdown "$out\actual_case_yplus_1p60.md" 2>&1 | Tee-Object -FilePath "$out\actual_case_yplus_1p60_snapshot.stdout.log"
```

`actual_case_audit_evidence_manifest.csv` records the size and SHA-256 of every
artifact in the original audit package. The later fan-domain supplement is
hashed separately in `RUN_STATUS.md`; the mesh-quality supplement records the
authoritative `checkMesh.prepare.log` hash directly.
