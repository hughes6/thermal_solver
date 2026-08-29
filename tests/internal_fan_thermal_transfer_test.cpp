#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/solver.hpp"

namespace {

constexpr double kAreaM2 = 0.01;
constexpr double kFlowM3s = 1.0e-3;
constexpr double kDensity = 0.9833;
constexpr double kCp = 1005.5;
constexpr double kDt = 1.0e-4;

struct CaseResult {
    double operating_flow = 0.0;
    double upstream_initial = 0.0;
    double upstream_final = 0.0;
    double downstream_initial = 0.0;
    double downstream_final = 0.0;
    double downstream_initial_capacity_j_per_k = 0.0;
    double frozen_capacity_energy_change_j = 0.0;
    double maximum_absolute_temperature_change = 0.0;
};

struct TemporaryCsv {
    std::filesystem::path path;

    ~TemporaryCsv() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
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

CaseResult run_case(
    bool adaptive, bool curved, int axis, int sign,
    double upstream_temperature, double downstream_temperature,
    double ambient_temperature, const std::string& label) {
    std::array<double, 3> rack_size{0.1, 0.1, 0.1};
    rack_size[axis] = 0.4;
    Rack rack = Rack::from_meters(
        rack_size[0], rack_size[1], rack_size[2], label);
    rack.set_t(ambient_temperature);
    rack.set_cp(kCp);
    rack.set_k(1.0e-30);
    rack.set_rho(kDensity);
    Environment environment(
        30.0, 5500.0, ambient_temperature, kCp,
        1.0e-30, 1.81e-5, 0.71, kDensity);
    Workload workload(100, 100000, 1000, 16);

    Mesh mesh;
    if(adaptive) {
        std::array<std::vector<double>, 3> widths{
            std::vector<double>{0.1},
            std::vector<double>{0.1},
            std::vector<double>{0.1}};
        // Deliberately give the two fan-adjacent cells different widths.
        // Conservative face-flux advection uses q/V_downstream; the legacy
        // cell-velocity/center-distance stencil would use the average center
        // spacing and therefore fail the adaptive tracer cases.
        widths[axis] = {0.08, 0.10, 0.14, 0.08};
        mesh = Mesh().build_adaptive_mesh(
            rack, widths[0], widths[1], widths[2],
            environment, workload);
    } else {
        mesh = Mesh().build_mesh(
            rack, 0.1, 0.1, 0.1, environment, workload);
    }

    for(const int end : {0, 3}) {
        const auto coordinate = cell_on_axis(axis, end);
        Cell& vent = mesh.at(
            coordinate[0], coordinate[1], coordinate[2]);
        vent.set_state(Cell::State::Vent);
        vent.set_vent_conductance(kAreaM2);
    }

    const int upstream_index = sign > 0 ? 1 : 2;
    const int downstream_index = sign > 0 ? 2 : 1;
    const auto upstream = cell_on_axis(axis, upstream_index);
    const auto downstream = cell_on_axis(axis, downstream_index);
    mesh.get_internal_fans().push_back(
        {upstream, downstream, kFlowM3s,
         direction_on_axis(axis, sign),
         curved ? 1.0 : 0.0,
         curved ? 500.0 : 0.0,
         0.0, kDensity, kFlowM3s});

    std::vector<double> initial_temperature;
    std::vector<double> initial_capacity;
    initial_temperature.reserve(mesh.get_cell_count());
    initial_capacity.reserve(mesh.get_cell_count());
    for(int index = 0; index < 4; ++index) {
        const auto coordinate = cell_on_axis(axis, index);
        Cell& cell = mesh.at(
            coordinate[0], coordinate[1], coordinate[2]);
        const bool on_upstream_side = sign > 0 ? index <= 1 : index >= 2;
        cell.set_T(on_upstream_side
            ? upstream_temperature : downstream_temperature);
        cell.set_k(1.0e-30);
        cell.set_rho(kDensity);
        cell.set_cp(kCp);
        initial_temperature.push_back(cell.get_T());
        initial_capacity.push_back(
            cell.get_rho() * cell.get_cp() * cell.volume());
    }

    TemporaryCsv output{
        std::filesystem::temp_directory_path() /
        ("thermal_internal_fan_transfer_" + label + ".csv")};
    Solver solver(
        std::move(mesh), kDt, kDt, false, 1, -1,
        4.6, 1.0e-12, 2000, 1.1, 100, 1.0e-8,
        false, 0.8, 10000, output.path.string(), "pcg", true);
    solver.solve();

    const Mesh& result_mesh = solver.get_mesh();
    const auto& fan = result_mesh.get_internal_fans().front();
    CaseResult result;
    result.operating_flow = curved ? fan.q_ref : fan.flow_m3s;
    result.upstream_initial = upstream_temperature;
    result.upstream_final = result_mesh.at(
        upstream[0], upstream[1], upstream[2]).get_T();
    result.downstream_initial = downstream_temperature;
    result.downstream_final = result_mesh.at(
        downstream[0], downstream[1], downstream[2]).get_T();
    result.downstream_initial_capacity_j_per_k =
        initial_capacity[static_cast<std::size_t>(downstream_index)];

    for(int index = 0; index < 4; ++index) {
        const auto coordinate = cell_on_axis(axis, index);
        const double final_temperature = result_mesh.at(
            coordinate[0], coordinate[1], coordinate[2]).get_T();
        require(std::isfinite(final_temperature),
                label + " produced a non-finite final temperature");
        const double temperature_change =
            final_temperature - initial_temperature[index];
        // The explicit update evaluates both transported enthalpy and each
        // cell's denominator from beginning-of-step rho/cp. Air density is
        // refreshed after T_new, so using final rho here would not audit the
        // energy equation that was actually advanced.
        result.frozen_capacity_energy_change_j +=
            initial_capacity[index] * temperature_change;
        result.maximum_absolute_temperature_change = std::max(
            result.maximum_absolute_temperature_change,
            std::abs(temperature_change));
    }
    return result;
}

void verify_tracer(bool adaptive, bool curved, int axis, int sign) {
    const std::string label =
        std::string(adaptive ? "adaptive" : "uniform") +
        (curved ? "_curve_" : "_fixed_") +
        std::to_string(axis) + (sign > 0 ? "_pos" : "_neg");
    const CaseResult result = run_case(
        adaptive, curved, axis, sign, 40.0, 20.0, 40.0, label);
    require(result.operating_flow > 0.0 &&
            std::isfinite(result.operating_flow),
            label + " has invalid fan flow");
    const double expected_rate = result.operating_flow * kDensity * kCp *
        (40.0 - 20.0) /
        result.downstream_initial_capacity_j_per_k;
    const double measured_rate =
        (result.downstream_final - result.downstream_initial) / kDt;
    require(std::abs(measured_rate - expected_rate) <=
                std::max(1.0e-8, 1.0e-8 * expected_rate),
            label + " does not advect the fan's upstream temperature at q/V; " +
            "measured=" + std::to_string(measured_rate) +
            " expected=" + std::to_string(expected_rate));
    require(std::abs(result.upstream_final - result.upstream_initial) <=
                1.0e-9,
            label + " changed the hot upstream cell despite equal-temperature "
                    "ambient replacement flow");

    const double expected_boundary_energy =
        kDt * result.operating_flow * kDensity * kCp * (40.0 - 20.0);
    require(std::abs(result.frozen_capacity_energy_change_j -
                     expected_boundary_energy) <=
                std::max(1.0e-9, 1.0e-8 * expected_boundary_energy),
            label + " fails the frozen-capacity one-step advective enthalpy "
                    "balance");
}

void verify_uniform_temperature_invariant(
    bool adaptive, bool curved, int axis, int sign) {
    const std::string label =
        std::string(adaptive ? "adaptive_uniform" : "uniform_uniform") +
        (curved ? "_curve_" : "_fixed_") +
        std::to_string(axis) + (sign > 0 ? "_pos" : "_neg");
    const CaseResult result = run_case(
        adaptive, curved, axis, sign, 30.0, 30.0, 30.0, label);
    require(result.maximum_absolute_temperature_change <= 1.0e-10,
            label + " changed at least one cell in a spatially uniform "
                    "temperature field");
    require(std::abs(result.frozen_capacity_energy_change_j) <= 1.0e-10,
            label + " created frozen-capacity energy in a uniform "
                    "temperature field");
}

} // namespace

int main() {
    try {
        std::size_t tracer_cases = 0;
        for(const bool adaptive : {false, true})
            for(const bool curved : {false, true})
                for(int axis = 0; axis < 3; ++axis)
                    for(const int sign : {-1, 1}) {
                        verify_tracer(adaptive, curved, axis, sign);
                        ++tracer_cases;
                    }
        std::size_t invariant_cases = 0;
        for(const bool adaptive : {false, true})
            for(const bool curved : {false, true})
                for(int axis = 0; axis < 3; ++axis)
                    for(const int sign : {-1, 1}) {
                        verify_uniform_temperature_invariant(
                            adaptive, curved, axis, sign);
                        ++invariant_cases;
                    }
        std::cout
            << "internal_fan_thermal_transfer_test PASSED: "
            << tracer_cases
            << " tracer cases plus " << invariant_cases
            << " uniform-field invariants\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "internal_fan_thermal_transfer_test FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}
