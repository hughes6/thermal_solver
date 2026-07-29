#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "collision.hpp"
#include "component.hpp"
#include "environment.hpp"
#include "fan.hpp"
#include "mesh.hpp"
#include "openfoam_exporter.hpp"
#include "rack.hpp"
#include "vent.hpp"
#include "workload.hpp"

int main(int argc, char* argv[]) {
    try {
        const std::filesystem::path case_directory =
            argc > 1
                ? std::filesystem::path(argv[1])
                : std::filesystem::path("openfoam_cases/basic_rack");

        Environment environment(
            30.0,       // maximum expected temperature, C
            5800.0,     // altitude, ft
            20.0,       // ambient temperature, C
            1005.0,     // air Cp, J/(kg K)
            0.02587,    // air conductivity, W/(m K)
            0.000018,   // dynamic viscosity, Pa s
            0.71,       // Prandtl number
            1.225);     // air density, kg/m^3

        Workload workload(
            1'000'000,  // maximum cells
            1'000'000,  // maximum mesh bytes
            100'000,    // maximum timesteps
            100);       // maximum output writes

        Rack rack = Rack::from_meters(
            0.40, 0.60, 0.40, "OpenFOAM example rack");
        rack.set_t(20.0);
        rack.set_cp(environment.get_cp());
        rack.set_k(environment.get_k());
        rack.set_rho(environment.get_rho());

        Component server = Component::from_meters(
            0.20, 0.30, 0.20, "example_server");
        server.set_coords_m(0.10, 0.15, 0.10);
        server.set_t(20.0);
        server.set_rho_solid(2700.0);
        server.set_cp(900.0);
        server.set_k_solid(150.0);
        server.set_watts(0.0);

        InternalRegion air_channel(
            "server_air_channel",
            {0.05, 0.30, 0.20},
            {0.00, 0.00, 0.00});
        server.add_region(air_channel);

        Fan server_internal_fan(
            "server_internal_fan",
            80.0,
            0.0,
            {0.05, 0.0, 0.20},
            {0.025, 0.10, 0.10},
            {0.0, 1.0, 0.0},
            FlowType::Intake,
            ShapeType::Rectangular);
        constexpr double internal_fan_free_flow =
            80.0 * Fan::CFM_TO_M3S;
        server_internal_fan.set_curve(
            12.0, 0.0,
            12.0/(internal_fan_free_flow*internal_fan_free_flow),
            1.2);
        server.add_region(InternalRegion(server_internal_fan));

        Vent server_internal_vent(
            "server_internal_vent",
            {0.05, 0.0, 0.20},
            0.75,
            0.0,
            0.65,
            {0.025, 0.20, 0.10},
            {0.0, 1.0, 0.0},
            VentShapeType::Rectangular);
        server.add_region(InternalRegion(server_internal_vent));

        InternalRegion processor(
            "processor_heat_source",
            {0.05, 0.10, 0.05},
            {0.10, 0.10, 0.075},
            700.0,      // Cp, J/(kg K)
            2300.0,     // density, kg/m^3
            120.0,      // conductivity, W/(m K)
            250.0);     // heat load, W
        server.add_region(processor);
        server.order_internal_regions();

        Fan inlet_fan_left(
            "rack_inlet_left",
            55.0,                 // fixed-flow fallback, CFM
            0.0,
            {0.10, 0.0, 0.20},   // rectangular footprint
            {0.15, 0.0, 0.20},   // center on the y-min rack face
            {0.0, 1.0, 0.0},
            FlowType::Intake,
            ShapeType::Rectangular);
        constexpr double left_fan_free_flow_m3s =
            55.0 * Fan::CFM_TO_M3S;
        inlet_fan_left.set_curve(
            18.0, 0.0,
            18.0/(left_fan_free_flow_m3s*left_fan_free_flow_m3s),
            1.2);

        Fan inlet_fan_right(
            "rack_inlet_right",
            45.0,                 // fixed-flow fallback, CFM
            0.0,
            {0.10, 0.0, 0.20},
            {0.25, 0.0, 0.20},
            {0.0, 1.0, 0.0},
            FlowType::Intake,
            ShapeType::Rectangular);
        constexpr double right_fan_free_flow_m3s =
            45.0 * Fan::CFM_TO_M3S;
        inlet_fan_right.set_curve(
            22.0, 0.0,
            22.0/(right_fan_free_flow_m3s*right_fan_free_flow_m3s),
            1.2);

        Vent outlet_vent(
            "rack_outlet",
            {0.20, 0.0, 0.20},   // rectangular footprint
            0.80,                 // free-area ratio
            0.0,
            0.65,                 // discharge coefficient
            {0.20, 0.60, 0.20},  // center on the y-max rack face
            {0.0, 1.0, 0.0},
            VentShapeType::Rectangular);

        std::vector<Component> components{server};
        std::vector<Fan> fans{inlet_fan_left,inlet_fan_right};
        std::vector<Vent> vents{outlet_vent};
        CollisionChecker::check_all(components, fans, vents);

        constexpr double dx = 0.05;
        constexpr double dy = 0.05;
        constexpr double dz = 0.05;
        Mesh mesh = Mesh().build_mesh(
            rack, dx, dy, dz, environment, workload);

        // The export-aware component sibling preserves all legacy stamping
        // behavior and additionally records solid/material/source identity.
        for(const Component& component : components)
            mesh.stamp_component_for_openfoam(component);

        for(const Fan& fan : fans)
            mesh.stamp_fan_for_openfoam(fan);
        for(const Vent& vent : vents)
            mesh.stamp_vent_for_openfoam(vent);

        // This single export now writes the polyMesh plus the complete
        // chtMultiRegionFoam case: region fields, material dictionaries,
        // heat-source fvOptions, solver dictionaries, and launch scripts.
        const OpenFoamExportOptions export_options{
            .case_directory = case_directory,
            .overwrite = true,
            .parallel_processes = 4,
            .end_time = 18'000.0,
            .initial_time_step = 0.01,
            .maximum_time_step = 1.0,
            .maximum_courant_number = 1.0,
            .field_write_interval = 60.0,
            .report_interval = 10.0,
            .use_k_omega_sst = true,
            .inlet_turbulence_intensity = 0.05,
            .turbulence_length_scale = 0.014,
            .turbulent_prandtl_number = 0.85,
            .temperature_dependent_air = true,
            .gravity = {0.0, 0.0, -9.80665},
            .sutherland_temperature = 110.4,
            .use_vent_pressure_loss = true,
            .use_fan_curves = true,
            .fan_curve_extension_multiplier = 2.0,
            // Multirate stages use semiFrozenChtMultiRegionFoam. Full stages
            // establish the fan/vent operating point; thermal-only stages hold
            // that airflow while advancing implicit CHT and temperature-
            // dependent density until the next airflow refresh.
            .use_multirate_thermal = true,
            .airflow_warmup_time = 5.0,
            .frozen_flow_maximum_time_step = 1.0,
            .frozen_flow_maximum_courant_number = 1000.0,
            .airflow_refresh_interval = 300.0,
            .airflow_refresh_duration = 1.0,
            .use_adaptive_airflow_refresh = true,
            .airflow_refresh_check_interval = 1.0,
            .maximum_airflow_refresh_duration = 20.0,
            .maximum_mass_imbalance_fraction = 0.01,
            .maximum_device_flow_change_fraction = 0.02
        };
        OpenFoamExporter::export_mesh(mesh, export_options);

        const std::vector<std::filesystem::path> required_case_files{
            "constant/polyMesh/points",
            "constant/regionProperties",
            "constant/g",
            "constant/fluid/thermophysicalProperties",
            "constant/example_server_0/thermophysicalProperties",
            "constant/example_server_0/fvOptions",
            "0/fluid/T",
            "0/fluid/U",
            "0/fluid/p",
            "0/fluid/p_rgh",
            "0/fluid/k",
            "0/fluid/omega",
            "0/fluid/nut",
            "0/example_server_0/T",
            "system/fluid/fvSchemes",
            "system/fluid/fvSolution",
            "system/example_server_0/fvSchemes",
            "system/example_server_0/fvSolution",
            "system/decomposeParDict",
            "prepare_regions.sh",
            "run_cht.sh",
            "run_parallel.sh"
        };
        for(const auto& relative_path : required_case_files) {
            if(!std::filesystem::is_regular_file(
                   case_directory/relative_path))
                throw std::runtime_error(
                    "incomplete OpenFOAM case export; missing '" +
                    (case_directory/relative_path).string() + "'");
        }

        double exported_watts = 0.0;
        for(const Cell& cell : mesh.get_cells())
            exported_watts += cell.get_qdot() * cell.volume();

        std::cout
            << "Runnable OpenFOAM CHT case exported to: "
            << std::filesystem::absolute(case_directory).string() << '\n'
            << "Cells: " << mesh.get_cell_count() << '\n'
            << "Solid regions: "
            << mesh.get_openfoam_component_regions().size() << '\n'
            << "Heat-source regions: "
            << mesh.get_openfoam_heat_source_regions().size() << '\n'
            << "Stamped heat load: " << exported_watts << " W\n"
            << "Simulation duration: " << export_options.end_time << " s\n"
            << "Initial/max timestep: "
            << export_options.initial_time_step << " / "
            << export_options.maximum_time_step << " s\n"
            << "Maximum Courant number: "
            << export_options.maximum_courant_number << '\n'
            << "Multirate thermal mode: "
            << (export_options.use_multirate_thermal ? "enabled" : "disabled")
            << " (warm-up " << export_options.airflow_warmup_time
            << " s, refresh every "
            << export_options.airflow_refresh_interval << " s)\n"
            << "Adaptive airflow refresh: "
            << (export_options.use_adaptive_airflow_refresh
                    ? "enabled" : "disabled")
            << " (check every "
            << export_options.airflow_refresh_check_interval
            << " s, maximum "
            << export_options.maximum_airflow_refresh_duration << " s)\n"
            << "Field/report intervals: "
            << export_options.field_write_interval << " / "
            << export_options.report_interval << " s\n"
            << "Turbulence model: kOmegaSST\n"
            << "Inlet turbulence intensity/length scale: "
            << export_options.inlet_turbulence_intensity << " / "
            << export_options.turbulence_length_scale << " m\n"
            << "CHT fields/materials/fvOptions: exported\n"
            << "Region preparation script: prepare_regions.sh\n"
            << "Solver launch script: run_cht.sh\n\n"
            << "Parallel launch script (default 4 processes): "
               "run_parallel.sh\n\n"
            << "Run the complete case from WSL with:\n"
            << "  bash <case-path>/run_cht.sh\n"
            << "or run in parallel with:\n"
            << "  bash <case-path>/run_parallel.sh [process-count]\n"
            << "or create a restartable warm start first:\n"
            << "  bash <case-path>/run_parallel.sh [process-count] "
               "--warm-start [end-time]\n";
        if(export_options.use_multirate_thermal)
            std::cout
                << "or run the multirate airflow/thermal schedule:\n"
                << "  bash <case-path>/run_parallel.sh [process-count] "
                   "--multirate [end-time]\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "foam_main: " << error.what() << '\n';
        return 1;
    }
}
