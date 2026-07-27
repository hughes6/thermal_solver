#include <algorithm>
#include <chrono>
#include <iostream>

#include "../mesh_refinement_planner.hpp"
#include "../solver.hpp"

struct Result {
    double seconds;
    std::size_t cells;
    double max_source_T;
    double max_wall_T;
};

static Component make_component() {
    Component c(0.10, 0.10, 0.10, "benchmark enclosure");
    c.set_coords_m(0.05,0.05,0.05);
    c.set_t(20.0);
    c.set_rho_solid(2700.0);
    c.set_cp(900.0);
    c.set_k_solid(150.0);
    c.set_watts(0.0);
    c.add_region(InternalRegion("interior air",
        {0.09,0.09,0.09}, {0.005,0.005,0.005}));
    c.add_region(InternalRegion("electronics",
        {0.02,0.02,0.02}, {0.04,0.04,0.04},
        500.0, 8000.0, 15.0, 100.0));
    c.order_internal_regions();
    return c;
}

static Result run(bool face_mode) {
    Environment env(30.0,0.0,20.0,1005.0,0.02587,0.000018,0.71,1.225);
    Workload load(100000,100000000,1000000,1000);
    Rack rack=Rack::from_meters(0.20,0.20,0.20);
    rack.set_t(20.0); rack.set_cp(1005.0); rack.set_k(0.02587); rack.set_rho(1.225);
    Component component=make_component();
    std::vector<Component> components{component};
    std::vector<Fan> fans;
    std::vector<Vent> vents;
    const auto plan=MeshRefinementPlanner::plan(
        rack,components,fans,vents,
        face_mode ? 0.02 : 0.005, 0.04, 0.01, !face_mode);
    Mesh mesh=Mesh().build_adaptive_mesh(
        rack,plan.dxs,plan.dys,plan.dzs,env,load);
    if(face_mode) mesh.stamp_component_face_walls_adaptive(component);
    else mesh.stamp_component_adaptive(component);

    const auto start=std::chrono::steady_clock::now();
    Solver solver(mesh,0.001,0.10,false,100);
    solver.solve();
    const double seconds=std::chrono::duration<double>(
        std::chrono::steady_clock::now()-start).count();

    const Mesh& out=solver.get_mesh();
    double source=-1e300, wall=-1e300;
    for(const Cell& cell:out.get_cells()) {
        if(cell.get_qdot()>0.0) source=std::max(source,cell.get_T());
        else if(!face_mode && cell.get_state()==Cell::State::Component)
            wall=std::max(wall,cell.get_T());
    }
    if(face_mode)
        for(const auto& f:out.get_wall_faces())
            if(f.active) wall=std::max(wall,f.temperature);
    return {seconds,out.get_cell_count(),source,wall};
}

int main() {
    const Result fine=run(false);
    const Result face=run(true);
    std::cout << "resolved_seconds=" << fine.seconds
              << " resolved_cells=" << fine.cells
              << " resolved_source_T=" << fine.max_source_T
              << " resolved_wall_T=" << fine.max_wall_T << '\n';
    std::cout << "face_seconds=" << face.seconds
              << " face_cells=" << face.cells
              << " face_source_T=" << face.max_source_T
              << " face_wall_T=" << face.max_wall_T << '\n';
    std::cout << "speedup=" << fine.seconds/face.seconds
              << " source_delta_C=" << std::abs(fine.max_source_T-face.max_source_T)
              << " wall_delta_C=" << std::abs(fine.max_wall_T-face.max_wall_T)
              << '\n';
}
