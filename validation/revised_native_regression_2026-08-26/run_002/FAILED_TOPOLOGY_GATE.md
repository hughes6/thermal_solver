# Run 002 stopped at the native topology gate

This directory is intentionally non-empty so
`simulation.native_overwrite=false` prevents accidental reuse.

- Mesh: internally aligned adaptive 50/200 mm, 204,768 cells.
- Result: rejected before flow or transient integration.
- Cause: all 40 realized cells in the Keysight N5766A `Power block` bounds
  were fluid after fan/vent stamping, leaving no solid volume on which to
  conserve its 120 W source.
- Raw log: `../run_002.stdout.log`.

No geometry, state CSV, structured log, or last-run metadata was published for
this rejected attempt.
