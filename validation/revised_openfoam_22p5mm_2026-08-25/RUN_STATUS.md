# Revised 22.5 mm OpenFOAM screening status

> **Current authority update (2026-08-27).** This file is the chronological
> record of the preserved 22.5 mm, pre-correction case. Every `model.exe`, O2
> executable, “current-source”, “production”, and “current geometry” claim below
> is pinned historical evidence, not present binary provenance. There is **no
> production-qualified exporter binary for current source**. Before any new
> export, rebuild and hash current source, record the compiler/build command and
> byte count, and designate that exact executable as `[CURRENT_EXPORTER_EXE]`.
>
> The final current-source software regression is
> `../revised_native_regression_2026-08-26/added_feature_regression_runner_state_gates_2026-08-27.run.json`
> (SHA-256
> `246620007dff4b6f7dea50b22bcc9bc987e359aa575310134a9930da672a6c8c`):
> PASS, exit 0, 516.584 s, affinity mask 3 (two logical processors), BelowNormal
> priority. Its stdout SHA-256 is
> `03d781c9abe21ec3722e038672ef4c6587bed734cc896d97ddde3ed17a7bd81c`
> and stderr SHA-256 is
> `f4dba93c473013728126639c6b07f4c86196eeba63b61600c3abab37dc051d23`.
> This is software-only evidence. The final idle gate,
> `../revised_native_regression_2026-08-26/openfoam_resource_gate_final_idle_20260827T095617107Z.json`
> (SHA-256
> `33c4fb9525398af3c7ec53156fe214a422b3d0e67ca13e144d0c5fcbe5ca6d51`),
> failed with exit 21: no competitors and 17,129,152,512 free disk bytes passed,
> but 951,701,504 available-memory bytes failed the 5,368,709,120-byte floor.
> WSL was not queried.

This directory records the resource-bounded, four-rank screening run of
`library/models/new_model_updated_openfoam_export_test.toml`. It is a
screening case, not an industry-ready validation result. The mesh contains
1,033,200 cells (925,388 fluid cells), one connected fluid volume, 24 ambient
opening patches, 13 component solid regions, 33 internal fan zones, and 47
heat-source zones totaling exactly 2,315 W.

The 33 internal zones reconcile with the 38 component fan definitions: five
Trenton front intake fans lie on the rack exterior and become ambient fan
patches. The rack contributes another nine ambient roof-exhaust fan patches.
Boundary-patch mass flow and direction are audited separately from the 33
internal-zone operating points.

## Retained historical airflow checkpoint

At 0.10 s, all 33 internal fans have positive flow. Six brief reverse-flow
points occurred during the 0.01--0.02 s startup transient and cleared by
0.03 s. Boundary outflow is 0.67858596 kg/s and inflow is 0.67289812 kg/s,
leaving +0.00568784 kg/s net flow. The mismatch is 0.83819% when normalized by
the larger gross direction, passing the configured 1% screening threshold for
the first time. Fluid temperature is 292.9322--293.3914 K with a 293.1856 K
volume average. These near-ambient temperatures are expected at such a short
physical time and do not validate thermal performance.

The mismatch history is strongly improving but not monotonic: 45.495% at
0.01 s, 15.770% at 0.02 s, 3.688% at 0.04 s, 1.919% at 0.05 s, 1.023% at
0.07 s, 1.170% at 0.08 s, and 0.838% at 0.10 s. A bounded 0.10--0.15 s
four-rank extension completed successfully. At 0.15 s, outflow is
0.64269906 kg/s, inflow is 0.640256718 kg/s, and mismatch improves to 0.380013%.
All 33 fans remain forward-flowing. Fluid temperature is
292.8803--293.3535 K with a 293.1849 K volume average. The interval required
1,971.7 solver CPU-seconds and 2,007 wall-seconds.

The good global balance does **not** mean the field is converged. From 0.10 to
0.15 s, whole-fluid velocity RMS changed by 0.89478 m/s, or 36.822% of the
0.15 s velocity RMS; gauge-adjusted `p_rgh` changed by 37.161%. Component-air
velocity changes remain large, including 85.0% in the Dell air region and
100.5% in the low-speed KVM air region. Temperature RMS changed only 0.00587 K
because this is still a very short cold-start interval. The bounded 0.15--0.20 s
four-rank extension described below was therefore required; stopping at
0.15 s would have misclassified a balanced but still-redistributing flow field
as converged.

The 0.15--0.20 s extension also completed successfully. At 0.20 s, outflow is
0.62216035 kg/s, inflow is 0.620158516 kg/s, and mismatch is 0.321755%; all
33 latest fan operating points remain positive. Fluid temperature is
292.9587--293.4114 K with a 293.1843 K average. The interval required
1,950.54 solver CPU-seconds and 1,978 wall-seconds.

Spatial change is declining but remains unacceptable for flow freezing:
whole-fluid velocity relative RMS change was 29.844% from 0.15 to 0.20 s and
gauge-adjusted `p_rgh` changed 25.121%. Dell and KVM component-air velocity
changes were 61.1% and 63.4%. The screening configuration independently
requires at least 0.30 s of initial airflow before acceptance, so a bounded
0.20--0.30 s continuation was required regardless of the passing global mass
balance. The retry from the intact 0.20 s checkpoint ran on exactly four MPI
ranks and completed and reconstructed successfully at
0.30 s. The reconstructed checkpoint and all four processor checkpoints each
contain the complete expected set of 74 files.

At 0.30 s, boundary outflow is 0.59128968 kg/s and inflow is
0.590139453 kg/s, leaving +0.001150227 kg/s net flow and a 0.194529% mismatch.
All 33 retained latest fan operating points remain positive. However, the
flow field still fails the configured 3% screening acceptance limit by a wide
margin: whole-fluid velocity relative RMS change is 37.2221% from 0.20 to
0.30 s and gauge-adjusted `p_rgh` change is 21.7148%. Component-air velocity
changes include 49.75% in the Dell region, 82.17% in the KVM region, and
47.08% in one Keysight N6701 region. The near-ambient temperature field only
changed 0.00470 K RMS. Therefore the 0.30 s state is a valid, globally balanced
checkpoint but is not an acceptable frozen-flow state for thermal prediction.

An equal-length 0.30--0.40 s four-rank continuation also completed and
reconstructed successfully; its reconstructed and four processor checkpoints
again contain 74 files each. At 0.40 s, outflow is 0.57229716 kg/s, inflow is
0.571582551 kg/s, and mismatch improves to 0.124867%. All 33 fans remain
forward-flowing. Whole-fluid velocity relative RMS change decreases from
37.2221% over 0.20--0.30 s to 31.1560% over 0.30--0.40 s, while gauge-adjusted
`p_rgh` change decreases from 21.7148% to 19.7337%. This proves a settling
trend but remains more than ten times the 3% screening acceptance limit.
Device-air changes are still large, including 61.57% for the Dell region,
56.02% for the KVM region, and 33.86% for external rack air. The 0.40 s state
therefore also fails the frozen-flow gate despite excellent global balance.

A third equal-length 0.40--0.50 s continuation completed in 3,756 wall-seconds
and again produced complete 74-file reconstructed and four-rank checkpoints.
At 0.50 s, outflow is 0.56062212 kg/s, inflow is 0.560127652 kg/s, and mismatch
is 0.0881999%; all 33 fans remain forward-flowing. Whole-fluid velocity change
decreases further to 24.3214% and gauge-adjusted `p_rgh` change to 13.8506%.
The three equal 0.10 s intervals therefore show velocity changes of 37.2221%,
31.1560%, and 24.3214%: a real but slow settling trend, still more than eight
times the 3% acceptance limit. Dell component air remains at 44.72%, a Keysight
N6701 region reaches 34.58%, and external rack air is 27.15%. The 0.50 s state
may be used to exercise workflow mechanics, but it is not accepted as a
validated frozen-flow field and any thermal result derived from it must be
labelled provisional.

A fourth equal-length 0.50--0.60 s continuation completed cleanly on exactly
four ranks. The endpoint directory is named `0.59999999999999998`; it contains
all 74 expected files in the reconstructed case and in each of `processor0`
through `processor3`. The final boundary audit gives 0.55297313 kg/s outflow,
0.552723987 kg/s inflow, and 0.0450552% mismatch, with all 33 internal fans
forward-flowing. Whole-fluid velocity change decreases again to 20.6088% and
gauge-adjusted `p_rgh` change to 10.0309%. This is continued improvement, but
velocity change is still nearly seven times the 3% acceptance limit. Dell air
remains at 38.05%, one Keysight N6701 region at 28.46%, external rack air at
23.17%, and NI chassis air at 8.92%. Temperature changes remain tiny because
this is still an airflow-settling interval; they do not establish a mature
thermal transient. The 0.60 state is therefore a valid restart checkpoint but
not an accepted frozen-flow solution.

A fifth equal-length 0.60--0.70 s continuation also completed cleanly on four
ranks, with all 74 expected files in the reconstructed endpoint and each of
the four processor endpoints. Boundary flow is 0.5475995 kg/s out and
0.547394618 kg/s in, a 0.0374146% mismatch, and all 33 fans remain
forward-flowing. Whole-fluid velocity change decreases to 17.9262%, while
gauge-adjusted `p_rgh` change is 10.8961% and does not improve over the prior
10.0309% interval. Dell air remains at 37.81%, one Keysight N6701 region at
35.01%, external rack air at 19.86%, and NI chassis air at 8.45%. The airflow
is still evolving materially and the 0.70 state is a valid restart checkpoint,
not an accepted frozen-flow solution.

A sixth equal-length 0.70--0.80 s continuation completed cleanly on exactly
four ranks. The reconstructed endpoint is named `0.80000000000000004`, and it
and each of the four processor endpoints contain all 74 expected files. The
run exited zero after 3,719.35 solver CPU-seconds and 3,744 wall-seconds. At
0.80 s, boundary outflow is 0.54428661 kg/s and inflow is 0.544168345 kg/s,
leaving a 0.0217284% mismatch; all 33 audited fans remain forward-flowing.
Whole-fluid velocity relative RMS change improves only modestly, from 17.9262%
to 16.4451%, while gauge-adjusted `p_rgh` change improves from 10.8961% to
9.1213%. The dominant Dell air region remains highly unsettled at 43.79%; one
Keysight N6701 region is 25.59%, external rack air is 17.42%, and NI chassis
air is 8.42%. The maximum velocity change, 23.45 m/s, occurs in the Dell air
region near `(0.489099, 0.27, 0.1838)` m. Thus the global balance is excellent
but the spatial velocity field remains more than five times the configured 3%
acceptance gate. Temperature changes near ambient remain too small and too
early to constitute thermal validation. The 0.80 state is a complete restart
checkpoint, not an accepted frozen-flow or industry-ready solution.

A seventh equal-length 0.80--0.90 s continuation completed cleanly on exactly
four ranks in 3,639.45 solver CPU-seconds and 3,665 wall-seconds. The
reconstructed `0.90000000000000002` endpoint and each processor endpoint again
contain all 74 expected files. Boundary outflow is 0.54211256 kg/s and inflow
is 0.542068403 kg/s, giving a 0.00814536% mismatch; all 33 fans remain
forward-flowing. However, spatial settling has stalled: whole-fluid velocity
change is 16.5161%, slightly worse than 16.4451% in the preceding interval,
and `p_rgh` change worsens from 9.1213% to 11.6470%. Dell air change increases
from 43.79% to 53.02%, while external rack air remains 15.93% and NI chassis
air increases to 10.12%. The largest velocity change remains in the Dell air
region near `(0.489099, 0.27, 0.1838)` m. A longer 0.70--0.90 comparison is
also large (24.9441% velocity and 9.8540% `p_rgh`), contradicting a simple
monotonic approach to a steady field over these short windows. This does not
yet prove a permanently oscillatory field: the fluid volume is 1.061551 m3,
and trapezoidal integration of the audited one-way boundary mass throughput,
using the model's 0.9833 kg/m3 ambient density, gives 0.543913 m3 or 0.512376
air replacements through 0.90 s. At the latest flow, one cumulative
replacement projects near 1.839 s, and the screening profile explicitly
requires at least one replacement before acceptance. The present evidence therefore
shows an incompletely developed, locally unsteady flow dominated by the Dell
region. Further continuation is physically justified through at least one
cumulative air exchange, followed by long-window and instantaneous checks;
thermal freezing remains prohibited meanwhile.

An eighth 0.90--1.00 s continuation completed cleanly on exactly four ranks
in 3,671.98 solver CPU-seconds and 3,705 wall-seconds. The reconstructed `1`
endpoint and all four processor endpoints contain all 74 expected files.
Boundary outflow is 0.5408542 kg/s and inflow is 0.540792258 kg/s, a
0.0114526% mismatch; all 33 fans remain forward-flowing. Whole-fluid velocity
change improves to 15.1474% and `p_rgh` change to 7.4017%, but both remain far
above the 3% screening gate. Dell air remains dominant at 51.91%, external
rack air is 13.92%, and NI chassis air is 11.55%. Integrated ventilation is
0.598978 m3, or 0.564248 air replacements. At the latest throughput, one
cumulative replacement is projected near 1.841 s. This endpoint is therefore
a complete and balanced restart, but is still physically too early for the
profile's minimum exchange criterion or thermal freezing.

A ninth 1.00--1.10 s continuation completed cleanly on exactly four ranks in
3,665.58 solver CPU-seconds and 3,701 wall-seconds. The reconstructed
`1.1000000000000001` endpoint and every processor endpoint contain all 74
expected files. Boundary outflow is 0.54070072 kg/s and inflow is
0.540639815 kg/s, a 0.0112641% mismatch; all 33 fans remain forward-flowing.
Whole-fluid velocity change improves to 14.1718%, while `p_rgh` change is
8.3916%. Dell air remains dominant at 48.16%, external rack air is 13.15%,
and NI chassis air is 9.32%. Integrated ventilation reaches 0.653971 m3, or
0.616052 air replacements; one replacement remains projected near 1.841 s.
The endpoint is valid for restart but still fails both the exchange and
instantaneous spatial gates, so thermal freezing remains prohibited.

A tenth 1.10--1.20 s continuation completed cleanly on exactly four ranks in
3,730.76 solver CPU-seconds and 3,759 wall-seconds. The reconstructed `1.2`
endpoint and all four processor endpoints contain all 74 expected files.
Boundary outflow is 0.54061885 kg/s and inflow is 0.540589539 kg/s, a
0.00542175% mismatch; all 33 fans remain forward-flowing. Whole-fluid
velocity change improves to 13.2531%, while `p_rgh` change is 7.5361%.
The Dell air region remains the dominant local transient at 40.8392%; NI
chassis air is 10.9966% and external rack air is 12.8727%. Temperature changes
remain negligible because this is still the airflow-development stage, not a
validated thermal result. Integrated ventilation reaches 0.708953 m3, or
0.667846 air replacements. The endpoint is balanced and restartable, but it
still fails the required one-exchange minimum and the 3% instantaneous spatial
gate; thermal freezing remains prohibited.

An eleventh 1.20--1.30 s continuation completed cleanly on exactly four ranks
in 3,650.28 solver CPU-seconds and 3,681 wall-seconds. The reconstructed `1.3`
endpoint and all four processor endpoints each contain all 74 expected files.
Boundary outflow is 0.54066863 kg/s and inflow is 0.540619325 kg/s, a
0.00911926% mismatch; all 33 fans remain forward-flowing. Whole-fluid
velocity change improves to 10.3478% and `p_rgh` change to 4.4406%, but both
still fail the 3% spatial gate. Dell air remains the dominant local transient
at 26.2522%; NI chassis air changes 9.1081% and external rack air 10.9117%.
Integrated ventilation reaches 0.763934 m3, or 0.719639 air replacements, with
one cumulative replacement projected near 1.841 s. This complete endpoint is
valid for restart but still too early for airflow freezing or thermal-stage
acceptance.

A twelfth 1.30--1.40 s continuation completed cleanly on exactly four ranks
in 3,685.31 solver CPU-seconds and 3,716 wall-seconds. The reconstructed
`1.3999999999999999` endpoint and all four processor endpoints each contain
all 74 expected files. Boundary outflow is 0.54067045 kg/s and inflow is
0.540648537 kg/s, a 0.00405293% mismatch; all 33 fans remain forward-flowing.
Whole-fluid velocity change improves to 9.3175%, while `p_rgh` change rises
to 5.6694%; both fail the 3% spatial gate. Dell air changes 30.0862%, external
rack air 9.0368%, and NI chassis air 5.6017%. Integrated ventilation reaches
0.818917 m3, or 0.771434 air replacements, with one replacement still
projected near 1.841 s. This endpoint is a complete balanced restart, but is
not suitable for airflow freezing or thermal-stage acceptance.

A thirteenth 1.40--1.50 s continuation completed cleanly on exactly four
ranks in 3,721.96 solver CPU-seconds and 3,755 wall-seconds. The reconstructed
`1.5` endpoint and all four processor endpoints each contain all 74 expected
files. Boundary outflow is 0.5404596 kg/s and inflow is 0.540490145 kg/s, a
0.00565135% mismatch; all 33 fans remain forward-flowing. Whole-fluid velocity
change is 9.3467% and `p_rgh` change is 6.0458%, so neither passes the 3%
spatial gate. Dell air changes 36.1819%, external rack air 7.8837%, and NI
chassis air 4.8751%. The required 1.30--1.50 long-window comparison is worse:
velocity changes 14.2558% and `p_rgh` 8.2830%, confirming accumulated spatial
drift rather than short-window cancellation. Integrated ventilation reaches
0.873892 m3, or 0.823222 air replacements, with one replacement projected
near 1.841 s. Airflow freezing and thermal-stage acceptance remain prohibited.

A fourteenth 1.50--1.60 s continuation solved all 200 requested timesteps on
four ranks and wrote complete 74-file processor checkpoints. The solver used
4,699.38 execution-seconds and 4,739 wall-seconds (23.695 s/step), about 26%
slower than the preceding window; an uncaptured live observation at the time
showed Windows available memory near 0.5 GiB. The final maximum Courant number
was 2.3208 and cumulative continuity
error was `-1.1264e-05`; no OpenFOAM fatal signature occurred. Boundary outflow
is 0.54023564 kg/s and inflow is 0.540265157 kg/s, a 0.00546343% mismatch.
All 14 boundary fans have the intended sign, all 10 passive openings are
measured, and all 33 internal fan operating points remain positive.

Positive direction is not sufficient to establish a physically supported fan
operating point. A new audit of the curves actually exported in `fvOptions`
and `0/fluid/p_rgh` compares each measured flow with its first zero-pressure
crossing. At 1.600 s, 10 of 47 fans are outside the positive-pressure curve
domain, 8 more are at or above 90% of that limit, and 29 pass. The failures are
the Eaton UPS rear fan (232.33%), the Trenton internal rear fan (102.31%),
Thruster exhaust fan 2 (100.03%), Cisco exhaust fans 1 and 3 (100.46% and
102.26%), and all five Trenton boundary intakes (138.45--153.83%). The six
Dell fans, Thruster exhaust fan 1, and NI bottom fan 1 are near-limit warnings.

The exported tables clamp pressure to zero beyond the crossing. Therefore the
10 failed flows are being driven through interfaces that add no modeled fan
pressure at those operating points. This does not invalidate the excellent
kinematic mass balance or make the solve numerically unstable, but it does
invalidate quantitative interpretation of those device flow splits until the
curves/resistances are updated or the exploratory approximation is explicitly
accepted. It is now a fail-closed airflow-freeze gate in newly exported
runners. The exact results are in `fan_operating_domain_1p60.csv` (SHA-256
`7ab6faeb69810deb31821508d2b88795fa1ae9c97feef925cfcced4f194f8ba2`)
and `fan_operating_domain_1p60.md` (SHA-256
`d24af0093ef34e0ea36a9c29d3191c49905edf7ed5a4726ff6cd488e0349209e`).

The flow field is still not acceptable for freezing. From 1.50 to 1.60 s,
whole-fluid velocity changes 8.8677% RMS and gauge-adjusted `p_rgh` changes
6.3867% RMS. Over the longer 1.40--1.60 s window those changes are 13.8391%
and 6.1058%. The Dell component-air region remains dominant at 35.0028%
velocity change over the latest 0.10 s; external rack air changes 7.2267% and
NI chassis air 4.9392%. Fluid temperature is 292.949--294.462 K, but its
1.50--1.60 s RMS change is only 0.001680 K because this remains an early
airflow-development run, not a mature thermal transient. Integrated
ventilation is 0.928846 m3, or 0.874989 air replacements; one replacement is
projected at 1.841535 s. Both the exchange and spatial-convergence gates still
prohibit thermal-stage acceptance.

The solver itself reached 1.6000000000000001 successfully, but the generated
`run_parallel.sh` was overwritten by a concurrent exporter regression while
Bash was still reading it. That changed the script inode contents underneath
the live shell and corrupted only the first post-solve command (`onFoam:
command not found`). All four processor endpoints were verified complete.
The exact endpoint was then reconstructed under the case lock and reports were
regenerated with `tools/openfoam_postprocess_checkpoint.sh`; the reconstructed
root endpoint also has 74/74 nonempty files. This is retained as a workflow
failure and recovery, not misreported as a clean wrapper exit.

The exchange figures above supersede earlier values that were inadvertently
converted with 1.204 kg/m3 sea-level density. The exported runner, the model,
and this corrected audit use the intended 0.9833 kg/m3 density at 5,500 ft.
This accounting correction does not change any OpenFOAM field or convergence
metric; it shortens the one-exchange observation horizon from the previously
reported approximately 2.28 s to approximately 1.84 s.

## Controlled timestep benchmark

Two four-rank branches restarted from the same complete 1.600 s checkpoint
and solved the identical 1.600--1.610 s interval. The accepted-reference cap
of 0.0005 s used 20 steps and 521 solver clock-seconds. A 0.00075 s cap used
14 steps but required 572 clock-seconds: 30% fewer steps produced a 9.79%
longer run, and post-initialization cost rose from 23.158 to 34.000 seconds per
step. Its maximum Courant number was 3.405.

The larger-step endpoint also fails the physics-equivalence screen. Relative
to the reference it changes whole-field velocity by 1.155754% RMS and
gauge-adjusted `p_rgh` by 11.946965% RMS; 10 of 33 internal fan operating
points exceed the per-fan tolerance, with a worst difference of 14.6761%.
Temperature RMS and gross boundary throughput pass, but those aggregate
passes do not override the spatial and device-flow failures. The 0.00075 s
setting is rejected, the generated 0.0005 s default remains authoritative,
and the prerequisite for a 0.001 s test was not met.

All logs, audits, comparisons, and timing data are retained under
`performance_benchmarks_1p60/`. The reconstructed reference and rejected
variant endpoints, their exact system inputs, linked mesh files, and raw
function-object reports are preserved as standalone cases under
`openfoam_benchmark_snapshots/dt_baseline_0p0005_case` and
`openfoam_benchmark_snapshots/dt_variant_0p00075_case`. SHA-256 comparison
confirms all 74 reconstructed variant files and all 196 variant report files
are byte-identical to the tested active endpoint. After that verification,
only the five rejected active-case 1.610 endpoint directories were removed
(261,417,911 bytes); the complete active-continuation 1.600 checkpoint remains in the
root and all four ranks.

## Controlled decomposition benchmark

An independent four-rank branch changed only the CHT interface-placement
request from processor `0` to auto selection (`-1`) and restarted from the
same byte-identified 1.600 s fields. The selected rank totals are 291,033 /
183,503 / 214,852 / 343,812 cells, reducing maximum-rank/mean-rank load from
1.661 to 1.331 and the heaviest rank by 19.86%.

Both branches completed the identical 20-step, 1.600--1.610 s interval at
0.0005 s. Auto placement reduced total clock time from 521 to 441 s (15.36%)
and post-first-step cost from 23.158 to 20.474 s/step (11.59%). It passed the
strict field and device checks: velocity differed by 0.002838% RMS,
gauge-adjusted `p_rgh` by 0.010373% RMS, temperature by 0.000003181 K RMS and
0.000183 K maximum, and the worst internal-fan flow difference was 0.01236%.
All 33 internal fans remained forward, all 14 boundary fans were correctly
directed, and mass mismatch was 0.001972%.

This advances auto placement as the short-screen-passing candidate for a
longer continuation confirmation, not as a generated default. The full
decision record and raw evidence are in
`performance_benchmarks_1p60/DECOMPOSITION_BENCHMARK.md`.

The first 1.610--1.700 s confirmation attempt was intentionally stopped during
its eighth step after transcribed live observations reported 515--557 MiB
available memory and about 303 pages/s. Its timing was classified as
resource-invalid: post-first-step time was 33.333 s/step and the latest complete
step took 52 s, versus 20.474 s/step in the matched benchmark. Seven steps
completed with peak maximum Courant 2.32716 and no fatal signature, but no new
checkpoint was written. The root plus four-rank 1.610 endpoint remains complete
at 74/74 nonempty files. No solver or physics-failure signature was observed.
See
`performance_benchmarks_1p60/DECOMPOSITION_LONG_CONFIRMATION_ATTEMPT.md`.

## Controlled PIMPLE benchmark

A second independent branch retained the auto decomposition, 0.0005 s
timestep, two pressure correctors, and exact 1.600 s restart, changing only
PIMPLE outer correctors from three to two. It completed all 20 steps with peak
maximum Courant 2.32911 and no true fatal signature. Total clock time fell from
441 to 324 s, and post-first-step wall cost fell from 20.474 to 14.895 s/step,
a 27.25% speedup.

The endpoint fails equivalence despite that speed. Velocity changed by
0.891725% RMS and gauge-adjusted `p_rgh` by 1.497398% RMS against 0.1% limits.
Temperature RMS passed at 0.000291910 K, but maximum temperature difference
was 0.0390625 K against 0.02 K. Three of 33 internal fans exceeded tolerance;
the worst flow change was 4.28286%. Boundary mismatch was only 0.000707%, all
14 boundary fans retained the intended direction, and all 33 internal fans
remained forward, but those aggregate passes do not override the spatial and
per-device failures. The 2x2 setting is rejected; 3x2 PIMPLE remains
authoritative while the generated decomposition remains pinned.
See `performance_benchmarks_1p60/PIMPLE_BENCHMARK.md`.

After the evidence and copied log were verified, the five rejected 1.610 field
directories were removed from the candidate scratch case: 370 files and
265,473,197 logical bytes, recovering 266,067,968 bytes. Its complete 1.600
restart and all reproduction inputs remain.

## Controlled three-rank resource benchmark

A root-only physical clone of the verified 1.600 s state was decomposed to
three ranks with unchanged 3x2 PIMPLE, 0.0005 s timestep, and auto interface
placement. All 731 copied inputs matched, all 74 start fields remained
byte-identical after decomposition, and only root `numberOfSubdomains` changed.
The partition was valid and well balanced: totals 391,272 / 400,425 / 241,503,
maximum/ideal 1.163, max/min 1.658, exact aggregate cell counts, every fluid
rank nonempty, and all 13 solids singly owned. Native restart links exposed
36 linked `uniform` files alongside 38 physical field files on every rank in a
live check. This per-rank observation was not retained as a standalone
manifest; the retained 74-row identity manifest verifies reconstructed root
start fields.

The solver nevertheless failed the memory-validity criteria used during the run. Live
samples showed only 409--463 MiB available, 323--1,467 pages/s, and up to
3.885 GiB WSL private memory with zero Fluent processes. It was intentionally
stopped after one complete step and after the second began. That step had the
expected 6 pressure and 3-per-component velocity solves, maximum Courant
2.32215, no fatal signature, and 84 s wall time versus 52 s for the four-rank
reference first step. No new endpoint was written, so no physics-equivalence
claim is made. Three ranks are rejected as a memory mitigation on this host;
see `performance_benchmarks_1p60/THREE_RANK_RESOURCE_BENCHMARK.md`.

After 11 evidence artifacts were hashed, the endpoint-free scratch case was
deleted under the approved redundant-field cleanup: 2,071 files and
798,457,589 logical bytes, recovering 804,163,584 bytes. The retained evidence
and source case are sufficient to recreate it.

## Active-case benchmark-state recovery

An integrity audit found that the active case's complete fields stopped at
1.600 s while its mutable `controlDict`, run-summary tail, and 196 tiny
post-processing directories still reflected the rejected 1.610 s / 0.00075 s
timestep branch. All 198 contaminated-state artifacts were copied and verified
against a SHA-256 manifest before cleanup. The active dictionary now starts at
1.600 s with both `deltaT` and `maxDeltaT` set to the accepted 0.0005 s cap;
its installed SHA-256 is
`29d2941c87e5c40854742fd8942b59b0786d1442021a3d777633ae675b5f7c97`.
The 196 preserved rejected report directories were removed, with zero remaining
and no accepted field directory touched. See
`performance_benchmarks_1p60/ACTIVE_CASE_RECOVERY_1P600.md`. The generated
exporter and installed active launcher now have a tested fail-closed lineage
preflight; the installed launcher hashes to
`e47c392ea50572dca29c48681ec7d3457baf871b5d22cdc8d1b229b9ade5d7cd`.
The content-level guard also identified one y+ file under a 1.600 directory
whose samples were from the rejected 1.610 branch. That 1,349-byte file was
hash-preserved and removed. A complete follow-up scan passed all 92 required
restart fields with zero future directories, zero future report files, and zero
lineage failures.
Continuation remains gated on memory validity and physics convergence.

## Reproducible checkpoint commands

The 1.50--1.60 s continuation was invoked from the active case in an
OpenFOAM 2606 Bash environment as:

```bash
./run_parallel.sh 4 --warm-start 1.60 \
  > revised_warm_1p50_to_1p60.stdout.log 2>&1
```

The log itself records the resolved controls, executable, build, case path,
host, and four-rank launch. Run the checkpoint audits from the project root in
PowerShell with the exact case and evidence paths:

```powershell
$case = 'C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases\new_model_updated_openfoam_export_test'
$evidence = Join-Path (Get-Location) 'validation\revised_openfoam_22p5mm_2026-08-25'
$sciencePython = 'C:\Users\hconn\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'

python tools\openfoam_progress.py $case `
  --log (Join-Path $case 'revised_warm_1p50_to_1p60.stdout.log') `
  --end-time 1.6 --minimum-free-gib 1

python tools\openfoam_boundary_mass_balance_audit.py $case `
  --csv (Join-Path $evidence 'boundary_mass_balance_through_1p60.csv') `
  --density 0.9833 --fluid-volume 1.061551306555625 `
  --airflow-devices `
  --openings-csv (Join-Path $evidence 'boundary_opening_directions_1p60.csv')

python tools\openfoam_fan_flow_audit.py $case `
  --markdown (Join-Path $evidence 'fan_flow_through_1p60.md')

python tools\validate_openfoam_case.py $case `
  --json (Join-Path $evidence 'validation_1p60.json') `
  --markdown (Join-Path $evidence 'validation_1p60.md')

& $sciencePython tools\openfoam_field_convergence.py $case `
  --times 1.4 1.5 1.6 --fields U T p_rgh --reference final `
  --geometry (Join-Path $case 'geometry.txt') `
  --csv (Join-Path $evidence 'field_convergence_1p40_1p50_to_1p60.csv')

python tools\openfoam_partition_audit.py $case `
  --json (Join-Path $evidence 'partition_audit_4rank_1p60.json') `
  --markdown (Join-Path $evidence 'partition_audit_4rank_1p60.md') `
  --maximum-over-ideal 0.45

Copy-Item -LiteralPath (Join-Path $case 'revised_warm_1p50_to_1p60.stdout.log') `
  -Destination $evidence
Get-FileHash -Algorithm SHA256 `
  -LiteralPath (Join-Path $evidence 'revised_warm_1p50_to_1p60.stdout.log')
```

The validation command intentionally retains a nonzero energy-balance result
at this very short physical time as failed screening evidence; it must not be
relabeled as a converged thermal pass. Field comparisons require the bundled
NumPy/PyVista runtime and should run only after the OpenFOAM process releases
memory.

## Multirate workflow gate

A no-extension `--multirate 0.50` probe at the existing 0.50 s checkpoint
exposed a false-success path: the generated script ran neither an airflow nor
thermal stage, created no convergence marker, but printed `Multirate run
complete` and returned success. The exporter and this case's generated script
now require the requested multirate end time to be strictly greater than the
latest processor checkpoint. The exporter regression compiles and passes, and
the repeated full-case probe now exits nonzero immediately with an explicit
`no airflow or thermal stage was run` diagnostic. This prevents automation
from recording a no-op as a completed thermal simulation.

## Regression status

The complete added-feature harness passes after two test-infrastructure fixes:
the device-report fixture now supplies the valid positive pressure curve
required for an internal pressure-jump fan, and NumPy-dependent reporting and
field-comparison tests explicitly skip when that optional dependency is absent
from the default Python runtime. The scientific field comparisons above were
run separately with the bundled NumPy/PyVista runtime. The main `test_runner`
suite also reports `ALL UNIT TESTS PASSED`; the focused updated-model,
geometry, fan-flow, material-fidelity, heat-source, and plotting suites pass.
The artist-level air-color test was run with the bundled Matplotlib runtime
rather than counted as a skip in the default Python environment. On 2026-08-26,
the bundled scientific runtime reran 33 targeted cross-case comparison,
field-convergence, plotting, fan-flow, and boundary-balance tests with zero
skips and zero failures, including both rendered and artist-level same-color
air-region checks. The preserved verbatim test output is
`scientific_targeted_tests_2026-08-26.stdout.log` (SHA-256
`80ff628cb8885c4dc30270955c927822c1d543dd430a3f75ce0cb8858279fab5`).
After the restart-lineage hardening, the complete added-feature harness was
also rebuilt and rerun successfully. Its preserved output is
`added_feature_regression_2026-08-26.stdout.log` (SHA-256
`9e765c88f25ec6955fe62f86ede12493ffc6a2ad81a60ddc7df13fca86b54e07`).
The real-WSL flock-lifetime integration check was explicitly skipped because
the WSL service was unavailable to that sandboxed test invocation; its
synthetic read-only, lock, archive, verification, rollback, resume, and delete
checks passed. NumPy/PyVista skips in the default runtime remain covered by the
separate zero-skip bundled-scientific run above.

After adding the fail-closed fan positive-pressure-domain gate and the
installed-component geometry contracts, the complete harness was rebuilt and
rerun again. It passed, including both modified C++ exporter/configuration
tests, the new seven-test fan-domain module, the machine-readable assumptions
contract, and the fail-closed mesh-determinant export policy. The strict policy
defaults to rejecting determinant warnings; screening must opt in explicitly,
and even then failed-check counts must exactly match determinant diagnostics.
The verbatim output is
`added_feature_regression_fan_domain_2026-08-26.stdout.log` (SHA-256
`49b83db848cd5a2b314eeef99153affb4c40c6461289132b2fc693ea5a5bb5eb`,
66,397 bytes, 890 lines).
An independently generated fan-enabled `run_parallel.sh` also passed Git Bash
`bash -n` syntax validation. A 57-test targeted rerun passed with one
Matplotlib artist-test skip because that optional package was no longer
present after runtime-cache cleanup; its log is
`scientific_targeted_tests_fan_domain_2026-08-26.stdout.log` (SHA-256
`1bd8e626aa6138b002c018cdb7e04c86a0c09fc896f7cb2dd4bc08dbb3d86a7c`).
This skip does not replace the earlier preserved zero-skip artist run: all
three plotter/render source hashes still match its current plot manifest, and
none was changed by the fan-domain work.
These green regressions validate the implemented policies and tooling; they do
not override the failed full-case airflow convergence or known geometry and
material-fidelity limitations.

## Preparation and execution robustness

The generated preparation workflow now checkpoints every `topoSet` operation
and `splitMeshRegions`, permits safe restart after WSL interruption, and
disconnects utilities from inherited stdin. The 22.5 mm case preparation
completed after earlier WSL memory instability. `checkMesh` found no failure
other than the determinant check, but that failure was previously described
too narrowly. It affects the fluid plus 12 of 13 solids: 39,878 cells are
below determinant 0.001, comprising 5,082 fluid cells and 34,796 solid cells;
only the storage-shelf solid reports `Mesh OK`. The screen retains this mesh
for qualitative and workflow evidence, but it is not accepted for quantitative
sign-off. The exact
per-region counts and release gate are in `MESH_QUALITY_AUDIT.md`.

The exporter now enforces that release distinction for future cases:
`allow_determinant_warnings` defaults to false in the low-level exporter and
the default, in-depth, and validation profiles. Screening alone sets it true
and emits an exploratory-only warning. Any non-determinant failure, unexpected
diagnostic, or mismatch between failed-check and determinant counts remains a
hard stop. This policy was not installed into the active 1.600 s case because
re-exporting with `overwrite=true` would destroy its preserved restart and
post-processing lineage; the external audit remains authoritative for it.
Reconstruction at 0.08 s initially failed while WSL was in an
I/O-faulted state; all processor fields remained intact, and reconstruction
succeeded after a WSL shutdown/restart. This recovery is documented rather
than silently discarded.

The first 0.20--0.30 s continuation remained numerically stable through its
last solved step near 0.30 s, but the C: volume reached zero free bytes during
the endpoint write. The resulting processor checkpoint contained only 43--46
of the 74 expected files per rank and was not reconstructable. With explicit
user approval, 54 redundant time directories were removed: reconstructed and
processor copies at 0.01--0.08, 0.10, and 0.15 s, plus the four incomplete
0.30 s processor directories. Initial fields, meshes, the complete 0.20 s
restart, logs, audits, plots, and native-run evidence were retained. The exact
cleanup recovered 2,667,730,334 bytes. The subsequent retry therefore started
from the last complete checkpoint rather than treating the partial endpoint as
valid evidence.

The complete 0.20 and 0.30 s five-directory checkpoint groups were
subsequently archived as GitHub Release assets and removed locally only after
remote byte-count and SHA-256 verification. The assets are
`new_model_updated_t0p2.tar` (261,710,848 bytes,
`a30a8b2dc09bf960d5711fadf1f58cb9c460e6ae418869a3b16aeef6bf4903b9`)
and `new_model_updated_t0p3.tar` (261,594,112 bytes,
`b61b71d1516df7a7674b26a912cf8b6cb0f74d06ead75650bc27ffd644578fc5`).
Their restore manifest is in the private
`hughes6/thermal-sim-openfoam-archive` repository, release
`openfoam-checkpoints-2026-08-25`. Therefore “retained” above describes the
post-cleanup recovery state, not current local residency.

## Runtime and case provenance

The active logs identify OpenFOAM 2606, build
`_481094f-20260618 OPENFOAM=2606`, 32-bit labels, 64-bit scalars, uncollated
I/O, one Windows/WSL host, and exactly four MPI ranks. The executable is
`chtMultiRegionFoam`; the case is
`C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases\new_model_updated_openfoam_export_test`.
Its `provenance/` directory preserves the exact exported model, fan curves,
screening profile, and all 12 referenced component-template instances. The
generated `system/`, `constant/`, scripts, `geometry.txt`, solver logs, and
audit reports remain with the case. This is stronger evidence than the dirty
project HEAD alone; a release baseline still requires hashing and archiving
those generated inputs and the source patch.

The audited exporter source snapshot generates a warm-start disk preflight that estimates the
latest reconstructed plus decomposed checkpoint footprint and requires two
checkpoint equivalents plus 512 MiB of reserve. The exporter regression
compiles and passes with this guard. This does not create disk space, but it
turns an otherwise late partial write into an early actionable refusal.

## Known fidelity limitations

- The coarse grid creates artificial contacts between three neighboring solid
  pairs (227, 188, and 2,322 faces respectively). These are mesh-snap artifacts
  and can bias component-to-component heat transfer.
- Nine heterogeneous components containing 55 defined internal solid regions
  are homogenized by the exporter used for this preserved case. Relative to the source materials,
  this adds about 8.9905 kg and 14,811 J/K of heat capacity and changes thermal
  conductivity. A longer run cannot correct this model-form error.
- The NI chassis lacks the thin rear/side separator walls described by the
  user, so mixing between its two air regions is not yet physically bounded.
- Rail-2 depth and unfinished NI geometry remain uncertain; heat loads are
  approximate; fan curves are subject to later update.
- The 22.5 mm grid is a resource screening mesh. On this measured retained
  1,033,200-cell case, the low-memory reconstruction matched 109/109 selectors
  and 43,603 selected cells without mutating the case.
- Independently, the nominal 19 mm failure inventory had 2,800,980 cells and
  109 64-bit scalar selectors, yielding the theoretical internal-field payload
  `109 * 2,800,980 * 8 = 2,442,454,560 bytes = 2.274713069 GiB`.
  This is not a measured RSS saving and does not prove the residual 19 mm split
  fits memory. That preparation exhausted the then-available WSL memory, so
  mesh independence is not established.

Consequently, the retained results can be used to debug flow topology, fan
orientation, solver stability, and gross ventilation balance. They must not be
used yet for calibrated component temperatures, design sign-off, or safety
margin claims.

## Thermal-only timestep acceleration (2026-08-26)

The apparent `0.0005 s` bottleneck is the live-airflow cap, not the thermal
cap. The active screening configuration previously inherited a 20 s maximum
for the implicit thermal-only stage. A controlled four-rank sensitivity study
branched from byte-identical decomposed fields at 1.600 s, held airflow fixed,
and advanced 600 simulated seconds without an airflow refresh. The reference
used 30 total 20 s steps and 463.527090 solver wall-seconds. The accepted rapid-
screen candidate used 25 total 24 s steps and 360.669262 wall-seconds: a
22.190252% cumulative reduction. On the less startup-sensitive 480 s
confirmation segment alone, the reduction was 13.725195%.
The longer reference segment performed 1,008 enthalpy linear solves with
13,316 total iterations; the 24 s candidate performed 840 solves with 11,949
iterations. Its 16.7% step reduction is partly offset by 7.7% more iterations
per solve, explaining why the sustained wall-time gain is nearer 14% than 22%.

At 601.6 s, the 24 s result differed from the 20 s reference by 0.006781 K RMS
and 0.123260 K maximum over all cells. The largest volume-average component
difference was -0.054939 K in the Keysight N5766A, and the largest peak-
temperature change was -0.123230 K in Meanwell instance 10. Velocity was byte-
equivalent numerically (`0` RMS difference); gauge-adjusted `p_rgh` differed by
0.000833 Pa RMS and 0.007902 Pa maximum. At the earlier 121.6 s checkpoint, the
temperature difference was 0.005412 K RMS and 0.186310 K maximum. Every accepted
branch ended normally. Candidates at 30, 40, and 60 s were rejected after their
120 s screens reached 0.458, 0.887, and 1.671 K maximum cell differences.

The 24 s override is scoped to
`library/models/new_model_updated_openfoam_export_test.toml` and the installed
22.5 mm active launcher. The reusable screening profile stays at 20 s, the
canonical 19 mm model inherits that 20 s value, and all validation profiles are
unchanged. The live-flow cap remains 0.0005 s and the 3% airflow-freeze gate is
unchanged. Therefore this change takes effect only after an airflow checkpoint
is accepted; it does not accelerate the currently unconverged live-flow stage.
The pre-change launcher is preserved as
`performance_benchmarks_1p60/active_case_run_parallel_pre_thermal_dt24_2026-08-26.sh`
with SHA-256 `e47c392ea50572dca29c48681ec7d3457baf871b5d22cdc8d1b229b9ade5d7cd`.
The installed and hardened 24 s launchers are byte-identical with SHA-256
`7d41624a89720edbc42fa52eab0a22495f90d58654b84c68ad9a61e96c7306ac`.
Full methodology, error tables, limitations, and reproduction paths are in
`THERMAL_TIMESTEP_ACCELERATION_2026-08-26.md`.

## Historical continuation resource gate (2026-08-26)

A fresh three-sample host check after the regression run found only
1,839--1,842 MiB physical memory available, with zero pages/s. No Fluent,
OpenFOAM, or MPI solver process was running; the only matching process was the
idle Windows `wslservice` at 16.4 MiB. This is still far below the agreed
five-GiB-for-60-seconds launch gate, so the 1.600--1.700 s continuation was not
started. Root and processor0--3 each retain 74/74 nonempty fields at
1.6000000000000001, and root numeric times remain exactly 0, 1.5, and
1.6000000000000001. The next solve remains the four-rank, 3x2-PIMPLE,
0.0005 s-capped recoverable continuation after the memory gate passes.

## Historical primary executable recovery

The workspace's `model.exe` was a stale build of the obsolete component-only
entry point and reproduced the user's `--geometry-only` filename-parsing
failure. It was rebuilt from `model_runner.cpp` for the source snapshot
recorded here; both `model.exe --geometry-only` and
`model.exe --geometry-only library/models/new_model_updated.toml` completed
successfully for that snapshot. The help banner was also changed to print the
actual invoked executable path instead of hardcoding `model_runner.exe`.
That rebuilt binary is now historical and must not be used as
`[CURRENT_EXPORTER_EXE]`.

## Historical runtime-acceleration controls and source-snapshot checks

The live-airflow cap remains 0.0005 s. The matched 0.00075 s branch was 9.79%
slower and failed velocity, pressure, and 10/33 internal-fan equivalence
checks, so reducing its nominal step count did not reduce elapsed time. The
campaign-specific 24 s thermal-only cap remains confined to this historical
pre-correction case; it cannot affect the current unfinished live-airflow
phase.

The screening profile now exposes a separate frozen-flow performance
candidate: two thermal-only PIMPLE outer energy-coupling passes while every
live stage retains three. Default, in-depth, and validation profiles inherit
the live count. `THERMAL_ONLY_OUTER_CORRECTORS=3` supplies an identical-case
control branch; explicit values below two fail before OpenFOAM launch, locking,
or case writes because one pass omits an additional nonlinear energy-coupling
loop. The generated runner records the selected count, edits the governing
root `system/fvSolution`, restores three before every live stage, and restores
three again on exit. An executable fake-launcher regression caught and fixed
an earlier wrong target of `system/fluid/fvSolution`; the final regression
exercises both rejected overrides and the actual generated exit trap without
starting OpenFOAM.

The final 76,800-cell generated microcase is
`validation/thermal_outer_microcase_2026-08-26`; its runner passes `bash -n`
and has SHA-256
`0f3ca317a4beea4d091ef2b93c804d39c3bf8634527a4449efde46f9cf89a399`.
The pinned historical source-snapshot added-feature harness passed, including the
generated-runner execution guard. Its 295,556-byte verbatim log is
`validation/revised_native_regression_2026-08-26/added_feature_regression_runtime_accel_2026-08-26.stdout.log`
(SHA-256
`933922b1cbfa191a42e95cbe6e20e4439d86575959ba327d9d5f2492f0f36379`).
Optional-dependency and real-WSL integration skips remain explicit; the
artist-level plot tests were run separately with their dependencies and passed
10/10.

The pinned exact O2 isolated-template campaign was rebuilt for those source
changes and passed 11/11 in 102.664 s. Its executable SHA-256 is
`b421dc714036340d0c5575a174ba249803e401ceba37b80a8276f2bb9d517ef9`;
the 57,670-byte log SHA-256 is
`e4b99873a92ce411cb731e48abd622e66ad90e658280696c2c8c1b984ce0518e`.
This is a numerical topology/continuity robustness result, not fan-data or
physical-realism validation: Dell retains a 0.912% bootstrap source-data
overrun, Thruster 30.7723%, and NI still has 42 stalled curved interfaces,
22.7193 m/s maximum speed, overlapping air envelopes, and missing separator
walls.

A 2026-08-26 source-snapshot geometry export (39,808 bytes, SHA-256
`dbcaaa237fb8bf59c4841cf4e3a8b0a226d28208d23560c72900ce113d2ff2ea`)
was rendered into 13 component plots plus one rack plot. The manifest is
`validation/updated_component_plots_current_2026-08-26/PLOT_MANIFEST.json`
(SHA-256
`7a872709ae676bad1dad91bd67fb1c7f34af28fa4abdc6a59ec24e811a2ddb85`).
All ten plotting tests passed, including artist-level proof that every air
region uses the same fill and edge color in both component and rack views.
The temporary plotting runtime was then removed, recovering 123,993,592 bytes.

The historical production `model.exe` was rebuilt after the runtime-control changes:
1,765,157 bytes, SHA-256
`9bed636a7b01b29b45ce1ea1005314215d4b5892d6a19a365f2142f0d2d84294`.
Its geometry-only output exactly matches the retained plot manifest for that
snapshot; it is not a current production exporter binary.

No full OpenFOAM timing branch was launched. Even after Fluent was paused and
cached memory was cleared, three samples showed only 1,809/1,847/1,845 MiB
available with 0.0/31.6/70.5 pages input per second, far below the five-GiB-
for-60-seconds gate. A final 20:26 MDT check was lower still at
1,433/1,410/1,435 MiB available and 16.9/120.8/55.9 pages input per second.
The two-pass thermal setting is therefore implemented and software-tested but
remains unmeasured on corrected full-rack fields. The
matched A/B procedure and promotion limits are in
`RUNTIME_ACCELERATION_POLICY_2026-08-26.md`. Isothermal startup remains blocked
until its mixed-region/mutually-exclusive mode defects and binary provenance
are fixed and a physical-time handoff is validated.
