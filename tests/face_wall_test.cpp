#include <cassert>
#include <cmath>
#include <iostream>

#include "../mesh.hpp"
#include "../solver.hpp"

static double energy(const Mesh& mesh) {
    double total = 0.0;
    for(const Cell& cell : mesh.get_cells())
        total += cell.get_rho()*cell.get_cp()*cell.volume()*cell.get_T();
    for(const auto& wall : mesh.get_wall_faces())
        if(wall.active)
            total += wall.rho*wall.cp*mesh.wall_face_area(wall)*
                     wall.thickness*wall.temperature;
    return total;
}

int main() {
    Environment env(30.0, 0.0, 20.0, 1005.0, 0.02587,
                    0.000018, 0.71, 1.225);
    Workload load(1000, 1000000, 100000, 100);
    Rack rack = Rack::from_meters(0.2, 0.1, 0.1);
    Mesh mesh = Mesh().build_mesh(rack, 0.1, 0.1, 0.1, env, load);

    mesh.at(0,0,0).set_T(40.0);
    mesh.at(1,0,0).set_T(20.0);
    for(int x=0; x<2; ++x) {
        mesh.at(x,0,0).set_state(Cell::State::Component);
        mesh.at(x,0,0).set_rho(2700.0);
        mesh.at(x,0,0).set_cp(900.0);
        mesh.at(x,0,0).set_k(150.0);
    }
    mesh.add_wall_face(0,0,0,0,0.005,150.0,2700.0,900.0,25.0);
    assert(mesh.wall_between(0,0,0,1,0,0) != nullptr);
    assert(mesh.wall_between(1,0,0,0,0,0) != nullptr);

    const double e0 = energy(mesh);
    Solver solver(mesh, 0.01, 0.01, false, 1);
    solver.solve();
    const Mesh& result = solver.get_mesh();
    const double e1 = energy(result);

    const double relative_error = std::abs(e1-e0)/std::max(std::abs(e0),1.0);
    assert(relative_error < 1e-12);
    assert(result.get_wall_faces()[0].temperature > 25.0);
    assert(result.at(0,0,0).get_T() < 40.0);
    assert(result.at(1,0,0).get_T() > 20.0);

    result.get_cells(); // keep this test warning-free under strict builds
    std::cout << "face_wall_test PASSED; relative energy error="
              << relative_error << '\n';

    Mesh advective = Mesh().build_mesh(rack, 0.1, 0.1, 0.1, env, load);
    advective.at(0,0,0).set_T(40.0);
    advective.at(1,0,0).set_T(20.0);
    advective.at(1,0,0).set_vx(1.0);
    advective.add_wall_face(0,0,0,0,0.005,150.0,2700.0,900.0,20.0);
    Solver advection_solver(advective,0.01,0.01,false,1);
    advection_solver.solve();
    assert(std::abs(advection_solver.get_mesh().at(1,0,0).get_T()-20.0) < 1e-12);
    std::cout << "face_wall_advection_block_test PASSED\n";
}
