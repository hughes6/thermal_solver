# Git state before heterogeneous-material fix

Recorded: 2026-08-31 (local v2.4 workspace)

- Branch: `v2.4`
- HEAD: `541e6297263c0fbb79400ea28c54d62b8a705657`
- Subject: `Skip compatible semi-frozen solver rebuilds`
- Working tree before this snapshot: clean

The next implementation work is limited to correcting OpenFOAM export of
component-specific material properties, which are currently observed to fall
back to the case material.
