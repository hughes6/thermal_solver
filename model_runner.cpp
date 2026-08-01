#include <vector> 
#include <string>
#include <iostream>
#include <memory>
#include <iomanip>
#include <cmath>

#include "src/cell.hpp"
#include "src/collision.hpp"
#include "src/component.hpp"
#include "src/environment.hpp"
#include "src/fan.hpp"
#include "src/flow_solver.hpp"
#include "src/grapher.hpp"
#include "src/mesh.hpp"
#include "src/mesh_refinement_planner.hpp"
#include "src/rack.hpp"
#include "src/solver.hpp"
#include "src/unit_test.hpp"
#include "src/vent.hpp"
#include "src/workload.hpp"
#include "src/input/model_loader.hpp"


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
  const std::filesystem::path model_path =
      argc > 1 ? argv[1] : "library/models/model.toml";
  const std::filesystem::path fan_curve_path =
      argc > 2 ? argv[2] : "library/fan_curves/fan_curves.toml";
  loader.load_fan_curves(fan_curve_path);
  loader.load_model(model_path);
  loader.run();

  return 0;
}
