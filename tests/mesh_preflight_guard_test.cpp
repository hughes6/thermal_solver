#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "../src/grapher.hpp"
#include "../src/mesh.hpp"
#include "../src/mesh_refinement_planner.hpp"

namespace {

Environment environment() {
    return Environment(
        30.0,0.0,20.0,1005.0,0.02587,1.81e-5,0.71,1.225);
}

int guard_check_index=0;

template<typename Callable>
void require_preallocation_rejection(Callable&& callable) {
    const int current_check=++guard_check_index;
    bool rejected=false;
    try {
        callable();
    } catch(const std::bad_alloc&) {
        assert(false && "guard must reject before allocation");
    } catch(const std::length_error&) {
        assert(false && "guard must reject before vector length failure");
    } catch(const std::invalid_argument&) {
        rejected=true;
    } catch(const std::overflow_error&) {
        rejected=true;
    }
    if(!rejected)
        std::cerr << "preallocation guard check " << current_check
                  << " unexpectedly succeeded\n";
    assert(rejected);
}

} // namespace

int main() {
    const Environment env=environment();
    const Workload small(10,1000,100,1);
    const Rack rack=Rack::from_meters(1.0,1.0,1.0);

    for(double invalid : {
            0.0,-0.1,std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity()}) {
        require_preallocation_rejection([&] {
            (void)Mesh().build_mesh(
                rack,invalid,1.0,1.0,env,small);
        });
    }
    require_preallocation_rejection([&] {
        (void)Mesh().build_mesh(
            rack,1.0e-6,1.0,1.0,env,small);
    });
    require_preallocation_rejection([&] {
        (void)Mesh::planned_uniform_axis_count(
            1.0,std::numeric_limits<double>::denorm_min(),"x");
    });

    for(double invalid_extent : {
            0.0,-1.0,std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity()}) {
        const Rack invalid=Rack::from_meters(
            invalid_extent,1.0,1.0);
        require_preallocation_rejection([&] {
            (void)Mesh().build_mesh(
                invalid,0.1,0.1,0.1,env,small);
        });
        require_preallocation_rejection([&] {
            (void)MeshRefinementPlanner::plan(
                invalid,{}, {}, {},0.1,0.2,0.0,true,100u);
        });
    }

    require_preallocation_rejection([&] {
        Mesh::validate_planned_mesh_density(
            static_cast<std::size_t>(
                std::numeric_limits<int>::max()),1u,1u,small);
    });

    require_preallocation_rejection([&] {
        (void)Mesh().build_adaptive_mesh(
            rack,{},std::vector<double>{1.0},
            std::vector<double>{1.0},env,small);
    });
    require_preallocation_rejection([&] {
        (void)Mesh().build_adaptive_mesh(
            rack,std::vector<double>{0.5,0.0,0.5},
            std::vector<double>{1.0},std::vector<double>{1.0},
            env,small);
    });
    require_preallocation_rejection([&] {
        (void)Mesh().build_adaptive_mesh(
            rack,std::vector<double>{
                std::numeric_limits<double>::quiet_NaN(),1.0},
            std::vector<double>{1.0},std::vector<double>{1.0},
            env,small);
    });

    require_preallocation_rejection([&] {
        (void)MeshRefinementPlanner::plan(
            rack,{}, {}, {},1.0e-6,0.1,0.0,false,100u);
    });
    require_preallocation_rejection([&] {
        (void)MeshRefinementPlanner::plan(
            rack,{}, {}, {},1.0e-6,1.0e-6,0.0,true,100u);
    });

    require_preallocation_rejection([&] {
        (void)Grapher(rack,0.0,0.1,0.1,100u);
    });
    require_preallocation_rejection([&] {
        (void)Grapher(rack,0.01,0.01,0.01,100u);
    });
    const Rack bitmap_rack=Rack::from_meters(10.0,1.0,1.0);
    (void)Grapher(bitmap_rack,1.0,1.0,1.0,10u,30u);
    require_preallocation_rejection([&] {
        (void)Grapher(bitmap_rack,1.0,1.0,1.0,10u,29u);
    });

    const MeshRefinementPlan representation_clamped=
        MeshRefinementPlanner::plan(
            rack,{}, {}, {},0.5,0.5,0.0,true,
            std::numeric_limits<std::size_t>::max());
    const MeshRefinementPlan representation_explicit=
        MeshRefinementPlanner::plan(
            rack,{}, {}, {},0.5,0.5,0.0,true,
            static_cast<std::size_t>(
                std::numeric_limits<int>::max())-1u);
    assert(representation_clamped.dxs==representation_explicit.dxs);
    assert(representation_clamped.dys==representation_explicit.dys);
    assert(representation_clamped.dzs==representation_explicit.dzs);
    require_preallocation_rejection([&] {
        (void)MeshRefinementPlanner::plan(
            rack,{}, {}, {},0.5,0.5,0.0,true,0u);
    });

    const Workload nominal_load(10,10000,1000,10);
    Mesh nominal=Mesh().build_mesh(
        Rack::from_meters(1.0,0.3,0.3),
        0.3,0.3,0.3,env,nominal_load);
    assert(nominal.get_nx()==4);
    assert(nominal.get_ny()==1);
    assert(nominal.get_nz()==1);
    for(int i=0;i<nominal.get_nx();++i)
        assert(nominal.get_dx(i)==0.3);
    assert(std::abs(nominal.get_x_bounds().back()-1.2)<1e-12);

    std::cout << "mesh_preflight_guard_test PASSED\n";
    return 0;
}
