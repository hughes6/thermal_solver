#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

#include "../environment.hpp"
#include "../fan.hpp"
#include "../flow_solver.hpp"
#include "../mesh.hpp"
#include "../rack.hpp"
#include "../vent.hpp"
#include "../workload.hpp"

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

int main() {
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
}
