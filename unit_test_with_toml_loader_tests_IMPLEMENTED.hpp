#ifndef UNIT_TEST_HPP
#define UNIT_TEST_HPP

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cell.hpp"
#include "collision.hpp"
#include "component.hpp"
#include "convection.hpp"
#include "environment.hpp"
#include "fan.hpp"
#include "flow_solver.hpp"
#include "grapher.hpp"
#include "mesh.hpp"
#include "input/model_loader.hpp"
#include "rack.hpp"
#include "solver.hpp"
#include "vent.hpp"

class UnitTest {
private:
    double dx = 0.01;
    double dy = 0.01;
    double dz = 0.01;
    double sim_length = 20;
    double dt = 0.1;
    double mu = 1.8e-5;
    double pr = 0.71;
    double T = 20.0;

    static bool nearly_equal(double a,
                             double b,
                             double absolute_tolerance = 1e-10,
                             double relative_tolerance = 1e-8) {
        const double scale = std::max(std::abs(a), std::abs(b));
        return std::abs(a - b) <=
               std::max(absolute_tolerance, relative_tolerance * scale);
    }

    Rack make_air_rack(double width,
                       double depth,
                       double height,
                       double temperature = 20.0) const {
        Rack rack = Rack::from_meters(width, depth, height);
        rack.set_t(temperature);
        rack.set_cp(1005.0);
        rack.set_k(0.02587);
        rack.set_rho(1.225);
        return rack;
    }

    Component make_test_component() const {
        // Component constructor order:
        // height, width, depth, name
        Component component(
            0.04,
            0.04,
            0.04,
            "Internal-region test component"
        );

        component.set_coords_m(0.02, 0.02, 0.02);
        component.set_t(50.0);
        component.set_rho_solid(2700.0);
        component.set_cp(900.0);
        component.set_k_solid(200.0);
        component.set_watts(0.0);

        return component;
    }

public:
    UnitTest() {}

    void run() {
        std::cout << "\n========== RUNNING UNIT TESTS ==========\n";

        test1();
        test2();

        test_workload_init_limits();
        
        test_mesh_density_limit();
        test_mesh_memory_limit();
        test_solver_timestep_limit();
        test_solver_cell_updates_limit();
        test_solver_output_interval_limit();

        test_natural_convection_used_when_stagnant();
        test_turbulent_regime_selected();
        test_no_driving_force_returns_minimum_nu();
        test_reynolds_scales_linearly_with_velocity();
        test_invalid_reynolds_inputs_return_zero();
        test_laminar_regime_returns_expected_nusselt();
        test_local_h_matches_nusselt_definition();
        test_convection_result_is_finite();

        test_vent_area_uses_plane_dimensions();
        test_vent_free_area_ratio_validation();
        test_component_stamping_sets_material_and_qdot();
        test_heat_generation_increases_temperature();

        test_flow_solver_keeps_stagnant_rack_at_zero_velocity();
        test_flow_solver_creates_nonzero_fan_to_vent_flow();
        test_flow_solver_rejects_source_without_vent();

        test_internal_air_region_stamps_as_air();

        test_internal_heat_source_stamps_properties();
        test_internal_heat_source_conserves_total_watts();

        test_internal_region_local_to_global_position();
        test_internal_region_exact_bounds_allowed();

        test_internal_region_negative_x_rejected();
        test_internal_region_negative_y_rejected();
        test_internal_region_negative_z_rejected();

        test_internal_region_x_overflow_rejected();
        test_internal_region_y_overflow_rejected();
        test_internal_region_z_overflow_rejected();

        test_uninitialized_internal_region_rejected_when_ordering();
        test_internal_region_ordering();

        test_valid_internal_vent_on_component_face();
        test_internal_vent_not_on_face_rejected();
        test_internal_vent_touching_two_faces_rejected();
        test_internal_vent_free_area_ratio_bounds();
        test_internal_vent_free_area_ratio_endpoints_allowed();

        test_valid_internal_rectangular_fan_on_face();
        test_internal_rectangular_fan_with_diameter_rejected();
        test_internal_fan_negative_cfm_rejected();
        test_internal_fan_cfm_conversion();
        test_internal_rectangular_fan_area();
        // test_internal_fan_uses_velocity_direction();

        // test_internal_vent_on_offset_component_face();

        test_fan_curve_throttles_vs_fixed_cfm_under_restrictive_vent();
        test_fan_curve_flow_increases_with_less_restrictive_vent();
        test_fan_curve_density_sensitivity();

        test_collision_allows_non_overlapping_components();
        test_collision_detects_component_component_overlap();
        test_collision_allows_flush_adjacent_components();
        test_collision_detects_fan_embedded_in_component();
        test_collision_detects_vent_overlapping_fan();
        test_collision_is_independent_of_mesh_resolution();

        test_component_loader_valid_toml_succeeds();
        test_component_loader_missing_watts_fails();
        test_component_loader_invalid_units_fail_on_run();
        test_model_loader_valid_toml_succeeds();
        test_model_loader_malformed_toml_fails();
        test_model_loader_missing_environment_fails();
        
        std::cout << "========== ALL UNIT TESTS PASSED ==========\n\n";
    }

    // Existing whole-rack scaffold test: uniform rack with no heat source
    // should remain uniform and stationary.
    void test1() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(100'000, 1'000'000, 100'000, 4);
        Rack rack = make_air_rack(0.1, 0.1, 0.1, T);
        Mesh mesh = Mesh().build_mesh(rack, dx, dy, dz, env, load);

        FlowSolver flow_solver(mesh);
        flow_solver.solve();
        Solver solver(mesh, dt, sim_length, false, 10);
        solver.solve();

        const Mesh& final_mesh = solver.get_mesh();
        std::array<bool, 3> result =
            test_all_mesh_values(final_mesh, T, 0.0, 0.0);

        if (!result[0]) {
            throw std::runtime_error(
                "Test 1 - All Temp = " + std::to_string(T) + " failed.");
        }
        if (!result[1]) {
            throw std::runtime_error("Test 1 - All velocity = 0.0 failed.");
        }
        if (!result[2]) {
            throw std::runtime_error("Test 1 - All Qdot = 0.0 failed.");
        }

        std::cout << "test1 uniform 20 C stagnant rack PASSED\n";
    }

    // Existing whole-rack scaffold test at a different initial temperature.
    void test2() {
        Environment env(30.0, 5800.0, 40.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(100'000, 1'000'000, 100'000, 4);
        Rack rack = make_air_rack(0.1, 0.1, 0.1, 40.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, env, load);

        FlowSolver flow_solver(mesh);
        flow_solver.solve();
        Solver solver(mesh, dt, sim_length, false, 10);
        solver.solve();

        const Mesh& final_mesh = solver.get_mesh();
        std::array<bool, 3> result =
            test_all_mesh_values(final_mesh, 40.0, 0.0, 0.0);

        if (!result[0]) {
            throw std::runtime_error("Test 2 - All Temp = 40.0 failed.");
        }

        std::cout << "test2 uniform 40 C stagnant rack PASSED\n";
    }

    void test_workload_init_limits() {
        bool threw1 = false;
        try {
            Workload load(0, 1, 1, 1);
        } catch (const std::invalid_argument&) {
            threw1 = true;
        }
        assert(threw1);
        std::cout << "test_workload_init_limits - (0, 1, 1, 1) PASSED\n";

        bool threw2 = false;
        try {
            Workload load(1, 0, 1, 1);
        } catch (const std::invalid_argument&) {
            threw2 = true;
        }
        assert(threw2);
        std::cout << "test_workload_init_limits - (1, 0, 1, 1) PASSED\n";

        bool threw3 = false;
        try {
            Workload load(1, 1, 0, 1);
        } catch (const std::invalid_argument&) {
            threw3 = true;
        }
        assert(threw3);
        std::cout << "test_workload_init_limits - (1, 1, 0, 1) PASSED\n";

        bool threw4 = false;
        try {
            Workload load(1, 1, 1, 0);
        } catch (const std::invalid_argument&) {
            threw4 = true;
        }
        assert(threw4);
        std::cout << "test_workload_init_limits - (1, 1, 1, 0) PASSED\n";
    }

    void test_mesh_density_limit() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(1'000'00, 10'000'000, 1'000'000, 4);
        Rack rack = make_air_rack(1.0, 1.0, 1.001, 20.0);

        bool threw = false;
        try {
        Mesh mesh = Mesh().build_mesh(rack, 0.01, 0.01, 0.01, env, load);
        } catch (const std::invalid_argument&) {
            threw = true;
        }

        assert(threw);
        std::cout << "test_mesh_density_limit PASSED\n";
    }

    void test_mesh_memory_limit() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(1'000'000, 10'000'000, 1'000'000, 1);
        Rack rack = make_air_rack(1.0, 1.0, 1.0, 20.0);

        bool threw = false;
        try {
        Mesh mesh = Mesh().build_mesh(rack, 0.01, 0.01, 0.01, env, load);
        } catch (const std::invalid_argument&) {
            threw = true;
        }

        assert(threw);
        std::cout << "test_mesh_memory_limit PASSED\n";
    }

    void test_solver_timestep_limit() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(1'000'00, 10'000'000, 1'000'000, 4);
        Rack rack = make_air_rack(1.0, 1.0, 1.0, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.5, 0.5, 0.5, env, load);

        bool threw = false;
        try {
            Solver solver(mesh, 0.000001, 10.0, false, 10);
        } catch (const std::invalid_argument&) {
            threw = true;
        }

        assert(threw);
        std::cout << "test_solver_timestep_limit PASSED\n";
    }

    void test_solver_cell_updates_limit() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(1'000'00, 10'000'000, 1'000'000, 4);
        Rack rack = make_air_rack(1.0, 1.0, 1.0, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.1, 0.1, 0.1, env, load);

        bool threw = false;
        try {
            Solver solver(mesh, 0.0001, 10.0, false, 10);
        } catch (const std::invalid_argument&) {
            threw = true;
        }

        assert(threw && "Solver: cell updates exceed the max of 10000000");
        std::cout << "    void test_solver_cell_updates_limit PASSED\n";
    }

    void test_solver_output_interval_limit() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(100'00, 1'000'000, 100'000, 4);
        Rack rack = make_air_rack(1.0, 1.0, 1.0, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.5, 0.5, 0.5, env, load);

        bool threw = false;
        try {
            Solver solver(mesh, 1.0, 10.0, false, 100);
        } catch (const std::invalid_argument&) {
            threw = true;
        }

        assert(threw && "Solver: output interval is larger than timesteps.");
        std::cout << "    void test_solver_output_interval_limit PASSED\n";
    }

    void test_natural_convection_used_when_stagnant() {
        // Re ~ 0, real delta_T -> should NOT be near-zero h.
        const double h = Convection::compute_local_h(
            /*velocity_mag=*/0.0,
            /*char_length=*/0.04445,
            /*rho=*/1.2,
            /*mu=*/1.8e-5,
            /*k_air=*/0.026,
            /*Pr=*/0.71,
            /*delta_T=*/40.0,
            /*t_film_K=*/313.15);

        assert(h > 0.5 &&
               "stagnant hot cell should still get nonzero h from natural convection");
        std::cout << "test_natural_convection_used_when_stagnant PASSED (h="
                  << h << ")\n";
    }

    void test_turbulent_regime_selected() {
        const double Re =
            Convection::local_reynolds(5.0, 0.04445, 1.2, 1.8e-5);
        assert(Re > Convection::RE_TURBULENT &&
               "test velocity should land in turbulent regime");

        const double Nu = Convection::local_nusselt(Re, 0.71, 0.0, true);
        assert(Nu > 4.36 &&
               "turbulent Nu should exceed laminar constant-Nu baseline");
        std::cout << "test_turbulent_regime_selected PASSED (Re="
                  << Re << ", Nu=" << Nu << ")\n";
    }

    void test_no_driving_force_returns_minimum_nu() {
        const double Nu =
            Convection::local_nusselt(0.0, 0.71, /*Gr=*/0.0, true);
        assert(Nu == 1.0 &&
               "zero velocity and zero deltaT should hit the floor, not blow up/NaN");
        std::cout << "test_no_driving_force_returns_minimum_nu PASSED\n";
    }

    void test_reynolds_scales_linearly_with_velocity() {
        const double Re1 =
            Convection::local_reynolds(1.0, 0.05, 1.2, 1.8e-5);
        const double Re2 =
            Convection::local_reynolds(2.0, 0.05, 1.2, 1.8e-5);

        assert(nearly_equal(Re2, 2.0 * Re1) &&
               "doubling velocity should double Reynolds number");
        std::cout << "test_reynolds_scales_linearly_with_velocity PASSED\n";
    }

    void test_invalid_reynolds_inputs_return_zero() {
        const double Re_zero_mu =
            Convection::local_reynolds(1.0, 0.05, 1.2, 0.0);
        const double Re_zero_length =
            Convection::local_reynolds(1.0, 0.0, 1.2, 1.8e-5);

        assert(Re_zero_mu == 0.0);
        assert(Re_zero_length == 0.0);
        std::cout << "test_invalid_reynolds_inputs_return_zero PASSED\n";
    }

    void test_laminar_regime_returns_expected_nusselt() {
        const double Re = 1000.0;
        const double Nu = Convection::local_nusselt(Re, 0.71, 0.0, true);

        assert(Re >= Convection::RE_NATURAL_THRESHOLD);
        assert(Re < Convection::RE_TURBULENT);
        assert(nearly_equal(Nu, 4.36) &&
               "current laminar model should return Nu = 4.36");
        std::cout << "test_laminar_regime_returns_expected_nusselt PASSED\n";
    }

    void test_local_h_matches_nusselt_definition() {
        const double Nu = 10.0;
        const double k_air = 0.026;
        const double length = 0.05;
        const double expected_h = Nu * k_air / length;
        const double h = Convection::local_h(Nu, k_air, length);

        assert(nearly_equal(h, expected_h));
        assert(Convection::local_h(Nu, k_air, 0.0) == 0.0);
        std::cout << "test_local_h_matches_nusselt_definition PASSED\n";
    }

    void test_convection_result_is_finite() {
        const double h = Convection::compute_local_h(
            2.0, 0.05, 1.2, 1.8e-5, 0.026, 0.71, 25.0, 305.0);

        assert(std::isfinite(h));
        assert(h > 0.0);
        std::cout << "test_convection_result_is_finite PASSED (h="
                  << h << ")\n";
    }

    void test_vent_area_uses_plane_dimensions() {
        Vent vent_y_normal(
            "XZ vent", {0.10, 0.0, 0.20}, 0.50, 0.0, 0.5,
            {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, VentShapeType::Rectangular);

        assert(nearly_equal(vent_y_normal.gross_area(), 0.02));
        assert(nearly_equal(vent_y_normal.free_area(), 0.01));

        Vent vent_z_normal(
            "XY vent", {0.10, 0.20, 0.0}, 0.25, 0.0, 0.5,
            {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, VentShapeType::Rectangular);

        assert(nearly_equal(vent_z_normal.gross_area(), 0.02));
        assert(nearly_equal(vent_z_normal.free_area(), 0.005));
        std::cout << "test_vent_area_uses_plane_dimensions PASSED\n";
    }

    void test_vent_free_area_ratio_validation() {
        bool threw = false;
        try {
            Vent invalid(
                "Invalid vent", {0.10, 0.0, 0.10}, 1.2, 0.0, 0.5,
                {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0},  VentShapeType::Rectangular);
        } catch (const std::invalid_argument&) {
            threw = true;
        }

        assert(threw && "free-area ratio outside [0,1] should throw");
        std::cout << "test_vent_free_area_ratio_validation PASSED\n";
    }

    void test_component_stamping_sets_material_and_qdot() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(100'00, 1'000'000, 100'000, 4);
        Rack rack = make_air_rack(0.10, 0.10, 0.10, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, env, load);

        Component component(0.05, 0.05, 0.05, "Test block");
        component.set_coords_m(0.0, 0.0, 0.0);
        component.set_t(35.0);
        component.set_k_solid(15.0);
        component.set_rho_solid(1000.0);
        component.set_cp(500.0);
        component.set_watts(10.0);
        mesh.stamp_component(component);

        const Cell& cell = mesh.at(0, 0, 0);
        assert(cell.is_solid());
        assert(nearly_equal(cell.get_T(), 35.0));
        assert(nearly_equal(cell.get_k(), 15.0));
        assert(nearly_equal(cell.get_rho(), 1000.0));
        assert(nearly_equal(cell.get_cp(), 500.0));
        assert(nearly_equal(cell.get_qdot(), component.watt_density()));
        std::cout << "test_component_stamping_sets_material_and_qdot PASSED\n";
    }

    void test_heat_generation_increases_temperature() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(100'00, 1'000'000, 100'000, 4);
        Rack rack = make_air_rack(0.10, 0.10, 0.10, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, env, load);

        Component component(0.05, 0.05, 0.05, "Heater");
        component.set_coords_m(0.0, 0.0, 0.0);
        component.set_t(20.0);
        component.set_k_solid(1.0);
        component.set_rho_solid(1000.0);
        component.set_cp(1000.0);
        component.set_watts(20.0);
        mesh.stamp_component(component);

        FlowSolver flow_solver(mesh);
        flow_solver.solve();

        Solver solver(mesh, /*dt=*/0.01, /*sim_length=*/0.02, false, 1);
        solver.solve();

        const double final_temperature = solver.get_mesh().at(0, 0, 0).get_T();
        assert(final_temperature > 20.0 &&
               "positive component qdot should increase component temperature");
        std::cout << "test_heat_generation_increases_temperature PASSED (T="
                  << final_temperature << ")\n";
    }

    void test_flow_solver_keeps_stagnant_rack_at_zero_velocity() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(100'00, 1'000'000, 100'000, 4);
        Rack rack = make_air_rack(0.10, 0.10, 0.10, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, env, load);

        FlowSolver flow_solver(mesh);
        flow_solver.solve();

        for (const Cell& cell : mesh.get_cells()) {
            assert(nearly_equal(cell.get_vx(), 0.0));
            assert(nearly_equal(cell.get_vy(), 0.0));
            assert(nearly_equal(cell.get_vz(), 0.0));
            assert(nearly_equal(cell.get_pressure(), 0.0));
        }
        std::cout << "test_flow_solver_keeps_stagnant_rack_at_zero_velocity PASSED\n";
    }

    void test_flow_solver_creates_nonzero_fan_to_vent_flow() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(100'00, 1'000'000, 100'000, 4);
        Rack rack = make_air_rack(0.10, 0.10, 0.20, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, env, load);

        Fan intake(
            "Intake", 1.0, 0.0,
            {0.0, 0.10, 0.10},
            {0.0, 0.05, 0.05},
            {1.0, 0.0, 0.0},
            FlowType::Intake,
            ShapeType::Rectangular);

        Vent outlet(
            "Outlet", {0.0, 0.10, 0.10}, 1.0, 0.0, 0.5,
            {0.10, 0.05, 0.05},
            {1.0, 0.0, 0.0},  VentShapeType::Rectangular);
        
        mesh.stamp_fan(intake);
        mesh.stamp_vent(outlet);

        FlowSolver flow_solver(
            mesh,
            /*linear_resistivity=*/4.5,
            /*pressure_tolerance=*/1e-10,
            /*max_pressure_iters=*/20000,
            /*sor_omega=*/1.1,
            /*max_outer_iters=*/60,
            /*flow_tolerance=*/1e-3);
        flow_solver.solve();

        double max_speed = 0.0;
        bool positive_pressure_found = false;
        for (const Cell& cell : mesh.get_cells()) {
            const double speed = std::sqrt(
                cell.get_vx() * cell.get_vx() +
                cell.get_vy() * cell.get_vy() +
                cell.get_vz() * cell.get_vz());
            max_speed = std::max(max_speed, speed);
            positive_pressure_found =
                positive_pressure_found || cell.get_pressure() > 0.0;
        }

        assert(max_speed > 0.0 &&
               "fan and vent should produce a nonzero velocity field");
        assert(positive_pressure_found &&
               "an intake source should create positive internal pressure relative to ambient");
        std::cout << "test_flow_solver_creates_nonzero_fan_to_vent_flow PASSED (max speed="
                  << max_speed << ")\n";
    }

    void test_flow_solver_rejects_source_without_vent() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(100'00, 1'000'000, 100'000, 4);
        Rack rack = make_air_rack(0.10, 0.10, 0.10, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, env, load);

        Fan intake(
            "Unvented intake", 1.0, 0.0,
            {0.0, 0.10, 0.10},
            {0.0, 0.05, 0.05},
            {1.0, 0.0, 0.0},
            FlowType::Intake,
            ShapeType::Rectangular);
        mesh.stamp_fan(intake);

        bool threw = false;
        try {
            FlowSolver flow_solver(mesh);
            flow_solver.solve();
        } catch (const std::runtime_error&) {
            threw = true;
        }

        assert(threw && "fan source without a pressure-reference vent should throw");
        std::cout << "test_flow_solver_rejects_source_without_vent PASSED\n";
    }

    std::array<bool, 3> test_all_mesh_values(const Mesh& m,
                                              double expected_T,
                                              double expected_v,
                                              double expected_qdot) const {
        bool T_passed = true;
        bool qdot_passed = true;
        bool v_passed = true;

        for (int x = 0; x < m.get_nx(); x++) {
            for (int y = 0; y < m.get_ny(); y++) {
                for (int z = 0; z < m.get_nz(); z++) {
                    if (!m.in_bounds(x, y, z)) continue;
                    const Cell& cell = m.at(x, y, z);
                    if (!nearly_equal(cell.get_T(), expected_T)) T_passed = false;
                    if (!nearly_equal(cell.get_vx(), expected_v)) v_passed = false;
                    if (!nearly_equal(cell.get_vy(), expected_v)) v_passed = false;
                    if (!nearly_equal(cell.get_vz(), expected_v)) v_passed = false;
                    if (!nearly_equal(cell.get_qdot(), expected_qdot)) qdot_passed = false;
                }
            }
        }
        return {T_passed, v_passed, qdot_passed};
    }

    void test_internal_air_region_stamps_as_air() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(100'00, 1'000'000, 100'000, 4);
        Rack rack = make_air_rack(0.10, 0.10, 0.10, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.01, 0.01, 0.01, env, load);
        Component component = make_test_component();

        InternalRegion air_region(
            "air",
            /*size=*/{0.02, 0.02, 0.02},
            /*local position=*/{0.01, 0.01, 0.01}
        );

        component.add_region(air_region);
        component.order_internal_regions();
        mesh.stamp_component(component);

        int air_region_cells = 0;
        int solid_component_cells = 0;

        // Component globally occupies indices [2, 6)
        for(int i = 2; i < 6; ++i) {
            for(int j = 2; j < 6; ++j) {
                for(int k = 2; k < 6; ++k) {
                    const Cell& cell = mesh.at(i, j, k);

                    // Internal air region occupies indices [3, 5)
                    const bool inside_air_region =
                        i >= 3 && i < 5 &&
                        j >= 3 && j < 5 &&
                        k >= 3 && k < 5;

                    if(inside_air_region) {
                        assert(cell.get_state() == Cell::State::Air);
                        assert(nearly_equal(
                            cell.get_T(),
                            env.get_T_ambient()
                        ));
                        assert(nearly_equal(
                            cell.get_rho(),
                            env.get_rho()
                        ));
                        assert(nearly_equal(
                            cell.get_cp(),
                            env.get_cp()
                        ));
                        assert(nearly_equal(
                            cell.get_k(),
                            env.get_k()
                        ));
                        assert(nearly_equal(
                            cell.get_qdot(),
                            0.0
                        ));

                        ++air_region_cells;
                    } else {
                        assert(
                            cell.get_state() ==
                            Cell::State::Component
                        );

                        ++solid_component_cells;
                    }
                }
            }
        }

        assert(air_region_cells == 8);
        assert(solid_component_cells == 56);

        std::cout
            << "test_internal_air_region_stamps_as_air PASSED\n";
    }


    // ============================================================
    // INTERNAL HEAT-SOURCE TESTS
    // ============================================================

    void test_internal_heat_source_stamps_properties() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(100'00, 1'000'000, 100'000, 4);
        Rack rack = make_air_rack(0.10, 0.10, 0.10, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.01, 0.01, 0.01, env, load);
        Component component = make_test_component();

        constexpr double source_cp = 500.0;
        constexpr double source_rho = 8000.0;
        constexpr double source_k = 15.0;
        constexpr double source_watts = 80.0;

        InternalRegion heat_source(
            "heat",
            /*size=*/{0.02, 0.02, 0.02},
            /*local position=*/{0.01, 0.01, 0.01},
            source_cp,
            source_rho,
            source_k,
            source_watts
        );

        component.add_region(heat_source);
        component.order_internal_regions();
        mesh.stamp_component(component);

        int heat_source_cells = 0;

        for(int i = 3; i < 5; ++i) {
            for(int j = 3; j < 5; ++j) {
                for(int k = 3; k < 5; ++k) {
                    const Cell& cell = mesh.at(i, j, k);

                    assert(
                        cell.get_state() ==
                        Cell::State::Component
                    );

                    assert(nearly_equal(
                        cell.get_cp(),
                        source_cp
                    ));

                    assert(nearly_equal(
                        cell.get_rho(),
                        source_rho
                    ));

                    assert(nearly_equal(
                        cell.get_k(),
                        source_k
                    ));

                    assert(cell.get_qdot() > 0.0);

                    ++heat_source_cells;
                }
            }
        }

        assert(heat_source_cells == 8);

        std::cout
            << "test_internal_heat_source_stamps_properties PASSED\n";
    }


    void test_internal_heat_source_conserves_total_watts() {
        Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
        Workload load(100'00, 1'000'000, 100'000, 4);
        Rack rack = make_air_rack(0.10, 0.10, 0.10, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.01, 0.01, 0.01, env, load);
        Component component = make_test_component();

        constexpr double requested_watts = 80.0;

        InternalRegion heat_source(
            "heat",
            {0.02, 0.02, 0.02},
            {0.01, 0.01, 0.01},
            /*cp=*/500.0,
            /*rho=*/8000.0,
            /*k=*/15.0,
            requested_watts
        );

        component.add_region(heat_source);
        component.order_internal_regions();
        mesh.stamp_component(component);

        double integrated_watts = 0.0;

        for(int i = 3; i < 5; ++i) {
            for(int j = 3; j < 5; ++j) {
                for(int k = 3; k < 5; ++k) {
                    integrated_watts +=
                        mesh.at(i, j, k).get_qdot() *
                        mesh.cell_volume();
                }
            }
        }

        assert(
            nearly_equal(
                integrated_watts,
                requested_watts
            ) &&
            "Internal heat-source qdot must integrate "
            "to the requested wattage."
        );

        std::cout
            << "test_internal_heat_source_conserves_total_watts "
            << "PASSED (integrated watts = "
            << integrated_watts << ")\n";
    }


    // ============================================================
    // LOCAL/GLOBAL POSITION TESTS
    // ============================================================

    void test_internal_region_local_to_global_position() {
        Component component = make_test_component();
        // 0.02, 0.02, 0.02 bot left corner
        // size 0.04, 0.04, 0.04
        InternalRegion region(
            "air",
            /*size=*/{0.01, 0.01, 0.01},
            /*local position=*/{0.01, 0.02, 0.03}
        );

        component.add_region(region);

        const std::vector<InternalRegion> regions =
            component.get_regions();

        assert(regions.size() == 1);

        const auto global =
            regions.front().get_global_position();

        assert(nearly_equal(global[0], 0.03));
        assert(nearly_equal(global[1], 0.04));
        assert(nearly_equal(global[2], 0.05));

        std::cout
            << "test_internal_region_local_to_global_position "
            << "PASSED\n";
    }


    void test_internal_region_exact_bounds_allowed() {
        Component component = make_test_component();

        bool threw = false;

        try {
            InternalRegion region(
                "air",
                /*size=*/{0.02, 0.02, 0.02},
                /*local position=*/{0.02, 0.02, 0.02}
            );

            component.add_region(region);
        }
        catch(const std::invalid_argument&) {
            threw = true;
        }

        assert(
            !threw &&
            "A region ending exactly at the component boundary "
            "should be accepted."
        );

        std::cout
            << "test_internal_region_exact_bounds_allowed PASSED\n";
    }


    // ============================================================
    // NEGATIVE LOCAL COORDINATE TESTS
    // ============================================================

    void test_internal_region_negative_x_rejected() {
        Component component = make_test_component();

        bool threw = false;

        try {
            InternalRegion region(
                "air",
                {0.01, 0.01, 0.01},
                {-0.001, 0.01, 0.01}
            );

            component.add_region(region);
        }
        catch(const std::invalid_argument&) {
            threw = true;
        }

        assert(
            threw &&
            "Negative internal-region x must be rejected."
        );

        std::cout
            << "test_internal_region_negative_x_rejected PASSED\n";
    }


    void test_internal_region_negative_y_rejected() {
        Component component = make_test_component();

        bool threw = false;

        try {
            InternalRegion region(
                "air",
                {0.01, 0.01, 0.01},
                {0.01, -0.001, 0.01}
            );

            component.add_region(region);
        }
        catch(const std::invalid_argument&) {
            threw = true;
        }

        assert(
            threw &&
            "Negative internal-region y must be rejected."
        );

        std::cout
            << "test_internal_region_negative_y_rejected PASSED\n";
    }


    void test_internal_region_negative_z_rejected() {
        Component component = make_test_component();

        bool threw = false;

        try {
            InternalRegion region(
                "air",
                {0.01, 0.01, 0.01},
                {0.01, 0.01, -0.001}
            );

            component.add_region(region);
        }
        catch(const std::invalid_argument&) {
            threw = true;
        }

        assert(
            threw &&
            "Negative internal-region z must be rejected."
        );

        std::cout
            << "test_internal_region_negative_z_rejected PASSED\n";
    }


    // ============================================================
    // POSITIVE BOUNDS OVERFLOW TESTS
    // ============================================================

    void test_internal_region_x_overflow_rejected() {
        Component component = make_test_component();

        bool threw = false;

        try {
            InternalRegion region(
                "air",
                {0.02, 0.01, 0.01},
                {0.021, 0.01, 0.01}
            );

            component.add_region(region);
        }
        catch(const std::invalid_argument& e) {
            threw = true;

            assert(
                std::string(e.what()).find("x bounds") !=
                std::string::npos
            );
        }

        assert(
            threw &&
            "An x-overflowing region must be rejected."
        );

        std::cout
            << "test_internal_region_x_overflow_rejected PASSED\n";
    }


    void test_internal_region_y_overflow_rejected() {
        Component component = make_test_component();

        bool threw = false;

        try {
            InternalRegion region(
                "air",
                {0.01, 0.02, 0.01},
                {0.01, 0.021, 0.01}
            );

            component.add_region(region);
        }
        catch(const std::invalid_argument& e) {
            threw = true;

            assert(
                std::string(e.what()).find("y bounds") !=
                std::string::npos
            );
        }

        assert(
            threw &&
            "A y-overflowing region must be rejected."
        );

        std::cout
            << "test_internal_region_y_overflow_rejected PASSED\n";
    }


    void test_internal_region_z_overflow_rejected() {
        Component component = make_test_component();

        bool threw = false;

        try {
            InternalRegion region(
                "air",
                {0.01, 0.01, 0.02},
                {0.01, 0.01, 0.021}
            );

            component.add_region(region);
        }
        catch(const std::invalid_argument& e) {
            threw = true;

            assert(
                std::string(e.what()).find("z bounds") !=
                std::string::npos
            );
        }

        assert(
            threw &&
            "A z-overflowing region must be rejected."
        );

        std::cout
            << "test_internal_region_z_overflow_rejected PASSED\n";
    }


    // ============================================================
    // INTERNAL REGION VALIDATION AND ORDERING
    // ============================================================

    void test_uninitialized_internal_region_rejected_when_ordering() {
        Component component = make_test_component();

        InternalRegion uninitialized_region;

        component.add_region(uninitialized_region);

        bool threw = false;

        try {
            component.order_internal_regions();
        }
        catch(const std::invalid_argument&) {
            threw = true;
        }

        assert(
            threw &&
            "An uninitialized region must be rejected."
        );

        std::cout
            << "test_uninitialized_internal_region_rejected_"
            << "when_ordering PASSED\n";
    }


    void test_internal_region_ordering() {
        Component component = make_test_component();

        InternalRegion heat_source(
            "heat",
            {0.01, 0.01, 0.01},
            {0.02, 0.02, 0.02},
            /*cp=*/500.0,
            /*rho=*/8000.0,
            /*k=*/15.0,
            /*watts=*/20.0
        );

        InternalRegion air_region(
            "air",
            {0.01, 0.01, 0.01},
            {0.01, 0.01, 0.01}
        );

        // Deliberately add in the wrong order.
        component.add_region(heat_source);
        component.add_region(air_region);

        component.order_internal_regions();

        const auto regions = component.get_regions();

        assert(regions.size() == 2);

        assert(
            regions[0].get_region_type() ==
            RegionType::Air
        );

        assert(
            regions[1].get_region_type() ==
            RegionType::HeatSource
        );

        std::cout
            << "test_internal_region_ordering PASSED\n";
    }


    // ============================================================
    // INTERNAL VENT TESTS
    // ============================================================

    void test_valid_internal_vent_on_component_face() {
        Component component(0.04, 0.04, 0.04, "Vent test component");

        component.set_coords_m(0.0, 0.0, 0.0);

        InternalRegion vent(
            "vent",
            /*size=*/{0.02, 0.0, 0.02},
            /*local position=*/{0.01, 0.0, 0.01},
            /*direction=*/{0.0, 1.0, 0.0},
            /*free area ratio=*/0.60,
            /*discharge coefficient=*/0.65
        );

        bool threw = false;

        try {
            component.add_region(vent);
        }
        catch(const std::invalid_argument&) {
            threw = true;
        }

        assert(
            !threw &&
            "A valid internal vent on one component face "
            "should be accepted."
        );

        const auto regions = component.get_regions();

        assert(regions.size() == 1);

        const InternalRegion& stored = regions.front();

        assert(
            stored.get_region_type() ==
            RegionType::Vent
        );

        assert(nearly_equal(stored.get_far(), 0.60));
        assert(nearly_equal(stored.get_cd(), 0.65));

        const double expected_free_area =
            0.02 * 0.02 * 0.60;

        assert(nearly_equal(
            stored.free_area(),
            expected_free_area
        ));

        std::cout
            << "test_valid_internal_vent_on_component_face PASSED\n";
    }


    void test_internal_vent_not_on_face_rejected() {
        Component component(0.04, 0.04, 0.04, "Vent test component");

        component.set_coords_m(0.0, 0.0, 0.0);

        InternalRegion vent(
            "vent",
            /*size=*/{0.02, 0.0, 0.02},
            /*local position=*/{0.01, 0.02, 0.01},
            /*direction=*/{0.0, 1.0, 0.0},
            /*free area ratio=*/0.60,
            /*discharge coefficient=*/0.65
        );

        bool threw = false;

        try {
            component.add_region(vent);
        }
        catch(const std::invalid_argument& e) {
            threw = true;

            assert(
                std::string(e.what()).find(
                    "does not intercept any component face"
                ) != std::string::npos
            );
        }

        assert(
            threw &&
            "An internal vent not touching a component face "
            "must be rejected."
        );

        std::cout
            << "test_internal_vent_not_on_face_rejected PASSED\n";
    }


    void test_internal_vent_touching_two_faces_rejected() {
        Component component(0.04, 0.04, 0.04, "Vent test component");

        component.set_coords_m(0.0, 0.0, 0.0);

        InternalRegion vent(
            "vent",
            /*size=*/{0.02, 0.0, 0.02},
            /*local position=*/{0.0, 0.0, 0.01},
            /*direction=*/{0.0, 1.0, 0.0},
            /*free area ratio=*/0.60,
            /*discharge coefficient=*/0.65
        );

        bool threw = false;

        try {
            component.add_region(vent);
        }
        catch(const std::invalid_argument& e) {
            threw = true;

            assert(
                std::string(e.what()).find(
                    "intercepts more than 1 face"
                ) != std::string::npos
            );
        }

        assert(
            threw &&
            "A vent intersecting multiple component faces "
            "must be rejected."
        );

        std::cout
            << "test_internal_vent_touching_two_faces_rejected "
            << "PASSED\n";
    }


    void test_internal_vent_free_area_ratio_bounds() {
        bool low_threw = false;
        bool high_threw = false;

        try {
            InternalRegion vent(
                "vent",
                {0.02, 0.0, 0.02},
                {0.01, 0.0, 0.01},
                {0.0, 1.0, 0.0},
                /*invalid FAR=*/-0.01,
                /*Cd=*/0.65
            );
        }
        catch(const std::invalid_argument&) {
            low_threw = true;
        }

        try {
            InternalRegion vent(
                "vent",
                {0.02, 0.0, 0.02},
                {0.01, 0.0, 0.01},
                {0.0, 1.0, 0.0},
                /*invalid FAR=*/1.01,
                /*Cd=*/0.65
            );
        }
        catch(const std::invalid_argument&) {
            high_threw = true;
        }

        assert(
            low_threw &&
            "A negative vent free-area ratio must be rejected."
        );

        assert(
            high_threw &&
            "A vent free-area ratio greater than one "
            "must be rejected."
        );

        std::cout
            << "test_internal_vent_free_area_ratio_bounds PASSED\n";
    }


    void test_internal_vent_free_area_ratio_endpoints_allowed() {
        bool threw = false;

        try {
            InternalRegion fully_closed(
                "vent",
                {0.02, 0.0, 0.02},
                {0.01, 0.0, 0.01},
                {0.0, 1.0, 0.0},
                /*FAR=*/0.0,
                /*Cd=*/0.65
            );

            InternalRegion fully_open(
                "vent1",
                {0.02, 0.0, 0.02},
                {0.01, 0.0, 0.01},
                {0.0, 1.0, 0.0},
                /*FAR=*/1.0,
                /*Cd=*/0.65
            );
        }
        catch(const std::invalid_argument&) {
            threw = true;
        }

        assert(
            !threw &&
            "Free-area-ratio endpoints zero and one "
            "should be accepted by the current validation."
        );

        std::cout
            << "test_internal_vent_free_area_ratio_"
            << "endpoints_allowed PASSED\n";
    }


    // ============================================================
    // INTERNAL FAN TESTS
    // ============================================================

    void test_valid_internal_rectangular_fan_on_face() {
        Component component(0.04, 0.04, 0.04, "Fan test component");

        component.set_coords_m(0.0, 0.0, 0.0);

        InternalRegion fan(
            "fan",
            /*size=*/{0.02, 0.0, 0.02},
            /*local position=*/{0.01, 0.0, 0.01},
            /*face direction=*/{0.0, 1.0, 0.0},
            /*velocity direction=*/{0.0, 1.0, 0.0},
            /*diameter=*/0.0,
            /*cfm=*/10.0,
            FlowType::Intake,
            ShapeType::Rectangular
        );

        component.add_region(fan);

        const auto regions = component.get_regions();

        assert(regions.size() == 1);

        const InternalRegion& stored = regions.front();

        assert(
            stored.get_region_type() ==
            RegionType::Fan
        );

        assert(
            stored.get_shape_type() ==
            ShapeType::Rectangular
        );

        assert(
            stored.get_flow_type() ==
            FlowType::Intake
        );

        assert(nearly_equal(
            stored.get_cfm(),
            10.0
        ));

        const auto velocity_direction =
            stored.get_velocity_direction();

        assert(nearly_equal(
            velocity_direction[0],
            0.0
        ));

        assert(nearly_equal(
            velocity_direction[1],
            1.0
        ));

        assert(nearly_equal(
            velocity_direction[2],
            0.0
        ));

        std::cout
            << "test_valid_internal_rectangular_fan_on_face "
            << "PASSED\n";
    }


    void test_internal_rectangular_fan_with_diameter_rejected() {
        bool threw = false;

        try {
            InternalRegion fan(
                "fan",
                {0.02, 0.0, 0.02},
                {0.01, 0.0, 0.01},
                {0.0, 1.0, 0.0},
                {0.0, 1.0, 0.0},
                /*invalid diameter=*/0.01,
                /*cfm=*/10.0,
                FlowType::Intake,
                ShapeType::Rectangular
            );
        }
        catch(const std::invalid_argument& e) {
            threw = true;

            assert(
                std::string(e.what()).find(
                    "rectangular fan has diameter defined"
                ) != std::string::npos
            );
        }

        assert(
            threw &&
            "A rectangular fan with a diameter must be rejected."
        );

        std::cout
            << "test_internal_rectangular_fan_with_diameter_"
            << "rejected PASSED\n";
    }


    void test_internal_fan_negative_cfm_rejected() {
        bool threw = false;

        try {
            InternalRegion fan(
                "fan",
                {0.02, 0.0, 0.02},
                {0.01, 0.0, 0.01},
                {0.0, 1.0, 0.0},
                {0.0, 1.0, 0.0},
                /*diameter=*/0.0,
                /*invalid CFM=*/-1.0,
                FlowType::Intake,
                ShapeType::Rectangular
            );
        }
        catch(const std::invalid_argument&) {
            threw = true;
        }

        assert(
            threw &&
            "Negative internal-fan CFM must be rejected."
        );

        std::cout
            << "test_internal_fan_negative_cfm_rejected PASSED\n";
    }


    void test_internal_fan_cfm_conversion() {
        InternalRegion fan(
            "fan",
            {0.02, 0.0, 0.02},
            {0.01, 0.0, 0.01},
            {0.0, 1.0, 0.0},
            {0.0, 1.0, 0.0},
            /*diameter=*/0.0,
            /*cfm=*/10.0,
            FlowType::Intake,
            ShapeType::Rectangular
        );

        const double expected_flow =
            10.0 * 0.00047194745;

        assert(nearly_equal(
            fan.flow_m3s(),
            expected_flow
        ));

        std::cout
            << "test_internal_fan_cfm_conversion PASSED\n";
    }


    void test_internal_rectangular_fan_area() {
        InternalRegion fan(
            "fan",
            {0.02, 0.0, 0.03},
            {0.01, 0.0, 0.005},
            {0.0, 1.0, 0.0},
            {0.0, 1.0, 0.0},
            /*diameter=*/0.0,
            /*cfm=*/10.0,
            FlowType::Intake,
            ShapeType::Rectangular
        );

        const double expected_area =
            0.02 * 0.03;

        assert(nearly_equal(
            fan.area(),
            expected_area
        ));

        std::cout
            << "test_internal_rectangular_fan_area PASSED\n";
    }


    void test_internal_fan_uses_velocity_direction() {
        InternalRegion fan(
            "fan",
            {0.02, 0.0, 0.02},
            {0.01, 0.0, 0.01},

            // Geometric face normal
            {0.0, 1.0, 0.0},

            // Actual airflow direction
            {0.0, -1.0, 0.0},

            /*diameter=*/0.0,
            /*cfm=*/10.0,
            FlowType::Exhaust,
            ShapeType::Rectangular
        );

        assert(nearly_equal(
            fan.velocity_x(),
            0.0
        ));

        assert(
            fan.velocity_y() < 0.0 &&
            "Fan velocity should follow velocity_direction."
        );

        assert(nearly_equal(
            fan.velocity_z(),
            0.0
        ));

        std::cout
            << "test_internal_fan_uses_velocity_direction PASSED\n";
    }


    // ============================================================
    // OFFSET COMPONENT FAN/VENT TEST
    // ============================================================

    void test_internal_vent_on_offset_component_face() {
        Component component(0.04, 0.04, 0.04, "Offset component");

        component.set_coords_m(
            0.20,
            0.30,
            0.40
        );

        InternalRegion vent(
            "fan",
            {0.02, 0.0, 0.02},
            {0.01, 0.0, 0.01},
            {0.0, 1.0, 0.0},
            /*free-area ratio=*/0.60,
            /*Cd=*/0.65
        );

        bool threw = false;

        try {
            component.add_region(vent);
        }
        catch(const std::invalid_argument&) {
            threw = true;
        }

        assert(
            !threw &&
            "A valid vent on an offset component face "
            "must be accepted."
        );

        const auto regions = component.get_regions();

        assert(regions.size() == 1);

        const auto global =
            regions.front().get_global_position();

        assert(nearly_equal(global[0], 0.21));
        assert(nearly_equal(global[1], 0.30));
        assert(nearly_equal(global[2], 0.41));

        std::cout
            << "test_internal_vent_on_offset_component_face "
            << "PASSED\n";
    }

    double total_fan_curve_flow(const Mesh& m) const {
        double total = 0.0;
        for (int x = 0; x < m.get_nx(); ++x)
            for (int y = 0; y < m.get_ny(); ++y)
                for (int z = 0; z < m.get_nz(); ++z) {
                    const Cell& c = m.at(x, y, z);
                    if (c.has_fan_curve()) total += c.get_fan_Q_ref();
                }
        return total;
    }

    void test_fan_curve_throttles_vs_fixed_cfm_under_restrictive_vent() {
        auto build_case = [](bool use_curve) {
            Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
            Workload load(100'00, 1'000'000, 100'000, 4);
            Rack rack = Rack::from_meters(0.10, 0.10, 0.20, "rack");
            rack.set_t(20.0); rack.set_cp(1005.0); rack.set_k(0.02587); rack.set_rho(1.225);
            Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, env, load);

            const double cfm = 20.0;
            Fan intake(
                "Intake", cfm, 0.0,
                {0.0, 0.10, 0.10}, {0.0, 0.05, 0.05}, {1.0, 0.0, 0.0},
                FlowType::Intake, ShapeType::Rectangular);

            const double Q_free = intake.flow_m3s();
            if (use_curve) {
                intake.set_curve(/*a=*/50.0, /*b=*/50.0 / Q_free, /*c=*/0.0, /*rho_rated=*/1.2);
            }

            // Deliberately restrictive vent -> forces meaningful backpressure.
            Vent outlet("Outlet", {0.0, 0.10, 0.10}, /*far=*/0.02, 0.0, /*cd=*/0.5,
                        {0.10, 0.05, 0.05}, {1.0, 0.0, 0.0},  VentShapeType::Rectangular);

            mesh.stamp_fan(intake);
            mesh.stamp_vent(outlet);

            FlowSolver flow_solver(mesh, 4.5, 1e-10, 20000, 1.1, 60, 1e-3);
            flow_solver.solve();

            return std::make_pair(mesh, Q_free);
        };

        auto [mesh_fixed, Q_free_a] = build_case(false);
        auto [mesh_curve, Q_free_b] = build_case(true);

        double total_fixed_source = 0.0;
        for (const Cell& c : mesh_fixed.get_cells()) {
            if (c.is_intake()) total_fixed_source += std::abs(c.get_flow_source());
        }
        assert(nearly_equal(total_fixed_source, Q_free_a, 1e-9, 1e-6) &&
            "fixed-CFM fan should inject its full rated flow regardless of backpressure");

        double total_curve_flow = total_fan_curve_flow(mesh_curve);
        assert(total_curve_flow < 0.85 * Q_free_b &&
            "curve-enabled fan should be throttled below rated CFM by a restrictive vent");
        assert(total_curve_flow > 0.0 && "throttled fan should still deliver nonzero flow");

        std::cout << "test_fan_curve_throttles_vs_fixed_cfm_under_restrictive_vent PASSED "
                << "(fixed=" << total_fixed_source << ", curve=" << total_curve_flow
                << ", free-air=" << Q_free_a << ")\n";
    }

    void test_fan_curve_flow_increases_with_less_restrictive_vent() {
        auto build_case = [](double free_area_ratio, double cd) {
            Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
            Workload load(100'00, 1'000'000, 100'000, 4);
            Rack rack = Rack::from_meters(0.10, 0.10, 0.20, "rack");
            rack.set_t(20.0); rack.set_cp(1005.0); rack.set_k(0.02587); rack.set_rho(1.225);
            Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, env, load);

            const double cfm = 20.0;
            Fan intake(
                "Intake", cfm, 0.0,
                {0.0, 0.10, 0.10}, {0.0, 0.05, 0.05}, {1.0, 0.0, 0.0},
                FlowType::Intake, ShapeType::Rectangular);

            const double Q_free = intake.flow_m3s();
            intake.set_curve(50.0, 50.0 / Q_free, 0.0, 1.2);

            Vent outlet("Outlet", {0.0, 0.10, 0.10}, free_area_ratio, 0.0, cd,
                        {0.10, 0.05, 0.05}, {1.0, 0.0, 0.0}, VentShapeType::Rectangular);

            mesh.stamp_fan(intake);
            mesh.stamp_vent(outlet);

            FlowSolver flow_solver(mesh, 4.5, 1e-10, 20000, 1.1, 60, 1e-3);
            flow_solver.solve();
            return mesh;
        };

        Mesh restrictive = build_case(/*far=*/0.02, /*cd=*/0.5);
        Mesh open        = build_case(/*far=*/0.95, /*cd=*/0.9);

        const double flow_restrictive = total_fan_curve_flow(restrictive);
        const double flow_open = total_fan_curve_flow(open);

        assert(flow_open > flow_restrictive &&
            "a less-restrictive vent should let the fan deliver more flow");

        std::cout << "test_fan_curve_flow_increases_with_less_restrictive_vent PASSED "
                << "(restrictive=" << flow_restrictive << ", open=" << flow_open << ")\n";
    }

    void test_fan_curve_density_sensitivity() {
        auto build_case = [](double fan_cell_rho) {
            Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
            Workload load(100'00, 1'000'000, 100'000, 4);
            Rack rack = Rack::from_meters(0.10, 0.10, 0.20, "rack");
            rack.set_t(20.0); rack.set_cp(1005.0); rack.set_k(0.02587); rack.set_rho(1.225);
            Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, env, load);

            const double cfm = 20.0;
            Fan intake(
                "Intake", cfm, 0.0,
                {0.0, 0.10, 0.10}, {0.0, 0.05, 0.05}, {1.0, 0.0, 0.0},
                FlowType::Intake, ShapeType::Rectangular);

            const double Q_free = intake.flow_m3s();
            intake.set_curve(50.0, 50.0 / Q_free, 0.0, /*rho_rated=*/1.2);

            // Moderate restriction so there's headroom for density to matter.
            Vent outlet("Outlet", {0.0, 0.10, 0.10}, /*far=*/0.15, 0.0, /*cd=*/0.6,
                        {0.10, 0.05, 0.05}, {1.0, 0.0, 0.0}, VentShapeType::Rectangular);

            mesh.stamp_fan(intake);
            mesh.stamp_vent(outlet);

            // Isolate the fan curve's own rho_ratio scaling: only override
            // density on the fan cells themselves, leaving the rest of the
            // network's rho (friction, vent conductance) untouched.
            for (int x = 0; x < mesh.get_nx(); ++x)
                for (int y = 0; y < mesh.get_ny(); ++y)
                    for (int z = 0; z < mesh.get_nz(); ++z) {
                        Cell& c = mesh.at(x, y, z);
                        if (c.has_fan_curve()) c.set_rho(fan_cell_rho);
                    }

            FlowSolver flow_solver(mesh, 4.5, 1e-10, 20000, 1.1, 60, 1e-3);
            flow_solver.solve();
            return mesh;
        };

        Mesh cool_dense = build_case(/*rho=*/1.2);   // matches rho_rated -> no derating
        Mesh hot_thin   = build_case(/*rho=*/0.85);  // hot, less-dense air

        const double flow_dense = total_fan_curve_flow(cool_dense);
        const double flow_thin  = total_fan_curve_flow(hot_thin);

        assert(flow_thin < flow_dense &&
            "lower local air density should reduce the fan's achievable pressure rise, "
            "and therefore its throttled delivered flow under the same vent resistance");

        std::cout << "test_fan_curve_density_sensitivity PASSED "
                << "(rho=1.2 -> Q=" << flow_dense << ", rho=0.85 -> Q=" << flow_thin << ")\n";
    }

    // ============================================================
    // COLLISION CHECKER TESTS
    // ============================================================
    // These validate geometry-level collision detection (collision.hpp),
    // which now runs once, before the mesh exists, rather than being
    // discovered reactively while stamping cells.

    void test_collision_allows_non_overlapping_components() {
        Component a(0.03, 0.03, 0.03, "A");
        a.set_coords_m(0.0, 0.0, 0.0);

        Component b(0.03, 0.03, 0.03, "B");
        b.set_coords_m(0.10, 0.0, 0.0); // well clear of A

        bool threw = false;
        try {
            CollisionChecker::check_all({a, b}, {}, {});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(!threw && "Non-overlapping components must not raise a collision.");

        std::cout << "test_collision_allows_non_overlapping_components PASSED\n";
    }

    void test_collision_detects_component_component_overlap() {
        Component a(0.03, 0.03, 0.03, "A");
        a.set_coords_m(0.0, 0.0, 0.0); // spans x:[0, 0.03]

        Component b(0.03, 0.03, 0.03, "B");
        b.set_coords_m(0.02, 0.0, 0.0); // spans x:[0.02, 0.05] -> overlaps A

        bool threw = false;
        try {
            CollisionChecker::check_all({a, b}, {}, {});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(threw && "Genuinely overlapping components must be rejected.");

        std::cout << "test_collision_detects_component_component_overlap PASSED\n";
    }

    void test_collision_allows_flush_adjacent_components() {
        Component a(0.03, 0.03, 0.03, "A");
        a.set_coords_m(0.0, 0.0, 0.0); // spans x:[0, 0.03]

        Component b(0.03, 0.03, 0.03, "B");
        b.set_coords_m(0.03, 0.0, 0.0); // spans x:[0.03, 0.06] -> shares a face, no volume overlap

        bool threw = false;
        try {
            CollisionChecker::check_all({a, b}, {}, {});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(!threw && "Components that only share a face must not be treated as overlapping.");

        std::cout << "test_collision_allows_flush_adjacent_components PASSED\n";
    }

    void test_collision_detects_fan_embedded_in_component() {
        Component c(0.05, 0.05, 0.05, "Block");
        c.set_coords_m(0.0, 0.0, 0.0); // spans [0,0.05] in all 3 axes

        // Fan sitting mid-volume, nowhere near any face - unambiguously invalid.
        Fan fan(
            "Buried fan", /*cfm=*/10.0, /*diameter=*/0.0,
            /*size=*/{0.02, 0.02, 0.0}, /*center=*/{0.025, 0.025, 0.025},
            /*direction=*/{0.0, 0.0, 1.0},
            FlowType::Exhaust, ShapeType::Rectangular
        );

        bool threw = false;
        try {
            CollisionChecker::check_all({c}, {fan}, {});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(threw && "A fan buried inside a component's solid volume must be rejected.");

        std::cout << "test_collision_detects_fan_embedded_in_component PASSED\n";
    }

    void test_collision_detects_vent_overlapping_fan() {
        // Both floating in open space (away from any component), footprints coincide.
        Fan fan(
            "Fan", 10.0, 0.0,
            {0.02, 0.02, 0.0}, {0.10, 0.10, 0.10}, {0.0, 0.0, 1.0},
            FlowType::Exhaust, ShapeType::Rectangular
        );

        Vent vent(
            "Vent", {0.02, 0.02, 0.0}, /*far=*/0.5, /*diameter=*/0.0, /*cd=*/0.6,
            /*center=*/{0.10, 0.10, 0.10}, /*direction=*/{0.0, 0.0, 1.0},
            VentShapeType::Rectangular
        );

        bool threw = false;
        try {
            CollisionChecker::check_all({}, {fan}, {vent});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(threw && "A fan and vent occupying the same footprint must be rejected.");

        std::cout << "test_collision_detects_vent_overlapping_fan PASSED\n";
    }

    void test_collision_is_independent_of_mesh_resolution() {
        // A and B have a real 1mm physical gap. A coarse mesh (dx=0.02) would
        // floor both components into the same cell index and previously would
        // have thrown "Component overlap detected" purely as a meshing
        // artifact. CollisionChecker takes no mesh at all, so it isn't fooled.
        Component a(0.03, 0.03, 0.03, "A");
        a.set_coords_m(0.0, 0.0, 0.0); // spans x:[0, 0.03]

        Component b(0.03, 0.03, 0.03, "B");
        b.set_coords_m(0.031, 0.0, 0.0); // spans x:[0.031, 0.061] -> real 1mm gap

        bool threw = false;
        try {
            CollisionChecker::check_all({a, b}, {}, {});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(!threw &&
            "A real physical gap must read as valid regardless of any mesh resolution, "
            "since the checker never looks at dx/dy/dz.");

        std::cout << "test_collision_is_independent_of_mesh_resolution PASSED\n";
    }

    // ============================================================
    // TOML LOADER TESTS
    // ============================================================

    static std::filesystem::path write_temp_toml(
        const std::string& filename,
        const std::string& contents
    ) {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "thermal_solver_toml_tests";
        std::filesystem::create_directories(dir);

        const std::filesystem::path path = dir / filename;
        std::ofstream file(path);
        if (!file) {
            throw std::runtime_error("Could not create temporary TOML file: " + path.string());
        }
        file << contents;
        file.close();
        return path;
    }

    static void remove_temp_toml(const std::filesystem::path& path) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    void test_component_loader_valid_toml_succeeds() {
        const auto path = write_temp_toml("valid_component.toml", R"toml(
name = "Loader test component"
watts = 25.0
internal_regions = []

[size]
units = "m"
width = 0.10
depth = 0.10
height = 0.10

[material]
rho = 2700.0
cp = 900.0
k = 150.0
)toml");

        bool threw = false;
        try {
            ComponentLoader loader;
            loader.load_component(path);
            loader.run();
        } catch (const std::exception&) {
            threw = true;
        }
        remove_temp_toml(path);

        assert(!threw && "A complete component TOML file must load and run successfully.");
        std::cout << "test_component_loader_valid_toml_succeeds PASSED\n";
    }

    void test_component_loader_missing_watts_fails() {
        const auto path = write_temp_toml("component_missing_watts.toml", R"toml(
name = "Missing watts"
internal_regions = []

[size]
units = "m"
width = 0.10
depth = 0.10
height = 0.10

[material]
rho = 2700.0
cp = 900.0
k = 150.0
)toml");

        bool threw = false;
        try {
            ComponentLoader loader;
            loader.load_component(path);
        } catch (const std::runtime_error& e) {
            threw = true;
            assert(std::string(e.what()).find("watts") != std::string::npos);
        }
        remove_temp_toml(path);

        assert(threw && "A component TOML missing required watts must be rejected.");
        std::cout << "test_component_loader_missing_watts_fails PASSED\n";
    }

    void test_component_loader_invalid_units_fail_on_run() {
        const auto path = write_temp_toml("component_invalid_units.toml", R"toml(
name = "Bad units"
watts = 25.0
internal_regions = []

[size]
units = "feet"
width = 1.0
depth = 1.0
height = 1.0

[material]
rho = 2700.0
cp = 900.0
k = 150.0
)toml");

        bool load_threw = false;
        bool run_threw = false;
        try {
            ComponentLoader loader;
            loader.load_component(path);
            try {
                loader.run();
            } catch (const std::runtime_error& e) {
                run_threw = true;
                assert(std::string(e.what()).find("Invalid component.size units") != std::string::npos);
            }
        } catch (const std::exception&) {
            load_threw = true;
        }
        remove_temp_toml(path);

        assert(!load_threw && "Unsupported units are validated during ComponentLoader::run().");
        assert(run_threw && "Unsupported component units must cause run() to fail.");
        std::cout << "test_component_loader_invalid_units_fail_on_run PASSED\n";
    }

    void test_model_loader_valid_toml_succeeds() {
        const auto path = write_temp_toml("valid_model.toml", R"toml(
name = "Minimal loader model"

[simulation]
dt = 0.01
duration = 0.01
output_interval = 1
max_timesteps = 10
max_updates = 1000
max_cell_count = 1000
max_megabyte_usage = 10
update_flow_interval = 1

[flow_solver]
enable_flow_solver = false

[environment]
humidity = 30.0
elevation = 5800.0
T_ambient = 20.0
cp = 1005.0
k = 0.02587
mu = 1.81e-5
pr = 0.71
rho = 1.225

[mesh]
dx = 0.05
dy = 0.05
dz = 0.05

[rack]
name = "Tiny rack"

[rack.size]
units = "m"
width = 0.05
depth = 0.05
height = 0.05

[rack.ambient]
temperature = 20.0
cp = 1005.0
k = 0.02587
rho = 1.225
)toml");

        bool threw = false;
        try {
            ModelLoader loader;
            loader.load_model(path);
            loader.run();
        } catch (const std::exception&) {
            threw = true;
        }
        remove_temp_toml(path);

        assert(!threw && "A complete minimal model TOML file must load and run successfully.");
        std::cout << "test_model_loader_valid_toml_succeeds PASSED\n";
    }

    void test_model_loader_malformed_toml_fails() {
        const auto path = write_temp_toml("malformed_model.toml", R"toml(
name = "Malformed model"
[simulation
 dt = 0.01
)toml");

        bool threw = false;
        try {
            ModelLoader loader;
            loader.load_model(path);
        } catch (const std::runtime_error& e) {
            threw = true;
            assert(std::string(e.what()).find("Failed to parse model file") != std::string::npos);
        }
        remove_temp_toml(path);

        assert(threw && "Malformed TOML syntax must be rejected.");
        std::cout << "test_model_loader_malformed_toml_fails PASSED\n";
    }

    void test_model_loader_missing_environment_fails() {
        const auto path = write_temp_toml("model_missing_environment.toml", R"toml(
name = "Missing environment"

[simulation]
dt = 0.01
duration = 0.01
max_timesteps = 10
max_updates = 1000
max_cell_count = 1000
max_megabyte_usage = 10

[flow_solver]
enable_flow_solver = false
)toml");

        bool threw = false;
        try {
            ModelLoader loader;
            loader.load_model(path);
        } catch (const std::runtime_error& e) {
            threw = true;
            assert(std::string(e.what()).find("environment") != std::string::npos);
        }
        remove_temp_toml(path);

        assert(threw && "A model TOML missing the required environment table must be rejected.");
        std::cout << "test_model_loader_missing_environment_fails PASSED\n";
    }

};

#endif