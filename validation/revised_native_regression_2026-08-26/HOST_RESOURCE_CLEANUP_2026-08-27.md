# Host resource cleanup record

Date: 2026-08-27 (America/Denver)

## Outcome

The idle WSL instance was shut down after a process audit found no Fluent,
ANSYS, OpenFOAM, or MPI solver.  `vmmemWSL` had been using 1,747.1 MiB of
working set and 2,067.5 MiB of private memory.  The shutdown removed that
process, but the subsequent Windows sample still had only 2,394 MiB available,
below the 5,120 MiB OpenFOAM launch gate.

Six abandoned Visual Studio installer extraction directories were then removed
from the user's Windows temporary directory.  Before deletion, each path was
resolved beneath `C:\Users\hconn\AppData\Local\Temp`, verified to be a
directory, inventoried by exact logical byte count, and gated on the absence of
Visual Studio installer, setup, MSI, IDE, and ServiceHub processes.

| Exact deleted directory | Logical bytes |
|---|---:|
| `cw5eyj1v` | 4,169,465,663 |
| `gggckm0l` | 3,519,198,862 |
| `f4edk5lh.rgh` | 131,676,180 |
| `q4igiaoz.fk0` | 131,676,180 |
| `q4qronup.rei` | 131,676,180 |
| `z44qqihh.z0m` | 131,676,180 |
| **Total** | **8,215,369,245** |

All six exact paths were absent after deletion.  C: free space increased from
approximately 8,757.3 MiB to 16,600.8 MiB.  These were permanent temp-tree
deletions and are not recoverable from this project.

The active `DiagOutputDir` tree was not deleted because its write timestamp was
current during the audit.  Codex index directories, ANSYS temp state, archive
helper state, the project tree, the WSL virtual disk, and all OpenFOAM cases,
checkpoints, configuration, logs, post-processing, and validation evidence were
left untouched.

## Post-cleanup gate

A later instantaneous sample reported 1,464 MiB available physical memory,
594.9 pages input per second, 16,600.7 MiB free on C:, and no `vmmemWSL`.
Disk headroom is materially improved, but memory and paging still prohibit a
build, export, mesh preparation, or solve.  No heavy CFD command was launched.
