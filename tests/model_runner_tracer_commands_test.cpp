#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "grapher.hpp"
#include "input/model_loader.hpp"

int main() {
    const std::filesystem::path case_directory =
        std::filesystem::temp_directory_path() /
        "thermal_sim_tracer_command_export";
    std::filesystem::remove_all(case_directory);

    ModelLoader unpaired_loader;
    unpaired_loader.load_model("library/tests/openfoam_model.toml");
    unpaired_loader.load_fan_curves("library/fan_curves/fan_curves.toml");
    unpaired_loader.model.openfoam_solver.case_directory = case_directory;
    unpaired_loader.model.openfoam_solver.overwrite = true;
    std::ostringstream unpaired_captured;
    std::streambuf* original = std::cout.rdbuf(unpaired_captured.rdbuf());
    unpaired_loader.run();
    std::cout.rdbuf(original);
    assert(unpaired_captured.str().find(
        "Report live simulation progress and ETA") != std::string::npos);
    assert(unpaired_captured.str().find("tools/openfoam_progress.py") !=
           std::string::npos);
    assert(unpaired_captured.str().find("set -o pipefail") !=
           std::string::npos);
    assert(unpaired_captured.str().find(
        "tee -a thermal_solver.stdout.log") != std::string::npos);
    assert(unpaired_captured.str().find(
        "tee -a multirate_18000.stdout.log") != std::string::npos);
    assert(unpaired_captured.str().find(
        "tee -a multirate_100000.stdout.log") != std::string::npos);
    assert(unpaired_captured.str().find(
        "Build the source-attributed exhaust tracer once") ==
        std::string::npos);

    ModelLoader loader;
    loader.load_model("library/tests/openfoam_model.toml");
    loader.load_fan_curves("library/fan_curves/fan_curves.toml");
    FanInput exhaust;
    exhaust.name = "Test rear exhaust";
    exhaust.shape = FanShape::Rectangular;
    exhaust.flow_type = FanFlowType::Exhaust;
    exhaust.position = {0.10,0.20,0.10,"m"};
    exhaust.size = SizeInput{0.10,0.0,0.10,"m"};
    exhaust.direction = {0.0,1.0,0.0};
    exhaust.cfm = 10.0;
    exhaust.curve_name = "test_curve";
    loader.fan_curve_library.emplace(
        "test_curve",FanCurveInput{"test_curve",50.0,1000.0,0.0,1.2});
    InternalRegionInput exhaust_region;
    exhaust_region.name = exhaust.name;
    exhaust_region.state = RegionState::Fan;
    exhaust_region.fan = exhaust;
    loader.model.components.back().internal_regions.push_back(exhaust_region);
    loader.model.openfoam_solver.case_directory = case_directory;
    loader.model.openfoam_solver.overwrite = true;

    std::ostringstream captured;
    original = std::cout.rdbuf(captured.rdbuf());
    loader.run();
    std::cout.rdbuf(original);

    const std::string output = captured.str();
    assert(output.find("Build the source-attributed exhaust tracer once") !=
           std::string::npos);
    assert(output.find("tools/build_openfoam_tracer.sh") !=
           std::string::npos);
    assert(output.find("tools/exhaust_recirculation_tracer.py") !=
           std::string::npos);
    assert(output.find("plot/exhaust_recirculation_matrix.py") !=
           std::string::npos);
    assert(output.find("tools/openfoam_progress.py") != std::string::npos);
    assert(output.find("steadyExhaustTracerFoam") != std::string::npos);
    assert(output.find(
        "source /usr/lib/openfoam/openfoam2606/etc/bashrc && cd /tmp && "
        "python3") != std::string::npos);
    assert(output.find("_tracer_RUN_ID") != std::string::npos);
#ifdef _WIN32
    assert(output.find("'/mnt/c/") != std::string::npos);
    assert(output.find("python3 'C:/") == std::string::npos);
#endif
    assert(std::filesystem::is_regular_file(
        case_directory / "internal_airflow_devices.csv"));
    {
        std::ifstream metadata_stream(
            case_directory / "internal_airflow_devices.csv");
        std::ostringstream metadata;
        metadata << metadata_stream.rdbuf();
        assert(metadata.str().find(",intake,\"Passive test vent\"") !=
               std::string::npos);
        assert(metadata.str().find(",exhaust,\"Test rear exhaust\"") !=
               std::string::npos);
    }
    std::filesystem::remove_all(case_directory);
    std::cout << "model_runner_tracer_commands_test PASSED\n";
    return 0;
}
