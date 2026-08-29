#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "../src/environment.hpp"
#include "../src/component.hpp"
#include "../src/fan.hpp"
#include "../src/mesh.hpp"
#include "../src/rack.hpp"
#include "../src/vent.hpp"
#include "../src/workload.hpp"

namespace {

constexpr double kWidthM = 0.012;
constexpr double kDepthM = 0.024;
constexpr double kHeightM = 0.012;
constexpr double kReferenceFlowM3s = 0.020;
constexpr double kCurveA = 80.0;
constexpr double kCurveB = 1000.0;
constexpr double kCurveC = 20000.0;

Environment make_environment() {
    return Environment(
        30.0, 5800.0, 20.0, 1005.0, 0.02587,
        1.81e-5, 0.71, 0.9833);
}

Workload make_workload() {
    return Workload(1000, 10000, 1000, 4);
}

Rack make_rack() {
    Rack rack = Rack::from_meters(kWidthM, kDepthM, kHeightM);
    rack.set_t(20.0);
    rack.set_cp(1005.0);
    rack.set_k(0.02587);
    rack.set_rho(0.9833);
    return rack;
}

Fan make_fan(double curve_c = kCurveC) {
    Fan fan(
        "parallel boundary fan",
        kReferenceFlowM3s / Fan::CFM_TO_M3S,
        0.0,
        {kWidthM, 0.0, kHeightM},
        {0.5 * kWidthM, 0.0, 0.5 * kHeightM},
        {0.0, 1.0, 0.0},
        FlowType::Intake,
        ShapeType::Rectangular);
    fan.set_curve(kCurveA, kCurveB, curve_c, 1.2);
    return fan;
}

bool nearly_equal(double actual, double expected, double relative = 1.0e-12) {
    return std::abs(actual - expected) <= relative *
        std::max({1.0, std::abs(actual), std::abs(expected)});
}

std::size_t verify_parallel_curve(Mesh mesh, std::size_t expected_count) {
    const Fan fan = make_fan();
    mesh.stamp_fan(fan);

    std::size_t count = 0;
    double geometric_area_sum = 0.0;
    double reference_flow_sum = 0.0;
    double stamped_area_sum = 0.0;
    for(const Cell& cell : mesh.get_cells()) {
        if(!cell.has_fan_curve()) continue;
        ++count;
        geometric_area_sum += cell.area_y();
        reference_flow_sum += cell.get_fan_Q_ref();
        stamped_area_sum += cell.get_fan_area();
    }
    assert(count == expected_count);
    assert(geometric_area_sum > 0.0);
    assert(nearly_equal(reference_flow_sum, kReferenceFlowM3s));
    assert(nearly_equal(stamped_area_sum, fan.area()));

    const double total_zero = fan_curve_first_positive_zero(
        kCurveA, kCurveB, kCurveC);
    const double sample_total_flow = 0.01;
    for(const Cell& cell : mesh.get_cells()) {
        if(!cell.has_fan_curve()) continue;
        const double weight = cell.area_y() / geometric_area_sum;
        const double expected_area_share = fan.area() * weight;
        assert(nearly_equal(cell.get_fan_curve_a(), kCurveA));
        assert(nearly_equal(
            cell.get_fan_curve_b(), kCurveB / weight));
        assert(nearly_equal(
            cell.get_fan_curve_c(),
            kCurveC / (weight * weight)));
        assert(nearly_equal(
            cell.get_fan_Q_ref(), kReferenceFlowM3s * weight));
        assert(nearly_equal(cell.get_fan_area(), expected_area_share));
        assert(nearly_equal(
            cell.get_fan_Q_ref() / cell.get_fan_area(),
            kReferenceFlowM3s / fan.area()));

        const double per_cell_zero = fan_curve_first_positive_zero(
            cell.get_fan_curve_a(), cell.get_fan_curve_b(),
            cell.get_fan_curve_c());
        assert(nearly_equal(per_cell_zero / weight, total_zero));

        const double per_cell_pressure = bounded_fan_curve_pressure(
            cell.get_fan_curve_a(), cell.get_fan_curve_b(),
            cell.get_fan_curve_c(), sample_total_flow * weight);
        const double total_curve_pressure = bounded_fan_curve_pressure(
            kCurveA, kCurveB, kCurveC, sample_total_flow);
        assert(nearly_equal(per_cell_pressure, total_curve_pressure));
    }
    return count;
}

Mesh make_adaptive_mesh(
    const Rack& rack,
    const Environment& environment,
    const Workload& workload) {
    return Mesh().build_adaptive_mesh(
        rack,
        {0.003, 0.003, 0.006},
        {0.006, 0.006, 0.012},
        {0.006, 0.003, 0.003},
        environment,
        workload);
}

Component make_air_component(const std::string& name) {
    Component component = Component::from_meters(
        kWidthM, kDepthM, kHeightM, name);
    component.set_coords_m(0.0, 0.0, 0.0);
    component.set_t(20.0);
    component.set_rho_solid(2700.0);
    component.set_cp(900.0);
    component.set_k_solid(150.0);
    component.set_watts(0.0);
    component.add_region(InternalRegion(
        "complete internal air",
        {kWidthM, kDepthM, kHeightM},
        {0.0, 0.0, 0.0}));
    return component;
}

void verify_internal_fan_area_weights(
    const Rack& rack,
    const Environment& environment,
    const Workload& workload) {
    Mesh mesh = make_adaptive_mesh(rack, environment, workload);
    Component component = make_air_component("adaptive internal fan fixture");
    Fan fan(
        "adaptive internal fan",
        kReferenceFlowM3s / Fan::CFM_TO_M3S,
        0.0,
        {kWidthM, 0.0, kHeightM},
        {0.5 * kWidthM, 0.012, 0.5 * kHeightM},
        {0.0, 1.0, 0.0},
        FlowType::Intake,
        ShapeType::Rectangular);
    fan.set_curve(kCurveA, kCurveB, kCurveC, 1.2);
    component.add_region(InternalRegion(fan));
    component.order_internal_regions();
    mesh.stamp_component_adaptive(component);

    const auto& interfaces = mesh.get_internal_fans();
    assert(interfaces.size() == 9);
    double geometric_area_sum = 0.0;
    for(const Mesh::InternalFanInterface& interface : interfaces) {
        geometric_area_sum += mesh.at(
            interface.downstream[0], interface.downstream[1],
            interface.downstream[2]).area_y();
    }
    assert(geometric_area_sum > 0.0);

    double reference_flow_sum = 0.0;
    double fixed_flow_sum = 0.0;
    for(const Mesh::InternalFanInterface& interface : interfaces) {
        const Cell& cell = mesh.at(
            interface.downstream[0], interface.downstream[1],
            interface.downstream[2]);
        const double weight = cell.area_y() / geometric_area_sum;
        reference_flow_sum += interface.q_ref;
        fixed_flow_sum += interface.flow_m3s;
        assert(nearly_equal(interface.q_ref, kReferenceFlowM3s * weight));
        assert(nearly_equal(interface.flow_m3s, kReferenceFlowM3s * weight));
        assert(nearly_equal(interface.curve_a, kCurveA));
        assert(nearly_equal(interface.curve_b, kCurveB / weight));
        assert(nearly_equal(
            interface.curve_c, kCurveC / (weight * weight)));
        assert(nearly_equal(
            interface.q_ref / cell.area_y(),
            kReferenceFlowM3s / geometric_area_sum));
    }
    assert(nearly_equal(reference_flow_sum, kReferenceFlowM3s));
    assert(nearly_equal(fixed_flow_sum, kReferenceFlowM3s));
}

void verify_vent_area_weights(
    const Rack& rack,
    const Environment& environment,
    const Workload& workload) {
    constexpr double free_area_ratio = 0.5;
    constexpr double discharge_coefficient = 0.82;
    const double expected_cda =
        kWidthM * kHeightM * free_area_ratio * discharge_coefficient;

    auto check_mesh = [&](const Mesh& mesh) {
        std::size_t count = 0;
        double geometric_area_sum = 0.0;
        double conductance_sum = 0.0;
        for(const Cell& cell : mesh.get_cells()) {
            if(cell.get_state() != Cell::State::Vent) continue;
            ++count;
            geometric_area_sum += cell.area_y();
            conductance_sum += cell.get_vent_conductance();
        }
        assert(count == 9);
        assert(geometric_area_sum > 0.0);
        assert(nearly_equal(conductance_sum, expected_cda));
        for(const Cell& cell : mesh.get_cells()) {
            if(cell.get_state() != Cell::State::Vent) continue;
            const double weight = cell.area_y() / geometric_area_sum;
            assert(nearly_equal(
                cell.get_vent_conductance(), expected_cda * weight));
        }
    };

    Mesh boundary_mesh = make_adaptive_mesh(rack, environment, workload);
    Vent boundary_vent(
        "adaptive boundary vent",
        {kWidthM, 0.0, kHeightM},
        free_area_ratio,
        0.0,
        discharge_coefficient,
        {0.5 * kWidthM, 0.0, 0.5 * kHeightM},
        {0.0, 1.0, 0.0},
        VentShapeType::Rectangular);
    boundary_mesh.stamp_vent_adaptive(boundary_vent);
    check_mesh(boundary_mesh);

    Mesh internal_mesh = make_adaptive_mesh(rack, environment, workload);
    Component component = make_air_component("adaptive internal vent fixture");
    component.add_region(InternalRegion(
        "adaptive internal vent",
        {kWidthM, 0.0, kHeightM},
        {0.5 * kWidthM, 0.0, 0.5 * kHeightM},
        {0.0, 1.0, 0.0},
        free_area_ratio,
        discharge_coefficient));
    component.order_internal_regions();
    internal_mesh.stamp_component_adaptive(component);
    check_mesh(internal_mesh);
}

void verify_circular_internal_vent_support() {
    constexpr double extent = 0.080;
    constexpr double depth = 0.020;
    constexpr double diameter = 0.060;
    constexpr double free_area_ratio = 0.5;
    constexpr double discharge_coefficient = 0.82;
    constexpr double pi = 3.14159265358979323846;
    const double requested_area = pi * diameter * diameter / 4.0;
    const double expected_cda =
        requested_area * free_area_ratio * discharge_coefficient;

    Environment environment = make_environment();
    Workload workload = make_workload();
    Rack rack = Rack::from_meters(extent, depth, extent);
    rack.set_t(20.0);
    rack.set_cp(1005.0);
    rack.set_k(0.02587);
    rack.set_rho(0.9833);
    Mesh mesh = Mesh().build_adaptive_mesh(
        rack,
        std::vector<double>(16, 0.005),
        {0.005, 0.015},
        std::vector<double>(16, 0.005),
        environment,
        workload);

    Component component = Component::from_meters(
        extent, depth, extent, "circular internal vent fixture");
    component.set_coords_m(0.0, 0.0, 0.0);
    component.set_t(20.0);
    component.set_rho_solid(2700.0);
    component.set_cp(900.0);
    component.set_k_solid(150.0);
    component.set_watts(0.0);
    component.add_region(InternalRegion(
        "complete circular fixture air",
        {extent, depth, extent},
        {0.0, 0.0, 0.0}));
    Vent vent(
        "circular internal vent",
        {0.0, 0.0, 0.0},
        free_area_ratio,
        diameter,
        discharge_coefficient,
        {0.5 * extent, 0.0, 0.5 * extent},
        {0.0, 1.0, 0.0},
        VentShapeType::Circular);
    const InternalRegion vent_region(vent);
    assert(nearly_equal(
        vent_region.free_area(), requested_area * free_area_ratio));
    component.add_region(vent_region);
    component.order_internal_regions();
    mesh.stamp_component_adaptive(component);

    std::size_t count = 0;
    double realized_area = 0.0;
    double conductance_sum = 0.0;
    for(const Cell& cell : mesh.get_cells()) {
        if(cell.get_state() != Cell::State::Vent) continue;
        ++count;
        realized_area += cell.area_y();
        conductance_sum += cell.get_vent_conductance();
    }
    assert(count > 0);
    assert(std::abs(realized_area / requested_area - 1.0) <= 0.02);
    assert(nearly_equal(conductance_sum, expected_cda));
    for(const Cell& cell : mesh.get_cells()) {
        if(cell.get_state() != Cell::State::Vent) continue;
        const double weight = cell.area_y() / realized_area;
        assert(nearly_equal(
            cell.get_vent_conductance(), expected_cda * weight));
    }
}

void verify_blocked_internal_vent_fails_closed() {
    constexpr double extent = 0.020;
    Environment environment = make_environment();
    Workload workload = make_workload();
    Rack rack = Rack::from_meters(extent, extent, extent);
    rack.set_t(20.0);
    rack.set_cp(1005.0);
    rack.set_k(0.02587);
    rack.set_rho(0.9833);
    Mesh mesh = Mesh().build_adaptive_mesh(
        rack,
        {0.005, 0.015},
        {0.005, 0.015},
        {0.005, 0.015},
        environment,
        workload);

    Component component = Component::from_meters(
        extent, extent, extent, "blocked vent fixture");
    component.set_coords_m(0.0, 0.0, 0.0);
    component.set_t(20.0);
    component.set_rho_solid(2700.0);
    component.set_cp(900.0);
    component.set_k_solid(150.0);
    component.set_watts(0.0);
    component.add_region(InternalRegion(
        "blocked full-face vent",
        {extent, 0.0, extent},
        {0.5 * extent, 0.0, 0.5 * extent},
        {0.0, 1.0, 0.0},
        0.5,
        0.82));
    component.order_internal_regions();

    bool rejected = false;
    try {
        mesh.stamp_component_adaptive(component);
    } catch(const std::runtime_error& error) {
        const std::string message = error.what();
        rejected = message.find("blocked full-face vent") != std::string::npos &&
            message.find("no fluid path") != std::string::npos;
    }
    assert(rejected);
}

void verify_underresolved_circle_fails_closed() {
    constexpr double extent = 0.024;
    constexpr double depth = 0.012;
    constexpr double diameter = 0.012;
    Environment environment = make_environment();
    Workload workload = make_workload();
    Rack rack = Rack::from_meters(extent, depth, extent);
    rack.set_t(20.0);
    rack.set_cp(1005.0);
    rack.set_k(0.02587);
    rack.set_rho(0.9833);
    Mesh mesh = Mesh().build_mesh(
        rack, 0.006, 0.006, 0.006, environment, workload);
    Vent vent(
        "under-resolved circle",
        {0.0, 0.0, 0.0},
        0.5,
        diameter,
        0.82,
        {0.5 * extent, 0.0, 0.5 * extent},
        {0.0, 1.0, 0.0},
        VentShapeType::Circular);

    bool rejected = false;
    try {
        mesh.stamp_vent(vent);
    } catch(const std::runtime_error& error) {
        const std::string message = error.what();
        rejected = message.find("under-resolved circle") != std::string::npos &&
            message.find("circular footprint is under-resolved") !=
                std::string::npos &&
            message.find("5%") != std::string::npos;
    }
    assert(rejected);
}

} // namespace

int main() {
    const Rack rack = make_rack();
    const Environment environment = make_environment();
    const Workload workload = make_workload();

    Mesh coarse = Mesh().build_mesh(
        rack, 0.006, 0.006, 0.006, environment, workload);
    Mesh fine = Mesh().build_mesh(
        rack, 0.003, 0.003, 0.003, environment, workload);
    Mesh adaptive = make_adaptive_mesh(rack, environment, workload);

    const std::size_t coarse_count = verify_parallel_curve(coarse, 4);
    const std::size_t fine_count = verify_parallel_curve(fine, 16);
    const std::size_t adaptive_count = verify_parallel_curve(adaptive, 9);
    verify_internal_fan_area_weights(rack, environment, workload);
    verify_vent_area_weights(rack, environment, workload);
    verify_circular_internal_vent_support();
    verify_blocked_internal_vent_fails_closed();
    verify_underresolved_circle_fails_closed();

    // Curve scaling must fail closed if an otherwise finite coefficient
    // cannot be represented after conversion to a per-cell parallel curve.
    bool overflow_rejected = false;
    try {
        Mesh overflow_mesh = Mesh().build_mesh(
            rack, 0.006, 0.006, 0.006, environment, workload);
        overflow_mesh.stamp_fan(
            make_fan(std::numeric_limits<double>::max()));
    } catch(const std::runtime_error&) {
        overflow_rejected = true;
    }
    assert(overflow_rejected);

    std::cout
        << "boundary_fan_curve_discretization_test PASSED: coarse="
        << coarse_count << " fine=" << fine_count
        << " adaptive=" << adaptive_count
        << " aggregate curve, fan/vent area weights, and overflow guards verified\n";
}
