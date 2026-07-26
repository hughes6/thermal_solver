#include <vector> 
#include <string>
#include <iostream>
#include <memory>
#include <iomanip>
#include <cmath>

#include "cell.hpp"
#include "collision.hpp"
#include "component.hpp"
#include "environment.hpp"
#include "fan.hpp"
#include "flow_solver.hpp"
#include "grapher.hpp"
#include "mesh.hpp"
#include "mesh_refinement_planner.hpp"
#include "rack.hpp"
#include "solver.hpp"
#include "unit_test.hpp"
#include "vent.hpp"
#include "workload.hpp"
#include "input/model_loader.hpp"


int main(int argc, char* argv[]) {


  // ComponentLoader loader;
  // loader.load_component("library/components/cisco_7603_network_switch.toml");
  // loader.load_component("library/components/dell_poweredge_r760.toml");
  // loader.load_component("library/components/eaton_9px3000irt3u_ups.toml");
  // loader.load_component("library/components/eaton_eats220_ats.toml");
  // loader.load_component("library/components/trenton_3u_bam.toml");
  // loader.load_component("library/components/tripplite_b020_u08_19_ip_kvm.toml");
  // loader.run();

  ModelLoader loader;
  loader.load_fan_curves("library/components/fan_curves.toml");  
  loader.load_model("library/models/model.toml");
  loader.run();

  return 0;
}