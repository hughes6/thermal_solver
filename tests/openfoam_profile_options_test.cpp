#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/grapher.hpp"
#include "../src/input/model_loader.hpp"

namespace {
std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    assert(input);
    return {std::istreambuf_iterator<char>(input),{}};
}

void replace_once(
    std::string& text,
    const std::string& from,
    const std::string& to) {
    const auto at=text.find(from);
    assert(at!=std::string::npos);
    text.replace(at,from.size(),to);
}

std::string remove_option_line(
    const std::string& text,
    const std::string& option) {
    std::istringstream input(text);
    std::ostringstream output;
    std::string line;
    while(std::getline(input,line)) {
        if(line.rfind(option+" =",0)==0) continue;
        output << line << '\n';
    }
    return output.str();
}
}

int main() {
    const auto root=std::filesystem::temp_directory_path()/
        "thermal_solver_openfoam_profile_options_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const std::string base_model=remove_option_line(
        read_file("library/tests/openfoam_model.toml"),
        "airflow_warmup_time");
    const std::string default_profile=
        "library/openfoam_cfg/default_foam_cfg.toml";

    const auto load_profile=[&](const std::string& profile) {
        std::string model=base_model;
        replace_once(
            model,default_profile,
            "library/openfoam_cfg/"+profile+"_foam_cfg.toml");
        const auto path=root/(profile+"_model.toml");
        std::ofstream output(path);
        output << model;
        output.close();
        ModelLoader loader;
        loader.load_model(path);
        return loader.model.openfoam_solver;
    };

    for(const std::string profile :
        {"default","screening","indepth","validation"}) {
        const auto options=load_profile(profile);
        assert(std::abs(options.airflow_checkpoint_interval-0.1)<1e-12);
        assert(std::abs(options.airflow_warmup_time-20.0)<1e-12);
    }
    assert(std::abs(
        load_profile("indepth").thermal_only_maximum_time_step-30.0)<1e-12);

    std::string fallback_profile=read_file(default_profile);
    fallback_profile=remove_option_line(
        fallback_profile,"airflow_checkpoint_interval");
    fallback_profile=remove_option_line(
        fallback_profile,"airflow_warmup_time");
    const auto fallback_path=root/"fallback_foam_cfg.toml";
    {
        std::ofstream output(fallback_path);
        output << fallback_profile;
    }
    std::string fallback_model=base_model;
    replace_once(
        fallback_model,default_profile,fallback_path.generic_string());
    const auto fallback_model_path=root/"fallback_model.toml";
    {
        std::ofstream output(fallback_model_path);
        output << fallback_model;
    }
    ModelLoader fallback_loader;
    fallback_loader.load_model(fallback_model_path);
    assert(std::abs(
        fallback_loader.model.openfoam_solver.airflow_checkpoint_interval-
        0.1)<1e-12);
    assert(std::abs(
        fallback_loader.model.openfoam_solver.airflow_warmup_time-20.0)<1e-12);

    std::filesystem::remove_all(root);
}
