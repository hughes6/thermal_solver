# Revised OpenFOAM 19 mm preparation failure

The canonical revised geometry exported 2,800,980 cells and passed exact heat
allocation (47 zones, 2,315 W). During `prepare_regions.sh`,
`splitMeshRegions` reached approximately 3.49 GB resident memory plus system
overhead. WSL had only about 48 MiB available with 528 MiB swap used, and the
kernel killed the process:

`Out of memory: Killed process ... splitMeshRegion ... anon-rss:3490724kB`

This case cannot be prepared, much less run on four ranks, under the current
3.775 GiB WSL allocation. The console log is preserved beside this report. A
separate 22.5 mm resource-screening export is used for continued solver testing;
the canonical 19 mm configuration remains the higher-fidelity target.
