#include <grapher.hpp>
#include <input/model_loader.hpp>
#include <cassert>
#include <sstream>
#include <iostream>
int main(int argc,char** argv) {
    assert(argc==2);
    assert(!std::filesystem::exists(argv[1]));
    ModelLoader loader;
    loader.load_model("library/tests/openfoam_model.toml");
    loader.model.openfoam_solver.case_directory=argv[1];
    loader.model.openfoam_solver.use_multirate_thermal=true;
    loader.model.openfoam_solver.use_adaptive_airflow_refresh=true;
    std::ostringstream captured;
    auto* previous=std::cout.rdbuf(captured.rdbuf());
    loader.run();
    std::cout.rdbuf(previous);
    for(const auto* required : {"--cold-flow-seed", "prepare_heated_airflow_reuse.sh", "prepare_mapped_airflow_reuse.sh", "DONOR_TIME=", "--multirate", "Qualification can need more airflow time"})
        assert(captured.str().find(required)!=std::string::npos);
    assert(std::filesystem::exists(std::filesystem::path(argv[1])/"prepare_heated_airflow_reuse.sh"));
    assert(std::filesystem::exists(std::filesystem::path(argv[1])/"prepare_mapped_airflow_reuse.sh"));
    assert(std::filesystem::exists(std::filesystem::path(argv[1])/"mapped_airflow_checks.py"));
    std::cout<<"PASS: model output includes separate cold/heated reuse commands and bundled helper\n";
}
