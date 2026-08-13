#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "openfoam_exporter.hpp"

int main(int argc, char** argv) {
    Environment env(30.0,0.0,20.0,1005.0,0.02587,
                    0.000018,0.71,1.225);
    Workload load(10000,1000000,100000,100);

    // Decimal coordinates that are exact grid cuts mathematically can fall
    // fractionally below the cut under direct floor(x/dx). OpenFOAM stamping
    // must use the same snapped boundary lookup for material cells and export
    // metadata, otherwise one complete layer disappears.
    {
        Rack aligned_rack=Rack::from_meters(0.5,1.0,0.3);
        Mesh aligned_mesh=Mesh().build_mesh(
            aligned_rack,0.0125,0.0125,0.0125,env,load);
        Component aligned_component=Component::from_meters(
            0.2,0.2,0.05,"decimal aligned block");
        aligned_component.set_coords_m(0.15,0.2,0.0);
        aligned_component.set_rho_solid(2700.0);
        aligned_component.set_cp(900.0);
        aligned_component.set_k_solid(205.0);
        aligned_component.set_watts(50.0);
        aligned_mesh.stamp_component_for_openfoam(aligned_component);

        std::size_t solid_cells=0;
        double solid_volume=0.0;
        for(std::size_t cell=0;
            cell<aligned_mesh.get_openfoam_cell_metadata().size();++cell) {
            if(aligned_mesh.get_openfoam_cell_metadata()[cell].region_type !=
               Mesh::OpenFoamCellMetadata::RegionType::Solid)
                continue;
            ++solid_cells;
            solid_volume+=aligned_mesh.get_cells()[cell].volume();
        }
        assert(solid_cells==1024);
        assert(std::abs(solid_volume-0.002)<1e-12);
    }

    Rack rack=Rack::from_meters(0.5,0.2,0.2);
    rack.set_t(20.0);
    rack.set_cp(1005.0);
    rack.set_k(0.02587);
    rack.set_rho(1.225);
    Mesh mesh=Mesh().build_mesh(rack,0.1,0.1,0.1,env,load);

    Component component =
        Component::from_meters(0.1,0.1,0.1,"test heater");
    component.set_coords_m(0.1,0.0,0.0);
    component.set_t(30.0);
    component.set_rho_solid(2700.0);
    component.set_cp(900.0);
    component.set_k_solid(150.0);
    component.set_watts(0.0);
    InternalRegion heat_source(
        "test_heat_source",{0.1,0.1,0.1},{0.0,0.0,0.0},
        900.0,2700.0,150.0,10.0);
    component.add_region(heat_source);
    component.order_internal_regions();
    mesh.stamp_component_for_openfoam(component);

    Component homogeneous =
        Component::from_meters(0.1,0.1,0.1,"homogeneous heater");
    homogeneous.set_coords_m(0.2,0.0,0.0);
    homogeneous.set_t(20.0);
    homogeneous.set_rho_solid(2700.0);
    homogeneous.set_cp(900.0);
    homogeneous.set_k_solid(150.0);
    homogeneous.set_watts(7.0);
    mesh.stamp_component_for_openfoam(homogeneous);

    Component air_heater =
        Component::from_meters(0.2,0.1,0.1,"air-side heater");
    air_heater.set_coords_m(0.3,0.0,0.0);
    air_heater.set_t(20.0);
    air_heater.set_rho_solid(1200.0);
    air_heater.set_cp(800.0);
    air_heater.set_k_solid(10.0);
    air_heater.set_watts(0.0);
    InternalRegion heated_air(
        "heated internal air",{0.1,0.1,0.1},{0.0,0.0,0.0});
    heated_air.set_watts(5.0);
    air_heater.add_region(heated_air);
    InternalRegion zero_watt_block(
        "zero watt geometry",{0.1,0.1,0.1},{0.1,0.0,0.0},
        800.0,1200.0,10.0,0.0);
    air_heater.add_region(zero_watt_block);
    air_heater.order_internal_regions();
    mesh.stamp_component_for_openfoam(air_heater);

    Fan inlet(
        "test_inlet",10.0,0.0,{0.1,0.0,0.1},
        {0.05,0.0,0.05},{0.0,1.0,0.0},
        FlowType::Intake,ShapeType::Rectangular);
    Vent outlet(
        "test_outlet",{0.1,0.0,0.1},1.0,0.0,0.65,
        {0.25,0.2,0.15},{0.0,1.0,0.0},
        VentShapeType::Rectangular);
    mesh.stamp_fan_for_openfoam(inlet);
    mesh.stamp_vent_for_openfoam(outlet);

    assert(mesh.has_openfoam_export_metadata());
    assert(mesh.get_openfoam_component_regions().size()==3);
    assert(mesh.get_openfoam_heat_source_regions().size()==3);
    assert(mesh.get_openfoam_heat_source_regions()[1].watts==7.0);
    assert(mesh.get_openfoam_heat_source_regions()[2].watts==5.0);
    assert(mesh.get_openfoam_heat_source_regions()[2].fluid);
    // A zero-watt solid remains stamped geometry, but must not create a
    // misleading active OpenFOAM source, mask, set, or fvOptions entry.
    for(const auto& source : mesh.get_openfoam_heat_source_regions())
        assert(source.name!="zero watt geometry");
    assert(mesh.get_openfoam_cell_metadata()[mesh.idx(3,0,0)]
               .heat_source_id==2);
    assert(!mesh.get_cells()[mesh.idx(3,0,0)].is_solid());
    assert(std::abs(mesh.get_cells()[mesh.idx(3,0,0)].get_qdot()*
                    mesh.get_cells()[mesh.idx(3,0,0)].volume()-5.0)<1e-12);
    assert(mesh.get_openfoam_cell_metadata()[mesh.idx(2,0,0)]
               .heat_source_id==1);
    assert(mesh.get_openfoam_cell_metadata()[mesh.idx(1,0,0)]
               .region_type ==
           Mesh::OpenFoamCellMetadata::RegionType::Solid);
    assert(mesh.get_openfoam_cell_metadata()[mesh.idx(0,0,0)]
               .region_type ==
           Mesh::OpenFoamCellMetadata::RegionType::Fluid);

    const bool keep_case = argc > 1;
    const std::filesystem::path case_path =
        keep_case
            ? std::filesystem::path(argv[1])
            : std::filesystem::temp_directory_path() /
                "thermal_solver_openfoam_export_test";
    OpenFoamExporter::export_mesh(
        mesh,
        {.case_directory=case_path,
         .overwrite=true,
         .parallel_processes=2,
         .end_time=12.5,
         .initial_time_step=0.005,
         .maximum_time_step=0.25,
         .maximum_courant_number=0.4,
         .field_write_interval=2.5,
         .report_interval=0.5,
         .use_k_omega_sst=true,
         .inlet_turbulence_intensity=0.05,
         .turbulence_length_scale=0.01,
         .turbulent_prandtl_number=0.85,
         .pimple_outer_correctors=3,
         .pimple_pressure_correctors=2,
         .use_multirate_thermal=true,
         .minimum_initial_air_exchange_fraction=1.0,
         .airflow_refresh_maximum_time_step=0.005,
         .airflow_checkpoint_interval=0.1,
         .airflow_refresh_duration=0.1,
         .stop_when_thermally_converged=true});

    for(const char* file :
        {"points","faces","owner","neighbour","boundary","cellZones"})
        assert(std::filesystem::is_regular_file(
            case_path/"constant"/"polyMesh"/file));
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"controlDict"));
    assert(std::filesystem::is_regular_file(
        case_path/"internal_airflow_devices.csv"));
    {
        std::ifstream metadata(case_path/"internal_airflow_devices.csv");
        std::ostringstream text;
        text << metadata.rdbuf();
        assert(text.str().find(
            "zone,component_id,component,kind,device") != std::string::npos);
        assert(text.str().find("expected_direction_x,expected_direction_y,") !=
               std::string::npos);
    }
    std::ifstream control_file(case_path/"system"/"controlDict");
    std::ostringstream control_text;
    control_text << control_file.rdbuf();
    control_file.close();
    assert(control_text.str().find("endTime         12.5;") !=
           std::string::npos);
    assert(control_text.str().find("deltaT          0.005") !=
           std::string::npos);
    assert(control_text.str().find("maxDeltaT       0.25;") !=
           std::string::npos);
    assert(control_text.str().find("writeControl    adjustableRunTime;") !=
           std::string::npos);
    assert(control_text.str().find("type yPlus;") != std::string::npos);
    const auto y_plus_position=control_text.str().find("type yPlus;");
    const auto y_plus_end=control_text.str().find("    }", y_plus_position);
    assert(control_text.str().substr(
        y_plus_position, y_plus_end-y_plus_position).find(
            "writeControl writeTime;") != std::string::npos);
    assert(control_text.str().find("fluid_temperature_average") !=
           std::string::npos);
    assert(control_text.str().find(
        "test_outlet_mass_weighted_temperature") != std::string::npos);
    assert(control_text.str().find(
        "operation weightedAverage;") != std::string::npos);
    assert(control_text.str().find("weightField phi;") != std::string::npos);
    assert(std::filesystem::is_regular_file(
        case_path/"constant"/"polyMesh"/"sets"/"test_heat_source_0"));
    assert(std::filesystem::is_regular_file(
        case_path/"constant"/"polyMesh"/"sets"/
            "homogeneous_heater_load_1"));
    assert(std::filesystem::is_regular_file(
        case_path/"constant"/"openfoamExportProperties"));
    {
        std::ifstream properties_file(
            case_path/"constant"/"openfoamExportProperties");
        std::ostringstream properties_text;
        properties_text << properties_file.rdbuf();
        assert(properties_text.str().find(
            "expectedConnectedFluidRegions 1;") != std::string::npos);
    }
    assert(std::filesystem::is_regular_file(
        case_path/"0"/"heatSourceMask_0"));
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"topoSetDict_test_heat_source_0"));
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"topoSetDict_heated_internal_air_2"));
    assert(std::filesystem::is_regular_file(
        case_path/"prepare_regions.sh"));
    std::ifstream preparation_file(case_path/"prepare_regions.sh");
    std::ostringstream preparation_text;
    preparation_text << preparation_file.rdbuf();
    preparation_file.close();
    const std::string preparation_script=preparation_text.str();
    const std::size_t failure_check=preparation_script.find(
        "failed_checks=$(awk");
    const std::size_t success_marker=preparation_script.find(
        "touch \"$case_dir/.openfoam_regions_prepared\"");
    assert(failure_check != std::string::npos);
    assert(success_marker != std::string::npos);
    assert(failure_check < success_marker);
    assert(preparation_script.find("unexpected_diagnostics") !=
           std::string::npos);
    assert(preparation_script.find(
        ".openfoam_mesh_determinant_warning") != std::string::npos);
    assert(preparation_script.find(
        "failed_checks > determinant_failures") != std::string::npos);
    assert(preparation_script.find("exit 1") != std::string::npos);
    assert(std::filesystem::is_regular_file(case_path/"run_cht.sh"));
    assert(std::filesystem::is_regular_file(case_path/"run_parallel.sh"));
    {
        std::ifstream stream(case_path/"run_parallel.sh");
        std::ostringstream text;
        text << stream.rdbuf();
        assert(text.str().find(
            "semiFrozenChtMultiRegionFoam -case \"$case_dir\" -parallel "
            "-postProcess -latestTime") != std::string::npos);
        assert(text.str().find("is_restartable_processor_time()") !=
               std::string::npos);
        assert(text.str().find(
            "[[ -f \"$root/U\" && -f \"$root/T\" ]]") !=
               std::string::npos);
        assert(text.str().find(
            "actual_time=$(latest_processor_restart_time)") !=
               std::string::npos);
    }
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"decomposeParDict"));
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"fluid"/"decomposeParDict"));
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"test_heater_0"/"decomposeParDict"));
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"topoSetDict_fluid_interfaces"));
    assert(std::filesystem::is_regular_file(case_path/"0"/"fluid"/"T"));
    assert(std::filesystem::is_regular_file(case_path/"0"/"fluid"/"U"));
    assert(std::filesystem::is_regular_file(case_path/"0"/"fluid"/"k"));
    assert(std::filesystem::is_regular_file(case_path/"0"/"fluid"/"omega"));
    assert(std::filesystem::is_regular_file(case_path/"0"/"fluid"/"nut"));
    assert(std::filesystem::is_regular_file(
        case_path/"0"/"test_heater_0"/"T"));
    for(const auto& temperature_file : {
        case_path/"0"/"fluid"/"T",
        case_path/"0"/"test_heater_0"/"T"}) {
        std::ifstream stream(temperature_file);
        std::ostringstream text;
        text << stream.rdbuf();
        assert(text.str().find("useImplicit false") != std::string::npos);
        assert(text.str().find("useImplicit true") == std::string::npos);
    }
    assert(std::filesystem::is_regular_file(
        case_path/"constant"/"fluid"/"thermophysicalProperties"));
    assert(std::filesystem::is_regular_file(
        case_path/"constant"/"test_heater_0"/"thermophysicalProperties"));
    assert(std::filesystem::is_regular_file(
        case_path/"constant"/"test_heater_0"/"fvOptions"));
    {
        std::ifstream fluid_options(case_path/"constant"/"fluid"/"fvOptions");
        std::ostringstream text;
        text << fluid_options.rdbuf();
        assert(text.str().find("heated_internal_air_2_energy") !=
               std::string::npos);
        assert(text.str().find("sources { h (5") != std::string::npos);
    }
    {
        std::ifstream flow_only(
            case_path/"constant"/"fluid"/"fvOptions.flowOnly");
        std::ostringstream text;
        text << flow_only.rdbuf();
        assert(text.str().find("heated_internal_air_2_energy") ==
               std::string::npos);
        assert(text.str().find("object      fvOptions;") !=
               std::string::npos);
    }
    {
        std::ifstream stream(case_path/"run_parallel.sh");
        std::ostringstream text;
        text << stream.rdbuf();
        assert(text.str().find(
            "Initial airflow uses fans and vents with fluid heat sources disabled.") !=
               std::string::npos);
        assert(text.str().find(
            "Restored full fluid heat sources for thermal evolution.") !=
               std::string::npos);
        assert(text.str().find(
            "Mapped initial airflow retains full fluid heat sources.") !=
               std::string::npos);
        assert(text.str().find(
            "Mapped airflow skips the cold-start air-exchange horizon after live spatial and device validation.") !=
               std::string::npos);
        assert(text.str().find(
            "Cumulative air exchange is $air_exchange_fraction; advancing to t=$exchange_target s before final local convergence checks.") !=
               std::string::npos);
        assert(text.str().find(
            "if [[ ! -f \"$mapped_state_marker\" ]] && ! awk -v completed=\"$air_exchange_fraction\"") !=
               std::string::npos);
        const auto exchange_advance=text.str().find(
            "Cumulative air exchange is $air_exchange_fraction;");
        const auto final_local_acceptance=text.str().find(
            "Initial airflow converged after $initial_elapsed s beyond the fan ramp;",
            exchange_advance);
        assert(exchange_advance!=std::string::npos);
        assert(final_local_acceptance!=std::string::npos);
        assert(exchange_advance<final_local_acceptance);
        assert(text.str().find(
            "Initial airflow gates pass, but cumulative air exchange") ==
               std::string::npos);
        assert(text.str().find(
            "airflow_metrics_converged || airflow_metrics_status=$?") !=
               std::string::npos);
        assert(text.str().find(
            "Airflow metrics evaluation failed; aborting initial airflow.") !=
               std::string::npos);
        assert(text.str().find(
            "Airflow metrics evaluation failed; aborting refresh.") !=
               std::string::npos);
        const auto exchange_reached=text.str().find(
            "Initial air-exchange and full-window settling requirements reached; collecting fresh local convergence windows before acceptance.");
        assert(exchange_reached!=std::string::npos);
        assert(exchange_reached<final_local_acceptance);
        assert(text.str().find(
            "exchange_was_incomplete=true") != std::string::npos);
        assert(text.str().find(
            "initial_physical_settling_marker=\"$case_dir/.initial_airflow_physical_settling\"") !=
               std::string::npos);
        assert(text.str().find(
            "Initial air exchange is complete, but the full-window airflow gate has not passed;") !=
               std::string::npos);
        assert(text.str().find(
            "Resuming full-window initial airflow settling toward") !=
               std::string::npos);
        assert(text.str().find(
            "minimum_observation=\"0.01\"") != std::string::npos);
        assert(text.str().find(
            "t>=minimum-tolerance") != std::string::npos);
        assert(text.str().find(
            "THERMAL_SOLVER_OPENFOAM_ENV_READY=1") !=
               std::string::npos);
        assert(text.str().find(
            "OPENFOAM_LAUNCHER=env") !=
               std::string::npos);
        assert(text.str().find(
            "Initializing OpenFOAM environment once with $foam_launcher.") !=
               std::string::npos);
        assert(text.str().find(
            "Detected mapped nonuniform velocity fields; retaining full heat sources and skipping the cold fan ramp.") !=
               std::string::npos);
        assert(text.str().find("mapped_state_marker=") !=
               std::string::npos);
        assert(text.str().find(
            "initial_exchange_state=\"$case_dir/.initial_air_exchange_state\"") !=
               std::string::npos);
        assert(text.str().find(
            "t>=start-tolerance && t<=now+tolerance") !=
               std::string::npos);
        assert(text.str().find(
            "Cumulative initial air exchange: fraction=$air_exchange_fraction") !=
               std::string::npos);
        assert(text.str().find(
            "increment=0.5*(previous_flow+flow)*dt/(volume*rho)") !=
               std::string::npos);
        assert(text.str().find(
            "completed+1e-9>=required") != std::string::npos);
        assert(text.str().find(
            "printf \"%.17g\", accumulated+increment") !=
               std::string::npos);
        assert(text.str().find(
            "latest_one_way_boundary_mass_flow=\"\"") !=
               std::string::npos);
        assert(text.str().find(
            "\"$case_dir/.initial_air_exchange_state\"\n"
            "    echo \"Warm start invalidated") !=
               std::string::npos);
        assert(text.str().find(
            "lead=minimum-2*interval; if(lead<0)lead=0") !=
               std::string::npos);
        assert(text.str().find(
            "eligibility_target=\"$eligibility_target\"") !=
               std::string::npos);
        assert(text.str().find(
            "cap=a+checkpoint; if(x>cap)x=cap") !=
               std::string::npos);
        assert(text.str().find(
            "-v checkpoint=\"0.10000000000000001\"") !=
               std::string::npos);
        assert(text.str().find(
            "stage_write_interval=\"$checkpoint_steps\"") !=
               std::string::npos);
        const auto preserve_stage_velocity = text.str().find(
            "stage_velocity_reference_tmp=");
        const auto launch_live_stage = text.str().find(
            "echo \"$label: t=$current -> $target\"");
        const auto use_stage_velocity = text.str().find(
            "source_field=\"$stage_velocity_reference/processor${rank}/U\"");
        assert(preserve_stage_velocity != std::string::npos);
        assert(preserve_stage_velocity < launch_live_stage);
        assert(use_stage_velocity > launch_live_stage);
        assert(text.str().find(
            "rm -rf -- \"$stage_velocity_reference\"") !=
               std::string::npos);
        assert(text.str().find(
            "read -r stage_dt stage_steps checkpoint_steps") !=
               std::string::npos);
        assert(text.str().find(
            "local candidate=\"$1\" rank_count=\"${2:-$processes}\"") !=
               std::string::npos);
        assert(text.str().find(
            "for ((rank=0; rank<rank_count; ++rank)); do") !=
               std::string::npos);
        assert(text.str().find(
            "existing_processes=0") != std::string::npos);
        assert(text.str().find(
            "latest_complete_processor_time \"$existing_processes\"") !=
               std::string::npos);
        assert(text.str().find(
            "-v required=\"1\"") != std::string::npos);
        assert(text.str().find(
            "mv -f \"$air_exchange_state_tmp\" \"$initial_exchange_state\"") !=
               std::string::npos);
        assert(text.str().find(
            "tol=1e-9*s; exit !(a<b-tol) }'; do") !=
               std::string::npos);
        assert(text.str().find(
            "tol=1e-9*s; exit !(a>=b-tol) }'; then") !=
               std::string::npos);
        assert(text.str().find(
            "Refreshing airflow at terminal thermal checkpoint") !=
               std::string::npos);
        assert(text.str().find(
            "pending_refresh_start=$(awk 'NF { print $1; exit }'") !=
               std::string::npos);
        assert(text.str().find(
            "Resuming airflow refresh observation window from t=$refresh_start s.") !=
               std::string::npos);
        assert(text.str().find(
            "Pending airflow refresh already exceeded the maximum duration") !=
               std::string::npos);
        assert(text.str().find(
            "Spatial velocity change: rmsDelta=") !=
               std::string::npos);
        assert(text.str().find(
            "fluid_average_root=\"$case_dir/postProcessing/fluid/") !=
               std::string::npos);
        assert(text.str().find(
            "previous_fluid_average=\"${state_values[2]:-}\"") !=
               std::string::npos);
        assert(text.str().find(
            "delta=$(awk -v a=\"$fluid_average\" -v b=\"$previous_fluid_average\"") !=
               std::string::npos);
        assert(text.str().find(
            "controlling_peak_region=fluidAverage") !=
               std::string::npos);
        assert(text.str().find(
            "fluidMaximumChange=$scaled_fluid_max_delta") !=
               std::string::npos);
        assert(text.str().find(
            "velocityRelativeRms=${latest_velocity_relative_rms:-unavailable}") !=
               std::string::npos);
        assert(text.str().find(
            "previousVelocityRelativeRms=${previous_velocity_relative_rms:-unavailable}") !=
               std::string::npos);
        assert(text.str().find(
            "previous_velocity_relative_rms=\"$latest_velocity_relative_rms\"") !=
               std::string::npos);
        assert(text.str().find(
            "velocity_convergence_state=\"$case_dir/.velocity_convergence_state\"") !=
               std::string::npos);
        assert(text.str().find(
            "Restored spatial velocity convergence state from t=$state_time s.") !=
               std::string::npos);
        assert(text.str().find(
            "mv -f \"$velocity_convergence_state.tmp.$$\"") !=
               std::string::npos);
        assert(text.str().find(
            "accepted_airflow_reference=\"$case_dir/.accepted_airflow_reference\"") !=
                std::string::npos);
        assert(text.str().find("field_internal_count()") != std::string::npos);
        assert(text.str().find(
            "incompatible with the current decomposition at rank $rank") !=
                std::string::npos);
        assert(text.str().find(
            "Accepted airflow drift: referenceTime=$reference_time") !=
               std::string::npos);
        assert(text.str().find(
            "airflow_refresh_long_lag_validated=1") !=
               std::string::npos);
        assert(text.str().find(
            "long_lag_failed=0") != std::string::npos);
        assert(text.str().find(
            "continuing live-flow settling at this thermal checkpoint") !=
               std::string::npos);
        assert(text.str().find(
            "this thermal checkpoint remains ineligible for convergence") !=
               std::string::npos);
        assert(text.str().find(
            "controllingPeakRegion=$controlling_peak_region") !=
               std::string::npos);
        assert(text.str().find(
            "accepted airflow has not converged across refresh cycles") !=
               std::string::npos);
        assert(text.str().find(
            "if ! awk -v value=\"$accepted_airflow_relative_rms\"") !=
               std::string::npos);
        assert(text.str().find(
            "record_accepted_airflow_reference || return 3") !=
               std::string::npos);
        assert(text.str().find(
            "-dict system/spatialConvergenceDict") !=
               std::string::npos);
        assert(text.str().find(
            "for field in UPrevious velocityDelta velocityDeltaSquared velocitySquared") !=
               std::string::npos);
        assert(text.str().find(
            "stage_wall_start=$(date +%s%N)") !=
               std::string::npos);
        assert(text.str().find(
            "Stage wall time: label=$label, thermalOnly=$thermal_only, ") !=
               std::string::npos);
        assert(text.str().find(
            "start=$current, target=$actual_time, seconds=$stage_wall_seconds") !=
               std::string::npos);
        assert(text.str().find(
            "summary_log=\"$case_dir/run_summary.log\"") !=
               std::string::npos);
        assert(text.str().find(
            "summary \"stage label=$label thermalOnly=$thermal_only") !=
               std::string::npos);
        assert(text.str().find(
            "summary \"airflow time=$current imbalance=$imbalance") !=
               std::string::npos);
        assert(text.str().find(
            "summary \"thermal time=$checkpoint_time") !=
               std::string::npos);
        assert(text.str().find(
            "summary \"checkpoint time=$current streak=$streak") !=
               std::string::npos);
        assert(text.str().find(
            "summary \"run_complete mode=$mode reconstructedTime=$reconstruct_time") !=
               std::string::npos);
        assert(text.str().find(
            "Advanced accepted airflow reference after validated checkpoint") !=
                std::string::npos);
        const auto accepted_checkpoint = text.str().find(
            "summary \"checkpoint time=$current streak=$streak");
        const auto advanced_reference = text.str().find(
            "reason=validatedCheckpoint");
        const auto completion_gate = text.str().find(
            "if (( streak >= ");
        assert(accepted_checkpoint < advanced_reference);
        assert(advanced_reference < completion_gate);
        assert(text.str().find("write_final_reports()") != std::string::npos);
        assert(text.str().find(
            "writeControl[[:space:]]+)[^;]+;") != std::string::npos);
        assert(text.str().find(
            "semiFrozenChtMultiRegionFoam -case \"$case_dir\" -postProcess") !=
                std::string::npos);
        assert(text.str().find(
            "Final OpenFOAM report generation failed") != std::string::npos);
        assert(text.str().find(
            "summary \"airflow_reference_rebased time=$current") !=
               std::string::npos);
        assert(text.str().find(
            "completedFraction=$air_exchange_fraction") !=
               std::string::npos);
        assert(text.str().find(
            "now+interval") != std::string::npos);
        assert(text.str().find(
            "summary \"run_paused mode=$mode reconstructedTime=$reconstruct_time reason=airflow_refresh_pending\"") !=
               std::string::npos);
        assert(text.str().find(
            "\"Adaptive airflow refresh\" 0.0050000000000000001") !=
               std::string::npos);
        assert(text.str().find(
            "\"Adaptive initial airflow\" 0.001") !=
               std::string::npos);
        const auto refresh_failure = text.str().find(
            "Airflow refresh failed to converge");
        const auto refresh_pause = text.str().find(
            "Airflow refresh reached requested end time");
        assert(refresh_failure != std::string::npos);
        assert(refresh_pause != std::string::npos);
        assert(refresh_failure < refresh_pause);
        assert(text.str().find(
            "-v v=\"$latest_velocity_relative_rms\" -v limit=\"0.01\"") !=
               std::string::npos);
        assert(text.str().find(
            "Warm start invalidated cached airflow and thermal convergence references.") !=
               std::string::npos);
        assert(text.str().find(
            "\"$case_dir/.thermal_convergence_streak\" \"") !=
               std::string::npos);
    }
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"spatialConvergenceDict"));
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"fluid"/"fvSolution"));
    {
        std::ifstream stream(case_path/"system"/"fvSolution");
        std::ostringstream text;
        text << stream.rdbuf();
        assert(text.str().find("nOuterCorrectors 3;") != std::string::npos);
    }
    {
        std::ifstream stream(case_path/"system"/"fluid"/"fvSolution");
        std::ostringstream text;
        text << stream.rdbuf();
        assert(text.str().find("nCorrectors 2;") != std::string::npos);
    }
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"test_heater_0"/"fvSchemes"));
    std::ifstream boundary_file(
        case_path/"constant"/"polyMesh"/"boundary");
    std::ostringstream boundary_text;
    boundary_text << boundary_file.rdbuf();
    boundary_file.close();
    assert(boundary_text.str().find("rack_walls") != std::string::npos);
    assert(boundary_text.str().find("test_inlet") != std::string::npos);
    assert(boundary_text.str().find("test_outlet") != std::string::npos);

    const auto invalid_outer_case=case_path.parent_path()/
        "thermal_solver_invalid_outer_correctors";
    bool rejected_invalid_outer=false;
    try {
        OpenFoamExporter::export_mesh(
            mesh,
            {.case_directory=invalid_outer_case,
             .overwrite=true,
             .pimple_outer_correctors=-1});
    } catch(const std::invalid_argument&) {
        rejected_invalid_outer=true;
    }
    assert(rejected_invalid_outer);
    assert(!std::filesystem::exists(invalid_outer_case));

    const auto invalid_pressure_case=case_path.parent_path()/
        "thermal_solver_invalid_pressure_correctors";
    bool rejected_invalid_pressure=false;
    try {
        OpenFoamExporter::export_mesh(
            mesh,
            {.case_directory=invalid_pressure_case,
             .overwrite=true,
             .pimple_pressure_correctors=-1});
    } catch(const std::invalid_argument&) {
        rejected_invalid_pressure=true;
    }
    assert(rejected_invalid_pressure);
    assert(!std::filesystem::exists(invalid_pressure_case));

    const auto invalid_parallel_case=case_path.parent_path()/
        "thermal_solver_invalid_parallel_processes";
    bool rejected_invalid_parallel=false;
    try {
        OpenFoamExporter::export_mesh(
            mesh,
            {.case_directory=invalid_parallel_case,
             .overwrite=true,
             .parallel_processes=1});
    } catch(const std::invalid_argument&) {
        rejected_invalid_parallel=true;
    }
    assert(rejected_invalid_parallel);
    assert(!std::filesystem::exists(invalid_parallel_case));

    std::cout << case_path.string() << '\n';
    if(!keep_case) std::filesystem::remove_all(case_path);
    std::cout << "openfoam_export_test PASSED\n";
}
