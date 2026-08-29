#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include "../src/environment.hpp"
#include "../src/fan.hpp"
#include "../src/flow_solver.hpp"
#include "../src/mesh.hpp"
#include "../src/rack.hpp"
#include "../src/workload.hpp"

namespace {

constexpr double kCellWidthM = 0.10;
constexpr double kDensityKgM3 = 0.9833;
constexpr double kCurveA = 1.0;
constexpr double kCurveB = 1000.0;
constexpr double kCurveC = 0.0;
constexpr double kUpperFlowM3s = kCurveA / kCurveB;
constexpr double kInitialFlowM3s = 0.5 * kUpperFlowM3s;
constexpr double kVentCdAreaM2 = 0.01;
constexpr double kPressureToleranceM3s = 1.0e-10;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if(!condition) fail(message);
}

Mesh make_line_mesh(int cell_count, const std::string& name) {
    Rack rack = Rack::from_meters(
        kCellWidthM * static_cast<double>(cell_count),
        kCellWidthM, kCellWidthM, name);
    rack.set_t(20.0);
    rack.set_cp(1005.5);
    rack.set_k(0.02587);
    rack.set_rho(kDensityKgM3);
    Environment environment(
        30.0, 5500.0, 20.0, 1005.5,
        0.02587, 1.81e-5, 0.71, kDensityKgM3);
    Workload workload(1, 64, 16, 16);
    return Mesh().build_mesh(
        rack, kCellWidthM, kCellWidthM, kCellWidthM,
        environment, workload);
}

void set_vent(Mesh& mesh, int x) {
    Cell& cell = mesh.at(x, 0, 0);
    cell.set_state(Cell::State::Vent);
    cell.set_flow_source(0.0);
    cell.set_vent_conductance(kVentCdAreaM2);
}

void set_boundary_curve_fan(Mesh& mesh, int x) {
    Cell& cell = mesh.at(x, 0, 0);
    cell.set_state(Cell::State::Intake);
    cell.set_flow_source(0.0);
    cell.set_vent_conductance(0.0);
    cell.set_fan_curve(
        kCurveA, kCurveB, kCurveC, kDensityKgM3);
    cell.set_fan_Q_ref(kInitialFlowM3s);
    cell.set_fan_dir(1.0, 0.0, 0.0);
    cell.set_fan_area(kCellWidthM * kCellWidthM);
}

void add_internal_curve_fan(Mesh& mesh) {
    mesh.get_internal_fans().push_back(
        {{1, 0, 0}, {2, 0, 0}, kInitialFlowM3s,
         {1.0, 0.0, 0.0},
         kCurveA, kCurveB, kCurveC,
         kDensityKgM3, kInitialFlowM3s});
}

double curve_pressure(double flow_m3s) {
    return bounded_fan_curve_pressure(
        kCurveA, kCurveB, kCurveC, flow_m3s);
}

void require_solution(
    const FlowSolver& solver, const std::string& label) {
    require(solver.converged(),
            label + ": nonlinear solve did not converge");
    require(solver.has_face_flux_solution(),
            label + ": face fluxes were not published");
    require(std::isfinite(
                solver.maximum_realized_continuity_residual_m3s()),
            label + ": continuity residual is not finite");
    require(solver.maximum_realized_continuity_residual_m3s() <=
                1.1 * kPressureToleranceM3s,
            label + ": exact published local continuity did not close");
}

void require_lower_bound(
    double flow_m3s, double required_head_pa,
    const std::string& label) {
    require(flow_m3s >= 0.0 && flow_m3s <= 2.0e-12,
            label + ": lower-bound flow is not exact zero");
    require(required_head_pa - curve_pressure(flow_m3s) > 0.25,
            label + ": lower-bound complementarity is not strict");
}

void require_upper_bound(
    double flow_m3s, double required_head_pa,
    const std::string& label) {
    require(std::abs(flow_m3s - kUpperFlowM3s) <= 1.0e-12,
            label + ": upper-bound flow differs from free delivery");
    require(curve_pressure(flow_m3s) - required_head_pa > 0.25,
            label + ": upper-bound complementarity is not strict");
}

void require_released(
    double flow_m3s, double required_head_pa,
    const std::string& label) {
    require(flow_m3s > 0.2 * kUpperFlowM3s &&
                flow_m3s < 0.6 * kUpperFlowM3s,
            label + ": released flow did not return to the curve interior");
    require(std::abs(required_head_pa - curve_pressure(flow_m3s)) < 1.0e-4,
            label + ": released operating point is off the fan curve");
}

double run_boundary_sequence(
    double forcing_source_m3s, bool expect_lower,
    const std::string& label) {
    Mesh mesh = make_line_mesh(3, label);
    set_boundary_curve_fan(mesh, 0);
    mesh.at(1, 0, 0).set_flow_source(forcing_source_m3s);
    set_vent(mesh, 2);
    FlowSolver solver(
        mesh, 100.0, kPressureToleranceM3s, 2000, 1.1,
        200, 1.0e-6, "pcg");

    solver.solve();
    require_solution(solver, label + " forced");
    const double bounded_flow = mesh.at(0, 0, 0).get_fan_Q_ref();
    const double bounded_head = mesh.at(0, 0, 0).get_pressure();
    if(expect_lower)
        require_lower_bound(bounded_flow, bounded_head, label);
    else
        require_upper_bound(bounded_flow, bounded_head, label);
    require(std::abs(
                solver.ambient_flow_into_cell_m3s(0, 0, 0) -
                bounded_flow) <= 1.0e-12,
            label + ": boundary active-set source differs from fan flow");

    mesh.at(1, 0, 0).set_flow_source(0.0);
    solver.solve();
    require_solution(solver, label + " released");
    const double released_flow = mesh.at(0, 0, 0).get_fan_Q_ref();
    const double released_head = mesh.at(0, 0, 0).get_pressure();
    require_released(released_flow, released_head, label);
    require(std::abs(
                solver.ambient_flow_into_cell_m3s(0, 0, 0) -
                released_flow) <= 1.0e-12,
            label + ": released boundary source differs from fan flow");
    std::cout << std::setprecision(10)
              << "active-set boundary: " << label
              << " boundQ=" << bounded_flow
              << " boundHead=" << bounded_head
              << " releasedQ=" << released_flow
              << " releasedHead=" << released_head
              << " residual="
              << solver.maximum_realized_continuity_residual_m3s()
              << '\n';
    return released_flow;
}

double run_internal_sequence(
    double upstream_source_m3s,
    double downstream_source_m3s,
    bool expect_lower,
    const std::string& label) {
    Mesh mesh = make_line_mesh(4, label);
    set_vent(mesh, 0);
    set_vent(mesh, 3);
    add_internal_curve_fan(mesh);
    mesh.at(1, 0, 0).set_flow_source(upstream_source_m3s);
    mesh.at(2, 0, 0).set_flow_source(downstream_source_m3s);
    FlowSolver solver(
        mesh, 100.0, kPressureToleranceM3s, 2000, 1.1,
        200, 1.0e-6, "pcg");

    solver.solve();
    require_solution(solver, label + " forced");
    const auto& fan = mesh.get_internal_fans().front();
    const double bounded_flow = fan.q_ref;
    const double bounded_head =
        mesh.at(2, 0, 0).get_pressure() -
        mesh.at(1, 0, 0).get_pressure();
    if(expect_lower)
        require_lower_bound(bounded_flow, bounded_head, label);
    else
        require_upper_bound(bounded_flow, bounded_head, label);
    require(solver.ordinary_face_is_suppressed_for_internal_fan(
                fan.upstream, fan.downstream),
            label + ": ordinary conductance remained across the fan face");
    require(std::abs(
                solver.internal_fan_face_flux_m3s(
                    fan.upstream, fan.downstream) - fan.q_ref) <= 1.0e-12,
            label + ": published internal fan flux differs from q_ref");

    mesh.at(1, 0, 0).set_flow_source(0.0);
    mesh.at(2, 0, 0).set_flow_source(0.0);
    solver.solve();
    require_solution(solver, label + " released");
    const auto& released_fan = mesh.get_internal_fans().front();
    const double released_head =
        mesh.at(2, 0, 0).get_pressure() -
        mesh.at(1, 0, 0).get_pressure();
    require_released(released_fan.q_ref, released_head, label);
    require(std::abs(
                solver.internal_fan_face_flux_m3s(
                    released_fan.upstream, released_fan.downstream) -
                released_fan.q_ref) <= 1.0e-12,
            label + ": released internal fan flux differs from q_ref");
    std::cout << std::setprecision(10)
              << "active-set internal: " << label
              << " boundQ=" << bounded_flow
              << " boundHead=" << bounded_head
              << " releasedQ=" << released_fan.q_ref
              << " releasedHead=" << released_head
              << " residual="
              << solver.maximum_realized_continuity_residual_m3s()
              << '\n';
    return released_fan.q_ref;
}

void boundary_lower_without_other_ground_fails_closed() {
    const std::string label = "boundary lower without another ground";
    Mesh mesh = make_line_mesh(2, label);
    set_boundary_curve_fan(mesh, 0);
    mesh.at(1, 0, 0).set_flow_source(3.0e-3);
    FlowSolver solver(
        mesh, 100.0, kPressureToleranceM3s, 2000, 1.1,
        200, 1.0e-6, "pcg");
    try {
        solver.solve();
    } catch(const std::runtime_error& error) {
        const std::string diagnostic = error.what();
        require(diagnostic.find("ungrounded") != std::string::npos &&
                    diagnostic.find("source-bearing") != std::string::npos,
                label + ": wrong rejection diagnostic: " + diagnostic);
        require(!solver.has_face_flux_solution(),
                label + ": rejected topology published face fluxes");
        return;
    }
    fail(label + ": a shut-off boundary fan incorrectly remained a ground");
}

} // namespace

int main() {
    try {
        const double boundary_from_lower = run_boundary_sequence(
            +3.0e-3, true, "boundary lower/release");
        const double boundary_from_upper = run_boundary_sequence(
            -3.0e-3, false, "boundary upper/release");
        require(std::abs(boundary_from_lower - boundary_from_upper) <= 5.0e-7,
                "boundary release depends on the prior active bound");

        const double internal_from_lower = run_internal_sequence(
            -2.0e-3, +2.0e-3, true, "internal lower/release");
        const double internal_from_upper = run_internal_sequence(
            +2.0e-3, -2.0e-3, false, "internal upper/release");
        require(std::abs(internal_from_lower - internal_from_upper) <= 5.0e-7,
                "internal release depends on the prior active bound");

        boundary_lower_without_other_ground_fails_closed();
        std::cout << "fan_active_set_test PASSED: lower/upper bounds, "
                     "release, exact continuity, and shut-off grounding\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "fan_active_set_test FAILED: " << error.what() << '\n';
        return 1;
    }
}
