#include <cassert>
#include <cmath>
#include <iostream>

#include "../solver.hpp"

static Mesh make_mesh() {
    Environment env(30.0,0.0,20.0,1005.0,0.02587,
                    0.000018,0.71,1.225);
    Workload load(10000,1000000,100000,100);
    Rack rack=Rack::from_meters(0.3,0.1,0.1);
    rack.set_t(20.0); rack.set_cp(1005.0);
    rack.set_k(0.02587); rack.set_rho(1.225);
    Mesh mesh=Mesh().build_mesh(rack,0.1,0.1,0.1,env,load);
    mesh.at(0,0,0).set_T(40.0);
    for(int x=0; x<3; ++x) {
        mesh.at(x,0,0).set_vx(1.0);
        // Isolate the advection comparison from fluid conduction.
        mesh.at(x,0,0).set_k(1e-12);
    }
    return mesh;
}

int main() {
    Mesh legacy_mesh=make_mesh();
    Solver legacy_default(legacy_mesh,0.01,0.01,false,1);
    Solver legacy_explicit(
        legacy_mesh,0.01,0.01,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,false,0.8,10000);
    legacy_default.solve();
    legacy_explicit.solve();
    for(int x=0; x<3; ++x)
        assert(legacy_default.get_mesh().at(x,0,0).get_T() ==
               legacy_explicit.get_mesh().at(x,0,0).get_T());

    Mesh reference_mesh=make_mesh();
    Mesh subcycled_mesh=make_mesh();
    Solver reference(reference_mesh,0.05,0.20,false,4);
    Solver subcycled(
        subcycled_mesh,0.20,0.20,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,true,0.5,100);
    reference.solve();
    subcycled.solve();
    for(int x=0; x<3; ++x)
        assert(std::abs(reference.get_mesh().at(x,0,0).get_T()-
                        subcycled.get_mesh().at(x,0,0).get_T()) < 1e-9);

    bool invalid_target_threw=false;
    try {
        Solver invalid(
            make_mesh(),0.1,0.1,false,1,-1,
            4.5,1e-3,10,1.3,2,1e-2,true,1.1,100);
        (void)invalid;
    } catch(const std::invalid_argument&) {
        invalid_target_threw=true;
    }
    assert(invalid_target_threw);

    bool substep_limit_threw=false;
    try {
        Solver limited(
            make_mesh(),0.2,0.2,false,1,-1,
            4.5,1e-3,10,1.3,2,1e-2,true,0.5,1);
        limited.solve();
    } catch(const std::runtime_error&) {
        substep_limit_threw=true;
    }
    assert(substep_limit_threw);

    std::cout << "advection_subcycling_test PASSED\n";
}
