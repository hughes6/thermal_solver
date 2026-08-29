# 22.5 mm screening mesh-quality audit

The authoritative source is the active case's `checkMesh.prepare.log`
(SHA-256
`da90f41547ad731dedb15c3c11e895934e96ecfebfde48120bf5fa506d895e4b`).
It records `checkMesh -allRegions -allGeometry -allTopology` results for the
1,033,200-cell case.

The earlier statement that the determinant warning affected “13 coarse solid
meshes” was incorrect. Thirteen of the fourteen regions fail the
`cellDeterminant < 0.001` check: the fluid region and twelve solid regions.
The affected-cell split is 5,082 fluid cells and 34,796 solid cells, totaling
39,878. Only the storage-shelf solid reports `Mesh OK`.

| Region | Cells | Cells below determinant 0.001 | Fraction | checkMesh result |
|---|---:|---:|---:|---|
| fluid | 925,388 | 5,082 | 0.5492% | Failed 1 mesh check |
| Eaton UPS | 13,206 | 8,720 | 66.0306% | Failed 1 mesh check |
| Dell R470 | 25,640 | 7,519 | 29.3253% | Failed 1 mesh check |
| Keysight N5766A | 3,164 | 2,642 | 83.5019% | Failed 1 mesh check |
| Keysight N6701C | 8,298 | 785 | 9.4601% | Failed 1 mesh check |
| Trenton 3U | 13,383 | 2,014 | 15.0489% | Failed 1 mesh check |
| Eaton KVM | 8,784 | 4,836 | 55.0546% | Failed 1 mesh check |
| 3U storage shelf | 13,932 | 0 | 0% | Mesh OK |
| Thruster load box | 6,903 | 105 | 1.5211% | Failed 1 mesh check |
| Cisco switch | 5,204 | 3,086 | 59.3005% | Failed 1 mesh check |
| Eaton PDU | 4,972 | 2,304 | 46.3395% | Failed 1 mesh check |
| Meanwell supply 1 | 240 | 192 | 80.0000% | Failed 1 mesh check |
| Meanwell supply 2 | 336 | 96 | 28.5714% | Failed 1 mesh check |
| NI PXIe chassis | 3,750 | 2,497 | 66.5867% | Failed 1 mesh check |
| **Solid subtotal** | **107,812** | **34,796** | **32.2747%** | **12 failed regions; 1 passing region** |
| **Total** | **1,033,200** | **39,878** | **3.8597%** | **13 failed regions** |

The log reports acceptable volumes, face areas, non-orthogonality, skewness,
openness, face pyramids, interpolation weights, and face-volume ratios. The
fluid is one connected region. Those passes do not cancel the determinant
failure: the minimum fluid determinant is zero, and `checkMesh` explicitly
writes 5,082 fluid cells to `cellDeterminant` before reporting a failed mesh
check.

## Interpretation and release gate

The mesh remains useful for resource-bounded software, topology, restart, and
qualitative-flow screening. It is not a mesh-quality pass and cannot support
an industry-ready validation claim. Treating the determinant result as an
accepted production warning would hide both the fluid failure and very large
failed fractions in several solids.

Future exported cases fail closed on this condition. The low-level exporter
and default, in-depth, and validation profiles set
`allow_determinant_warnings = false`; only the explicitly exploratory
screening profile sets it true. Even screening proceeds only when the total
failed-check count exactly matches determinant diagnostics and no other
`checkMesh` diagnostic exists. The preserved active case was not regenerated,
so this document and its original `checkMesh.prepare.log` remain its release
gate.

Before quantitative sign-off:

1. locate and explain the failed-cell sets, especially the 5,082 fluid cells;
2. change the mesh/refinement or geometry treatment until the production
   acceptance criterion is met, rather than suppressing the check;
3. prove all thin and zero-watt structural regions survive mesh realization;
4. repeat mass balance, fan-domain, field-convergence, y+, and energy audits;
   and
5. establish mesh sensitivity against at least one independently refined
   case that fits the available hardware.

No field was changed by this audit.
