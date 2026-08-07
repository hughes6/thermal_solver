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

    Rack rack=Rack::from_meters(0.3,0.2,0.2);
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
    assert(mesh.get_openfoam_component_regions().size()==2);
    assert(mesh.get_openfoam_heat_source_regions().size()==2);
    assert(mesh.get_openfoam_heat_source_regions()[1].watts==7.0);
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
         .pimple_pressure_correctors=2});

    for(const char* file :
        {"points","faces","owner","neighbour","boundary","cellZones"})
        assert(std::filesystem::is_regular_file(
            case_path/"constant"/"polyMesh"/file));
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"controlDict"));
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
        case_path/"0"/"heatSourceMask_0"));
    assert(std::filesystem::is_regular_file(
        case_path/"system"/"topoSetDict_test_heat_source_0"));
    assert(std::filesystem::is_regular_file(
        case_path/"prepare_regions.sh"));
    assert(std::filesystem::is_regular_file(case_path/"run_cht.sh"));
    assert(std::filesystem::is_regular_file(case_path/"run_parallel.sh"));
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

    std::cout << case_path.string() << '\n';
    if(!keep_case) std::filesystem::remove_all(case_path);
    std::cout << "openfoam_export_test PASSED\n";
}
