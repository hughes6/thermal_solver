# Auto-decomposition long-confirmation attempt

## Outcome

The planned 1.610--1.700 s auto-decomposition confirmation was stopped during
its eighth timestep after live observations indicated severe resource
pressure, so its timing was classified as non-representative. This is an
aborted resource diagnostic, not a completed continuation; no solver or
physics-failure signature was observed before the intentional interruption.

The run retained the short-screen-selected four-rank auto-placement candidate,
3x2 PIMPLE controls, and fixed 0.0005 s timestep. Seven steps completed and the
eighth started. The last completed timing line was `ExecutionTime = 236.4 s,
ClockTime = 246 s`; peak maximum Courant was 2.32716 and no true fatal
signature occurred. The process was interrupted intentionally, so the log has
no `End` marker.

## Resource evidence and stop decision

Live Windows performance-counter observations during the run reported only 515--557 MiB of
available physical memory, approximately 16.0 GB committed, and about 303
pages/s. Total CPU utilization was 99.6%. WSL held approximately 3.88 GiB of
private memory. The same transcribed process sample reported zero Fluent
processes, so the observations are consistent with residual host-capacity
pressure rather than Fluent core contention. These values were transcribed
from the live diagnostic calls and are retained, with post-stop observations
and provenance limits, in `decomposition_long_resource_observations.csv`.

Pressure convergence did not deteriorate: the seven complete steps used 306
GAMG iterations, or 43.714/step, versus 43.450/step in the matched auto
benchmark. The nearly unchanged linear work alongside a 62.81% wall-time
increase supports host paging/contention rather than a new pressure-solver
stall as the explanation.

The complete-step clock sequence was 46, 79, 113, 140, 162, 194, and 246 s.
After the first step, the six increments were 33, 34, 27, 22, 32, and 52 s,
averaging 33.333 s/step. That is 62.81% slower than the matched short auto
benchmark's 20.474 s/step, and the latest step had deteriorated to 52 s. A
longer run under those conditions would neither measure the decomposition
fairly nor use the workstation responsibly, so it was stopped.

## Checkpoint integrity

No output time was written beyond the previously verified 1.610 endpoint.
The root and all four processor directories still contain exactly 74 nonempty
files at `1.6100000000000001`; the case lock was released and no `wsl.exe`
launcher remained. The run can restart from that verified short-screen
checkpoint when adequate memory is available.

The intentional interruption occurred before the runner restored its ordinary
post-run controls. The current auto-case `system/controlDict` therefore retains
`startFrom latestTime`, `endTime 1.7`, `maxDeltaT 0.0005`, and
`writeInterval 0.09`; its copied post-abort SHA-256 is
`2C34A1B3712D9E8284F0F77EFD84D6A42013A7B4542FB023F4A2074DF29E3FC6`.
The separately copied retained short-benchmark control hashes to
`38FBA8F6A1F79F28C944DC3B1A920D2E377B72F0D7CEF5716916BD8FEB7A953E`.

The copied raw log is
`decomposition_auto_long_aborted_memory_pressure.stdout.log`, SHA-256
`BEBFD1E780D77C3952A74EB7E09FCCBBD2C1CAEAE2B46341B471E07452AF7428`.
