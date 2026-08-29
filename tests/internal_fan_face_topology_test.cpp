#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/environment.hpp"
#include "../src/flow_solver.hpp"
#include "../src/mesh.hpp"
#include "../src/rack.hpp"
#include "../src/workload.hpp"

namespace {

constexpr double kAmbientTemperatureC = 20.0;
constexpr double kDensity = 0.9833;
constexpr double kCrossSectionAreaM2 = 0.01;
constexpr double kFixedFlowM3s = 1.0e-3;
constexpr double kCurveA = 1.0;
constexpr double kCurveB = 500.0;
constexpr double kCurveC = 0.0;

enum class GridKind { Uniform, Adaptive };

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if(!condition) fail(message);
}

bool close_flow(double actual, double expected) {
    return std::isfinite(actual) && std::isfinite(expected) &&
           std::abs(actual - expected) <=
               std::max(1.0e-9, 2.0e-3 * std::max(std::abs(actual),
                                                  std::abs(expected)));
}

std::array<int, 3> cell_on_axis(int axis, int index) {
    std::array<int, 3> coordinate{0, 0, 0};
    coordinate[axis] = index;
    return coordinate;
}

std::array<double, 3> direction_on_axis(int axis, int sign) {
    std::array<double, 3> direction{0.0, 0.0, 0.0};
    direction[axis] = static_cast<double>(sign);
    return direction;
}

double velocity_component(const Cell& cell, int axis) {
    if(axis == 0) return cell.get_vx();
    if(axis == 1) return cell.get_vy();
    return cell.get_vz();
}

double face_area(const Cell& cell, int axis) {
    if(axis == 0) return cell.area_x();
    if(axis == 1) return cell.area_y();
    return cell.area_z();
}

std::string case_label(GridKind grid, bool curved, int axis, int sign) {
    static const char* axis_names[] = {"x", "y", "z"};
    std::ostringstream label;
    label << (grid == GridKind::Uniform ? "uniform" : "adaptive")
          << '_' << (curved ? "curve" : "fixed")
          << '_' << axis_names[axis]
          << '_' << (sign > 0 ? "positive" : "negative");
    return label.str();
}

Mesh make_fixture(GridKind grid, bool curved, int axis, int sign) {
    require(axis >= 0 && axis < 3, "fixture axis is out of range");
    require(sign == -1 || sign == 1, "fixture direction sign is invalid");

    std::array<double, 3> rack_size{0.1, 0.1, 0.1};
    rack_size[axis] = 0.4;
    Rack rack = Rack::from_meters(
        rack_size[0], rack_size[1], rack_size[2],
        case_label(grid, curved, axis, sign));
    rack.set_t(kAmbientTemperatureC);
    rack.set_cp(1005.5);
    rack.set_k(0.02587);
    rack.set_rho(kDensity);

    Environment environment(
        30.0, 5500.0, kAmbientTemperatureC, 1005.5,
        0.02587, 1.81e-5, 0.71, kDensity);
    Workload workload(1, 8, 4, 16);

    Mesh mesh;
    if(grid == GridKind::Uniform) {
        mesh = Mesh().build_mesh(
            rack, 0.1, 0.1, 0.1, environment, workload);
    } else {
        std::array<std::vector<double>, 3> widths{
            std::vector<double>{0.1},
            std::vector<double>{0.1},
            std::vector<double>{0.1}};
        widths[axis] = {0.08, 0.12, 0.12, 0.08};
        mesh = Mesh().build_adaptive_mesh(
            rack, widths[0], widths[1], widths[2],
            environment, workload);
    }

    const auto low_end = cell_on_axis(axis, 0);
    const auto high_end = cell_on_axis(axis, 3);
    for(const auto& coordinate : {low_end, high_end}) {
        Cell& vent = mesh.at(coordinate[0], coordinate[1], coordinate[2]);
        vent.set_state(Cell::State::Vent);
        vent.set_vent_conductance(kCrossSectionAreaM2);
    }

    const auto upstream = cell_on_axis(axis, sign > 0 ? 1 : 2);
    const auto downstream = cell_on_axis(axis, sign > 0 ? 2 : 1);
    mesh.get_internal_fans().push_back(
        {upstream, downstream, kFixedFlowM3s,
         direction_on_axis(axis, sign),
         curved ? kCurveA : 0.0,
         curved ? kCurveB : 0.0,
         curved ? kCurveC : 0.0,
         kDensity, kFixedFlowM3s});
    return mesh;
}

void run_case(GridKind grid, bool curved, int axis, int sign) {
    const std::string label = case_label(grid, curved, axis, sign);
    Mesh mesh = make_fixture(grid, curved, axis, sign);
    FlowSolver solver(
        mesh, 4.6, 1.0e-10, 2000, 1.1,
        100, 1.0e-5, "pcg");
    solver.solve();

    require(solver.converged(), label + " nonlinear flow did not converge");
    require(mesh.get_internal_fans().size() == 1,
            label + " changed the fixture's internal-fan inventory");
    const Mesh::InternalFanInterface& fan =
        mesh.get_internal_fans().front();
    const double operating_flow = curved ? fan.q_ref : fan.flow_m3s;
    require(std::isfinite(operating_flow) && operating_flow > 0.0,
            label + " produced an invalid fan operating flow");

    // The accessor is signed in the supplied endpoint orientation. Positive
    // therefore means upstream-to-downstream for both positive- and
    // negative-coordinate fans.
    const double signed_fan_face_flux =
        solver.internal_fan_face_flux_m3s(fan.upstream, fan.downstream);
    require(close_flow(signed_fan_face_flux, operating_flow),
            label + " oriented fan-face flux does not equal operating flow");
    require(signed_fan_face_flux > 0.0,
            label + " fan-face flux reversed against the fan direction");

    require(solver.ordinary_face_is_suppressed_for_internal_fan(
                fan.upstream, fan.downstream),
            label + " retained an ordinary pressure/flow link in parallel "
                    "with the internal fan");

    const Cell& upstream_cell = mesh.at(
        fan.upstream[0], fan.upstream[1], fan.upstream[2]);
    const Cell& downstream_cell = mesh.at(
        fan.downstream[0], fan.downstream[1], fan.downstream[2]);
    require(std::abs(face_area(upstream_cell, axis) -
                     kCrossSectionAreaM2) < 1.0e-14,
            label + " fixture cross-sectional area changed");

    // In this one-dimensional duct the two faces of each fan-adjacent cell
    // carry the same through-flow. Cell velocity times area therefore measures
    // the end-leg throughput without exposing unrelated raw face arrays.
    const double upstream_throughput = static_cast<double>(sign) *
        velocity_component(upstream_cell, axis) *
        face_area(upstream_cell, axis);
    const double downstream_throughput = static_cast<double>(sign) *
        velocity_component(downstream_cell, axis) *
        face_area(downstream_cell, axis);
    require(upstream_throughput > 0.0 && downstream_throughput > 0.0,
            label + " stored cell velocity points opposite the fan direction");
    require(close_flow(upstream_throughput, operating_flow),
            label + " upstream end throughput differs from fan operating flow");
    require(close_flow(downstream_throughput, operating_flow),
            label + " downstream end throughput differs from fan operating flow");
    require(close_flow(upstream_throughput, downstream_throughput),
            label + " loses continuity across the fan interface");

    require(std::isfinite(solver.mass_imbalance_m3s()) &&
                std::abs(solver.mass_imbalance_m3s()) <= 1.0e-8,
            label + " global mass continuity did not close");
}

} // namespace

int main() {
    try {
        std::size_t cases = 0;
        for(const GridKind grid : {GridKind::Uniform, GridKind::Adaptive}) {
            for(const bool curved : {false, true}) {
                for(int axis = 0; axis < 3; ++axis) {
                    for(const int sign : {-1, 1}) {
                        run_case(grid, curved, axis, sign);
                        ++cases;
                    }
                }
            }
        }
        std::cout << "internal_fan_face_topology_test PASSED: "
                  << cases
                  << " uniform/adaptive fixed/curve signed-axis cases\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "internal_fan_face_topology_test FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}
