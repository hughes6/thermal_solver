# OpenFOAM heat-source audit

Each PASS confirms a non-empty, internally unique heat cell set; no overlap with another heat source in the same solver region; and an active absolute-watt OpenFOAM source matching exported metadata.

Total: 47 sources, 2315 W. Solid regions: 47 sources, 2315 W. Fluid region: 0 sources, 0 W.

| Case | Source | Component | Cells | Volume (m^3) | Watts | Status |
|---|---|---|---:|---:|---:|---|
| new_model_updated_openfoam_export_test | Battery_pack_0 | Eaton_SU3000RTXLCD2UTAA_UPS_0 | 3276 | 0.0036 | 40 | PASS |
| new_model_updated_openfoam_export_test | block_1 | Eaton_SU3000RTXLCD2UTAA_UPS_0 | 598 | 0.000878361 | 40 | PASS |
| new_model_updated_openfoam_export_test | block2_2 | Eaton_SU3000RTXLCD2UTAA_UPS_0 | 276 | 0.00040731438 | 40 | PASS |
| new_model_updated_openfoam_export_test | CPU_and_memory_zone_3 | Dell_PowerEdge_R470_1U_1 | 3519 | 0.00109824 | 600 | PASS |
| new_model_updated_openfoam_export_test | Storage_and_front_backplane_4 | Dell_PowerEdge_R470_1U_1 | 3950 | 0.00185705262 | 120 | PASS |
| new_model_updated_openfoam_export_test | PCIe_and_system_board_rear_zone_5 | Dell_PowerEdge_R470_1U_1 | 4400 | 0.0017054135 | 180 | PASS |
| new_model_updated_openfoam_export_test | Power_supply_zone_6 | Dell_PowerEdge_R470_1U_1 | 600 | 0.000340026348 | 100 | PASS |
| new_model_updated_openfoam_export_test | Power_block_7 | Keysight_N5766A_PS_2 | 76 | 3.18568741e-05 | 120 | PASS |
| new_model_updated_openfoam_export_test | Power_Supply_block_8 | Keysight_N5766A_PS_2 | 240 | 0.000123730786 | 30 | PASS |
| new_model_updated_openfoam_export_test | Module_1_9 | Keysight_N5766A_PS_2 | 256 | 0.000113095855 | 30 | PASS |
| new_model_updated_openfoam_export_test | Module_2_10 | Keysight_N5766A_PS_2 | 224 | 0.000115300526 | 30 | PASS |
| new_model_updated_openfoam_export_test | Module_3_11 | Keysight_N5766A_PS_2 | 224 | 0.000107147371 | 30 | PASS |
| new_model_updated_openfoam_export_test | Module_4_12 | Keysight_N5766A_PS_2 | 176 | 9.96082828e-05 | 30 | PASS |
| new_model_updated_openfoam_export_test | Power_block_13 | Keysight_N6701C_PS_3 | 152 | 3.5046145e-05 | 120 | PASS |
| new_model_updated_openfoam_export_test | Power_Supply_block_14 | Keysight_N6701C_PS_3 | 672 | 0.000140863048 | 30 | PASS |
| new_model_updated_openfoam_export_test | Module_1_15 | Keysight_N6701C_PS_3 | 630 | 0.000116281526 | 30 | PASS |
| new_model_updated_openfoam_export_test | Module_2_16 | Keysight_N6701C_PS_3 | 588 | 0.000123686775 | 30 | PASS |
| new_model_updated_openfoam_export_test | Module_3_17 | Keysight_N6701C_PS_3 | 630 | 0.000121135725 | 30 | PASS |
| new_model_updated_openfoam_export_test | Module_4_18 | Keysight_N6701C_PS_3 | 462 | 0.000106853175 | 30 | PASS |
| new_model_updated_openfoam_export_test | CPU_and_motherboard_19 | Trenton_3U_BAM_4 | 1925 | 0.0010300584 | 10 | PASS |
| new_model_updated_openfoam_export_test | Bottom_front_block_20 | Trenton_3U_BAM_4 | 2574 | 0.00174466427 | 10 | PASS |
| new_model_updated_openfoam_export_test | Top_front_block_21 | Trenton_3U_BAM_4 | 2574 | 0.00199432917 | 10 | PASS |
| new_model_updated_openfoam_export_test | PS_Heat_Block_22 | Trenton_3U_BAM_4 | 64 | 5.653384e-05 | 10 | PASS |
| new_model_updated_openfoam_export_test | slot_1_card_23 | Trenton_3U_BAM_4 | 98 | 4.9421064e-05 | 30 | PASS |
| new_model_updated_openfoam_export_test | slot_2_card_24 | Trenton_3U_BAM_4 | 196 | 8.2908e-05 | 30 | PASS |
| new_model_updated_openfoam_export_test | slot_3_card_25 | Trenton_3U_BAM_4 | 98 | 4.7047e-05 | 30 | PASS |
| new_model_updated_openfoam_export_test | slot_4_card_26 | Trenton_3U_BAM_4 | 98 | 6.0409664e-05 | 30 | PASS |
| new_model_updated_openfoam_export_test | slot_5_card_27 | Trenton_3U_BAM_4 | 98 | 4.03354e-05 | 30 | PASS |
| new_model_updated_openfoam_export_test | slot_6_card_28 | Trenton_3U_BAM_4 | 98 | 5.1616152e-05 | 30 | PASS |
| new_model_updated_openfoam_export_test | slot_7_card_29 | Trenton_3U_BAM_4 | 196 | 8.3540338e-05 | 30 | PASS |
| new_model_updated_openfoam_export_test | slot_8_card_30 | Trenton_3U_BAM_4 | 98 | 5.899628e-05 | 30 | PASS |
| new_model_updated_openfoam_export_test | slot_9_card_31 | Trenton_3U_BAM_4 | 98 | 5.6929831e-05 | 30 | PASS |
| new_model_updated_openfoam_export_test | slot_10_card_32 | Trenton_3U_BAM_4 | 98 | 0.000152199348 | 30 | PASS |
| new_model_updated_openfoam_export_test | slot_11_card_33 | Trenton_3U_BAM_4 | 98 | 5.078773e-05 | 30 | PASS |
| new_model_updated_openfoam_export_test | Front_region_34 | Tripp_Lite_B020_U08_19_IP_KVM_5 | 1560 | 0.0018137637 | 10 | PASS |
| new_model_updated_openfoam_export_test | Back_region_35 | Tripp_Lite_B020_U08_19_IP_KVM_5 | 1920 | 0.0018137637 | 10 | PASS |
| new_model_updated_openfoam_export_test | Resistors_36 | Thruster_Load_Box_2U_7 | 1008 | 0.000949883 | 10 | PASS |
| new_model_updated_openfoam_export_test | Heat_region_1_37 | Cisco_9300_Network_Switch_8 | 528 | 0.000506118375 | 50 | PASS |
| new_model_updated_openfoam_export_test | Heat_region_2_38 | Cisco_9300_Network_Switch_8 | 528 | 0.000520694584 | 50 | PASS |
| new_model_updated_openfoam_export_test | Power_supply_zone_39 | Cisco_9300_Network_Switch_8 | 1008 | 0.00101187573 | 50 | PASS |
| new_model_updated_openfoam_export_test | Main_block_40 | Eaton_PDUMNH30_PDU_9 | 1416 | 0.00130365 | 10 | PASS |
| new_model_updated_openfoam_export_test | Main_block_41 | Meanwell_EDR_120_24_10 | 48 | 3.700176e-05 | 15 | PASS |
| new_model_updated_openfoam_export_test | Main_block_42 | Meanwell_EDR_120_24_11 | 48 | 4.58870924e-05 | 15 | PASS |
| new_model_updated_openfoam_export_test | Power_Supply_43 | NI_PXIe_784782_01_Chassis_12 | 90 | 0.000126457622 | 20 | PASS |
| new_model_updated_openfoam_export_test | Connectors_44 | NI_PXIe_784782_01_Chassis_12 | 232 | 0.00017325 | 5 | PASS |
| new_model_updated_openfoam_export_test | Card_1_45 | NI_PXIe_784782_01_Chassis_12 | 90 | 5.56421376e-05 | 20 | PASS |
| new_model_updated_openfoam_export_test | Card_2_46 | NI_PXIe_784782_01_Chassis_12 | 90 | 5.66901048e-05 | 20 | PASS |
