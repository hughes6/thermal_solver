#ifndef UNIT_TEST_HPP
#define UNIT_TEST_HPP

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cell.hpp"
#include "component.hpp"
#include "convection.hpp"
#include "environment.hpp"
#include "fan.hpp"
#include "flow_solver.hpp"
#include "grapher.hpp"
#include "mesh.hpp"
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

    Rack make_air_rack(double height,
                       double width,
                       double depth,
                       double temperature = 20.0) const {
        Rack rack = Rack::from_meters(height, width, depth);
        rack.set_t(temperature);
        rack.set_cp(1005.0);
        rack.set_k(0.02587);
        rack.set_rho(1.225);
        return rack;
    }

public:
    UnitTest() {}

    void run() {
        std::cout << "\n========== RUNNING UNIT TESTS ==========\n";

        test1();
        test2();

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
        // test_flow_solver_creates_nonzero_fan_to_vent_flow();
        // test_flow_solver_rejects_source_without_vent();

        std::cout << "========== ALL UNIT TESTS PASSED ==========\n\n";
    }

    // Existing whole-rack scaffold test: uniform rack with no heat source
    // should remain uniform and stationary.
    void test1() {
        Rack rack = make_air_rack(0.1, 0.1, 0.1, T);
        Mesh mesh = Mesh().build_mesh(rack, dx, dy, dz, mu, pr);

        FlowSolver flow_solver(mesh);
        flow_solver.solve();
        Solver solver(mesh, dt, sim_length);
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
        Rack rack = make_air_rack(0.1, 0.1, 0.1, 40.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, mu, pr);

        FlowSolver flow_solver(mesh);
        flow_solver.solve();
        Solver solver(mesh, dt, sim_length);
        solver.solve();

        const Mesh& final_mesh = solver.get_mesh();
        std::array<bool, 3> result =
            test_all_mesh_values(final_mesh, 40.0, 0.0, 0.0);

        if (!result[0]) {
            throw std::runtime_error("Test 2 - All Temp = 40.0 failed.");
        }

        std::cout << "test2 uniform 40 C stagnant rack PASSED\n";
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
            "XZ vent", {0.10, 0.0, 0.20}, 0.50,
            {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});

        assert(nearly_equal(vent_y_normal.gross_area(), 0.02));
        assert(nearly_equal(vent_y_normal.free_area(), 0.01));

        Vent vent_z_normal(
            "XY vent", {0.10, 0.20, 0.0}, 0.25,
            {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});

        assert(nearly_equal(vent_z_normal.gross_area(), 0.02));
        assert(nearly_equal(vent_z_normal.free_area(), 0.005));
        std::cout << "test_vent_area_uses_plane_dimensions PASSED\n";
    }

    void test_vent_free_area_ratio_validation() {
        bool threw = false;
        try {
            Vent invalid(
                "Invalid vent", {0.10, 0.0, 0.10}, 1.2,
                {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        } catch (const std::invalid_argument&) {
            threw = true;
        }

        assert(threw && "free-area ratio outside [0,1] should throw");
        std::cout << "test_vent_free_area_ratio_validation PASSED\n";
    }

    void test_component_stamping_sets_material_and_qdot() {
        Rack rack = make_air_rack(0.10, 0.10, 0.10, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, mu, pr);

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
        Rack rack = make_air_rack(0.10, 0.10, 0.10, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, mu, pr);

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

        Solver solver(mesh, /*dt=*/0.01, /*sim_length=*/0.02);
        solver.solve();

        const double final_temperature = solver.get_mesh().at(0, 0, 0).get_T();
        assert(final_temperature > 20.0 &&
               "positive component qdot should increase component temperature");
        std::cout << "test_heat_generation_increases_temperature PASSED (T="
                  << final_temperature << ")\n";
    }

    void test_flow_solver_keeps_stagnant_rack_at_zero_velocity() {
        Rack rack = make_air_rack(0.10, 0.10, 0.10, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, mu, pr);

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
        Rack rack = make_air_rack(0.10, 0.10, 0.20, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, mu, pr);

        Fan intake(
            "Intake", 1.0, 0.0,
            {0.0, 0.10, 0.10},
            {0.0, 0.05, 0.05},
            {1.0, 0.0, 0.0},
            FlowType::Intake,
            ShapeType::Rectangular);

        Vent outlet(
            "Outlet", {0.0, 0.10, 0.10}, 1.0,
            {0.20, 0.05, 0.05},
            {1.0, 0.0, 0.0});

        mesh.stamp_fan(intake);
        mesh.stamp_vent(outlet, 0.6);

        FlowSolver flow_solver(
            mesh,
            /*linear_resistivity=*/4.5,
            /*vent_discharge_coeff=*/0.6,
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
        Rack rack = make_air_rack(0.10, 0.10, 0.10, 20.0);
        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, mu, pr);

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
};

#endif