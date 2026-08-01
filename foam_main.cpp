#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "src/collision.hpp"
#include "src/component.hpp"
#include "src/environment.hpp"
#include "src/fan.hpp"
#include "src/grapher.hpp"
#include "src/mesh.hpp"
#include "src/openfoam_exporter.hpp"
#include "src/rack.hpp"
#include "src/vent.hpp"
#include "src/workload.hpp"

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
            0.60, 0.80, 0.60, "OpenFOAM multi-device rack");
        rack.set_t(20.0);
        rack.set_cp(environment.get_cp());
        rack.set_k(environment.get_k());
        rack.set_rho(environment.get_rho());

        std::vector<Component> components;
        auto add_server = [&](const std::string& name, double x,
                              double heat_load, double fan_cfm) {
            Component server = Component::from_meters(
                0.20, 0.40, 0.20, name);
            server.set_coords_m(x, 0.20, 0.20);
            server.set_t(20.0);
            server.set_rho_solid(2700.0);
            server.set_cp(900.0);
            server.set_k_solid(150.0);
            server.set_watts(0.0);

            server.add_region(InternalRegion(
                name + "_air_channel",
                {0.10, 0.40, 0.20},
                {0.00, 0.00, 0.00}));

            Fan internal_fan(
                name + "_internal_fan", fan_cfm, 0.0,
                {0.05, 0.0, 0.20}, {0.025, 0.10, 0.10},
                {0.0, 1.0, 0.0}, FlowType::Intake,
                ShapeType::Rectangular);
            const double free_flow = fan_cfm * Fan::CFM_TO_M3S;
            internal_fan.set_curve(
                12.0, 0.0, 12.0/(free_flow*free_flow), 1.2);
            server.add_region(InternalRegion(internal_fan));

            server.add_region(InternalRegion(Vent(
                name + "_internal_vent",
                {0.05, 0.0, 0.20}, 0.72, 0.0, 0.62,
                {0.025, 0.30, 0.10}, {0.0, 1.0, 0.0},
                VentShapeType::Rectangular)));

            server.add_region(InternalRegion(
                name + "_processor",
                {0.10, 0.10, 0.05},
                {0.10, 0.15, 0.075},
                700.0, 2300.0, 120.0, heat_load));
            server.order_internal_regions();
            components.push_back(server);
        };
        add_server("server_left", 0.05, 250.0, 80.0);
        add_server("server_right", 0.30, 325.0, 95.0);

        std::vector<Fan> fans;
        auto add_boundary_fan =
            [&](const std::string& name, double cfm, double shutoff_pressure,
                const std::array<double,3>& center,
                const std::array<double,3>& direction, FlowType flow_type) {
                Fan fan(
                    name, cfm, 0.0, {0.20, 0.0, 0.20}, center, direction,
                    flow_type, ShapeType::Rectangular);
                const double free_flow = cfm * Fan::CFM_TO_M3S;
                fan.set_curve(
                    shutoff_pressure, 0.0,
                    shutoff_pressure/(free_flow*free_flow), 1.2);
                fans.push_back(fan);
            };

        add_boundary_fan(
            "rack_inlet_lower_left", 55.0, 38.0,
            {0.15, 0.0, 0.15}, {0.0, 1.0, 0.0}, FlowType::Intake);
        add_boundary_fan(
            "rack_inlet_lower_right", 48.0, 40.0,
            {0.45, 0.0, 0.15}, {0.0, 1.0, 0.0}, FlowType::Intake);
        add_boundary_fan(
            "rack_inlet_upper_left", 60.0, 36.0,
            {0.15, 0.0, 0.45}, {0.0, 1.0, 0.0}, FlowType::Intake);
        add_boundary_fan(
            "rack_inlet_upper_right", 52.0, 42.0,
            {0.45, 0.0, 0.45}, {0.0, 1.0, 0.0}, FlowType::Intake);

        std::vector<Vent> vents{
            Vent(
                "rack_outlet_lower_left", {0.20, 0.0, 0.20},
                0.80, 0.0, 0.66, {0.15, 0.80, 0.15},
                {0.0, 1.0, 0.0}, VentShapeType::Rectangular),
            Vent(
                "rack_outlet_lower_right", {0.20, 0.0, 0.20},
                0.78, 0.0, 0.64, {0.45, 0.80, 0.15},
                {0.0, 1.0, 0.0}, VentShapeType::Rectangular),
            Vent(
                "rack_outlet_upper_left", {0.20, 0.0, 0.20},
                0.74, 0.0, 0.61, {0.15, 0.80, 0.45},
                {0.0, 1.0, 0.0}, VentShapeType::Rectangular),
            Vent(
                "rack_outlet_upper_right", {0.20, 0.0, 0.20},
                0.70, 0.0, 0.58, {0.45, 0.80, 0.45},
                {0.0, 1.0, 0.0}, VentShapeType::Rectangular)
        };
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
            .frozen_flow_maximum_time_step = 5.0,
            .frozen_flow_maximum_courant_number = 1000.0,
            .airflow_refresh_interval = 1200.0,
            .airflow_refresh_duration = 1.0,
            .use_adaptive_airflow_refresh = true,
            .airflow_refresh_check_interval = 1.0,
            .maximum_airflow_refresh_duration = 20.0,
            .maximum_mass_imbalance_fraction = 0.01,
            .maximum_device_flow_change_fraction = 0.02,
            .stop_when_thermally_converged = true,
            .minimum_thermal_convergence_time = 3600.0,
            .thermal_convergence_reference_interval = 300.0,
            .maximum_temperature_change = 0.1,
            .maximum_component_average_temperature_change = 0.05,
            .thermal_convergence_required_checkpoints = 2
        };
        OpenFoamExporter::export_mesh(mesh, export_options);
        Grapher geometry_export(rack, dx, dy, dz);
        for(const Component& component : components)
            geometry_export.add_component(component);
        for(const Fan& fan : fans)
            geometry_export.add_fan(fan);
        for(const Vent& vent : vents)
            geometry_export.add_vent(vent);
        geometry_export.export_to_file(
            (case_directory/"geometry.txt").string());

        const std::vector<std::filesystem::path> required_case_files{
            "geometry.txt",
            "constant/polyMesh/points",
            "constant/regionProperties",
            "constant/g",
            "constant/fluid/thermophysicalProperties",
            "constant/server_left_0/thermophysicalProperties",
            "constant/server_left_0/fvOptions",
            "constant/server_right_1/thermophysicalProperties",
            "constant/server_right_1/fvOptions",
            "0/fluid/T",
            "0/fluid/U",
            "0/fluid/p",
            "0/fluid/p_rgh",
            "0/fluid/k",
            "0/fluid/omega",
            "0/fluid/nut",
            "0/server_left_0/T",
            "0/server_right_1/T",
            "system/fluid/fvSchemes",
            "system/fluid/fvSolution",
            "system/server_left_0/fvSchemes",
            "system/server_left_0/fvSolution",
            "system/server_right_1/fvSchemes",
            "system/server_right_1/fvSolution",
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
        for(const auto& source : mesh.get_openfoam_heat_source_regions())
            exported_watts += source.watts;

        std::cout
            << "Runnable OpenFOAM CHT case exported to: "
            << std::filesystem::absolute(case_directory).string() << '\n'
            << "Cells: " << mesh.get_cell_count() << '\n'
            << "Solid regions: "
            << mesh.get_openfoam_component_regions().size() << '\n'
            << "Heat-source regions: "
            << mesh.get_openfoam_heat_source_regions().size() << '\n'
            << "Exported OpenFOAM heat load: " << exported_watts << " W\n"
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
