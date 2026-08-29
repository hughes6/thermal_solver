# Historical post-N5766 geometry export (superseded)

Date: 2026-08-26

> This 39,609-byte snapshot predates the final Trenton opening repair. The
> current geometry authority is
> `../updated_component_plots_current_2026-08-26/geometry_input.txt`, which is
> 39,808 bytes with SHA-256 `dbcaaa237fb8bf59c4841cf4e3a8b0a226d28208d23560c72900ce113d2ff2ea`
> and exactly matches the rebuilt root `output.txt`. Its manifest covers 13
> component PNGs plus the rack PNG and the current 10/10 artist regression.
> The hashes below remain valid lineage for this older snapshot.

`output.txt` is a geometry-only export of
`library/models/new_model_updated.toml` after correcting the Keysight N5766A
front intake to the device cavity span (`z = 21.8 mm`, `height = 33.6 mm`).
No native transient, OpenFOAM export, or solver was run to create it.

- `output.txt`: 39,609 bytes; SHA-256
  `4e3c4a4b96b12929ab141b7975aabb08db9b4d84d86a5d04e3f42db35f25d676`.
- Canonical model: 9,539 bytes; SHA-256
  `de1b43b8db0ac732718629495d6b528be7617f911855b1b69768495554683695`.
- Corrected N5766A component: 8,971 bytes; SHA-256
  `17ab6fa85ca1619b555ed55b824a9dc242bc4af8ec572350e2a174d50dcbaa1d`.

Against the preserved repository-root pre-correction `output.txt`, the only
geometry-report changes are N5766A `Front intake` height
`0.03445 -> 0.0336 m`, local z `0.022225 -> 0.0218 m`, and corresponding
global z `0.288925 -> 0.2885 m`.

At the time this snapshot was created, the plotter source assigned every
component air region `tab:cyan` and its parser/color tests passed, but no new
PNG set was rendered in that runtime. The later current plot set identified in
the supersession note above completes that render-and-hash step; the 2026-08-25
PNG set and this text export remain historical lineage.
