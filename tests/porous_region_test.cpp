#include <cassert>
#include <cmath>
#include <iostream>

#include "flow_solver.hpp"
#include "mesh.hpp"
#include "porous_region.hpp"

static Mesh make_mesh(bool porous) {
    Environment env(30.0,0.0,20.0,1005.0,0.02587,1.8e-5,0.71,1.2);
    Workload load(10000,1000000,100000,100);
    Rack rack=Rack::from_meters(1.0,1.0,1.0);
    Mesh mesh=Mesh().build_mesh(rack,0.1,1.0,1.0,env,load);
    mesh.at(0,0,0).set_flow_source(0.01);
    mesh.at(9,0,0).set_state(Cell::State::Vent);
    mesh.at(9,0,0).set_vent_conductance(1.0);
    if(porous) {
        PorousRegion region{"calibrated cable bundle",
            {0.41,0.0,0.0},{0.01,1.0,1.0},{1.0,0.0,0.0},
            100000.0,50.0,100000.0,50.0};
        mesh.stamp_porous_region(region);
        assert(mesh.at(4,0,0).is_porous());
        assert(!mesh.at(5,0,0).is_porous());
    }
    return mesh;
}

int main() {
    bool rejected=false;
    try {
        PorousRegion invalid{"no resistance",{0,0,0},{1,1,1},{1,0,0}};
        invalid.validate();
    } catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);

    Mesh baseline=make_mesh(false);
    Mesh obstructed=make_mesh(true);
    FlowSolver baseline_solver(baseline,4.5,1e-10,20000,1.1,100,1e-5,"pcg");
    FlowSolver porous_solver(obstructed,4.5,1e-10,20000,1.1,100,1e-5,"pcg");
    baseline_solver.solve();
    porous_solver.solve();
    const double base_drop=baseline.at(0,0,0).get_pressure()-baseline.at(9,0,0).get_pressure();
    const double porous_drop=obstructed.at(0,0,0).get_pressure()-obstructed.at(9,0,0).get_pressure();
    // Physical region is 0.01 m thick but occupies a 0.1 m cell. The
    // coefficient scaling must preserve the requested physical drop.
    const double expected=1.8e-5*100000.0*0.01*0.01+
                          0.5*1.2*50.0*0.01*0.01*0.01;
    assert(std::abs((porous_drop-base_drop)-expected)<1e-7);
    std::cout << "porous_region_test PASSED; added pressure drop="
              << porous_drop-base_drop << " Pa\n";
}
