#include <cassert>
#include <cmath>
#include <iostream>

#include "flow_solver.hpp"
#include "mesh.hpp"
#include "porous_region.hpp"

static Mesh make_mesh(bool porous,double cell_size=0.1,double region_thickness=0.01,
                      double darcy=100000.0,double forchheimer=50.0,
                      double flow=0.01) {
    Environment env(30.0,0.0,20.0,1005.0,0.02587,1.8e-5,0.71,1.2);
    Workload load(10000,1000000,100000,100);
    Rack rack=Rack::from_meters(1.0,1.0,1.0);
    Mesh mesh=Mesh().build_mesh(rack,cell_size,1.0,1.0,env,load);
    const size_t nx=static_cast<size_t>(std::lround(1.0/cell_size));
    mesh.at(0,0,0).set_flow_source(flow);
    mesh.at(nx-1,0,0).set_state(Cell::State::Vent);
    mesh.at(nx-1,0,0).set_vent_conductance(1.0);
    if(porous) {
        PorousRegion region{"calibrated cable bundle",
            {0.41,0.0,0.0},{region_thickness,1.0,1.0},{1.0,0.0,0.0},
            darcy,forchheimer,darcy,forchheimer};
        mesh.stamp_porous_region(region);
        bool found=false;
        for(size_t x=0;x<nx;++x) found=found || mesh.at(x,0,0).is_porous();
        assert(found);
    }
    return mesh;
}

static void check_pressure_drop(double cell_size,double thickness,double darcy,
                                double forchheimer,double velocity) {
    Mesh baseline=make_mesh(false,cell_size,thickness,darcy,forchheimer,velocity);
    Mesh obstructed=make_mesh(true,cell_size,thickness,darcy,forchheimer,velocity);
    FlowSolver baseline_solver(baseline,4.5,1e-10,20000,1.1,100,1e-5,"pcg");
    FlowSolver porous_solver(obstructed,4.5,1e-10,20000,1.1,100,1e-5,"pcg");
    baseline_solver.solve();
    porous_solver.solve();
    const size_t nx=static_cast<size_t>(std::lround(1.0/cell_size));
    const double actual=(obstructed.at(0,0,0).get_pressure()-
                         obstructed.at(nx-1,0,0).get_pressure())-
                        (baseline.at(0,0,0).get_pressure()-
                         baseline.at(nx-1,0,0).get_pressure());
    const double expected=1.8e-5*darcy*thickness*velocity+
                          0.5*1.2*forchheimer*thickness*velocity*velocity;
    // The nonlinear flow solve itself stops at 1e-5 relative face-flow
    // change, so a 5e-5 pressure tolerance tests the constitutive law without
    // pretending the iterative operating point is machine-exact.
    const double tolerance=std::max(1e-8,std::abs(expected)*5e-5);
    if(std::abs(actual-expected)>=tolerance) {
        std::cerr << "pressure mismatch: dx=" << cell_size
                  << " L=" << thickness << " D=" << darcy
                  << " F=" << forchheimer << " U=" << velocity
                  << " actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << "\n";
    }
    assert(std::abs(actual-expected)<tolerance);
}

int main() {
    bool rejected=false;
    try {
        PorousRegion invalid{"no resistance",{0,0,0},{1,1,1},{1,0,0}};
        invalid.validate();
    } catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);

    Mesh openfoam_mesh=make_mesh(false);
    PorousRegion thin_openfoam{"thin OpenFOAM tray",
        {0.41,0.0,0.0},{0.01,1.0,1.0},{1.0,0.0,0.0},
        100000.0,5000.0,100000.0,5000.0};
    openfoam_mesh.stamp_porous_region(thin_openfoam,true);
    const auto& exported=openfoam_mesh.get_openfoam_porous_regions().at(0);
    assert(exported.cells.size()==2);
    assert(std::abs(exported.darcy*0.2-100000.0*0.01)<1e-10);
    assert(std::abs(exported.forchheimer*0.2-5000.0*0.01)<1e-10);

    // Pure Darcy, pure Forchheimer, and mixed curves over a 16:1 velocity
    // range. Repeat a mixed point with sub-cell, one-cell, and multi-cell
    // physical thicknesses to prove mesh-thickness scaling.
    for(double velocity:{0.01,0.04,0.08,0.16}) {
        check_pressure_drop(0.1,0.01,100000.0,0.0,velocity);
        check_pressure_drop(0.1,0.01,0.0,5000.0,velocity);
        check_pressure_drop(0.1,0.01,100000.0,5000.0,velocity);
    }
    check_pressure_drop(0.1,0.10,100000.0,5000.0,0.08);
    check_pressure_drop(0.1,0.20,100000.0,5000.0,0.08);
    check_pressure_drop(0.05,0.20,100000.0,5000.0,0.08);
    std::cout << "porous_region_test PASSED; 16 analytic pressure-drop "
                 "and thickness/mesh checks\n";
}
