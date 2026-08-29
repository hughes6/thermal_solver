# Convection and NI hardening — 2026-08-26

## Outcome

This source snapshot is stronger as a screening implementation, but it is not
an industry-ready model. Native solid/air convection now uses one deterministic
face coefficient on both sides of an interface and refuses an explicitly
unstable timestep before temperatures change. The current full software
harness and optimized component campaign pass. The OpenFOAM isothermal shortcut
now fails closed in source, but that source has not been compiled or exercised
with OpenFOAM. The provisional NI separator is intentionally inactive and is
currently an expected geometry rejection rather than a solved sensitivity.

| Item | Current result | Claim boundary |
|---|---|---|
| Native solid/air convection | PASS: 12 uniform/adaptive, natural/forced, all-axis cases | Frozen-capacity one-step internal-face closure and property fidelity, not a full-rack first-law audit |
| Native explicit-convection stability | PASS: 8 safe/unsafe unequal-cell and six-face cases | Unsafe `C=1.25` cases fail before thermal advancement; this does not establish accuracy at every allowed timestep |
| Complete added-feature harness | PASS: exit 0, all executed checks | Seven individual optional-dependency skips, four NumPy/PyVista-gated modules containing 16 defined tests, and real-WSL flock integration were not executed |
| Optimized component campaign | PASS: 11 canonical functional screens; one expected provisional-NI rejection | Two 10-microsecond thermal steps are robustness screens, not thermal soaks or physical validation |
| OpenFOAM isothermal mode | Source now refuses the mode before time advancement; 9 static policy tests pass | **Uncompiled and not runtime-proven**; the installed executable is not attested to this source |
| Provisional NI separator | Geometry-only export succeeds; native stamping rejects it exactly | Not active in the canonical model and not a flow/thermal result |
| Provisional NI plot | Not rendered: Matplotlib's `mpl_toolkits` is unavailable | The existing canonical 14-image plot set and its 10/10 color regression remain the current plot authority |

## Native convection correction

The former adaptive calculation could derive different film coefficients for
the two rows sharing one solid/air face because the coefficient depended on the
row-owning cell width and on hard-coded sea-level air properties. That allowed
an internal face to create or destroy energy when neighboring cells differed in
size, and it made the thermal-time estimator disagree with the configured
5500-ft environment.

`src/solver.hpp` now evaluates one shared solid/air face coefficient from the
air cell's width normal to the face, velocity, `rho`, `mu`, `k`, and `Pr`.
Uniform and adaptive thermal rows reuse that value. `src/thermal_estimator.hpp`
uses the same air-side properties. The regression covers x, y, and z faces,
uniform and 19/50 mm unequal cells, natural and 0.8 m/s forced flow. For all 12
cases it requires equal cached coefficients, exact agreement with the
altitude-aware correlation and estimator, a measurable difference from the
legacy sea-level correlation, and frozen-capacity energy closure within
`1e-9 J`.

The former convection stability check was warning-only and combined unrelated
mesh-wide extrema. It is replaced by the actual explicit row sum

`C = dt * sum(h_face * A_face) / (rho * cp * V)`.

The guard checks all six faces with each cell's actual geometry and heat
capacity. It uses at least an 80 K solid/air temperature-difference bound for
the natural-convection correlation, reports the limiting cell and maximum safe
timestep, and throws before changing temperatures when `C > 1`. The eight-case
test accepts `C=0.75`, rejects `C=1.25`, checks all three unequal-cell axes and
a six-face center cell, and proves every rejected fixture remains thermally
unchanged.

These are important local conservation and robustness results. They do not
replace the missing gap-free full-rack transient energy ledger, a converged
thermal soak, or comparison with measurements.

## OpenFOAM isothermal mode: fail closed, source only

The prior custom-solver branch could advance physical time while globally
suppressing solid and coupled-energy work. Mixed per-fluid-region modes and a
thermal/isothermal dual selection could also leave state ambiguous. The
reviewed source now:

- resolves one uniform invocation mode before the time loop, including a
  zero-step invocation;
- rejects simultaneous `thermalOnlyFlow` and `isothermalAirflow`;
- rejects every isothermal request before time advancement;
- rejects mixed live/thermal-only fluid modes and a runtime mode change; and
- keeps solid solves and coupled-matrix solve/clear unconditional for accepted
  live and thermal-only operation.

`tests/semifrozen_solver_policy_test.py` passes 9 source-policy checks in the
complete harness. No `wmake`, clean-build executable hash, installed-binary
provenance match, or OpenFOAM runtime microcase was obtained under the current
memory gate. Therefore the only authorized operational interpretation is:
**isothermal startup remains disabled and must not be used for acceleration**.
A separate scratch-case initializer with a validated time-zero field handoff
would still need implementation, clean build provenance, focused transition
tests, and a matched full-rack comparison before consideration.

## NI minimum-separator sensitivity

The canonical active component remains
`library/components/updated_NI_PXIe_Chassis.toml`. The opt-in file
`library/components/updated_NI_PXIe_Chassis_minimum_separator_sensitivity.toml`
is an exact canonical copy except for its explicit provisional name and one
5 x 155 x 160 mm aluminum wall (`0.000124 m^3`) mirrored at local
`(5, 59.2, 10) mm`. The canonical model contains only a commented opt-in path,
and the 16 model-contract tests require the provisional variant to stay
inactive.

This one wall does not close the card-air region. It leaves 5 mm below and
2.2 mm above the modeled walls; sheet gauge, wall terminations, actual side and
rear surfaces, and vent/plenum communication are still unknown. Those four
items require an as-built drawing or measurements and must not be inferred.

The isolated fixture produced a geometry-only report, but the 19 mm native
microcase then failed the exact volume-preservation gate before flow:

`Card Slot wall 1: requested=0.000192 m^3, realized=0.000174 m^3`

The optimized campaign treats this as one expected rejection while all 11
canonical templates pass their functional screens. This is the correct result:
the provisional topology must not silently distort an existing wall and then
produce a misleading flow field. No NI separator flow, temperature, fan, or
energy comparison exists yet. The current canonical NI screen still reports
42 curved fan interfaces at `Q=0` and a 22.7193 m/s peak, so physical realism
remains red pending measured flow, open area, fan curves, and completed walls.

The attempt to render the provisional geometry retained its exact input and
failure log, but produced no PNG or plot manifest because `mpl_toolkits` was
not installed. This does not invalidate the separately preserved current
canonical plot set, whose manifest and 10/10 artist regression prove that all
air regions use the same color. Against current source in the present Python
environment, eight plotting source/logic tests pass and the one artist-level
case skips explicitly for missing Matplotlib; that is not a new rendered-image
pass.

## Runtime policy and resource gate

The speed policy is unchanged by this hardening:

- retain `0.0005 s` for live-airflow coupled CHT; the matched `0.00075 s`
  branch was 9.79% slower and failed field/fan equivalence;
- retain live three-outer/two-inner PIMPLE; live two-outer was about 27%
  faster but failed velocity, pressure, maximum-temperature, and fan gates;
- allow a 24 s thermal-only cap only for the exact retained pre-correction
  22.5 mm exploratory case after airflow-freeze gates pass. Versus 20 s it
  reduced cumulative time 22.190252% and the less startup-sensitive segment
  13.725195%, but it is not validation-grade timestep independence. The
  canonical 19 mm model still inherits the reusable 20 s cap; and
- keep two thermal-only outer energy-coupling passes as an unpromoted screening
  candidate. A corrected-full-rack 3-versus-2 A/B remains required.

No heavy OpenFOAM continuation or build was launched. Observed headroom during
this hardening was about 1.18--1.31 GiB available; the fresh post-test sample
was 1,312 MiB, with no Fluent, OpenFOAM, or MPI process present. This remains
far below the standing requirement of at least 5 GiB available continuously
for 60 seconds. Timing collected under paging would not be valid performance
evidence and could endanger the recoverable active checkpoint.

The exploratory screening profile may continue through determinant-only mesh
warnings when its exact diagnostic-count guard passes. That exception is a
screening allowance, not a determinant-quality or release pass.

## Evidence

| Artifact | Bytes | SHA-256 | Interpretation |
|---|---:|---|---|
| `added_feature_regression_convection_hardening_2026-08-26.stdout.log` | 301,654 | `bb573ac51bc8d9037b5e50b08e1199c2e2040bfbf15115b1fdc1a8139c999576` | Current full harness; ends `All added-feature tests passed.` |
| `native_template_microcase_convection_hardening_2026-08-26.stdout.log` | 58,145 | `9d9f6182059470ea4a7ff89fa300b574c411814832a0b17793ba2e0571668c95` | 11 canonical functional passes plus one expected provisional-NI rejection; 68.2119 s |
| `native_template_microcase_convection_hardening_2026-08-26.exe` | 1,086,574 | `514b76ab53429773383b5ac2eaf11480c31cf15a33f5cdcf55084061928d0027` | Exact optimized executable for that current campaign |
| `native_template_microcase_ni_separator_sensitivity_2026-08-26.stdout.log` | 542 | `6dc457f4be400f50aa6e8fdcb256200487e18264dbc909417a0ab14dd976dcc6` | Filtered reproduction of the exact NI expected rejection |
| `../ni_minimum_separator_sensitivity_2026-08-26/output.txt` | 4,604 | `356f4080ff8f76d9e1c9514778e7c6cf470ce301df471b5a1c6ab40d0a503821` | Geometry-only report; no mesh or transient solve |
| `../ni_minimum_separator_sensitivity_2026-08-26/render_plots.stdout.log` | 1,312 | `bb75211e83aee83a5dee07a45ad3d4a1ef3f4e7a771f15a320d58a0ca3551c2b` | Exact Matplotlib dependency failure; no provisional PNG |

The first full-harness attempt is also retained as
`added_feature_regression_convection_hardening_2026-08-26.failed_initial.log`.
It exposed that the new isolated model's original duration was shorter than
the existing OpenFOAM warm-up contract. The fixture was corrected to 30 s and
the complete harness was rerun from the beginning; the failed log is diagnostic
history, not a pass.

## Remaining release blockers

The current result is suitable for continued controlled screening, not an
industry release. Promotion still requires, at minimum:

1. measured NI separator dimensions/vent communication and a topology that
   passes exact stamping, followed by native and OpenFOAM sensitivity runs;
2. measured rail-2 depths, heat loads, fan curves/operating points, and NI open
   areas;
3. a determinant-clean, topology-preserving mesh and a mesh-independence study;
4. a current-geometry OpenFOAM airflow convergence result, thermal soak,
   gap-free transient energy audit, and experimental flow/temperature
   comparison;
5. a clean build and runtime proof of the hardened custom OpenFOAM solver; and
6. memory-stable full-rack A/B evidence before promoting any remaining speed
   candidate.

The isolated NI fixture is configured for only 30 s, while the thermal
convergence gate begins at 2400 s. Even if its geometry passed stamping, that
fixture could not support a soak or convergence claim.
