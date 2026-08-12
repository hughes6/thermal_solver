#include <vector> 
#include <string>
#include <iostream>
#include <memory>
#include <iomanip>
#include <cmath>
#include <filesystem>
#include <cstdlib>

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
  bool plot_existing = false;
  std::string openfoam_case_name;
  std::vector<std::string> positional_arguments;
  for(int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if(argument == "--native") {
      force_native = true;
    } else if(argument == "--geometry-only") {
      geometry_only = true;
      force_native = true;
    } else if(argument == "--plot-existing") {
      plot_existing = true;
    } else if(argument == "--case-name") {
      if(i + 1 >= argc) {
        std::cerr << "--case-name requires a directory name.\n";
        return 2;
      }
      openfoam_case_name = argv[++i];
      const std::filesystem::path requested(openfoam_case_name);
      if(openfoam_case_name.empty() || openfoam_case_name == "." ||
         openfoam_case_name == ".." || requested.has_parent_path() ||
         requested.filename().string() != openfoam_case_name) {
        std::cerr << "--case-name must be one directory name without a path.\n";
        return 2;
      }
    } else if(argument == "--help" || argument == "-h") {
      std::cout
          << "Usage: model_runner.exe [--native] [--geometry-only] "
             "[--plot-existing] [--case-name NAME] "
             "[model.toml] [fan_curves.toml]\n"
          << "  --native  Run the built-in solver even when the model enables "
             "OpenFOAM.\n"
          << "  --geometry-only  Write output.txt without running a transient "
             "solver or exporting OpenFOAM.\n"
          << "  --plot-existing  Plot existing native results without loading "
             "a model or modifying data. Optional positional arguments are "
             "[simulation.csv] [output.txt].\n";
      std::cout
          << "  --case-name NAME  Override only the exported OpenFOAM case "
             "directory name while preserving its configured root.\n";
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

  if(plot_existing) {
    if(force_native || geometry_only || !openfoam_case_name.empty()) {
      std::cerr << "--plot-existing cannot be combined with --native or "
                   "--geometry-only or --case-name.\n";
      return 2;
    }
    const std::filesystem::path simulation_path =
        positional_arguments.empty() ? "simulation.csv"
                                     : positional_arguments[0];
    const std::filesystem::path geometry_path =
        positional_arguments.size() < 2 ? "output.txt"
                                        : positional_arguments[1];
    if(!std::filesystem::is_regular_file(simulation_path)) {
      std::cerr << "Existing simulation data not found: "
                << std::filesystem::absolute(simulation_path) << "\n";
      return 2;
    }
    if(!std::filesystem::is_regular_file(geometry_path)) {
      std::cerr << "Existing geometry report not found: "
                << std::filesystem::absolute(geometry_path) << "\n";
      return 2;
    }
    const auto quote = [](const std::filesystem::path& path) {
      std::string value = std::filesystem::absolute(path).string();
      std::string result = "\"";
      for(const char character : value) {
        if(character == '\"') result += "\\\"";
        else result += character;
      }
      return result + "\"";
    };
    const std::filesystem::path plot_script =
        std::filesystem::absolute("plot/heat_animation.py");
    const std::string command =
        "python " + quote(plot_script) + " --format native --sim " +
        quote(simulation_path) + " --rack " + quote(geometry_path);
    std::cout << "Plotting existing results without modifying simulation.csv "
                 "or output.txt.\n";
    const int status = std::system(command.c_str());
    if(status != 0) {
      std::cerr << "Existing-results plot command failed with status "
                << status << ".\nCommand: " << command << "\n";
      return 1;
    }
    return 0;
  }
  
  std::string load_model_path = "library/models/validation_fan_rack.toml";

  ModelLoader loader;
  const std::filesystem::path model_path = positional_arguments.empty()
      ? load_model_path : positional_arguments[0];
  const std::filesystem::path fan_curve_path = positional_arguments.size() < 2
      ? "library/fan_curves/fan_curves.toml" : positional_arguments[1];
  loader.load_fan_curves(fan_curve_path);
  loader.load_model(model_path);
  if(!openfoam_case_name.empty()) {
    const std::filesystem::path configured =
        loader.model.openfoam_solver.case_directory;
    loader.model.openfoam_solver.case_directory =
        configured.parent_path() / openfoam_case_name;
    std::cout << "OpenFOAM case name overridden: "
              << loader.model.openfoam_solver.case_directory << "\n";
  }
  if(force_native) {
    loader.model.openfoam_solver.enabled = false;
    std::cout << "Native backend forced; OpenFOAM export is disabled.\n";
  }
  loader.run(geometry_only);

  return 0;
}
