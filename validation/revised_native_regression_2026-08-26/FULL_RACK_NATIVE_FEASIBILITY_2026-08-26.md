# Full-rack native feasibility and geometry-resolution audit — 2026-08-26

## Status and claim boundary

This is a resource-bounded, plan-only audit of the current full-rack model and native rectilinear planner. It did not run a transient, allocate a full production mesh, export OpenFOAM, start WSL, or start Fluent. The exact planning results below are suitable for preflight and workload decisions; they are not temperature, flow-field, convergence, or validation results.

Verdict: none of the current full-rack native production meshes is feasible for the canonical 30 s / 0.1 s run under the configured 30,000,000-update guard. The 50 mm coarse stage alone plans 216,580 cells and 64,974,000 base cell updates. Refining the current global tensor mesh does not reliably preserve the model's thin local features: the current 19 mm native plan still suppresses requested boundaries for 18 component envelope faces, 35 thin (15 mm or smaller) volume regions, and 66 openings. A prior native regression actually lost the Keysight N5766A Power block as a solid after opening stamping, so this is not only a theoretical risk.

Subsequent authoritative post-audit status: the resource preflight described as
future work in the original audit is implemented and was exercised by the
post-audit production runner. Forced-native execution planned 2,847,663 fine and
216,580 coarse cells
and rejected 1,838,545,800 mandatory visits against the 30,000,000 limit in
0.109 s, before mesh allocation or solver construction. Its 210-byte stdout has
SHA-256
`2df73fed20722b742083324b84615c8d7871dde489c0e9a82630b3bd6c05f512`;
its 274-byte stderr has SHA-256
`1b93670fc5ce981665811837fae75b9f9eb46ba33132b54182a738989b372433`.
It preserved all four root sentinels; the current 609-byte
`.thermal_sim_last_run.json` has SHA-256
`39ae95c8825c855dd03bbd233747cb95000bb49c888ca918c185b2f655ccd1e7`.
It produced no flow or temperature field. This remains a workload result, not a
flow, temperature, convergence, or validation result. The isolated
11-functional-plus-one-expected-rejection component campaign does not overturn
the full-rack resource or topology findings. The earlier 0.288 s
`final_runtime_hardening` preflight is retained only as historical evidence for
its pinned pre-post-audit source snapshot.

## Authoritative input snapshot

The original plan/cut probes used the following byte-identical snapshot.
SHA-256 values were rechecked after those probes. Later runtime-hardening source
changes are pinned separately in
`WORKLOAD_MEMORY_RUNTIME_HARDENING_2026-08-26.md`; the authoritative post-audit
production preflight reconfirmed the canonical fine/coarse cell counts above.

| Input | Bytes | SHA-256 |
|---|---:|---|
| library/models/new_model_updated.toml | 9,860 | a6448cc3a6ee00fdc72a3f6d49a27b7aeedb89a66e71b06e525f7190a335a05a |
| library/fan_curves/fan_curves_updated_2026_08_24.toml | 2,392 | 0365c4e75a11aae906878c60d68f4e69e532d6ad9e836474b126705d87eb35cf |
| src/mesh_refinement_planner.hpp | 18,090 | f0f374975af6ce35d18757b002393751514ecba66fa2c37116b416fda18b8172 |
| src/mesh.hpp | 145,385 | 75f9d9b72e97d0828361e6b57a1ed99da1bbf086e38db26d77805215bf85d065 |
| src/solver.hpp | 56,770 | 16377b425467a0abf111d7624ea56c878d87f2f5e70ef3852518bd47f7011ad9 |
| src/input/model_loader.hpp | 127,642 | 7562ead2b474a1ca404ed87a27ce87c9c7d54c20125b9a5ebafa71c2a2e84b98 |

The canonical controls are dt = 0.1 s, duration = 30 s, max_updates = 30,000,000, max_cell_count = 10,000,000, and max_megabyte_usage = 1,536 MiB in library/models/new_model_updated.toml:11-19. Its root/native spacing is 19/50 mm at lines 60-61; the OpenFOAM-local spacing is 19/100 mm at lines 86-87; and its requested native coarse stage is 50/200 mm with 0.1 s / 30 s at lines 96-102.

## Exact method and reproduction

Temporary C++20 probes were compiled from standard input against the live headers with MSYS2 g++.exe 16.1.0:

~~~powershell
$source | g++ -x c++ -std=c++20 -O0 -I. -o $resolvedProbe -
& $resolvedProbe
Remove-Item -LiteralPath $resolvedProbe -Force
~~~

Each executable path was resolved and checked to be under the process temporary directory before removal. No probe source file was written. The probe:

1. called ModelLoader.load_fan_curves("library/fan_curves/fan_curves_updated_2026_08_24.toml");
2. called ModelLoader.load_model("library/models/new_model_updated.toml", true), retaining the root/native mesh settings instead of applying the OpenFOAM profile override;
3. reconstructed the rack, components, internal regions, fans, and vents with the loader's exact size_to_meters, position_to_meters, build_internal_region, and template-cache paths;
4. ran RackBoundsChecker::check_all and CollisionChecker::check_all;
5. called the current MeshRefinementPlanner::plan for each spacing/margin row below; and
6. reconstructed every planner-requested component-envelope, Air/HeatSource AABB, fan, and vent cut, then compared it with cumulative planned axis boundaries at an absolute tolerance of 1e-10 m.

The planner probe measured sizeof(Cell) = 224 bytes in this compiled ABI. “One mesh” is cells × 224 bytes. “Pair” is 2 × cells × 224 bytes, matching the current mesh-density guard's current-plus-next Cell-vector accounting. These are exact Cell payload values for the plan, not peak resident set size (RSS). They exclude vector capacity slack, flow-solver vectors and adjacency, wall faces, fan/vent metadata, template/model objects, CSV or post-processing buffers, OpenFOAM export and region-splitting data, runtime/allocator/OS overhead, and MPI duplication.

All temporary executables were absent after cleanup: thermal_full_rack_plan_probe_20260826.exe, thermal_full_rack_plan_probe_summary_20260826.exe, thermal_full_rack_plan_counts_20260826.exe, thermal_full_rack_cut_summary_20260826.exe, and thermal_full_rack_stamp_50mm_20260826.exe. The attempted 50 mm stamping probe did not yield retained output and is not used as evidence.

## Exact plan, payload, and base-work results

The workload column preserves the original one-pass comparison formula:

    cells × ceil(duration / dt) = cells × ceil(30 / 0.1) = cells × 300

It is a one-pass baseline, not the current acceptance minimum. With advection
subcycling enabled, each global step requires one non-advection pass plus at
least one advection pass. The current canonical preflight therefore requires
1,708,597,800 fine visits plus 129,948,000 coarse visits, or 1,838,545,800 total.
Solved-flow CFL can only increase that value.

| Plan label | fine_dx (m) | coarse_dx (m) | margin (m) | nx × ny × nz | Cells | One mesh (MiB) | Pair (MiB) | Base updates at 300 steps |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Native coarse | 0.0500 | 0.200 | 0.020 | 52 × 49 × 85 | 216,580 | 46.2664794922 | 92.5329589844 | 64,974,000 |
| Native regression | 0.0500 | 0.200 | 0.000 | 50 × 48 × 79 | 189,600 | 40.5029296875 | 81.0058593750 | 56,880,000 |
| 40 mm | 0.0400 | 0.100 | 0.020 | 61 × 61 × 94 | 349,774 | 74.7197875977 | 149.439575195 | 104,932,200 |
| 30 mm | 0.0300 | 0.100 | 0.020 | 75 × 69 × 105 | 543,375 | 116.077423096 | 232.154846191 | 163,012,500 |
| 25 mm | 0.0250 | 0.100 | 0.020 | 90 × 80 × 119 | 856,800 | 183.032226562 | 366.064453125 | 257,040,000 |
| 22.5 mm | 0.0225 | 0.100 | 0.020 | 99 × 84 × 123 | 1,022,868 | 218.508178711 | 437.016357422 | 306,860,400 |
| 20 mm | 0.0200 | 0.100 | 0.020 | 115 × 129 × 178 | 2,640,630 | 564.099426270 | 1,128.19885254 | 792,189,000 |
| OpenFOAM-profile 19 mm | 0.0190 | 0.100 | 0.020 | 117 × 133 × 180 | 2,800,980 | 598.353881836 | 1,196.70776367 | 840,294,000 |
| Native 19 mm | 0.0190 | 0.050 | 0.020 | 117 × 133 × 183 | 2,847,663 | 608.326446533 | 1,216.65289307 | 854,298,900 |
| 15 mm | 0.0150 | 0.100 | 0.020 | 127 × 140 × 185 | 3,289,300 | 702.670288086 | 1,405.34057617 | 986,790,000 |
| 10 mm | 0.0100 | 0.100 | 0.020 | 140 × 163 × 217 | 4,951,940 | 1,057.84851074 | 2,115.69702148 | 1,485,582,000 |
| 8.8 mm | 0.0088 | 0.100 | 0.020 | 151 × 177 × 232 | 6,200,664 | 1,324.60473633 | 2,649.20947266 | 1,860,199,200 |

The “Native regression” row is shown at 300 steps only to compare meshes. Its actual regression controls are duration = 0.00010 s and dt = 0.00001 s (10 steps), so the current replanned base work would be 1,896,000 updates and would pass that test's 5,000,000-update limit. Its geometry still is not adequate; the prior executable's actual failure is documented below.

At 300 steps, max_updates = 30,000,000 allows at most 100,000 cells. Every full-rack row above exceeds that count. Raising only max_updates does not address geometry fidelity or peak memory. Decreasing fine_dx is especially poor leverage: the 20 mm threshold abruptly expands the tensor grid to 2.64 million cells, 10 mm already exceeds the 1,536 MiB configured pair-payload limit before uncounted overhead, and 8.8 mm requires 2.649 GiB for the Cell pair alone.

## Exact-cut suppression audit

The current planner gives rack cuts priority 3, component-envelope cuts priority 2, and internal-feature/opening cuts priority 1 (src/mesh_refinement_planner.hpp:88-110). It globally suppresses any requested cut closer than 0.25 × fine_dx to an already accepted higher/equal-priority cut (lines 243-266), snaps protected two-cell material spans to the realized breakpoints (lines 172-195 and 309-329), and then enforces a maximum 4:1 adjacent-cell-width ratio (lines 353-375). Every accepted cut tensorizes across the entire rack, even when only one local component needs it.

The table counts:

- unique suppressed requested coordinates on each axis;
- suppressed request rows by category (a coordinate requested by multiple features can contribute multiple row hits);
- component envelopes with at least one requested face missing;
- volume regions no thicker than 15 mm with at least one requested boundary missing;
- fan/vent openings with at least one requested footprint or normal-plane cut missing; and
- requested NI chassis cut labels with at least one exact cut missing.

| fine_dx | Unique suppressed x / y / z | Suppressed rows: component / solid / air / opening | Affected component faces | Affected thin regions ≤15 mm | Affected openings | Affected NI cut labels |
|---:|---:|---:|---:|---:|---:|---:|
| 50 mm | 196 / 47 / 56 | 24 / 222 / 158 / 279 | 24 | 59 | 75 | 64 |
| 22.5 mm | 165 / 32 / 37 | 19 / 171 / 136 / 197 | 19 | 59 | 71 | 40 |
| 20 mm | 155 / 12 / 18 | 18 / 130 / 65 / 121 | 18 | 36 | 67 | 29 |
| 19 mm | 153 / 12 / 18 | 18 / 129 / 64 / 120 | 18 | 35 | 66 | 29 |
| 15 mm | 144 / 10 / 15 | 17 / 120 / 61 / 115 | 17 | 27 | 61 | 27 |
| 10 mm | 130 / 8 / 10 | 16 / 91 / 50 / 107 | 16 | 22 | 60 | 18 |
| 8.8 mm | 116 / 8 / 8 | 13 / 85 / 48 / 94 | 13 | 21 | 59 | 18 |

At the current 19 mm native spacing, the 18 affected component-envelope face labels are:

- 3U storage Shelf: max-x, min-x, min-z
- Cisco 9300: max-x
- Dell R470: max-x, min-x
- Eaton PDU: max-x, min-x, min-z
- Eaton UPS: min-x
- Keysight N5766A: min-x
- Keysight N6701C: max-x, min-z
- Meanwell: min-x
- NI: min-z
- Thruster: max-x
- Trenton: max-x
- KVM: min-x

The 35 affected thin-volume labels at 19 mm are:

- Eaton UPS: rear fan air duct (5 mm in y)
- Keysight N5766A: Power block (10 mm in z); rear fan air ducts 1-5 (5 mm in y); walls 1, 2, and 4 (5 mm in x)
- Keysight N6701C: modules 1-4 (5 mm in z); Power Supply block (5 mm in z); Power block (10 mm in z); rear fan air ducts 1-5 (5 mm in y); walls 2, 3, and 4 (5 mm in x)
- Meanwell: Main block (10 mm in x)
- NI: Card 1 and Card 2 (5 mm in x)
- Trenton: slots 1, 4, 5, 6, 8, 9, 10, and 11 (5 mm in x)

Selected 19 mm NI requested-to-realized cut shifts quantify the snapping:

| NI cut | Realized shift |
|---|---:|
| Card 1 / Card 2 minimum y | -4.2 mm |
| Connector minimum y | -3.1 mm |
| Wall 1 / Wall 2 maximum z | +2.35 mm |
| Power Supply minimum x | +1.634 mm |
| Power Supply fan maximum x | -1.466 mm |
| Power Supply fan minimum x | -0.9192 mm |
| Card x faces | approximately -0.2175 to -0.81795 mm |
| NI chassis minimum z and both bottom-fan normal planes | -0.45 mm |
| Interior-air minimum x / maximum x / maximum z | +0.334 / +0.517 / +0.150 mm |
| Top-main-vent minimum x | +0.334 mm |

A missing exact requested footprint edge does not, by itself, prove that an opening disappeared or that its effective area is unacceptable. It proves that the planned grid snapped/suppressed that model datum. Physical consequence requires stamping, retained cell-volume/area, connectivity, and conservation checks. Conversely, the counts cannot be dismissed as harmless: even at 8.8 mm the global near-coincident-cut scheme still affects 13 component faces, 21 thin regions, and 59 openings, and an actual regression has already demonstrated complete source-solid loss.

## Corroborating retained evidence

The retained validation/revised_native_regression_2026-08-26/run_002.stdout.log records an actual prior-snapshot mesh with 204,768 cells and 91,736,064 bytes of Cell-pair accounting. It then fails:

> Internal heat source 'Power block' has no remaining solid cells after fan/vent stamping ... state_counts={1:40}.

All 40 cells in the source bounds were fluid (state 1). The current-source zero-margin 50 mm replan is 189,600 cells, so the old count must not be presented as the current planner count; its value here is direct evidence of the failure mode.

The retained validation/revised_openfoam_22p5mm_2026-08-25/RUN_STATUS.md reports 1,033,200 cells for its retained 22.5 mm case (line 6), three artificial neighboring-solid contacts of 227, 188, and 2,322 faces on the coarse grid (lines 639-640), and a nominal 19 mm export with approximately 2.8 million cells that exhausted available WSL memory during region splitting (lines 650-651). The current-source 22.5 mm plan is 1,022,868 cells, so retained-artifact and current-plan counts are intentionally distinguished. The WSL result also demonstrates why the Cell pair is not a peak-RSS estimate.

Any older 2,561,280-cell / approximately 1,094 MiB static estimate in UPDATED_MODEL_STATIC_AUDIT.md is stale relative to the hashed source snapshot above.

## Implemented resource guard and remaining improvement

### Implemented fail-closed resource preflight

The current loader now reports planned coarse/fine cells, checks exact
`sizeof(Cell)`-based pair payload and overflow-safe timestep/visit arithmetic,
shares one cumulative coarse-plus-fine visit budget, and rejects the canonical
run before mesh allocation or solver construction. The executed early rejection
also occurs before `output.txt`, legacy CSV, or structured logger streams are
opened or written.

The remaining geometry-fidelity preflight should:

1. audit all requested component, material/heat-source, fan, and vent planes against realized boundaries;
2. report retained solid/fluid cells, volumes, opening areas, fan/vent adjacency, and fluid connectivity after a bounded stamping dry run; and
3. reject quantitative/native profiles when a component envelope, material or heat-source boundary, opening normal plane, or designated critical aperture edge is not preserved within an explicit model tolerance.

The workload gate now prevents the unaffordable canonical native solve. It does
not yet make every suppressed cut or post-stamping geometry consequence
inspectable or acceptable.

### Structural local-feature representation

Do not pursue full-rack fidelity by adding ever-finer global tensor cuts. Extend the existing face-wall/embedded-boundary mechanism to represent thin internal separators, chassis skins, cards, and opening masks locally while retaining a coarser rack grid. The implementation must conserve volume/area and energy, enforce impermeability where required, and carry thickness, conductivity, density, heat capacity, temperature, and component identity.

Powered thin faces require an explicit energy-source field and conservation tests: the current Mesh::WallFace at src/mesh.hpp:119-129 stores geometry/material/temperature/activity/group but no watts or volumetric/surface generation. Adding thermal mass without heat-source provenance would not be sufficient. For geometries that cannot be reduced to embedded faces, OpenFOAM local octree/unstructured refinement is the appropriate alternative; a globally exact rectilinear full-rack mesh is not.

Finally, update the rail-2 and unfinished NI chassis datums from measurements when available. Legitimately aligning near-duplicate measured faces can reduce cut conflicts, but geometry must not be moved merely to satisfy the mesh. Raising max_updates/max_megabyte_usage or reducing fine_dx without these changes is not a defensible industry-readiness path.
