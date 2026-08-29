#include <algorithm>
#include <array>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include "../src/environment.hpp"
#include "../src/fan.hpp"
#include "../src/flow_solver.hpp"
#include "../src/mesh.hpp"
#include "../src/rack.hpp"
#include "../src/vent.hpp"
#include "../src/workload.hpp"

namespace {

constexpr double kWidthM = 0.006;
constexpr double kDepthM = 0.012;
constexpr double kCellWidthM = 0.003;
constexpr double kDensity = 0.9833;
constexpr double kViscosity = 1.81e-5;
constexpr double kNormalTolerance = 1.0e-3;
constexpr double kReferenceTolerance = 1.0e-8;
constexpr double kAbsoluteUpdateTolerance = 1.0e-8;
constexpr double kCurveA = 250.0;
constexpr double kCurveB = 1.25e6;
constexpr double kCurveC = 0.0;
constexpr double kPressureDropSnapshotTolerancePa = 1.0e-6;

double haaland_smooth_pipe(double reynolds) {
    const double inverse_sqrt = -1.8 * std::log10(6.9 / reynolds);
    return 1.0 / (inverse_sqrt * inverse_sqrt);
}

double source_flow_for_reynolds(double target_reynolds) {
    // Four equal square passages share the boundary source.
    return target_reynolds * 4.0 * kCellWidthM * kViscosity / kDensity;
}

Mesh make_transitional_case(double target_reynolds, bool curved_fan) {
    const double source_m3s = source_flow_for_reynolds(target_reynolds);
    Environment environment(
        30.0, 5800.0, 20.0, 1005.0, 0.02587,
        kViscosity, 0.71, kDensity);
    Workload workload(1000, 10000, 1000, 4);
    Rack rack = Rack::from_meters(kWidthM, kDepthM, kWidthM);
    rack.set_t(20.0);
    rack.set_cp(1005.0);
    rack.set_k(0.02587);
    rack.set_rho(kDensity);
    Mesh mesh = Mesh().build_mesh(
        rack, kCellWidthM, kCellWidthM, kCellWidthM,
        environment, workload);

    Fan inlet(
        curved_fan ? "curved transitional intake" : "transitional intake",
        source_m3s / Fan::CFM_TO_M3S, 0.0,
        {kWidthM, 0.0, kWidthM},
        {0.5 * kWidthM, 0.0, 0.5 * kWidthM},
        {0.0, 1.0, 0.0}, FlowType::Intake, ShapeType::Rectangular);
    if(curved_fan) inlet.set_curve(kCurveA, kCurveB, kCurveC, 1.2);
    Vent outlet(
        "transitional outlet", {kWidthM, 0.0, kWidthM},
        1.0, 0.0, 0.82,
        {0.5 * kWidthM, kDepthM, 0.5 * kWidthM},
        {0.0, 1.0, 0.0}, VentShapeType::Rectangular);
    mesh.stamp_fan(inlet);
    mesh.stamp_vent(outlet);
    return mesh;
}

struct SolveResult {
    bool converged = false;
    int outer_iterations = 0;
    double face_norm = 0.0;
    double fan_norm = 0.0;
    double maximum_face_flow = 0.0;
    double maximum_fan_flow = 0.0;
    double total_source = 0.0;
    double total_vent = 0.0;
    double imbalance = 0.0;
    double pressure_drop = 0.0;
    std::vector<double> pressure;
    std::vector<double> speed;
};

SolveResult solve_case(
    double target_reynolds,
    double flow_tolerance,
    int maximum_outer_iterations,
    bool curved_fan = false) {
    Mesh mesh = make_transitional_case(target_reynolds, curved_fan);
    FlowSolver solver(
        mesh, 4.6, 1.0e-12, 500, 1.1,
        maximum_outer_iterations, flow_tolerance, "pcg");
    solver.solve();

    SolveResult result;
    result.converged = solver.converged();
    result.outer_iterations = solver.outer_iterations();
    result.face_norm = solver.last_relative_face_flow_change();
    result.fan_norm = solver.last_relative_fan_flow_change();
    result.maximum_face_flow = solver.last_maximum_face_flow_m3s();
    result.maximum_fan_flow = solver.maximum_fan_operating_flow_m3s();
    result.total_source = solver.total_source_m3s();
    result.total_vent = solver.total_vent_flow_m3s();
    result.imbalance = solver.mass_imbalance_m3s();
    double inlet_pressure_sum = 0.0;
    double outlet_pressure_sum = 0.0;
    std::size_t inlet_cells = 0;
    std::size_t outlet_cells = 0;
    for(const Cell& cell : mesh.get_cells()) {
        if(!cell.is_fluid()) continue;
        result.pressure.push_back(cell.get_pressure());
        result.speed.push_back(cell.get_vmag());
        if(cell.get_state() == Cell::State::Intake) {
            inlet_pressure_sum += cell.get_pressure();
            ++inlet_cells;
        }
        if(cell.get_state() == Cell::State::Vent) {
            outlet_pressure_sum += cell.get_pressure();
            ++outlet_cells;
        }
    }
    assert(inlet_cells > 0 && outlet_cells > 0);
    result.pressure_drop = inlet_pressure_sum / inlet_cells -
        outlet_pressure_sum / outlet_cells;
    return result;
}

double maximum_relative_difference(
    const std::vector<double>& candidate,
    const std::vector<double>& reference,
    double scale_floor) {
    assert(candidate.size() == reference.size());
    double maximum_difference = 0.0;
    double maximum_reference = scale_floor;
    for(std::size_t i = 0; i < candidate.size(); ++i) {
        maximum_difference = std::max(
            maximum_difference, std::abs(candidate[i] - reference[i]));
        maximum_reference = std::max(
            maximum_reference, std::abs(reference[i]));
    }
    return maximum_difference / maximum_reference;
}

double relative_difference(double candidate, double reference) {
    return std::abs(candidate - reference) /
        std::max(std::abs(reference), 1.0e-15);
}

void verify_reference_agreement(
    double target_reynolds,
    const SolveResult& normal,
    const SolveResult& reference) {
    const double speed_difference = maximum_relative_difference(
        normal.speed, reference.speed, 1.0e-12);
    const double pressure_difference = maximum_relative_difference(
        normal.pressure, reference.pressure, 1.0e-9);
    std::cout << std::setprecision(17)
              << "transition reference Re=" << target_reynolds
              << " normalOuter=" << normal.outer_iterations
              << " referenceOuter=" << reference.outer_iterations
              << " faceDifference=" << relative_difference(
                     normal.maximum_face_flow, reference.maximum_face_flow)
              << " speedDifference=" << speed_difference
              << " pressureDifference=" << pressure_difference
              << " referencePressureDrop=" << reference.pressure_drop << '\n';
    assert(normal.converged);
    assert(reference.converged);
    assert(normal.outer_iterations <= 40);
    assert(reference.outer_iterations <= 100);
    assert(normal.face_norm < kNormalTolerance);
    assert(normal.fan_norm < kNormalTolerance);
    assert(reference.face_norm < kReferenceTolerance);
    assert(reference.fan_norm < kReferenceTolerance);
    assert(std::abs(normal.imbalance) < 1.0e-7);
    assert(std::abs(reference.imbalance) < 1.0e-7);
    assert(relative_difference(
        normal.maximum_face_flow, reference.maximum_face_flow) < 2.0e-3);
    assert(relative_difference(
        normal.maximum_fan_flow, reference.maximum_fan_flow) < 2.0e-3);
    assert(relative_difference(normal.total_source, reference.total_source) <
           2.0e-3);
    assert(relative_difference(normal.total_vent, reference.total_vent) <
           2.0e-3);
    assert(speed_difference < 3.0e-3);
    assert(pressure_difference < 1.0e-2);

    const double achieved_reynolds =
        kDensity * reference.maximum_face_flow /
        (kCellWidthM * kViscosity);
    assert(std::abs(achieved_reynolds - target_reynolds) /
           target_reynolds < 1.0e-5);
}

void assert_invalid_mixed_norm(
    double update,
    double value,
    double relative_tolerance,
    double absolute_tolerance) {
    assert(std::isinf(FlowSolver::mixed_relative_update_norm(
        update, value, relative_tolerance, absolute_tolerance)));
}

} // namespace

int main() {
    constexpr double transition_start = 2000.0;
    constexpr double transition_end = 4000.0;
    static_assert(
        FlowSolver::transitional_reynolds_start() == transition_start);
    static_assert(FlowSolver::transitional_reynolds_end() == transition_end);

    // The correlations remain exact outside the transition band.
    assert(FlowSolver::smooth_pipe_friction_factor(1000.0) == 64.0 / 1000.0);
    assert(FlowSolver::smooth_pipe_friction_factor(transition_start) ==
           64.0 / transition_start);
    assert(FlowSolver::smooth_pipe_friction_factor(transition_end) ==
           haaland_smooth_pipe(transition_end));
    assert(FlowSolver::smooth_pipe_friction_factor(10000.0) ==
           haaland_smooth_pipe(10000.0));

    // Independent precomputed smoothstep values exercise the interior rather
    // than merely duplicating the production expression in the test.
    const std::array<std::pair<double, double>, 3> interior_values{{
        {2100.0, 0.030618073839359397},
        {3000.0, 0.032837693291988600},
        {3900.0, 0.040570043769288410}}};
    for(const auto& [reynolds, expected] : interior_values)
        assert(std::abs(
            FlowSolver::smooth_pipe_friction_factor(reynolds) - expected) <
            5.0e-15);

    const double old_laminar_at_switch = 64.0 / 2300.0;
    const double old_turbulent_at_switch = haaland_smooth_pipe(2300.0);
    assert(old_turbulent_at_switch / old_laminar_at_switch > 1.7);
    for(int reynolds = 2001; reynolds <= 4000; ++reynolds) {
        const double value =
            FlowSolver::smooth_pipe_friction_factor(reynolds);
        const double laminar = 64.0 / reynolds;
        const double turbulent = haaland_smooth_pipe(reynolds);
        assert(std::isfinite(value));
        assert(value >= std::min(laminar, turbulent));
        assert(value <= std::max(laminar, turbulent));
    }

    // One-sided numerical derivatives agree at both transition boundaries,
    // guarding the C1 endpoint behavior of the smoothstep blend.
    constexpr double derivative_step = 1.0e-2;
    for(const double boundary : {transition_start, transition_end}) {
        const double center =
            FlowSolver::smooth_pipe_friction_factor(boundary);
        const double left_derivative =
            (center - FlowSolver::smooth_pipe_friction_factor(
                boundary - derivative_step)) / derivative_step;
        const double right_derivative =
            (FlowSolver::smooth_pipe_friction_factor(
                boundary + derivative_step) - center) / derivative_step;
        assert(std::abs(left_derivative - right_derivative) < 5.0e-10);
    }

    // Mixed relative/absolute convergence is fail-closed for malformed input.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const double maximum_finite = DBL_MAX;
    const double minimum_positive = DBL_MIN;
    assert_invalid_mixed_norm(nan, 1.0, 1.0e-3, 1.0e-8);
    assert_invalid_mixed_norm(1.0, nan, 1.0e-3, 1.0e-8);
    assert_invalid_mixed_norm(1.0, 1.0, nan, 1.0e-8);
    assert_invalid_mixed_norm(1.0, 1.0, 1.0e-3, nan);
    assert_invalid_mixed_norm(infinity, 1.0, 1.0e-3, 1.0e-8);
    assert_invalid_mixed_norm(1.0, infinity, 1.0e-3, 1.0e-8);
    assert_invalid_mixed_norm(1.0, 1.0, infinity, 1.0e-8);
    assert_invalid_mixed_norm(1.0, 1.0, 1.0e-3, infinity);
    assert_invalid_mixed_norm(-1.0, 1.0, 1.0e-3, 1.0e-8);
    assert_invalid_mixed_norm(1.0, -1.0, 1.0e-3, 1.0e-8);
    assert_invalid_mixed_norm(1.0, 1.0, 0.0, 1.0e-8);
    assert_invalid_mixed_norm(1.0, 1.0, -1.0e-3, 1.0e-8);
    assert_invalid_mixed_norm(1.0, 1.0, 1.0e-3, 0.0);
    assert_invalid_mixed_norm(1.0, 1.0, 1.0e-3, -1.0e-8);
    // All inputs are finite and positive, but DBL_MAX/DBL_MIN overflows the
    // scale floor or final quotient. Both paths must still fail closed.
    assert_invalid_mixed_norm(
        1.0, minimum_positive, minimum_positive, maximum_finite);
    assert_invalid_mixed_norm(
        maximum_finite, minimum_positive, 1.0, minimum_positive);

    // A reversal below the absolute update tolerance no longer lets a
    // near-zero face dominate, while a material update in a low-flow branch
    // still fails the same 1e-3 relative gate.
    const double near_zero_chatter = FlowSolver::mixed_relative_update_norm(
        1.0e-9, 0.0, kNormalTolerance, kAbsoluteUpdateTolerance);
    const double settled_low_flow = FlowSolver::mixed_relative_update_norm(
        5.0e-9, 1.0e-6, kNormalTolerance, kAbsoluteUpdateTolerance);
    const double changing_low_flow = FlowSolver::mixed_relative_update_norm(
        1.1e-8, 1.0e-6, kNormalTolerance, kAbsoluteUpdateTolerance);
    const double changing_main_flow = FlowSolver::mixed_relative_update_norm(
        2.0e-6, 1.0e-3, kNormalTolerance, kAbsoluteUpdateTolerance);
    const double zero_flow = FlowSolver::mixed_relative_update_norm(
        0.0, 0.0, kNormalTolerance, kAbsoluteUpdateTolerance);
    assert(near_zero_chatter < kNormalTolerance);
    assert(settled_low_flow < kNormalTolerance);
    assert(changing_low_flow > kNormalTolerance);
    assert(changing_main_flow > kNormalTolerance);
    assert(std::isfinite(zero_flow) && zero_flow == 0.0);

    // Three tiny networks target the beginning, midpoint, and end of the
    // transitional band. These pressure-drop snapshots come from independent
    // converged fixture results after the final exact, continuity-checked flux
    // publication, not the public friction-factor helper. The 1e-6 Pa
    // allowance is about 6e-9 relative at the largest snapshot, while
    // the legacy Re=2300 switch moves these drops by approximately 0.03 Pa,
    // 5.7 Pa, and 0.15 Pa respectively; it therefore accommodates stable
    // compiler variation without allowing the old private wiring to return.
    const std::array<std::pair<double, double>, 3> pressure_drop_snapshots{{
        {2100.0, 44.763431621053343},
        {3000.0, 92.136803553092307},
        {3900.0, 161.94478709482399}}};
    for(const auto& [target_reynolds, expected_pressure_drop] :
        pressure_drop_snapshots) {
        const SolveResult normal = solve_case(
            target_reynolds, kNormalTolerance, 40);
        const SolveResult reference = solve_case(
            target_reynolds, kReferenceTolerance, 100);
        verify_reference_agreement(target_reynolds, normal, reference);
        assert(std::abs(reference.pressure_drop - expected_pressure_drop) <
               kPressureDropSnapshotTolerancePa);
    }

    // A curved boundary fan exercises the separately strict fan norm.  Its
    // operating point must move away from the free-air initial guess and agree
    // with the tight-reference solution.
    const SolveResult curved_normal = solve_case(
        3000.0, kNormalTolerance, 60, true);
    const SolveResult curved_reference = solve_case(
        3000.0, kReferenceTolerance, 120, true);
    assert(curved_normal.converged);
    assert(curved_reference.converged);
    assert(curved_normal.fan_norm > 0.0);
    assert(curved_normal.fan_norm < kNormalTolerance);
    assert(curved_reference.fan_norm < kReferenceTolerance);
    const double initial_fan_flow = source_flow_for_reynolds(3000.0) / 4.0;
    assert(relative_difference(
        curved_normal.maximum_fan_flow, initial_fan_flow) > 1.0e-2);
    assert(curved_normal.maximum_fan_flow > 0.0);
    assert(curved_normal.maximum_fan_flow < kCurveA / kCurveB);
    assert(relative_difference(
        curved_normal.maximum_fan_flow,
        curved_reference.maximum_fan_flow) < 3.0e-3);
    assert(relative_difference(
        curved_normal.maximum_face_flow,
        curved_reference.maximum_face_flow) < 3.0e-3);
    assert(maximum_relative_difference(
        curved_normal.speed, curved_reference.speed, 1.0e-12) < 5.0e-3);
    assert(maximum_relative_difference(
        curved_normal.pressure, curved_reference.pressure, 1.0e-9) < 5.0e-3);
    assert(std::abs(curved_normal.imbalance) < 1.0e-7);
    assert(std::abs(curved_reference.imbalance) < 1.0e-7);

    std::cout << "flow_nonlinear_convergence_test PASSED: "
              << "three transition references + curved fan; curvedOuter="
              << curved_normal.outer_iterations
              << " curvedFaceNorm=" << curved_normal.face_norm
              << " curvedFanNorm=" << curved_normal.fan_norm
              << " curvedMaxFace=" << curved_normal.maximum_face_flow
              << " curvedMaxFan=" << curved_normal.maximum_fan_flow
              << " curvedImbalance=" << curved_normal.imbalance << '\n';
}
