# Run 001 stopped at the native topology gate

This directory is intentionally non-empty so
`simulation.native_overwrite=false` prevents accidental reuse.

- Mesh: uniform 20 mm, 146,850 cells.
- Result: rejected before flow or transient integration.
- Cause: the Dell rear exhaust fan had no fluid cell immediately upstream on
  the realized grid.
- Raw log: `../run_001.stdout.log`.

No geometry, state CSV, structured log, or last-run metadata was published for
this rejected attempt.
