#include <vector>
#include <string>
#include <iostream>
#include <memory>
#include <iomanip>
#include <cmath>

#include "cell.hpp"
#include "component.hpp"
#include "environment.hpp"
#include "fan.hpp"
#include "flow_solver.hpp"
#include "grapher.hpp"
#include "mesh.hpp"
#include "rack.hpp"
#include "solver.hpp"
#include "unit_test.hpp"
#include "vent.hpp"
#include "workload.hpp"

/* 
=========================================================
TODO
 - input yaml fil
 - adaptive meshing
=========================================================
*/

int main(int argc, char* argv[]) {
  // ===========
  // UNIT TEST
  // ===========
  // UnitTest tester;
  // tester.run();
  

  Environment env(30.0, 5800.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
  Workload load(1'000'000, 10'000'000, 1'000'000, 4);
  
  double dx = 0.04445;   // 1U
  double dy = 0.04445;   // 1U
  double dz = 0.04445/2.0;   // 1U
  double sim_length = 5.0; 
  double dt = 0.05;
  double mu = 1.8e-5;  // air at atmospheric pressure and temp
  double pr = 0.71;    // air at atmospheric pressure and temp

  Rack rack = Rack::from_rack_units(
      5.0,     // width  (5U)
      5.0,     // depth  (5U)
      10.0,    // height (10U)
      "rack"
  );  

  rack.set_t(20);        // 68 F
  rack.set_cp(1005);     // cp of air
  rack.set_k(0.02587);   // k of air at 20 C
  rack.set_rho(1.225);   // density of air kg/m^3

  Fan top_fan{
    "Top exhust", // name
    3.0,        // cfm
    0.1,          // diameter (m),
    {0.0, 0.0, 0.0}, // rectangular size
    {rack.get_width_m()/2, rack.get_depth_m()/2, rack.get_height_m()}, // m m m
    {0.0, 0.0, 1.0}, // velocity vector 0x 0y 1z air going up
    FlowType::Exhaust,
    ShapeType::Circular
  };

  Fan mid_fan{
    "mid intake", // name
    3.0,        // cfm
    0.0,          // diameter (m),
    {0.0, 0.1, 0.1}, // rectangular size
    {rack.get_width_m(), rack.get_depth_m()/2, rack.get_height_m() * 0.5}, // m m m center
    {-1.0, 0.0, 0.0}, // velocity vector 0x 0y 0z air going in
    FlowType::Intake,
    ShapeType::Rectangular
  };

  Vent vent("Rack main vent", 
      {0.1, 0.0, 0.1},
      0.5,
      0.5,
      {rack.get_width_m()/2, 0.0, rack.get_height_m()*0.5},
      {0.0, 1.0, 0.0}
  );

  Mesh mesh = Mesh().build_mesh(rack, dx, dy, dz, env, load);
  // mesh.print_mesh();


  Component server1 = Component::from_rack_units(5.0, 5.0, 1.0, "S-1"); // u u u
  server1.set_coords_rack_units(0,0,1); // in in u
  server1.set_k_solid(200.0);    // W/(m·K)
  server1.set_cp(900.0);         // J/(kg·K)
  server1.set_rho_solid(2700.0); // kg/m^3
  server1.set_t(20.0);           // °C
  server1.set_watts(1000);      // W

  //component x y z  {0.22225, 0.22225, 0.04445} 
  InternalRegion air_channel("air channel", {server1.get_width_m() / 2.0, server1.get_depth_m(), server1.get_height_m()}, {0.0, 0.0, 0.0}); 
  server1.add_region(air_channel);

  InternalRegion metal_bar("metal bar", {server1.get_width_m() / 4.0, server1.get_depth_m() / 2.0, server1.get_height_m() / 2.0}, 
  {server1.get_width_m() / 4.0, server1.get_height_m() / 2.0, 0.0}, 897.0, 2170.0, 237.0, 200.0); 
  server1.add_region(metal_bar);

  //  Component server2 = Component::from_rack_units(8.75, 8.75, 10., "S-2"); // u in in
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
  grapher.add_fan(mid_fan);
  grapher.add_vent(vent);
  grapher.stamp_components();
  grapher.stamp_fans();
  grapher.stamp_vents();
  // grapher.print_bitmap();
  grapher.export_to_file("output.txt");

  server1.order_internal_regions();
  mesh.stamp_component(server1);
  mesh.stamp_fan(top_fan);
  mesh.stamp_fan(mid_fan);
  mesh.stamp_vent(vent);
  //  mesh.stamp_component(server2);
  // mesh.print_mesh();
  
  double resistivity = 4.5;
  double tolerance = 1e-9;
  int max_iterations = 2000;
  double sor_omega = 1.1;
  // FlowSolver flow_solver(mesh, resistivity, tolerance, max_iterations, sor_omega, 10, 1e-3);
  // flow_solver.solve(); // pre populate all velocity cells

  int otuput_interval = 1;
  Solver solver(mesh, dt, sim_length, false, otuput_interval);
  //  solver.apply_bulk_velocity(0.0, 0.0, 0.0222);
  /*advection stability ratio C = vz * dt / dz
    should  be C <= 1
    so C = 0.0222*0.1/0.0222 = 1*/

  // solver.solve();
  // Mesh final_mesh = solver.get_mesh();
  // final_mesh.print_mesh_layer_temp(3);
  return 0;
}
