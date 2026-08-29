# Revised 22.5 mm case provenance

This file identifies the immutable input snapshots embedded in the active
OpenFOAM case. It supplements the project revision because the validation
worktree remains dirty during the campaign.

Case:
`C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases\new_model_updated_openfoam_export_test`

The case's `provenance/manifest.txt` maps each snapshot to its original source
path. The snapshot files, rather than the later working copies, are the
authority for this mesh and solved field lineage.

| Snapshot | SHA-256 |
|---|---|
| `model.toml` | `88f1d230efa6a2fb7d24c2f1a906a22c6f7f77e6a61ae6cd118b6095ed0d50cb` |
| `fan_curves.toml` | `2a64aba798205a9eff061ba1e9edd2d62b94baeafe740ee16b636f333fd483d4` |
| `openfoam_profile.toml` | `3125b3cd74e7d82d6bcf8127ee4f7b9a0daa7bb82fa18a6c07365dc0def9ad8b` |
| `component_0_updated_eaton_UPS.toml` | `a359589388dc2976ac690d50c61c0cf4b06491ab76dd74bd275c85755ade6b55` |
| `component_1_updated_DELL_R470.toml` | `f82f56ffa6ba3104fba8e0afcdc829dbcbc4e9300ca1ee3f1182c7a20cb88b48` |
| `component_2_updated_Keysight_N5766A.toml` | `ada3afb4a9b53b86fb8a8b8932127b190475a359c46010550ebe0522876f5610` |
| `component_3_updated_keysight_N6701C.toml` | `e318a606d0ce79a832348c0d474b758601cd77b45e44225908b86c535b1f0c76` |
| `component_4_updated_trenton_3u_bam.toml` | `1a3f546421662427e6886ac55005a1ed8cbf2691361346b7a1f6a51222064de1` |
| `component_5_eaton_KVM.toml` | `9e29094ac8dc87481ad76502a2ccd5c1cd5b8ab421f978589f0ab10274e473f0` |
| `component_6_updated_Thruster_Load_Box.toml` | `7ef504f08de35bcf3d0a3922734953aa1bd872983ac311165679589d89cca091` |
| `component_7_updated_cisco_catalyst_9300_24.toml` | `f162881a9cff0f5f117f5d830d4283192701bdf095e6a8e1ed375e88b8c0338a` |
| `component_8_Eaton_PDU_PDUMNH30.toml` | `f674e74a57246566e94175cffa08da462e6206225d32fd6cdadfd5d335f28b3b` |
| `component_9_Fan_control_kit_PS.toml` | `70cc1cbaa910041654550563ec13d2adb077d9bd8bc2c5307d80edec8ff35e04` |
| `component_10_Fan_control_kit_PS.toml` | `70cc1cbaa910041654550563ec13d2adb077d9bd8bc2c5307d80edec8ff35e04` |
| `component_11_updated_NI_PXIe_Chassis.toml` | `77e9fd89af89543aa338ea89ac7b764bea30b3f6d83d95dbc8ea0816ba3457e6` |
| `manifest.txt` | `f1647c8729a9b1670111673c8012c7e1c8bd0dd0b88595e10d0e8334acdadb7b` |

The exported `geometry.txt` has SHA-256
`5a1893f92d162cfe17d66240d34770351302a2eb961a332be9b2092d1a9981f1`
and is byte-identical to the current canonical `output.txt` and the preserved
component-plot input.

Two source files changed after export without changing solved physics:

- the working fixture added rail-depth uncertainty comments and corrected only
  the filename case of the Eaton PDU path; and
- the working NI template added comments documenting its missing separator
  walls and connected-air limitation.

All other mapped source inputs remain byte-identical to their case snapshots.
The current working fixture hash therefore must not be substituted for the
case-snapshot model hash above. Solver-version, invocation, checkpoint-log,
and generated-dictionary hashes are recorded separately with the run evidence.
