#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "openfoam_exporter.hpp"

int main(int argc, char** argv) {
    assert(!OpenFoamExportOptions{}.allow_determinant_warnings);
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

    // Heterogeneous nested solids must become distinct OpenFOAM CHT regions;
    // they must not be collapsed to the outer enclosure material.
    {
        Rack warning_rack=Rack::from_meters(0.2,0.1,0.1);
        Mesh warning_mesh=Mesh().build_mesh(
            warning_rack,0.1,0.1,0.1,env,load);
        Component matching=Component::from_meters(
            0.1,0.1,0.1,"matching heater");
        matching.set_coords_m(0.0,0.0,0.0);
        matching.set_rho_solid(2700.0);
        matching.set_cp(900.0);
        matching.set_k_solid(150.0);
        matching.add_region(InternalRegion(
            "matching core",{0.1,0.1,0.1},{0.0,0.0,0.0},
            900.0,2700.0,150.0,1.0));
        std::ostringstream matching_warning;
        std::streambuf* original_cerr =
            std::cerr.rdbuf(matching_warning.rdbuf());
        warning_mesh.stamp_component_for_openfoam(matching);
        std::cerr.rdbuf(original_cerr);
        assert(matching_warning.str().empty());

        Component differing=Component::from_meters(
            0.1,0.1,0.1,"heterogeneous heater");
        differing.set_coords_m(0.1,0.0,0.0);
        differing.set_rho_solid(2700.0);
        differing.set_cp(900.0);
        differing.set_k_solid(150.0);
        differing.add_region(InternalRegion(
            "dissimilar core",{0.1,0.1,0.1},{0.0,0.0,0.0},
            700.0,2330.0,130.0,3.0));
        std::ostringstream differing_warning;
        original_cerr = std::cerr.rdbuf(differing_warning.rdbuf());
        warning_mesh.stamp_component_for_openfoam(differing);
        std::cerr.rdbuf(original_cerr);
        assert(differing_warning.str().empty());
        const auto& material_regions=
            warning_mesh.get_openfoam_component_regions();
        assert(material_regions.size()==2);
        const auto& dissimilar_core=material_regions.back();
        assert(dissimilar_core.name=="heterogeneous heater dissimilar core");
        assert(dissimilar_core.rho==2330.0);
        assert(dissimilar_core.cp==700.0);
        assert(dissimilar_core.conductivity==130.0);
    }

    // A component used only to reserve an ambient-air volume must not leave a
    // zero-cell solid region for splitMeshRegions to process.
    {
        Rack air_rack=Rack::from_meters(0.2,0.1,0.1);
        Mesh air_mesh=Mesh().build_mesh(air_rack,0.1,0.1,0.1,env,load);
        Component air_placeholder=Component::from_meters(
            0.2,0.1,0.1,"2U X 2U Air Block");
        air_placeholder.set_coords_m(0.0,0.0,0.0);
        air_placeholder.set_rho_solid(1200.0);
        air_placeholder.set_cp(800.0);
        air_placeholder.set_k_solid(10.0);
        air_placeholder.set_watts(0.0);
        air_placeholder.add_region(InternalRegion(
            "Air",{0.2,0.1,0.1},{0.0,0.0,0.0}));
        air_placeholder.order_internal_regions();
        air_mesh.stamp_component_for_openfoam(air_placeholder);
        assert(air_mesh.get_openfoam_component_regions().empty());
        for(const auto& cell : air_mesh.get_cells())
            assert(cell.is_fluid());
        for(const auto& metadata : air_mesh.get_openfoam_cell_metadata())
            assert(metadata.region_type==
                   Mesh::OpenFoamCellMetadata::RegionType::Fluid &&
                   metadata.component_id==-1 && metadata.material_id==-1);
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
    // This bounded fit has two positive roots (0.5 and 0.75 m^3/s) and
    // becomes positive again above the second. Export must use only the first
    // zero-pressure crossing, then retain a signed C1 assisted-flow branch.
    inlet.set_curve(1.5,5.0,-4.0,1.2);
    Vent outlet(
        "test_outlet",{0.1,0.0,0.1},1.0,0.0,0.65,
        {0.25,0.2,0.15},{0.0,1.0,0.0},
        VentShapeType::Rectangular);
    mesh.stamp_fan_for_openfoam(inlet);
    mesh.stamp_vent_for_openfoam(outlet);
    PorousRegion porous{"perforated tray",{0.0,0.1,0.1},{0.1,0.1,0.1},
        {1.0,0.0,0.0},0.0,2500.0,1.0e8,1.0e5};
    mesh.stamp_porous_region(porous,true);

    assert(mesh.has_openfoam_export_metadata());
    assert(mesh.get_openfoam_component_regions().size()==3);
    assert(mesh.get_openfoam_porous_regions().size()==1);
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
    std::filesystem::create_directories(case_path);

    // A fully solid mesh has zero ambient-connected fluid volume. This is a
    // deterministic generated-runner rejection and must happen during
    // read-only preflight, before overwrite cleanup can erase old evidence.
    {
        Rack solid_rack=Rack::from_meters(0.1,0.1,0.1);
        Mesh solid_mesh=Mesh().build_mesh(
            solid_rack,0.1,0.1,0.1,env,load);
        Component solid=Component::from_meters(
            0.1,0.1,0.1,"fully solid guard");
        solid.set_coords_m(0.0,0.0,0.0);
        solid.set_rho_solid(2700.0);
        solid.set_cp(900.0);
        solid.set_k_solid(150.0);
        solid_mesh.stamp_component_for_openfoam(solid);

        const std::filesystem::path solid_case=
            case_path/"solid_preflight_guard";
        std::filesystem::create_directories(solid_case/"0.25");
        const std::filesystem::path checkpoint=
            solid_case/"0.25"/"checkpoint_sentinel.txt";
        const std::string checkpoint_bytes="preserved checkpoint\n";
        std::ofstream(checkpoint,std::ios::binary) << checkpoint_bytes;
        bool solid_rejected=false;
        try {
            OpenFoamExporter::export_mesh(
                solid_mesh,
                {.case_directory=solid_case,
                 .overwrite=true,
                 .parallel_processes=2});
        } catch(const std::invalid_argument& error) {
            solid_rejected=std::string(error.what()).find(
                "ambient-connected fluid volume") != std::string::npos;
        }
        assert(solid_rejected);
        std::ifstream checkpoint_input(checkpoint,std::ios::binary);
        std::ostringstream checkpoint_contents;
        checkpoint_contents << checkpoint_input.rdbuf();
        assert(checkpoint_contents.str()==checkpoint_bytes);
        checkpoint_input.close();
        assert(!std::filesystem::exists(solid_case/"system"));
        std::filesystem::remove_all(solid_case);
    }
    // Regression: a nested solid with a different material must export as its
    // own CHT region, retain its exact dictionary values, and couple thermally
    // to both the enclosure and adjacent fluid rather than falling back to the
    // enclosure material.
    {
        Rack heterogeneous_rack=Rack::from_meters(0.3,0.1,0.1);
        Mesh heterogeneous_mesh=Mesh().build_mesh(
            heterogeneous_rack,0.1,0.1,0.1,env,load);
        Component heterogeneous=Component::from_meters(
            0.2,0.1,0.1,"heterogeneous assembly");
        heterogeneous.set_coords_m(0.0,0.0,0.0);
        heterogeneous.set_rho_solid(2700.0);
        heterogeneous.set_cp(900.0);
        heterogeneous.set_k_solid(150.0);
        heterogeneous.add_region(InternalRegion(
            "mixed core",{0.1,0.1,0.1},{0.1,0.0,0.0},
            800.0,1200.0,10.0,3.0));
        heterogeneous.order_internal_regions();
        heterogeneous_mesh.stamp_component_for_openfoam(heterogeneous);
        const std::filesystem::path heterogeneous_case=
            case_path/"heterogeneous_material_case";
        OpenFoamExporter::export_mesh(
            heterogeneous_mesh,
            {.case_directory=heterogeneous_case,
             .overwrite=true,
             .parallel_processes=2});
        const auto read_case_file=[&](const std::filesystem::path& path) {
            std::ifstream input(path);
            std::ostringstream text;
            text << input.rdbuf();
            return text.str();
        };
        const std::string enclosure_properties=read_case_file(
            heterogeneous_case/"constant"/"heterogeneous_assembly_0"/
                "thermophysicalProperties");
        const std::string core_properties=read_case_file(
            heterogeneous_case/"constant"/
                "heterogeneous_assembly_mixed_core_1"/
                "thermophysicalProperties");
        assert(enclosure_properties.find("kappa 150") != std::string::npos);
        assert(enclosure_properties.find("Cp 900") != std::string::npos);
        assert(enclosure_properties.find("rho 2700") != std::string::npos);
        assert(core_properties.find("kappa 10") != std::string::npos);
        assert(core_properties.find("Cp 800") != std::string::npos);
        assert(core_properties.find("rho 1200") != std::string::npos);
        const std::string enclosure_temperature=read_case_file(
            heterogeneous_case/"0"/"heterogeneous_assembly_0"/"T");
        const std::string core_temperature=read_case_file(
            heterogeneous_case/"0"/
                "heterogeneous_assembly_mixed_core_1"/"T");
        assert(enclosure_temperature.find(
            "heterogeneous_assembly_0_to_heterogeneous_assembly_mixed_core_1")
               != std::string::npos);
        assert(core_temperature.find(
            "heterogeneous_assembly_mixed_core_1_to_heterogeneous_assembly_0")
               != std::string::npos);
        const std::string core_sources=read_case_file(
            heterogeneous_case/"constant"/
                "heterogeneous_assembly_mixed_core_1"/"fvOptions");
        assert(core_sources.find("mixed_core_0_energy") != std::string::npos);
        std::filesystem::remove_all(heterogeneous_case);
    }
    {
        std::ofstream(case_path/"validation_4800.json") << "stale\n";
        std::ofstream(case_path/"component_thermal_report.csv") << "stale\n";
        std::ofstream(case_path/"multirate_18000.stdout.log") << "stale\n";
        std::ofstream(case_path/"engineering_notes.md") << "preserve\n";
        std::filesystem::create_directories(
            case_path/".openfoam_selector_fields");
        std::ofstream(
            case_path/".openfoam_selector_fields"/"stale_mask") << "stale\n";
        std::ofstream(case_path/"selector_mapping_audit.json") << "stale\n";
        std::ofstream(case_path/"splitMeshRegions.low_memory.log") << "stale\n";
        for(const char* marker : {
                ".fan_ramp_complete",
                ".mapped_initial_state",
                ".initial_airflow_converged",
                ".initial_airflow_pending",
                ".initial_air_exchange_state",
                ".initial_airflow_physical_settling",
                ".airflow_refresh_pending",
                ".airflow_convergence_state",
                ".velocity_convergence_state",
                ".thermal_convergence_state",
                ".thermal_convergence_streak",
                ".openfoam_mesh_determinant_warning"})
            std::ofstream(case_path/marker) << "stale\n";
        for(const char* directory : {
                ".accepted_airflow_reference",
                ".accepted_airflow_reference.tmp",
                ".stage_velocity_reference",
                ".stage_velocity_reference.tmp.123"}) {
            std::filesystem::create_directories(case_path/directory);
            std::ofstream(case_path/directory/"stale") << "stale\n";
        }
    }
    OpenFoamExporter::export_mesh(
        mesh,
        {.case_directory=case_path,
         .overwrite=true,
         .allow_determinant_warnings=true,
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
         .use_fan_curves=true,
         .pimple_outer_correctors=3,
         .pimple_pressure_correctors=2,
         .thermal_only_pimple_outer_correctors=2,
         .use_multirate_thermal=true,
         .minimum_initial_air_exchange_fraction=1.0,
         .airflow_refresh_maximum_time_step=0.005,
         .airflow_checkpoint_interval=0.1,
         .airflow_refresh_duration=0.1,
         .stop_when_thermally_converged=true});

    assert(!std::filesystem::exists(case_path/"validation_4800.json"));
    assert(!std::filesystem::exists(
        case_path/"component_thermal_report.csv"));
    assert(!std::filesystem::exists(
        case_path/"multirate_18000.stdout.log"));
    assert(!std::filesystem::exists(case_path/".openfoam_selector_fields"));
    assert(!std::filesystem::exists(case_path/"selector_mapping_audit.json"));
    assert(!std::filesystem::exists(case_path/"splitMeshRegions.low_memory.log"));
    for(const char* state : {
            ".fan_ramp_complete",
            ".mapped_initial_state",
            ".initial_airflow_converged",
            ".initial_airflow_pending",
            ".initial_air_exchange_state",
            ".initial_airflow_physical_settling",
            ".airflow_refresh_pending",
            ".airflow_convergence_state",
            ".velocity_convergence_state",
            ".thermal_convergence_state",
            ".thermal_convergence_streak",
            ".openfoam_mesh_determinant_warning",
            ".accepted_airflow_reference",
            ".accepted_airflow_reference.tmp",
            ".stage_velocity_reference",
            ".stage_velocity_reference.tmp.123"})
        assert(!std::filesystem::exists(case_path/state));
    assert(std::filesystem::is_regular_file(case_path/"engineering_notes.md"));

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
    assert(control_text.str().find("writePrecision  17;") !=
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
    assert(std::filesystem::is_regular_file(
        case_path/"0"/"porous_perforated_tray_0_mask"));
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"topoSetDict_porous_perforated_tray_0"));
    {
        std::ifstream zones(case_path/"constant"/"polyMesh"/"cellZones");
        std::ostringstream text; text << zones.rdbuf();
        assert(text.str().find("porous_perforated_tray_0")==std::string::npos);
    }
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
        "allow_determinant_warnings=\"true\"") != std::string::npos);
    assert(preparation_script.find(
        "mesh quality policy rejects determinant warnings") !=
           std::string::npos);
    assert(preparation_script.find(
        "SCREENING WARNING:") != std::string::npos);
    assert(preparation_script.find(
        "failed_checks != determinant_failures") != std::string::npos);
    const std::size_t determinant_rejection=preparation_script.find(
        "mesh quality policy rejects determinant warnings");
    const std::size_t prepared_marker=preparation_script.find(
        "touch \"$case_dir/.openfoam_regions_prepared\"");
    assert(determinant_rejection != std::string::npos);
    assert(prepared_marker != std::string::npos);
    assert(determinant_rejection < prepared_marker);
    assert(preparation_script.find(
        "run_toposet") != std::string::npos);
    assert(preparation_script.find(
        "THERMAL_SIM_LOW_MEMORY_PREP_ACTIVE") != std::string::npos);
    assert(preparation_script.find(
        "verify-split --case \"$case_dir\"") != std::string::npos);
    assert(preparation_script.find(
        "verify-materialized --case \"$case_dir\"") != std::string::npos);
    assert(preparation_script.find("splitMeshRegions.done") ==
           std::string::npos);
    assert(preparation_script.find("$key.done") == std::string::npos);
    assert(preparation_script.find(
        "splitMeshRegions -case") == std::string::npos);
    assert(preparation_script.find(
        "topoSet \"$@\" </dev/null") != std::string::npos);
    assert(preparation_script.find("exit 1") != std::string::npos);
    assert(std::filesystem::is_regular_file(
        case_path/"openfoam_stream_region_selectors.py"));
    assert(std::filesystem::is_regular_file(
        case_path/"prepare_regions_low_memory.sh"));
    assert(std::filesystem::is_regular_file(
        case_path/"build_semifrozen_solver.sh"));
    assert(std::filesystem::is_regular_file(
        case_path/"solver_build_bundle"/"manifest.txt"));
    assert(std::filesystem::is_regular_file(
        case_path/"solver_build_bundle"/"tools"/
            "build_openfoam_semifrozen_solver.sh"));
    assert(std::filesystem::is_regular_file(
        case_path/"solver_build_bundle"/"tools"/
            "openfoam_semifrozen_attestation.py"));
    assert(std::filesystem::is_regular_file(
        case_path/"solver_build_bundle"/"openfoam_semifrozen_solver"/
            "semiFrozenChtMultiRegionFoam.C"));
    for(const auto& relative : {
            std::filesystem::path("build_semifrozen_solver.sh"),
            std::filesystem::path("solver_build_bundle/tools/")/
                "build_openfoam_semifrozen_solver.sh",
            std::filesystem::path("solver_build_bundle/tools/")/
                "openfoam_semifrozen_attestation.py"}) {
        std::ifstream stream(case_path/relative,std::ios::binary);
        const std::string contents{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        assert(contents.find('\r')==std::string::npos);
    }
    assert(std::filesystem::is_regular_file(case_path/"run_cht.sh"));
    assert(std::filesystem::is_regular_file(case_path/"run_parallel.sh"));
    {
        std::ifstream stream(case_path/"run_cht.sh");
        std::ostringstream text;
        text << stream.rdbuf();
        assert(text.str().find(
            "Serial run_cht.sh is disabled for this multirate export") !=
               std::string::npos);
        assert(text.str().find(
            "bash \"$case_dir/prepare_regions.sh\"") == std::string::npos);
        assert(text.str().find(
            "prepare_regions_low_memory.sh") == std::string::npos);
        assert(text.str().find(
            "chtMultiRegionFoam -case") == std::string::npos);
    }
    {
        std::ifstream stream(case_path/"run_parallel.sh");
        std::ostringstream text;
        text << stream.rdbuf();
        const std::string mode_policy_marker =
            "THERMAL_SIM_SEMIFROZEN_MODE_POLICY_V1";
        const auto environment_setup = text.str().find(
            "Initializing OpenFOAM environment once with $foam_launcher.");
        const auto policy_gate = text.str().find(
            "solver_mode_policy_marker=\"" + mode_policy_marker + "\"");
        const auto runtime_attestation = text.str().find(
            "solver_runtime_attestation=$(\"$semi_frozen_solver\" "
            "--thermal-sim-attest");
        const auto case_lock = text.str().find(
            "run_lock=\"$case_dir/.thermal_solver_run.lock\"");
        assert(environment_setup != std::string::npos);
        assert(policy_gate != std::string::npos);
        assert(runtime_attestation != std::string::npos);
        assert(case_lock != std::string::npos);
        assert(environment_setup < policy_gate);
        assert(policy_gate < runtime_attestation);
        assert(runtime_attestation < case_lock);
        assert(text.str().find(
            "semi_frozen_solver=\"$(command -v "
            "semiFrozenChtMultiRegionFoam || true)\"") !=
               std::string::npos);
        assert(text.str().find(
            "case_solver_builder=\"$case_dir/build_semifrozen_solver.sh\"") !=
               std::string::npos);
        assert(text.str().find(
            "Build the case-bound solver first with: bash") !=
               std::string::npos);
        assert(text.str().find(
            "semi_frozen_solver=\"$(readlink -f "
            "\"$semi_frozen_solver\")\"") != std::string::npos);
        assert(text.str().find(
            "grep -aFq --") == std::string::npos);
        assert(text.str().find(
            "custom OpenFOAM solver runtime attestation failed before case "
            "locking.") != std::string::npos);
        assert(text.str().find(
            "run_tracked bash \"$case_dir/prepare_regions_low_memory.sh\" "
            "\"$case_dir\"") != std::string::npos);
        assert(text.str().find(
            "bash \"$case_dir/prepare_regions.sh\"") == std::string::npos);
        assert(text.str().find(
            "fan_curve_domain_rules=(\"test_inlet:") !=
               std::string::npos);
        assert(text.str().find(
            "Fan outside signed curve domain") !=
               std::string::npos);
        assert(text.str().find(
            "Fan on signed assisted-flow branch") !=
               std::string::npos);
        assert(text.str().find(
            "\"$semi_frozen_solver\" -case \"$case_dir\" -parallel "
            "-postProcess -latestTime") != std::string::npos);
        assert(text.str().find(
            "semiFrozenChtMultiRegionFoam -case") == std::string::npos);
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
        assert(text.str().find("porous_perforated_tray_0") != std::string::npos);
        // The physical tray occupies one axial mesh cell, so OpenFOAM spreads
        // it over two and halves every coefficient to conserve delta-p.
        assert(text.str().find("f (1250 50000 50000)") != std::string::npos);
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
            "Multirate end time $requested_end must be greater than the latest "
            "processor checkpoint $current; no airflow or thermal stage was run.") !=
               std::string::npos);
        assert(text.str().find("exit 11") != std::string::npos);
        assert(text.str().find(
            "Solid-region heat sources remain active during initial airflow; CHT and buoyancy continue to evolve.") !=
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
            "THERMAL_SOLVER_SCRIPT_SNAPSHOT=1") !=
               std::string::npos);
        assert(text.str().find(
            "THERMAL_SOLVER_CASE_DIR=\"$case_dir\"") !=
               std::string::npos);
        assert(text.str().find(
            "bash -n \"$script_snapshot_path\"") !=
               std::string::npos);
        const auto restore_production_controls=text.str().find(
            "restore_production_solver_controls()");
        const auto restore_run_state=text.str().find("restore_run_state()");
        const auto run_tracked=text.str().find("run_tracked()");
        const auto run_tracked_capture=text.str().find(
            "run_tracked_capture()",run_tracked);
        const auto terminate_run=text.str().find("terminate_run()");
        const auto bootstrap_exit_trap=text.str().find(
            "trap cleanup_script_snapshot EXIT",terminate_run);
        const auto interrupt_trap=text.str().find(
            "trap 'terminate_run INT' INT",bootstrap_exit_trap);
        const auto terminate_trap=text.str().find(
            "trap 'terminate_run TERM' TERM",interrupt_trap);
        const auto exit_trap=text.str().find(
            "trap restore_run_state EXIT",restore_run_state);
        assert(run_tracked != std::string::npos);
        assert(run_tracked_capture > run_tracked);
        assert(terminate_run > run_tracked_capture);
        const std::string tracked_body=text.str().substr(
            run_tracked,run_tracked_capture-run_tracked);
        const auto defer_int=tracked_body.find(
            "trap 'pending_termination_signal=INT' INT");
        const auto defer_term=tracked_body.find(
            "trap 'pending_termination_signal=TERM' TERM",defer_int);
        const auto setsid_launch=tracked_body.find("setsid -- \"$@\" &");
        const auto job_control_fallback=tracked_body.find("set -m",setsid_launch);
        const auto job_control_reset=tracked_body.find(
            "set +m",job_control_fallback);
        const auto child_registration=tracked_body.find(
            "active_child_pid=$!",job_control_reset);
        const auto restore_int=tracked_body.find(
            "trap 'terminate_run INT' INT",child_registration);
        const auto restore_term=tracked_body.find(
            "trap 'terminate_run TERM' TERM",restore_int);
        const auto deliver_pending=tracked_body.find(
            "terminate_run \"$pending_termination_signal\"",restore_term);
        assert(defer_int != std::string::npos);
        assert(defer_term > defer_int);
        assert(setsid_launch > defer_term);
        assert(job_control_fallback > setsid_launch);
        assert(job_control_reset > job_control_fallback);
        assert(child_registration > job_control_reset);
        assert(restore_int > child_registration);
        assert(restore_term > restore_int);
        assert(deliver_pending > restore_term);
        assert(bootstrap_exit_trap > terminate_run);
        assert(interrupt_trap > bootstrap_exit_trap);
        assert(terminate_trap > interrupt_trap);
        const auto script_snapshot_gate=text.str().find(
            "if [[ \"${THERMAL_SOLVER_SCRIPT_SNAPSHOT:-0}\" != 1 ]]",
            terminate_trap);
        const auto tracked_preparation=text.str().find(
            "run_tracked bash \"$case_dir/prepare_regions_low_memory.sh\"",
            script_snapshot_gate);
        assert(script_snapshot_gate > terminate_trap);
        assert(tracked_preparation > script_snapshot_gate);
        assert(restore_production_controls != std::string::npos);
        assert(restore_run_state > restore_production_controls);
        assert(exit_trap > restore_run_state);
        const std::string restore_state_body=text.str().substr(
            restore_run_state,exit_trap-restore_run_state);
        assert(restore_state_body.find(
            "restore_full_fan_options || true") != std::string::npos);
        assert(restore_state_body.find(
            "restore_production_solver_controls || true") !=
               std::string::npos);
        assert(restore_state_body.find(
            "cleanup_script_snapshot || true") != std::string::npos);
        const std::string terminate_body=text.str().substr(
            terminate_run,bootstrap_exit_trap-terminate_run);
        assert(terminate_body.find("trap - EXIT") !=
            std::string::npos);
        assert(terminate_body.find("trap '' INT TERM") !=
            std::string::npos);
        assert(terminate_body.find(
            "declare -F restore_run_state") != std::string::npos);
        assert(terminate_body.find("cleanup_script_snapshot") !=
               std::string::npos);
        assert(terminate_body.find(
            "kill -s \"$signal\" -- \"-$child_pid\"") !=
               std::string::npos);
        assert(terminate_body.find(
            "kill -s \"$signal\" -- \"$child_pid\"") !=
               std::string::npos);
        const auto first_watchdog=terminate_body.find(
            "for ((attempt=0; attempt<50; ++attempt))");
        const auto watchdog_term=terminate_body.find(
            "kill -s TERM -- \"-$child_pid\"",first_watchdog);
        const auto second_watchdog=terminate_body.find(
            "for ((attempt=0; attempt<20; ++attempt))",watchdog_term);
        const auto watchdog_kill=terminate_body.find(
            "kill -s KILL -- \"-$child_pid\"",second_watchdog);
        const auto watchdog_wait=terminate_body.find(
            "wait \"$watchdog_pid\"",watchdog_kill);
        assert(first_watchdog != std::string::npos);
        assert(watchdog_term > first_watchdog);
        assert(second_watchdog > watchdog_term);
        assert(watchdog_kill > second_watchdog);
        assert(watchdog_wait > watchdog_kill);
        assert(terminate_body.find(
            "declare -F restore_preparation_controls") !=
               std::string::npos);
        assert(terminate_body.find("exit \"$status\"") !=
               std::string::npos);
        assert(text.str().find(
            "trap restore_run_state EXIT INT TERM") == std::string::npos);
        assert(text.str().find(
            "OPENFOAM_LAUNCHER=env") !=
               std::string::npos);
        assert(text.str().find(
            "warm_start_maximum_time_step=\"${THERMAL_WARM_START_MAX_DT:-0.001}\"") !=
               std::string::npos);
        assert(text.str().find(
            "THERMAL_WARM_START_MAX_DT must be a positive finite number.") !=
               std::string::npos);
        assert(text.str().find(
            "warmStartMaxDt=$warm_start_maximum_time_step") !=
               std::string::npos);
        assert(text.str().find(
            "thermal_only_outer_correctors=\"${THERMAL_ONLY_OUTER_CORRECTORS:-2}\"") !=
               std::string::npos);
        assert(text.str().find(
            "THERMAL_ONLY_OUTER_CORRECTORS must be a positive integer.") !=
               std::string::npos);
        assert(text.str().find(
            "THERMAL_ONLY_OUTER_CORRECTORS must be at least 2") !=
               std::string::npos);
        assert(text.str().find(
            "liveOuterCorrectors=3 thermalOnlyOuterCorrectors=$thermal_only_outer_correctors") !=
               std::string::npos);
        assert(text.str().find(
            "stage_outer_correctors=\"$thermal_only_outer_correctors\"") !=
               std::string::npos);
        assert(text.str().find(
            "stage_outer_correctors=\"3\"") != std::string::npos);
        assert(text.str().find(
            "restore_live_outer_correctors()") != std::string::npos);
        assert(text.str().find(
            "PIMPLE/nOuterCorrectors -set 3") != std::string::npos);
        assert(text.str().find(
            "\"$case_dir/system/fvSolution\" -entry PIMPLE/nOuterCorrectors") !=
               std::string::npos);
        assert(text.str().find(
            "\"$case_dir/system/fluid/fvSolution\" -entry PIMPLE/nOuterCorrectors") ==
               std::string::npos);
        assert(text.str().find(
            "outerCorrectors=$stage_outer_correctors") !=
               std::string::npos);
        assert(text.str().find(
            "run_tracked \"$foam_launcher\" decomposePar") !=
               std::string::npos);
        assert(text.str().find(
            "run_tracked \"$foam_launcher\" reconstructPar") !=
               std::string::npos);
        assert(text.str().find(
            "run_tracked \"$foam_launcher\" mpirun -np \"$processes\"") !=
               std::string::npos);
        assert(text.str().find(
            "run_tracked_capture postflight_output") !=
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
        assert(text.str().find("invalidate_airflow_acceptance_state()") !=
               std::string::npos);
        assert(text.str().find(
            "\"$case_dir/.initial_air_exchange_state\" "
            "\"$case_dir/.initial_airflow_physical_settling\"") !=
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
        assert(text.str().find("preflight_warm_start_state()") !=
               std::string::npos);
        assert(text.str().find(
            "common_time=$(latest_complete_processor_time \"$expected\")") !=
               std::string::npos);
        assert(text.str().find(
            "case \"$configured_start_from\" in") !=
               std::string::npos);
        assert(text.str().find(
            "startTime|latestTime|firstTime) ;;") !=
               std::string::npos);
        assert(text.str().find(
            "[[ \"$configured_start_from\" == startTime ]] && awk -v configured=") !=
               std::string::npos);
        assert(text.str().find(
            "configured startTime $configured_start: it is newer than the latest common complete processor checkpoint $common_time") !=
               std::string::npos);
        assert(text.str().find(
            "only the decomposed t=0 initial state; no completed solver checkpoint exists yet") !=
               std::string::npos);
        assert(text.str().find(
            "postProcessing time directories newer than the latest common complete processor checkpoint $common_time") !=
               std::string::npos);
        assert(text.str().find(
            "data files containing first-column time samples newer than the latest common complete processor checkpoint $common_time") !=
               std::string::npos);
        assert(text.str().find(
            "print FILENAME \"\\t\" value") !=
               std::string::npos);
        assert(text.str().find("nextfile") != std::string::npos);
        assert(text.str().find("-exec awk -v common=\"$common_time\"") !=
               std::string::npos);
        assert(text.str().find(
            "staleReportFiles=${#stale_report_samples[@]}") !=
               std::string::npos);
        const auto early_warm_preflight=text.str().find(
            "preflight_warm_start_state false true || exit $?");
        const auto decomposition_selection=text.str().find(
            "reuse_decomposition=false");
        assert(early_warm_preflight != std::string::npos);
        assert(decomposition_selection != std::string::npos);
        assert(early_warm_preflight < decomposition_selection);
        assert(text.str().find(
            "preflight_warm_start_state true false || exit $?",
            decomposition_selection) != std::string::npos);
        assert(text.str().find(
            "-v required=\"1\"") != std::string::npos);
        assert(text.str().find(
            "mv -f \"$air_exchange_state_tmp\" \"$initial_exchange_state\"") !=
               std::string::npos);
        const auto empty_initial_marker=text.str().find(
            "if [[ -f \"$initial_pending_marker\" && "
            "! -s \"$initial_pending_marker\" ]]");
        const auto resumable_initial_marker=text.str().find(
            "if [[ -s \"$initial_pending_marker\" ]]",empty_initial_marker);
        const auto new_initial_marker=text.str().find(
            "if [[ ! -f \"$initial_pending_marker\" ]]",
            resumable_initial_marker);
        const auto write_initial_marker=text.str().find(
            "printf '%s\\n' \"$initial_start\" > \"$initial_pending_tmp\"",
            new_initial_marker);
        const auto publish_initial_marker=text.str().find(
            "mv -f \"$initial_pending_tmp\" \"$initial_pending_marker\"",
            write_initial_marker);
        const auto reset_initial_airflow_state=text.str().find(
            "rm -f \"$velocity_convergence_state\" "
            "\"$airflow_convergence_state\"",publish_initial_marker);
        assert(empty_initial_marker != std::string::npos);
        assert(resumable_initial_marker > empty_initial_marker);
        assert(new_initial_marker > resumable_initial_marker);
        assert(write_initial_marker > new_initial_marker);
        assert(publish_initial_marker > write_initial_marker);
        assert(reset_initial_airflow_state > publish_initial_marker);
        assert(text.str().find(
            "tol=1e-9*s; if(tol>1e-8)tol=1e-8; ") !=
               std::string::npos);
        const auto exact_endpoint_helper=text.str().find(
            "require_exact_endpoint()");
        const auto stage_interval_helper=text.str().find(
            "classify_stage_interval()",exact_endpoint_helper);
        assert(exact_endpoint_helper != std::string::npos);
        assert(stage_interval_helper > exact_endpoint_helper);
        assert(text.str().substr(
            exact_endpoint_helper,
            stage_interval_helper-exact_endpoint_helper).find(
                "if(tolerance>1e-8)tolerance=1e-8") !=
               std::string::npos);
        assert(text.str().find(
            "Reached the exact requested endpoint t=$current s; terminal "
            "airflow validation remains pending") !=
               std::string::npos);
        assert(text.str().find(
            "Refreshing airflow at terminal thermal checkpoint") ==
               std::string::npos);
        assert(text.str().find("write_airflow_refresh_state()") !=
               std::string::npos);
        const auto refresh_commit_helper=text.str().find(
            "commit_airflow_refresh_state()");
        assert(text.str().find("normalize_airflow_refresh_journal()") !=
               std::string::npos);
        assert(refresh_commit_helper != std::string::npos);
        const auto refresh_normalizer=text.str().find(
            "normalize_airflow_refresh_journal()",refresh_commit_helper);
        const std::string refresh_commit_body=text.str().substr(
            refresh_commit_helper,refresh_normalizer-refresh_commit_helper);
        assert(refresh_commit_body.find(
            "if [[ \"$state\" != owed ]]") != std::string::npos);
        assert(refresh_commit_body.find(
            "[[ ! \"$start\" =~ ^[0-9]+") != std::string::npos);
        assert(refresh_commit_body.find(
            "[[ ! \"$target\" =~ ^[0-9]+") != std::string::npos);
        assert(refresh_commit_body.find(
            "require_exact_endpoint \"$start\" \"$expected_start\"") !=
               std::string::npos);
        assert(refresh_commit_body.find(
            "require_exact_endpoint \"$target\" \"$expected_target\"") !=
               std::string::npos);
        assert(refresh_commit_body.find(
            "require_exact_endpoint \"$actual\" \"$expected_target\"") !=
               std::string::npos);
        assert(refresh_commit_body.find(
            "write_airflow_refresh_state active \"$actual\"") !=
               std::string::npos);
        assert(text.str().find(
            "read -r pending_refresh_state pending_refresh_start") !=
               std::string::npos);
        assert(text.str().find(
            "write_airflow_refresh_state active \"$refresh_start\"") !=
               std::string::npos);
        const auto owed_refresh_journal=text.str().find(
            "write_airflow_refresh_state owed \"$current\" "
            "\"$frozen_target\"");
        const auto thermal_only_stage=text.str().find(
            "stage true \"$frozen_target\"",owed_refresh_journal);
        assert(owed_refresh_journal != std::string::npos);
        assert(thermal_only_stage > owed_refresh_journal);
        const auto stage_function=text.str().find("stage()\n");
        const auto stage_endpoint_check=text.str().find(
            "require_exact_endpoint \"$actual_time\" \"$target\"",
            stage_function);
        const auto stage_thermal_source_restore=text.str().find(
            "if [[ \"$thermal_only\" == \"true\" && ",
            stage_endpoint_check);
        const auto active_refresh_commit=text.str().find(
            "commit_airflow_refresh_state \"$actual_time\" "
            "\"$current\" \"$target\"",
            stage_thermal_source_restore);
        const auto stage_prune=text.str().find(
            "prune_processor_times",active_refresh_commit);
        const auto stage_summary=text.str().find(
            "summary \"stage label=$label",active_refresh_commit);
        const auto stage_current_commit=text.str().find(
            "current=\"$actual_time\"",active_refresh_commit);
        assert(stage_function != std::string::npos);
        assert(stage_endpoint_check > stage_function);
        assert(stage_thermal_source_restore > stage_endpoint_check);
        assert(active_refresh_commit > stage_thermal_source_restore);
        assert(stage_prune > active_refresh_commit);
        assert(stage_summary > active_refresh_commit);
        assert(stage_current_commit > stage_summary);
        assert(text.str().find(
            "An uncommitted thermal-only checkpoint advanced from") !=
               std::string::npos);
        assert(text.str().find(
            "refusing automatic recovery.") != std::string::npos);
        assert(text.str().find(
            "Recovered completed or partial thermal-only checkpoint") ==
               std::string::npos);
        const auto refresh_journal_temporary=text.str().find(
            "temporary=\"${refresh_pending_marker}.tmp.$$\"");
        const auto refresh_journal_publish=text.str().find(
            "mv -f -- \"$temporary\" \"$refresh_pending_marker\"",
            refresh_journal_temporary);
        assert(refresh_journal_temporary != std::string::npos);
        assert(refresh_journal_publish > refresh_journal_temporary);
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
        const auto acceptance_reference_guard=text.str().find(
            "acceptance_reference_complete=true");
        const auto acceptance_time_check=text.str().find(
            "[[ -f \"$accepted_airflow_reference/time\" ]]",
            acceptance_reference_guard);
        const auto acceptance_rank_check=text.str().find(
            "[[ -f \"$accepted_airflow_reference/processor${rank}/U\" ]]",
            acceptance_time_check);
        const auto incomplete_acceptance=text.str().find(
            "if [[ \"$acceptance_reference_complete\" != true ]]",
            acceptance_rank_check);
        const auto revoke_initial_acceptance=text.str().find(
            "rm -f -- \"$initial_convergence_marker\"",
            incomplete_acceptance);
        const auto remove_incomplete_reference=text.str().find(
            "rm -rf -- \"$accepted_airflow_reference\"",
            revoke_initial_acceptance);
        const auto force_initial_revalidation=text.str().find(
            "if [[ ! -f \"$initial_convergence_marker\" ]]",
            remove_incomplete_reference);
        assert(acceptance_reference_guard != std::string::npos);
        assert(acceptance_time_check > acceptance_reference_guard);
        assert(acceptance_rank_check > acceptance_time_check);
        assert(incomplete_acceptance > acceptance_rank_check);
        assert(revoke_initial_acceptance > incomplete_acceptance);
        assert(remove_incomplete_reference > revoke_initial_acceptance);
        assert(force_initial_revalidation > remove_incomplete_reference);
        assert(text.str().find(
            "Initial-airflow acceptance evidence is incomplete; revoking the "
            "marker and requiring full revalidation.") != std::string::npos);
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
            "\"$semi_frozen_solver\" -case \"$case_dir\" -postProcess") !=
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
            "summary \"run_paused mode=$mode reconstructedTime=$reconstruct_time reason=$run_pause_reason\"") !=
               std::string::npos);
        assert(text.str().find("multirate_pause_reason()") !=
               std::string::npos);
        assert(text.str().find("classify_stage_interval()") !=
               std::string::npos);
        assert(text.str().find("Refusing reversed stage target") !=
               std::string::npos);
        assert(text.str().find(
            "Missing fan-ramp time metadata for processor${rank}") !=
               std::string::npos);
        assert(text.str().find(
            "Missing stage time metadata for processor${rank}") !=
               std::string::npos);
        assert(text.str().find(
            "Missing warm-start time metadata for processor${rank}") !=
               std::string::npos);
        assert(text.str().find("printf '%s\\n' fan_ramp_pending") !=
               std::string::npos);
        assert(text.str().find("printf '%s\\n' initial_airflow_pending") !=
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
            "Warm start invalidated cached airflow and thermal convergence "
            "references before checkpoint mutation.") !=
               std::string::npos);
        assert(text.str().find(
            "rm -f -- \"$case_dir/.initial_airflow_converged\"") !=
               std::string::npos);
        assert(text.str().find(
            "\"$case_dir/.initial_airflow_pending\" "
            "\"$case_dir/.initial_air_exchange_state\"") !=
               std::string::npos);
        assert(text.str().find("preflight_checkpoint_space()") !=
               std::string::npos);
        assert(text.str().find(
            "required_kb=$((2*checkpoint_kb+524288))") !=
               std::string::npos);
        assert(text.str().find(
            "preflight_checkpoint_space || exit $?") !=
               std::string::npos);
        assert(text.str().find(
            "Insufficient disk space for a recoverable checkpoint") !=
               std::string::npos);
        assert(text.str().find(
            "fan_ramp_complete_marker=\"$case_dir/.fan_ramp_complete\"") !=
               std::string::npos);
        const auto ramp_completion_gate=text.str().find(
            "if fan_ramp_endpoint_reached \"$ramp_current\"; then");
        const auto ramp_completion_touch=text.str().find(
            "touch \"$fan_ramp_complete_marker\"",ramp_completion_gate);
        assert(ramp_completion_gate != std::string::npos);
        assert(ramp_completion_touch > ramp_completion_gate);
        assert(text.str().find("full scale remains pending") !=
               std::string::npos);
        assert(text.str().find(
            "[[ ! -f \"$fan_ramp_complete_marker\" ]]") !=
               std::string::npos);
        const auto warm_windows_function=text.str().find(
            "run_warm_start_windows()");
        const auto warm_windows_call=text.str().find(
            "    run_warm_start_windows\n",warm_windows_function);
        assert(warm_windows_function != std::string::npos);
        assert(warm_windows_call > warm_windows_function);
        const auto warm_checkpoint_preflight=text.str().find(
            "        preflight_checkpoint_space || exit $?");
        const auto warm_acceptance_invalidation=text.str().find(
            "        invalidate_airflow_acceptance_state\n",
            warm_checkpoint_preflight);
        const auto warm_fan_ramp=text.str().find(
            "        run_fan_ramp ",warm_acceptance_invalidation);
        assert(warm_checkpoint_preflight != std::string::npos);
        assert(warm_acceptance_invalidation > warm_checkpoint_preflight);
        assert(warm_fan_ramp > warm_acceptance_invalidation);
        assert(warm_windows_call > warm_acceptance_invalidation);
        const auto warm_restart_plan = text.str().find(
            "warm_restart_plan=$(awk -v maximum=\"$warm_start_maximum_time_step\"");
        assert(warm_restart_plan != std::string::npos);
        assert(text.str().find("-v remaining=\"$warm_interval\"",
                               warm_restart_plan) != std::string::npos);
        assert(text.str().find(
            "-entry maxDeltaT -set \"$warm_restart_dt\"",
                               warm_restart_plan) != std::string::npos);
        assert(text.str().find(
            "read -r warm_restart_dt warm_restart_steps",
                               warm_restart_plan) != std::string::npos);
        assert(text.str().find(
            "-entry writeControl -set timeStep",
                               warm_restart_plan) != std::string::npos);
        assert(text.str().find(
            "-entry writeInterval -set \"$warm_restart_steps\"",
                               warm_restart_plan) != std::string::npos);
        assert(text.str().find(
            "require_exact_endpoint \"$warm_actual\" \"$warm_window_target\"",
                               warm_restart_plan) != std::string::npos);
        assert(text.str().find(
            "warm_window_target=$(next_warm_start_window") < warm_restart_plan);
        assert(text.str().find(
            "summary \"warm_start_window index=$warm_window_index",
                               warm_restart_plan) != std::string::npos);
        assert(text.str().find(
            "validate_latest_airflow_courant \"$warm_window_target\"",
                               warm_restart_plan) != std::string::npos);
        const auto restart_courant_helper=text.str().find(
            "validate_latest_airflow_courant()");
        const auto restart_courant_precheck=text.str().find(
            "checkpoint before Courant validation",restart_courant_helper);
        const auto restart_courant_postprocess=text.str().find(
            "run_tracked_capture output",restart_courant_helper);
        const auto restart_courant_postcheck=text.str().find(
            "checkpoint after Courant validation",restart_courant_postprocess);
        const auto restart_courant_resume=text.str().find(
            "validate_latest_airflow_courant \"$warm_current\"",
            warm_acceptance_invalidation);
        assert(restart_courant_helper != std::string::npos);
        assert(restart_courant_precheck > restart_courant_helper);
        assert(restart_courant_postprocess > restart_courant_precheck);
        assert(restart_courant_postcheck > restart_courant_postprocess);
        assert(restart_courant_resume > warm_acceptance_invalidation);
        assert(restart_courant_resume < warm_fan_ramp);
        const auto fan_ramp_restart_helper=text.str().find(
            "validate_interrupted_fan_ramp_checkpoint()");
        const auto fan_ramp_restart_validation=text.str().find(
            "validate_latest_airflow_courant \"$actual\"",
            fan_ramp_restart_helper);
        const auto fan_ramp_restart_marker=text.str().find(
            "touch \"$fan_ramp_complete_marker\"",
            fan_ramp_restart_validation);
        const auto multirate_fan_ramp_restart=text.str().find(
            "validate_interrupted_fan_ramp_checkpoint \"$current\"");
        const auto warm_fan_ramp_restart=text.str().find(
            "validate_interrupted_fan_ramp_checkpoint \"$warm_current\"",
            warm_acceptance_invalidation);
        assert(fan_ramp_restart_helper != std::string::npos);
        assert(fan_ramp_restart_validation > fan_ramp_restart_helper);
        assert(fan_ramp_restart_marker > fan_ramp_restart_validation);
        assert(multirate_fan_ramp_restart != std::string::npos);
        assert(multirate_fan_ramp_restart < warm_fan_ramp_restart);
        assert(warm_fan_ramp_restart > warm_acceptance_invalidation);
        assert(warm_fan_ramp_restart < restart_courant_resume);
        assert(text.str().find(
            "completion marker is missing but latest time",
            fan_ramp_restart_helper) != std::string::npos);
        assert(text.str().find(
            "validate_latest_airflow_courant \"$target\" "
            "\"Fan-ramp stage $step\"") != std::string::npos);
        assert(text.str().find(
            "warm_restart_relation\" == equal") != std::string::npos);
        assert(text.str().find(
            "Conventional run mode is disabled for this multirate export") !=
               std::string::npos);
        assert(text.str().find(
            "terminal airflow validation remains pending") !=
               std::string::npos);
        assert(text.str().find("terminal_requested_end") ==
               std::string::npos);
        assert(text.str().find(
            "\"$case_dir/.thermal_convergence_streak\" \"") !=
               std::string::npos);
    }
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"spatialConvergenceDict"));
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"courantValidationDict"));
    {
        std::ifstream stream(case_path/"system"/"courantValidationDict");
        std::ostringstream text;
        text << stream.rdbuf();
        assert(text.str().find("type        CourantNo;") != std::string::npos);
        assert(text.str().find("fields      (phi rho);") != std::string::npos);
        assert(text.str().find("field       Co;") != std::string::npos);
    }
    {
        std::ifstream stream(case_path/"0"/"fluid"/"p_rgh");
        std::ostringstream text;
        text << stream.rdbuf();
        assert(text.str().find("(1 -") != std::string::npos);
        assert(text.str().find("(1 0)") == std::string::npos);
        assert(text.str().find("(1 0.5)") == std::string::npos);
    }
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

    const auto invalid_assisted_slope_case=case_path.parent_path()/
        "thermal_solver_invalid_assisted_slope";
    bool rejected_invalid_assisted_slope=false;
    try {
        OpenFoamExporter::export_mesh(
            mesh,
            {.case_directory=invalid_assisted_slope_case,
             .overwrite=true,
             .use_fan_curves=true,
             .fan_assisted_flow_slope_multiplier=0.0});
    } catch(const std::invalid_argument& error) {
        rejected_invalid_assisted_slope=std::string(error.what()).find(
            "fan_assisted_flow_slope_multiplier") != std::string::npos;
    }
    assert(rejected_invalid_assisted_slope);
    assert(!std::filesystem::exists(invalid_assisted_slope_case));

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

    const auto invalid_thermal_outer_case=case_path.parent_path()/
        "thermal_solver_invalid_thermal_outer_correctors";
    bool rejected_invalid_thermal_outer=false;
    try {
        OpenFoamExporter::export_mesh(
            mesh,
            {.case_directory=invalid_thermal_outer_case,
             .overwrite=true,
             .thermal_only_pimple_outer_correctors=-1});
    } catch(const std::invalid_argument&) {
        rejected_invalid_thermal_outer=true;
    }
    assert(rejected_invalid_thermal_outer);
    assert(!std::filesystem::exists(invalid_thermal_outer_case));

    const auto unsafe_single_thermal_outer_case=case_path.parent_path()/
        "thermal_solver_single_thermal_outer_corrector";
    bool rejected_single_thermal_outer=false;
    try {
        OpenFoamExporter::export_mesh(
            mesh,
            {.case_directory=unsafe_single_thermal_outer_case,
             .overwrite=true,
             .thermal_only_pimple_outer_correctors=1});
    } catch(const std::invalid_argument&) {
        rejected_single_thermal_outer=true;
    }
    assert(rejected_single_thermal_outer);
    assert(!std::filesystem::exists(unsafe_single_thermal_outer_case));

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

    const auto unsafe_nonadaptive_case=case_path.parent_path()/
        "thermal_solver_nonadaptive_multirate";
    bool rejected_nonadaptive_multirate=false;
    try {
        OpenFoamExporter::export_mesh(
            mesh,
            {.case_directory=unsafe_nonadaptive_case,
             .overwrite=true,
             .use_multirate_thermal=true,
             .use_adaptive_airflow_refresh=false});
    } catch(const std::invalid_argument& error) {
        rejected_nonadaptive_multirate=std::string(error.what()).find(
            "requires use_adaptive_airflow_refresh=true") !=
            std::string::npos;
    }
    assert(rejected_nonadaptive_multirate);
    assert(!std::filesystem::exists(unsafe_nonadaptive_case));

    std::cout << case_path.string() << '\n';
    if(!keep_case) std::filesystem::remove_all(case_path);
    std::cout << "openfoam_export_test PASSED\n";
}
