#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "$0")" && pwd)"
foam_launcher="${OPENFOAM_LAUNCHER:-openfoam2606}"

if [[ -f "$case_dir/.openfoam_regions_prepared" ]]; then
    echo "Region meshes already prepared; reusing existing topology."
    exit 0
fi

"$foam_launcher" splitMeshRegions -case "$case_dir" -cellZonesOnly -overwrite

"$foam_launcher" topoSet -case "$case_dir" -region fluid -latestTime -dict "$case_dir/system/topoSetDict_fluid_interfaces"
"$foam_launcher" topoSet -case "$case_dir" -region Eaton_SU3000RTXLCD2UTAA_UPS_0 -time 0 -dict "$case_dir/system/topoSetDict_Battery_pack_0"
"$foam_launcher" topoSet -case "$case_dir" -region Eaton_SU3000RTXLCD2UTAA_UPS_0 -time 0 -dict "$case_dir/system/topoSetDict_block_1"
"$foam_launcher" topoSet -case "$case_dir" -region Eaton_SU3000RTXLCD2UTAA_UPS_0 -time 0 -dict "$case_dir/system/topoSetDict_block2_2"
"$foam_launcher" topoSet -case "$case_dir" -region Dell_PowerEdge_R470_1U_1 -time 0 -dict "$case_dir/system/topoSetDict_CPU_and_memory_zone_3"
"$foam_launcher" topoSet -case "$case_dir" -region Dell_PowerEdge_R470_1U_1 -time 0 -dict "$case_dir/system/topoSetDict_Storage_and_front_backplane_4"
"$foam_launcher" topoSet -case "$case_dir" -region Dell_PowerEdge_R470_1U_1 -time 0 -dict "$case_dir/system/topoSetDict_PCIe_and_system_board_rear_zone_5"
"$foam_launcher" topoSet -case "$case_dir" -region Dell_PowerEdge_R470_1U_1 -time 0 -dict "$case_dir/system/topoSetDict_Power_supply_zone_6"
"$foam_launcher" topoSet -case "$case_dir" -region Trenton_3U_BAM_2 -time 0 -dict "$case_dir/system/topoSetDict_CPU_and_motherboard_7"
"$foam_launcher" topoSet -case "$case_dir" -region Trenton_3U_BAM_2 -time 0 -dict "$case_dir/system/topoSetDict_Expansion_card_bank_8"
"$foam_launcher" topoSet -case "$case_dir" -region Trenton_3U_BAM_2 -time 0 -dict "$case_dir/system/topoSetDict_Storage_zone_9"
"$foam_launcher" topoSet -case "$case_dir" -region Trenton_3U_BAM_2 -time 0 -dict "$case_dir/system/topoSetDict_Power_supply_10"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Front_left_intake_0"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Front_intake_1"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Rear_exhaust_fan_2"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Front_intake_3"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Rear_exhaust_4"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Cooling_fan_1_5"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Cooling_fan_2_6"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Cooling_fan_3_7"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Cooling_fan_4_8"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Cooling_fan_5_9"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Cooling_fan_6_10"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Front_intake_11"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_internal_Rear_exhaust_12"
"$foam_launcher" topoSet -case "$case_dir" -region fluid -time 0 -dict "$case_dir/system/topoSetDict_external_Vent_main_intake_9"

"$foam_launcher" checkMesh -case "$case_dir" -allRegions -allGeometry -allTopology

touch "$case_dir/.openfoam_regions_prepared"
echo "Region meshes prepared successfully."
