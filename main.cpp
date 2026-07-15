#include <vector>
#include <string>
#include <iostream>
#include <memory>
#include <iomanip>
#include <cmath>

#include "cell.hpp"
#include "component.hpp"
#include "fan.hpp"
#include "grapher.hpp"
#include "mesh.hpp"
#include "rack.hpp"
#include "solver.hpp"
#include "vent.hpp"

/* 
=========================================================
TODO
---------------------------------------------------------
 - add rack walls with vents
 - adaptive meshing
 - make sure temps lower to rt right? Newtonian
 - add specialized components, cpu location & fan

 - step 2 air cells have velo vec based on fans/vents
 - step 3 compute h from correlations, Re, Pr, Nu, h
    need v to calc reynolds number - from
 - step 4 cons of mass, energy, momentum

 then you can add flow resistance for blocked/open regions

 velocity propogation - need cons of mass
 air entering = air leaving

 fan steps
 3 - crude pressure/flow solver
 AIRFLOW MODEL DECIDE WHERE AIR GOES BEOFRE HEAT

1. Stamp rack/components/vents/fans
2. Solve approximate pressure/velocity field
3. Use velocity field for air advection
4. Use convection between solid and air
5. Update temperatures
=========================================================
*/

int main(int argc, char* argv[]) {
    double dx = 0.04445;   // 1U
    double dy = 0.04445;   // 1U
    double dz = 0.04445/2.0;   // 1U
    double sim_length = 20; 
    double dt = 0.1;

    Rack rack = Rack::from_rack_units(
       10.0,      // height (U)
       8.75,     // width (5U = 5 × 1.75 in)
       8.75      // depth (5U = 5 × 1.75 in)
    );  

    rack.set_t(20);
    rack.set_cp(1000);
    rack.set_h(1);
    rack.set_k(1);
    rack.set_rho(1);
    Fan top_fan{
      "Top exhust", // name
      3.0,        // cfm
      0.1,          // diameter (m)
      {rack.get_width_m()/2, rack.get_depth_m()/2, rack.get_height_m()}, // m m m
      {0.0, 0.0, 1.0}, // velocity vector 0x 0y 1z air going up
      FlowType::Exhaust
    };

    Vent vent("Rack main vent", 0.1, 0.1, 0.5,
       {rack.get_width_m()/2, 0.0, rack.get_height_m()*0.25},
      {})

    Mesh mesh = Mesh().build_mesh(rack, dx, dy, dz);
    // mesh.print_mesh();

    Component server1 = Component::from_rack_units(1.0, 8.75, 8.75, "S-1"); // u in in
    server1.set_coords_rack_units(0,0,1); // in in u
    server1.set_k_solid(200.0);    // W/(m·K)
    server1.set_cp(900.0);         // J/(kg·K)
    server1.set_rho_solid(2700.0); // kg/m^3
    server1.set_t(20.0);           // °C
    server1.set_watts(1000);      // W

   //  Component server2 = Component::from_rack_units(1.0, 8.75, 8.75, "S-2"); // u in in
   //  server2.set_coords_rack_units(0,0,2); // in in u
   //  server2.set_k_solid(100.0);    // W/(m·K)
   //  server2.set_cp(400.0);         // J/(kg·K)
   //  server2.set_rho_solid(2100.0); // kg/m^3
   //  server2.set_t(90.0);           // °C
   //  server2.set_watts(10000);      // W

    Grapher grapher(rack, dx, dy, dz);
    grapher.add_component(server1);
   //  grapher.add_component(server2);
    grapher.add_fan(top_fan);
    grapher.stamp_components();
    grapher.stamp_fans();
   //  grapher.print_bitmap();
    grapher.export_to_file("output.txt");

    mesh.stamp_component(server1);
    mesh.stamp_fan(top_fan);
   //  mesh.stamp_component(server2);
   //  mesh.print_mesh();

    Solver solver(mesh, dt, sim_length);
   //  solver.apply_bulk_velocity(0.0, 0.0, 0.0222);
    /*advection stability ratio C = vz * dt / dz
      should  be C <= 1
      so C = 0.0222*0.1/0.0222 = 1*/
   
    solver.solve();
    // Mesh final_mesh = solver.get_mesh();
    return 0;
}