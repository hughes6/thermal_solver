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

  bool force_native = false;
  bool geometry_only = false;
  std::vector<std::string> positional_arguments;
  for(int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if(argument == "--native") {
      force_native = true;
    } else if(argument == "--geometry-only") {
      geometry_only = true;
      force_native = true;
    } else if(argument == "--help" || argument == "-h") {
      std::cout
          << "Usage: model_runner.exe [--native] [--geometry-only] "
             "[model.toml] [fan_curves.toml]\n"
          << "  --native  Run the built-in solver even when the model enables "
             "OpenFOAM.\n"
          << "  --geometry-only  Write output.txt without running a transient "
             "solver or exporting OpenFOAM.\n";
      return 0;
    } else if(!argument.empty() && argument[0] == '-') {
      std::cerr << "Unknown option: " << argument << "\n";
      return 2;
    } else {
      positional_arguments.push_back(argument);
    }
  }
  if(positional_arguments.size() > 2) {
    std::cerr << "Too many arguments. Run with --help for usage.\n";
    return 2;
  }
  
  std::string load_model_path = "library/models/validation_fan_rack.toml";

  ModelLoader loader;
  const std::filesystem::path model_path = positional_arguments.empty()
      ? load_model_path : positional_arguments[0];
  const std::filesystem::path fan_curve_path = positional_arguments.size() < 2
      ? "library/fan_curves/fan_curves.toml" : positional_arguments[1];
  loader.load_fan_curves(fan_curve_path);
  loader.load_model(model_path);
  if(force_native) {
    loader.model.openfoam_solver.enabled = false;
    std::cout << "Native backend forced; OpenFOAM export is disabled.\n";
  }
  loader.run(geometry_only);

  return 0;
}
