#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

#include "../src/environment.hpp"
#include "../src/fan.hpp"
#include "../src/flow_solver.hpp"
#include "../src/mesh.hpp"
#include "../src/rack.hpp"
#include "../src/vent.hpp"
#include "../src/workload.hpp"

static Mesh make_case() {
    Environment env(
        30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
    Workload load(10000, 1000000, 100000, 4);
    Rack rack = Rack::from_meters(0.10, 0.10, 0.20);
    rack.set_t(20.0);
    rack.set_cp(1005.0);
    rack.set_k(0.02587);
    rack.set_rho(1.225);
    Mesh mesh =
        Mesh().build_mesh(rack, 0.05, 0.05, 0.05, env, load);

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
        {1.0, 0.0, 0.0},
        VentShapeType::Rectangular);
    mesh.stamp_fan(intake);
    mesh.stamp_vent(outlet);
    return mesh;
}

static Mesh make_adaptive_case() {
    Environment env(
        30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
    Workload load(10000, 1000000, 100000, 4);
    Rack rack = Rack::from_meters(0.10, 0.10, 0.20);
    rack.set_t(20.0);
    rack.set_cp(1005.0);
    rack.set_k(0.02587);
    rack.set_rho(1.225);
    Mesh mesh = Mesh().build_adaptive_mesh(
        rack,
        {0.04, 0.06},
        {0.03, 0.07},
        {0.04, 0.06, 0.10},
        env, load);

    Fan intake(
        "Adaptive intake", 1.0, 0.0,
        {0.0, 0.10, 0.10},
        {0.0, 0.05, 0.05},
        {1.0, 0.0, 0.0},
        FlowType::Intake,
        ShapeType::Rectangular);
    Vent outlet(
        "Adaptive outlet", {0.0, 0.10, 0.10}, 1.0, 0.0, 0.5,
        {0.10, 0.05, 0.05},
        {1.0, 0.0, 0.0},
        VentShapeType::Rectangular);
    mesh.stamp_fan_adaptive(intake);
    mesh.stamp_vent_adaptive(outlet);
    return mesh;
}

static double compare_pressure_fields(const Mesh& a, const Mesh& b) {
    assert(a.get_cells().size() == b.get_cells().size());
    double maximum = 0.0;
    for(size_t i = 0; i < a.get_cells().size(); ++i)
        maximum = std::max(
            maximum,
            std::abs(a.get_cells()[i].get_pressure() -
                     b.get_cells()[i].get_pressure()));
    return maximum;
}

int main() {
    // The omitted method must remain exactly equivalent to explicit SOR.
    Mesh default_sor_mesh = make_case();
    Mesh explicit_sor_mesh = default_sor_mesh;
    FlowSolver default_sor(
        default_sor_mesh, 4.5, 1e-10, 20000, 1.1, 60, 1e-3);
    FlowSolver explicit_sor(
        explicit_sor_mesh, 4.5, 1e-10, 20000, 1.1, 60, 1e-3, "sor");
    default_sor.solve();
    explicit_sor.solve();
    assert(compare_pressure_fields(default_sor_mesh, explicit_sor_mesh) <
           1e-12);

    Mesh sor_mesh = make_case();
    Mesh pcg_mesh = sor_mesh;

    FlowSolver sor(
        sor_mesh, 4.5, 1e-10, 20000, 1.1, 60, 1e-3, "sor");
    FlowSolver pcg(
        pcg_mesh, 4.5, 1e-10, 20000, 1.1, 60, 1e-3, "pcg");
    sor.solve();
    pcg.solve();

    double max_pressure_difference = 0.0;
    double max_pcg_speed = 0.0;
    for(size_t i = 0; i < sor_mesh.get_cells().size(); ++i) {
        const Cell& a = sor_mesh.get_cells()[i];
        const Cell& b = pcg_mesh.get_cells()[i];
        max_pressure_difference = std::max(
            max_pressure_difference,
            std::abs(a.get_pressure() - b.get_pressure()));
        max_pcg_speed = std::max(
            max_pcg_speed,
            std::sqrt(b.get_vx()*b.get_vx() +
                      b.get_vy()*b.get_vy() +
                      b.get_vz()*b.get_vz()));
    }

    assert(max_pressure_difference < 1e-5);
    assert(max_pcg_speed > 0.0);
    assert(std::abs(pcg.mass_imbalance_m3s()) < 1e-5);
    std::cout << "PCG/SOR comparison passed; max pressure difference = "
              << max_pressure_difference << " Pa\n";

    // Exercise the adaptive-neighbor assembly rather than only the uniform
    // matrix path.
    Mesh adaptive_sor_mesh = make_adaptive_case();
    Mesh adaptive_pcg_mesh = adaptive_sor_mesh;
    FlowSolver adaptive_sor(
        adaptive_sor_mesh, 4.5, 1e-10, 20000, 1.1, 60, 1e-3, "sor");
    FlowSolver adaptive_pcg(
        adaptive_pcg_mesh, 4.5, 1e-10, 20000, 1.1, 60, 1e-3, "pcg");
    adaptive_sor.solve();
    adaptive_pcg.solve();
    assert(compare_pressure_fields(adaptive_sor_mesh, adaptive_pcg_mesh) <
           1e-5);
    assert(std::abs(adaptive_pcg.mass_imbalance_m3s()) < 1e-5);

    // Invalid configuration must fail immediately rather than silently
    // falling back to a different algorithm.
    bool invalid_method_threw = false;
    try {
        Mesh invalid_mesh = make_case();
        FlowSolver invalid(
            invalid_mesh, 4.5, 1e-6, 100, 1.1, 2, 1e-3, "not-a-method");
        (void)invalid;
    } catch(const std::invalid_argument&) {
        invalid_method_threw = true;
    }
    assert(invalid_method_threw);

    // A boundary source without a vent/ambient reference is singular and
    // must be rejected before PCG starts.
    bool ungrounded_threw = false;
    try {
        Mesh ungrounded = make_case();
        for(int x=0; x<ungrounded.get_nx(); ++x)
            for(int y=0; y<ungrounded.get_ny(); ++y)
                for(int z=0; z<ungrounded.get_nz(); ++z)
                    if(ungrounded.at(x,y,z).get_state() ==
                       Cell::State::Vent)
                        ungrounded.at(x,y,z).set_state(Cell::State::Air);
        FlowSolver invalid_network(
            ungrounded, 4.5, 1e-6, 100, 1.1, 2, 1e-3, "pcg");
        invalid_network.solve();
    } catch(const std::runtime_error&) {
        ungrounded_threw = true;
    }
    assert(ungrounded_threw);

    std::cout << "pcg_flow_test PASSED\n";
}
