#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "run_metadata.hpp"

int main() {
    const auto directory=std::filesystem::temp_directory_path()/
        "thermal_sim_run_metadata_cpp_test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto output=directory/".thermal_sim_last_run.json";
    write_run_metadata(output,{
        .executable=directory/"model runner.exe",
        .working_directory=directory,
        .model=directory/"model.toml",
        .fan_curves=directory/"fan_curves.toml",
        .case_directory=directory/"long_case_name",
        .geometry=directory/"long_case_name"/"geometry.txt",
        .simulation={},
        .backend="openfoam",
        .mode="normal"
    });
    assert(std::filesystem::is_regular_file(output));
    assert(!std::filesystem::exists(output.string()+".tmp"));
    std::ifstream input(output);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string json=buffer.str();
    assert(json.find("\"schema_version\": 1")!=std::string::npos);
    assert(json.find("\"executable\":")!=std::string::npos);
    assert(json.find("model runner.exe")!=std::string::npos);
    assert(json.find("\"case_directory\":")!=std::string::npos);
    assert(json.find("long_case_name")!=std::string::npos);
    assert(json.find("\"backend\": \"openfoam\"")!=std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}
