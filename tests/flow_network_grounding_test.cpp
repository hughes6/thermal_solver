#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/environment.hpp"
#include "../src/flow_solver.hpp"
#include "../src/mesh.hpp"
#include "../src/rack.hpp"
#include "../src/workload.hpp"

namespace {

constexpr double kAmbientTemperatureC = 20.0;
constexpr double kDensityKgM3 = 0.9833;
constexpr double kCellWidthM = 0.1;
constexpr double kOpeningAreaM2 = 0.01;
constexpr double kFixedFlowM3s = 1.0e-3;
constexpr double kPressureToleranceM3s = 1.0e-8;
constexpr double kCurveA = 1.0;
constexpr double kCurveB = 500.0;
constexpr double kCurveC = 0.0;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if(!condition) fail(message);
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool close_flow(double actual, double expected) {
    return std::isfinite(actual) && std::isfinite(expected) &&
           std::abs(actual - expected) <=
               std::max(1.0e-10,
                        2.0e-3 * std::max(std::abs(actual),
                                         std::abs(expected)));
}

Mesh make_line_mesh(int cells, const std::string& name) {
    require(cells >= 2, name + ": fixture needs at least two cells");
    Rack rack = Rack::from_meters(
        kCellWidthM * static_cast<double>(cells),
        kCellWidthM, kCellWidthM, name);
    rack.set_t(kAmbientTemperatureC);
    rack.set_cp(1005.5);
    rack.set_k(0.02587);
    rack.set_rho(kDensityKgM3);

    Environment environment(
        30.0, 5500.0, kAmbientTemperatureC, 1005.5,
        0.02587, 1.81e-5, 0.71, kDensityKgM3);
    Workload workload(1, 8, 4, 16);
    return Mesh().build_mesh(
        rack, kCellWidthM, kCellWidthM, kCellWidthM,
        environment, workload);
}

void add_partition(Mesh& mesh, int low_x) {
    require(low_x >= 0 && low_x + 1 < mesh.get_nx(),
            "partition face is outside the fixture");
    mesh.add_wall_face(
        low_x, 0, 0, 0,
        0.001, 0.02587, kDensityKgM3, 1005.5,
        kAmbientTemperatureC);
}

void set_vent(Mesh& mesh, int x) {
    Cell& cell = mesh.at(x, 0, 0);
    cell.set_state(Cell::State::Vent);
    cell.set_flow_source(0.0);
    // Mesh stamping stores Cd*A in this field.  The exact value is not
    // material to topology, but it must be finite and strictly positive.
    cell.set_vent_conductance(kOpeningAreaM2);
}

void set_fixed_boundary_source(Mesh& mesh, int x, double flow_m3s) {
    require(std::isfinite(flow_m3s) && flow_m3s != 0.0,
            "fixed boundary source must be finite and nonzero");
    Cell& cell = mesh.at(x, 0, 0);
    cell.set_state(flow_m3s > 0.0 ?
                   Cell::State::Intake : Cell::State::Exhaust);
    cell.set_vent_conductance(0.0);
    cell.set_flow_source(flow_m3s);
}

void set_curved_boundary_fan(Mesh& mesh, int x) {
    Cell& cell = mesh.at(x, 0, 0);
    cell.set_state(Cell::State::Intake);
    cell.set_flow_source(0.0);
    cell.set_vent_conductance(0.0);
    cell.set_fan_curve(kCurveA, kCurveB, kCurveC, kDensityKgM3);
    cell.set_fan_Q_ref(kFixedFlowM3s);
    cell.set_fan_dir(1.0, 0.0, 0.0);
    cell.set_fan_area(kOpeningAreaM2);
}

void add_internal_fan(Mesh& mesh, bool curved) {
    require(mesh.get_nx() == 4,
            "internal-fan line fixture must contain four cells");
    mesh.get_internal_fans().push_back(
        {{1, 0, 0}, {2, 0, 0}, kFixedFlowM3s,
         {1.0, 0.0, 0.0},
         curved ? kCurveA : 0.0,
         curved ? kCurveB : 0.0,
         curved ? kCurveC : 0.0,
         kDensityKgM3, kFixedFlowM3s});
}

void require_finite_solution(const Mesh& mesh,
                             const FlowSolver& solver,
                             const std::string& label) {
    require(solver.converged(),
            label + ": nonlinear flow did not converge");
    require(solver.has_face_flux_solution(),
            label + ": solver did not publish a face-flux solution");
    require(std::isfinite(
                solver.maximum_realized_continuity_residual_m3s()),
            label + ": realized continuity residual is not finite");
    require(solver.maximum_realized_continuity_residual_m3s() <=
                1.1 * kPressureToleranceM3s,
            label + ": realized local continuity did not close");
    for(const Cell& cell : mesh.get_cells()) {
        if(!cell.is_fluid()) continue;
        require(std::isfinite(cell.get_pressure()),
                label + ": pressure is not finite");
        require(std::isfinite(cell.get_vx()) &&
                std::isfinite(cell.get_vy()) &&
                std::isfinite(cell.get_vz()),
                label + ": velocity is not finite");
    }
}

// This intentionally specifies a small diagnostic contract.  A late generic
// PCG singularity is not an acceptable substitute for a topology preflight:
// rejected cases must say that an ungrounded component was identified.
template<typename BuildFixture>
void expect_ungrounded_component_rejection(
    const std::string& label, BuildFixture&& build_fixture) {
    Mesh mesh = build_fixture();
    FlowSolver solver(
        mesh, 4.6, kPressureToleranceM3s, 2000, 1.1,
        100, 1.0e-5, "pcg");
    try {
        solver.solve();
    } catch(const std::runtime_error& error) {
        const std::string diagnostic = lower_copy(error.what());
        require(diagnostic.find("flowsolver") != std::string::npos &&
                    diagnostic.find("ungrounded") != std::string::npos &&
                    diagnostic.find("component") != std::string::npos,
                label + ": expected a FlowSolver ungrounded-component "
                        "diagnostic, got: " + error.what());
        require(!solver.has_face_flux_solution(),
                label + ": rejected topology published face fluxes");
        return;
    }
    fail(label + ": ungrounded source-bearing component was accepted");
}

void expect_malformed_internal_fan_endpoint_rejection(
    const std::string& label, bool curved) {
    Mesh mesh = make_line_mesh(2, label);
    mesh.at(0, 0, 0).set_state(Cell::State::Component);
    set_vent(mesh, 1);
    mesh.get_internal_fans().push_back(
        {{0, 0, 0}, {1, 0, 0}, kFixedFlowM3s,
         {1.0, 0.0, 0.0},
         curved ? kCurveA : 0.0,
         curved ? kCurveB : 0.0,
         curved ? kCurveC : 0.0,
         kDensityKgM3, kFixedFlowM3s});

    FlowSolver solver(
        mesh, 4.6, kPressureToleranceM3s, 2000, 1.1,
        100, 1.0e-5, "pcg");
    try {
        solver.solve();
    } catch(const std::exception& error) {
        const std::string diagnostic = lower_copy(error.what());
        require(diagnostic.find("internal") != std::string::npos &&
                    diagnostic.find("fan") != std::string::npos &&
                    diagnostic.find("endpoint") != std::string::npos &&
                    diagnostic.find("fluid") != std::string::npos,
                label + ": expected a clear internal-fan fluid-endpoint "
                        "diagnostic, got: " + error.what());
        require(!solver.has_face_flux_solution(),
                label + ": malformed interface published face fluxes");
        return;
    }
    fail(label + ": internal fan with a solid endpoint was accepted");
}

void disconnected_source_and_vent_are_rejected() {
    expect_ungrounded_component_rejection(
        "disconnected source and vent",
        [] {
            Mesh mesh = make_line_mesh(4, "disconnected_source_and_vent");
            add_partition(mesh, 1);
            set_fixed_boundary_source(mesh, 0, kFixedFlowM3s);
            set_vent(mesh, 3);
            return mesh;
        });
}

void fixed_internal_fan_with_one_ground_is_rejected() {
    expect_ungrounded_component_rejection(
        "fixed internal fan with one grounded side",
        [] {
            Mesh mesh = make_line_mesh(4, "fixed_fan_one_ground");
            add_internal_fan(mesh, false);
            set_vent(mesh, 0);
            return mesh;
        });
}

void fixed_internal_fan_with_both_sides_grounded_solves() {
    const std::string label = "fixed internal fan with two grounds";
    Mesh mesh = make_line_mesh(4, "fixed_fan_two_grounds");
    add_internal_fan(mesh, false);
    set_vent(mesh, 0);
    set_vent(mesh, 3);
    FlowSolver solver(
        mesh, 4.6, kPressureToleranceM3s, 2000, 1.1,
        100, 1.0e-5, "pcg");
    solver.solve();
    require_finite_solution(mesh, solver, label);

    const Mesh::InternalFanInterface& fan =
        mesh.get_internal_fans().front();
    require(solver.ordinary_face_is_suppressed_for_internal_fan(
                fan.upstream, fan.downstream),
            label + ": ordinary conductance remained in parallel with fan");
    require(close_flow(
                solver.internal_fan_face_flux_m3s(
                    fan.upstream, fan.downstream),
                kFixedFlowM3s),
            label + ": published fan-face flux differs from fixed flow");
}

void curved_internal_fan_connects_pressure_components() {
    const std::string label = "curved internal fan with one ground";
    Mesh mesh = make_line_mesh(4, "curved_fan_one_ground");
    add_internal_fan(mesh, true);
    set_vent(mesh, 0);
    FlowSolver solver(
        mesh, 4.6, kPressureToleranceM3s, 2000, 1.1,
        100, 1.0e-5, "pcg");
    solver.solve();
    require_finite_solution(mesh, solver, label);

    const Mesh::InternalFanInterface& fan =
        mesh.get_internal_fans().front();
    require(solver.ordinary_face_is_suppressed_for_internal_fan(
                fan.upstream, fan.downstream),
            label + ": ordinary conductance remained in parallel with fan");
    require(std::isfinite(fan.q_ref) && fan.q_ref > 0.0,
            label + ": curved fan operating flow is invalid");
    require(close_flow(
                solver.internal_fan_face_flux_m3s(
                    fan.upstream, fan.downstream),
                fan.q_ref),
            label + ": published fan-face flux differs from operating flow");
}

void curved_boundary_fan_is_an_ambient_ground() {
    const std::string label = "curved boundary fan ground";
    Mesh mesh = make_line_mesh(2, "curved_boundary_ground");
    set_curved_boundary_fan(mesh, 0);
    FlowSolver solver(
        mesh, 4.6, kPressureToleranceM3s, 2000, 1.1,
        100, 1.0e-5, "pcg");
    solver.solve();
    require_finite_solution(mesh, solver, label);
    require(std::isfinite(mesh.at(0, 0, 0).get_fan_Q_ref()) &&
                mesh.at(0, 0, 0).get_fan_Q_ref() > 0.0,
            label + ": boundary fan operating flow is invalid");
}

void balanced_active_floating_components_are_rejected() {
    expect_ungrounded_component_rejection(
        "two balanced but active floating components",
        [] {
            Mesh mesh = make_line_mesh(4, "balanced_active_components");
            add_partition(mesh, 1);
            set_fixed_boundary_source(mesh, 0, +kFixedFlowM3s);
            set_fixed_boundary_source(mesh, 1, -kFixedFlowM3s);
            set_fixed_boundary_source(mesh, 2, +kFixedFlowM3s);
            set_fixed_boundary_source(mesh, 3, -kFixedFlowM3s);
            return mesh;
        });
}

void passive_sealed_components_are_allowed() {
    const std::string label = "passive sealed components";
    Mesh mesh = make_line_mesh(4, "passive_sealed_components");
    add_partition(mesh, 1);
    FlowSolver solver(
        mesh, 4.6, kPressureToleranceM3s, 2000, 1.1,
        10, 1.0e-5, "pcg");
    solver.solve();
    require_finite_solution(mesh, solver, label);
    for(const Cell& cell : mesh.get_cells()) {
        require(std::abs(cell.get_pressure()) <= 1.0e-12,
                label + ": arbitrary passive pressure offset was published");
    }
}

void fixed_internal_fan_with_solid_endpoint_is_rejected() {
    expect_malformed_internal_fan_endpoint_rejection(
        "fixed_internal_fan_solid_endpoint", false);
}

void curved_internal_fan_with_solid_endpoint_is_rejected() {
    expect_malformed_internal_fan_endpoint_rejection(
        "curved_internal_fan_solid_endpoint", true);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> cases{
        {"disconnected_source_and_vent_are_rejected",
         disconnected_source_and_vent_are_rejected},
        {"fixed_internal_fan_with_one_ground_is_rejected",
         fixed_internal_fan_with_one_ground_is_rejected},
        {"fixed_internal_fan_with_both_sides_grounded_solves",
         fixed_internal_fan_with_both_sides_grounded_solves},
        {"curved_internal_fan_connects_pressure_components",
         curved_internal_fan_connects_pressure_components},
        {"curved_boundary_fan_is_an_ambient_ground",
         curved_boundary_fan_is_an_ambient_ground},
        {"balanced_active_floating_components_are_rejected",
         balanced_active_floating_components_are_rejected},
        {"passive_sealed_components_are_allowed",
         passive_sealed_components_are_allowed},
        {"fixed_internal_fan_with_solid_endpoint_is_rejected",
         fixed_internal_fan_with_solid_endpoint_is_rejected},
        {"curved_internal_fan_with_solid_endpoint_is_rejected",
         curved_internal_fan_with_solid_endpoint_is_rejected},
    };

    int failures = 0;
    for(const auto& test_case : cases) {
        try {
            test_case.second();
            std::cout << "[PASS] " << test_case.first << '\n';
        } catch(const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test_case.first
                      << ": " << error.what() << '\n';
        }
    }

    if(failures != 0) {
        std::cerr << "flow_network_grounding_test FAILED: "
                  << failures << " of " << cases.size()
                  << " fixtures failed\n";
        return 1;
    }
    std::cout << "flow_network_grounding_test PASSED: "
              << cases.size() << " grounding fixtures\n";
    return 0;
}
