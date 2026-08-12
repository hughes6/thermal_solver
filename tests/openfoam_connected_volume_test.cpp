#include <cassert>
#include <cmath>

#include "openfoam_exporter.hpp"

int main() {
    Environment environment(
        30.0,0.0,20.0,1005.0,0.02587,0.000018,0.71,1.225);
    Workload workload(10000,1000000,100000,100);
    Rack rack=Rack::from_meters(0.4,0.3,0.3);
    Mesh mesh=Mesh().build_mesh(
        rack,0.1,0.1,0.1,environment,workload);

    Component solid=Component::from_meters(0.1,0.1,0.1,"metadata seed");
    solid.set_coords_m(0.0,0.0,0.0);
    solid.set_rho_solid(2700.0);
    solid.set_cp(900.0);
    solid.set_k_solid(150.0);
    mesh.stamp_component_for_openfoam(solid);

    Fan inlet(
        "ambient inlet",10.0,0.0,{0.15,0.0,0.15},
        {0.1,0.0,0.1},{0.0,1.0,0.0},
        FlowType::Intake,ShapeType::Rectangular);
    mesh.stamp_fan_for_openfoam(inlet);

    const double initially_connected=
        OpenFoamExporter::ambient_connected_fluid_volume(mesh);
    assert(std::abs(initially_connected-0.035)<1e-12);

    // The corner cell is bounded by rack walls on its positive faces. Add
    // walls on its three inward faces to create a sealed fluid cavity.
    mesh.add_wall_face(2,2,2,0,0.001,1.0,1000.0,1000.0,20.0);
    mesh.add_wall_face(3,1,2,1,0.001,1.0,1000.0,1000.0,20.0);
    mesh.add_wall_face(3,2,1,2,0.001,1.0,1000.0,1000.0,20.0);

    const double ambient_connected=
        OpenFoamExporter::ambient_connected_fluid_volume(mesh);
    assert(std::abs(ambient_connected-0.034)<1e-12);
    assert(mesh.at(3,2,2).is_fluid());
}
