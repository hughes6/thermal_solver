#include <algorithm>
#include <cassert>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

#include "../src/grapher.hpp"
#include "../src/input/model_loader.hpp"

namespace {

struct ScopedTemporaryDirectory {
    std::filesystem::path path;

    explicit ScopedTemporaryDirectory(const std::string& prefix) {
        const auto nonce=std::chrono::steady_clock::now()
            .time_since_epoch().count();
        const std::filesystem::path parent=
            std::filesystem::temp_directory_path();
        for(int attempt=0;attempt<1000;++attempt) {
            const std::filesystem::path candidate=parent/
                (prefix+"_"+std::to_string(nonce)+"_"+
                 std::to_string(attempt));
            std::error_code error;
            if(std::filesystem::create_directory(candidate,error)) {
                path=candidate;
                return;
            }
            if(error)
                throw std::runtime_error(
                    "Unable to create unique test directory '"+
                    candidate.string()+"': "+error.message());
        }
        throw std::runtime_error(
            "Unable to reserve a unique model_config_test directory.");
    }

    ScopedTemporaryDirectory(const ScopedTemporaryDirectory&)=delete;
    ScopedTemporaryDirectory& operator=(
        const ScopedTemporaryDirectory&)=delete;

    ~ScopedTemporaryDirectory() {
        if(path.empty()) return;
        std::error_code error;
        std::filesystem::remove_all(path,error);
    }
};

} // namespace

int main() {
    assert(!OpenFoamSolverInput{}.allow_determinant_warnings);
    ScopedTemporaryDirectory temporary("thermal_sim_model_config_test");
    const std::filesystem::path& test_root=temporary.path;
    auto read_file=[](const std::filesystem::path& path) {
        std::ifstream input(path);
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    };

    // The documented no-argument geometry command must resolve to the
    // canonical research-lab model, not the removed validation fixture that
    // previously made `model --geometry-only` fail before parsing.
    const std::string runner_source=read_file("model_runner.cpp");
    assert(runner_source.find(
        "library/models/new_model_updated.toml")!=std::string::npos);
    assert(runner_source.find(
        "library/models/validation_fan_rack.toml")==std::string::npos);

    // The fine native geometry preview owns three full-domain bitmaps. Keep
    // its lexical lifetime after coarse warm-start release and before Solver
    // construction so those payloads cannot overlap.
    const std::string loader_source=read_file("src/input/model_loader.hpp");
    const std::size_t fine_mesh_at=loader_source.find("        Mesh mesh;");
    const std::size_t coarse_release_at=loader_source.find(
        "coarse_warm_start.reset();",fine_mesh_at);
    const std::size_t fine_grapher_at=loader_source.find(
        "Grapher grapher(",coarse_release_at);
    const std::size_t geometry_export_at=loader_source.find(
        "grapher.export_to_file(native_geometry_path.string());",
        fine_grapher_at);
    const std::size_t grapher_scope_end_at=loader_source.find(
        "        }\n        const int update_flow_interval",
        geometry_export_at);
    const std::size_t fine_solver_at=loader_source.find(
        "Solver solver(",geometry_export_at);
    assert(fine_mesh_at != std::string::npos);
    assert(coarse_release_at != std::string::npos);
    assert(fine_grapher_at != std::string::npos);
    assert(geometry_export_at != std::string::npos);
    assert(grapher_scope_end_at != std::string::npos);
    assert(fine_solver_at != std::string::npos);
    assert(coarse_release_at < fine_grapher_at);
    assert(fine_grapher_at < geometry_export_at);
    assert(geometry_export_at < grapher_scope_end_at);
    assert(grapher_scope_end_at < fine_solver_at);

    // OpenFOAM overwrite clears generated solution/checkpoint state. Build
    // and write the geometry sidecar to a staging file before that destructive
    // export, then install it into the exported case afterward.
    const std::size_t foam_branch_at=loader_source.find(
        "if(model.openfoam_solver.enabled) {");
    const std::size_t foam_preflight_at=loader_source.find(
        "OpenFoamExporter::preflight(mesh,options);",foam_branch_at);
    const std::size_t staged_geometry_at=loader_source.find(
        "grapher.export_to_file(staged_geometry_path.string());",
        foam_branch_at);
    const std::size_t staged_install_at=loader_source.find(
        "staged_geometry_path,geometry_path,install_error",
        staged_geometry_at);
    const std::size_t foam_export_at=loader_source.find(
        "OpenFoamExporter::export_mesh(mesh,options);",foam_branch_at);
    const std::size_t provenance_at=loader_source.find(
        "write_openfoam_provenance(absolute_case_directory);",
        foam_export_at);
    assert(foam_branch_at != std::string::npos);
    assert(foam_preflight_at != std::string::npos);
    assert(staged_geometry_at != std::string::npos);
    assert(staged_install_at != std::string::npos);
    assert(foam_export_at != std::string::npos);
    assert(provenance_at != std::string::npos);
    assert(foam_preflight_at < staged_geometry_at);
    assert(staged_geometry_at < staged_install_at);
    assert(staged_install_at < foam_export_at);
    assert(foam_export_at < provenance_at);

    ModelLoader loader;
    loader.load_fan_curves("library/fan_curves/fan_curves.toml");
    loader.load_model("library/models/model.toml");

    const auto signed_curve_path=test_root/"signed_curve.toml";
    std::ofstream(signed_curve_path)
        << "[[fan_curve]]\nname=\"bounded\"\nrho_rated=1.2\n"
        << "a=1.5\nb=5.0\nc=-4.0\n";
    ModelLoader signed_curve_loader;
    signed_curve_loader.load_fan_curves(signed_curve_path);

    const auto invalid_curve_path=test_root/"invalid_curve.toml";
    std::ofstream(invalid_curve_path)
        << "[[fan_curve]]\nname=\"no_crossing\"\nrho_rated=1.2\n"
        << "a=1.0\nb=-1.0\nc=0.0\n";
    bool invalid_curve_rejected=false;
    try {
        ModelLoader invalid_curve_loader;
        invalid_curve_loader.load_fan_curves(invalid_curve_path);
    } catch(const std::runtime_error&) {
        invalid_curve_rejected=true;
    }
    assert(invalid_curve_rejected);

    const ModelInput& model = loader.model;
    assert(model.simulation.native_output_directory ==
           std::filesystem::path("."));
    assert(model.flow_solver.enable_flow_solver);
    assert(model.flow_solver.pressure_method == "pcg");
    assert(model.mesh.adaptive);
    assert(model.multistage.enabled);
    assert(model.simulation.advection_subcycling);
    assert(model.simulation.advection_cfl_target > 0.0);
    assert(model.simulation.advection_cfl_target <= 1.0);
    assert(model.simulation.max_advection_substeps >= 1);
    assert(model.multistage.coarse_mesh.fine_dx > 0.0);
    assert(model.multistage.coarse_mesh.coarse_dx >=
           model.multistage.coarse_mesh.fine_dx);
    assert(!model.components.empty());
    assert(!model.fans.empty());
    assert(!model.vents.empty());
    const auto occurrence_count=[](
        const std::string& text, const std::string& pattern) {
        std::size_t count=0;
        for(std::size_t at=0;
            (at=text.find(pattern,at))!=std::string::npos;
            at+=pattern.size())
            ++count;
        return count;
    };
    assert(occurrence_count(
        read_file("library/components/eaton_2U_UPS.toml"),
        "curve = \"provisional_eaton_ups_45cfm\"")==1);
    assert(occurrence_count(
        read_file("library/components/DELL_R470.toml"),
        "curve = \"generic_80mm_low_speed\"")==6);
    assert(model.openfoam_solver.enabled);
    assert(model.openfoam_solver.template_file ==
           "library/openfoam_cfg/screening_foam_cfg.toml");
    assert(model.openfoam_solver.case_directory ==
           std::filesystem::path(
               "C:/OpenFOAM/thermal_sim_v2/model"));
    assert(model.mesh.adaptive);
    assert(model.mesh.fine_dx == 0.02);
    assert(model.mesh.coarse_dx == 0.10);

    ModelLoader updated_lab_export_loader;
    updated_lab_export_loader.load_fan_curves(
        "library/fan_curves/fan_curves.toml");
    updated_lab_export_loader.load_model(
        "library/models/new_model_updated_openfoam_export_test.toml");
    const auto& updated_lab_solver=
        updated_lab_export_loader.model.openfoam_solver;
    assert(updated_lab_solver.template_file ==
           "library/openfoam_cfg/screening_foam_cfg.toml");
    assert(updated_lab_solver.thermal_only_maximum_time_step == 20.0);
    assert(updated_lab_solver.airflow_maximum_time_step == 0.0005);
    assert(updated_lab_solver.pimple_outer_correctors == 3);
    assert(updated_lab_solver.thermal_only_pimple_outer_correctors == 2);
    assert(updated_lab_solver.fan_curve_extension_multiplier == 4.0);
    assert(updated_lab_solver.fan_assisted_flow_slope_multiplier == 1.0);

    // A disabled OpenFOAM table may still name a reusable profile for future
    // export, but that profile must not replace the active native root mesh.
    ModelLoader native_regression_loader;
    native_regression_loader.load_fan_curves(
        "library/fan_curves/fan_curves.toml");
    native_regression_loader.load_model(
        "library/models/new_model_updated_native_regression.toml");
    const auto& native_regression=native_regression_loader.model;
    assert(!native_regression.openfoam_solver.enabled);
    assert(native_regression.mesh.adaptive);
    assert(native_regression.mesh.fine_dx == 0.050);
    assert(native_regression.mesh.coarse_dx == 0.200);
    assert(native_regression.mesh.refinement_margin == 0.0);
    assert(native_regression.simulation.native_output_directory ==
           std::filesystem::path(
               "validation/revised_native_regression_2026-08-26/run_002"));
    assert(!native_regression.simulation.native_overwrite);
    assert(native_regression_loader.config.output_directory ==
           std::filesystem::path("structured"));

    // Workload limits are integral and range-checked at parse time; a
    // fractional value must not silently truncate into a weaker guard.
    {
        std::string fractional=read_file("library/tests/valid_model.toml");
        const std::string valid="max_updates = 1000";
        const std::size_t at=fractional.find(valid);
        assert(at != std::string::npos);
        fractional.replace(at,valid.size(),"max_updates = 1.5");
        const auto path=test_root/"fractional_max_updates.toml";
        std::ofstream(path) << fractional;
        bool rejected=false;
        try {
            ModelLoader parsed;
            parsed.load_model(path);
        } catch(const std::runtime_error& error) {
            rejected=std::string(error.what()).find(
                "positive integer: simulation.max_updates") !=
                std::string::npos;
        }
        assert(rejected);
    }

    // Optional integer controls may be omitted, but an explicitly malformed
    // value must never silently become a performance-changing default.
    {
        struct InvalidIntegerCase {
            std::string name;
            std::string source;
            std::string replacement;
            std::string expected_context;
        };
        const std::vector<InvalidIntegerCase> invalid_cases{
            {"fractional_output_interval","output_interval = 1",
             "output_interval = 1.5","simulation.output_interval"},
            {"string_flow_interval","update_flow_interval = 1",
             "update_flow_interval = \"every step\"",
             "simulation.update_flow_interval"}
        };
        for(const auto& invalid : invalid_cases) {
            std::string contents=read_file(
                "library/tests/valid_model.toml");
            const std::size_t at=contents.find(invalid.source);
            assert(at != std::string::npos);
            contents.replace(
                at,invalid.source.size(),invalid.replacement);
            const auto path=test_root/(invalid.name+".toml");
            std::ofstream(path) << contents;
            bool rejected=false;
            try {
                ModelLoader parsed;
                parsed.load_model(path);
            } catch(const std::runtime_error& error) {
                const std::string message=error.what();
                rejected=message.find("Invalid integer") !=
                             std::string::npos &&
                         message.find(invalid.expected_context) !=
                             std::string::npos;
            }
            assert(rejected);
        }

        for(const std::string invalid_interval : {"0","-1"}) {
            std::string contents=read_file(
                "library/tests/valid_model.toml");
            const std::string source="output_interval = 1";
            const std::size_t interval_at=contents.find(source);
            assert(interval_at != std::string::npos);
            contents.replace(
                interval_at,source.size(),
                "output_interval = "+invalid_interval);
            const auto invalid_path=test_root/
                ("invalid_output_interval_"+
                 (invalid_interval=="0" ? std::string("zero") :
                                           std::string("negative"))+
                 ".toml");
            std::ofstream(invalid_path) << contents;
            bool interval_rejected=false;
            try {
                ModelLoader parsed;
                parsed.load_model(invalid_path);
            } catch(const std::runtime_error& error) {
                interval_rejected=std::string(error.what()).find(
                    "simulation.output_interval must be >= 1")!=
                    std::string::npos;
            }
            assert(interval_rejected);
        }

        std::string fractional_substeps=read_file(
            "library/tests/valid_model.toml");
        const std::string flow_header="[flow_solver]\n";
        const std::size_t at=fractional_substeps.find(flow_header);
        assert(at != std::string::npos);
        fractional_substeps.insert(
            at,"max_advection_substeps = 2.5\n\n");
        const auto path=test_root/"fractional_advection_substeps.toml";
        std::ofstream(path) << fractional_substeps;
        bool rejected=false;
        try {
            ModelLoader parsed;
            parsed.load_model(path);
        } catch(const std::runtime_error& error) {
            const std::string message=error.what();
            rejected=message.find("Invalid integer") != std::string::npos &&
                     message.find("simulation.max_advection_substeps") !=
                         std::string::npos;
        }
        assert(rejected);
    }

    // An output cadence longer than the entire native transient is rejected
    // from the loader's allocation-free preflight.
    {
        const auto native_output=test_root/"output_interval_preflight_output";
        std::string impossible=read_file("library/tests/valid_model.toml");
        const std::string simulation_header="[simulation]\n";
        std::size_t at=impossible.find(simulation_header);
        assert(at != std::string::npos);
        impossible.insert(
            at+simulation_header.size(),
            "native_output_directory = \""+
            native_output.generic_string()+"\"\n");
        at=impossible.find("output_interval = 1");
        assert(at != std::string::npos);
        impossible.replace(
            at,std::string("output_interval = 1").size(),
            "output_interval = 2");
        at=impossible.find(
            "[mesh]\n"
            "adaptive = false\n"
            "dx = 0.05\n"
            "dy = 0.05\n"
            "dz = 0.05\n");
        assert(at != std::string::npos);
        impossible.replace(
            at,
            std::string(
                "[mesh]\n"
                "adaptive = false\n"
                "dx = 0.05\n"
                "dy = 0.05\n"
                "dz = 0.05\n").size(),
            "[mesh]\n"
            "adaptive = true\n"
            "fine_dx = 0.001\n"
            "coarse_dx = 0.001\n"
            "refinement_margin = 0.0\n");
        at=impossible.find("max_cell_count = 1000");
        assert(at != std::string::npos);
        impossible.replace(
            at,std::string("max_cell_count = 1000").size(),
            "max_cell_count = 1");
        const auto path=test_root/"output_interval_preflight_model.toml";
        std::ofstream(path) << impossible;
        bool rejected=false;
        try {
            ModelLoader parsed;
            parsed.load_model(path);
            parsed.run();
        } catch(const std::runtime_error& error) {
            rejected=std::string(error.what()).find(
                "output_interval exceeds the fine timestep count before "
                "mesh planning or allocation")!=std::string::npos;
        }
        assert(rejected);
        assert(!std::filesystem::exists(native_output/"output.txt"));
        assert(!std::filesystem::exists(native_output/"simulation.csv"));
    }

    // Geometry-only mode owns three char bitmaps. Its cell count may fit the
    // cell ceiling while their combined payload still violates the memory
    // ceiling; reject that combination before any bitmap or output artifact.
    {
        const auto native_output=test_root/"geometry_bitmap_memory_output";
        std::string impossible=read_file("library/tests/valid_model.toml");
        const std::string simulation_header="[simulation]\n";
        std::size_t at=impossible.find(simulation_header);
        assert(at != std::string::npos);
        impossible.insert(
            at+simulation_header.size(),
            "native_output_directory = \""+
            native_output.generic_string()+"\"\n");
        const auto replace_once=[&](const std::string& from,
                                    const std::string& to) {
            const std::size_t position=impossible.find(from);
            assert(position != std::string::npos);
            impossible.replace(position,from.size(),to);
        };
        replace_once("max_cell_count = 1000",
                     "max_cell_count = 500000");
        replace_once("max_megabyte_usage = 10",
                     "max_megabyte_usage = 1");
        replace_once("dx = 0.05","dx = 0.01");
        replace_once("dy = 0.05","dy = 0.01");
        replace_once("dz = 0.05","dz = 0.01");
        replace_once("width = 0.05","width = 1.0");
        replace_once("depth = 0.05","depth = 1.0");
        replace_once("height = 0.05","height = 0.4");
        const auto path=test_root/"geometry_bitmap_memory_model.toml";
        std::ofstream(path) << impossible;
        bool rejected=false;
        try {
            ModelLoader parsed;
            parsed.load_model(path);
            parsed.run(true);
        } catch(const std::invalid_argument& error) {
            rejected=std::string(error.what()).find(
                "three-bitmap payload exceeds the configured memory "
                "maximum before bitmap allocation")!=std::string::npos;
        }
        assert(rejected);
        assert(!std::filesystem::exists(native_output/"output.txt"));
    }

    // A fine stage that cannot fit the global budget is rejected before mesh
    // allocation and before the geometry/CSV artifacts are written.
    {
        const auto native_output=test_root/"preflight_rejection_output";
        std::string impossible=read_file("library/tests/valid_model.toml");
        const std::string simulation_header="[simulation]\n";
        std::size_t at=impossible.find(simulation_header);
        assert(at != std::string::npos);
        impossible.insert(
            at+simulation_header.size(),
            "native_output_directory = \""+
            native_output.generic_string()+"\"\n");
        at=impossible.find("duration = 0.01");
        assert(at != std::string::npos);
        impossible.replace(at,std::string("duration = 0.01").size(),
                           "duration = 0.02");
        at=impossible.find("max_updates = 1000");
        assert(at != std::string::npos);
        impossible.replace(at,std::string("max_updates = 1000").size(),
                           "max_updates = 1");
        const auto path=test_root/"preflight_rejection_model.toml";
        std::ofstream(path) << impossible;
        bool rejected=false;
        try {
            ModelLoader parsed;
            parsed.load_model(path);
            parsed.run();
        } catch(const std::runtime_error& error) {
            rejected=std::string(error.what()).find(
                "minimum planned cell visits (2)") !=
                std::string::npos;
        }
        assert(rejected);
        assert(!std::filesystem::exists(native_output/"output.txt"));
        assert(!std::filesystem::exists(native_output/"simulation.csv"));
    }

    // max_updates is one cumulative native budget, not one full allowance per
    // multistage Solver. Individually legal one-visit stages total two visits.
    {
        const auto native_output=test_root/"multistage_budget_output";
        std::string impossible=read_file("library/tests/valid_model.toml");
        const std::string simulation_header="[simulation]\n";
        std::size_t at=impossible.find(simulation_header);
        assert(at != std::string::npos);
        impossible.insert(
            at+simulation_header.size(),
            "native_output_directory = \""+
            native_output.generic_string()+"\"\n");
        at=impossible.find("max_updates = 1000");
        assert(at != std::string::npos);
        impossible.replace(at,std::string("max_updates = 1000").size(),
                           "max_updates = 1");
        impossible +=
            "\n[multistage]\n"
            "enabled = true\n"
            "coarse_dt = 0.01\n"
            "coarse_duration = 0.01\n"
            "coarse_update_flow_interval = -1\n"
            "\n[multistage.coarse_mesh]\n"
            "fine_dx = 0.05\n"
            "coarse_dx = 0.05\n"
            "refinement_margin = 0.0\n";
        const auto path=test_root/"multistage_budget_model.toml";
        std::ofstream(path) << impossible;
        bool rejected=false;
        try {
            ModelLoader parsed;
            parsed.load_model(path);
            parsed.run();
        } catch(const std::runtime_error& error) {
            const std::string message=error.what();
            rejected=message.find("minimum planned cell visits (2)") !=
                         std::string::npos &&
                     message.find("before mesh allocation") !=
                         std::string::npos;
        }
        assert(rejected);
        assert(!std::filesystem::exists(native_output/"output.txt"));
        assert(!std::filesystem::exists(native_output/"simulation.csv"));
        assert(!std::filesystem::exists(
            native_output/"coarse_simulation.csv"));
    }

    // A fine mesh that violates the Cell-payload ceiling is rejected before a
    // legal coarse warm-start is allocated or solved.
    {
        const auto native_output=test_root/"fine_memory_preflight_output";
        std::string impossible=read_file("library/tests/valid_model.toml");
        const std::string simulation_header="[simulation]\n";
        std::size_t at=impossible.find(simulation_header);
        assert(at != std::string::npos);
        impossible.insert(
            at+simulation_header.size(),
            "native_output_directory = \""+
            native_output.generic_string()+"\"\n");
        auto replace_once=[&](const std::string& from,
                              const std::string& to) {
            const std::size_t position=impossible.find(from);
            assert(position != std::string::npos);
            impossible.replace(position,from.size(),to);
        };
        replace_once("max_updates = 1000","max_updates = 100000");
        replace_once("max_cell_count = 1000","max_cell_count = 10000");
        replace_once("max_megabyte_usage = 10",
                     "max_megabyte_usage = 1");
        replace_once("width = 0.05","width = 3.0");
        replace_once("depth = 0.05","depth = 1.0");
        replace_once("height = 0.05","height = 1.0");
        replace_once("dx = 0.05","dx = 0.1");
        replace_once("dy = 0.05","dy = 0.1");
        replace_once("dz = 0.05","dz = 0.1");
        impossible +=
            "\n[multistage]\n"
            "enabled = true\n"
            "coarse_dt = 0.01\n"
            "coarse_duration = 0.01\n"
            "coarse_update_flow_interval = -1\n"
            "\n[multistage.coarse_mesh]\n"
            "fine_dx = 0.25\n"
            "coarse_dx = 0.25\n"
            "refinement_margin = 0.0\n";
        const auto path=test_root/"fine_memory_preflight_model.toml";
        std::ofstream(path) << impossible;
        bool rejected=false;
        try {
            ModelLoader parsed;
            parsed.load_model(path);
            parsed.run();
        } catch(const std::runtime_error& error) {
            const std::string message=error.what();
            rejected=message.find("fine two-mesh Cell payload") !=
                         std::string::npos &&
                     message.find("before mesh allocation") !=
                         std::string::npos;
        }
        assert(rejected);
        assert(!std::filesystem::exists(
            native_output/"coarse_simulation.csv"));
        assert(!std::filesystem::exists(native_output/"simulation.csv"));
    }

    // Two-mesh byte accounting remains exact above the signed 32-bit range,
    // and an unsafe direct Mesh construction rejects before Cell allocation.
    {
        const std::size_t large_count=10000000u;
        assert(Mesh::planned_two_mesh_cell_bytes(large_count) ==
               large_count*sizeof(Cell)*2u);
        assert(Mesh::planned_two_mesh_cell_bytes(large_count) >
               static_cast<std::size_t>(
                   std::numeric_limits<int>::max()));
        bool overflow_rejected=false;
        try {
            (void)Mesh::planned_cell_count(
                std::numeric_limits<std::size_t>::max(),2u,1u);
        } catch(const std::overflow_error&) {
            overflow_rejected=true;
        }
        assert(overflow_rejected);

        Environment environment(
            30.0,0.0,20.0,1005.0,0.02587,0.000018,0.71,1.225);
        Workload workload(10,1000000,200000,1);
        bool allocation_rejected=false;
        try {
            Mesh unsafe(
                100,100,10,
                std::vector<double>(100,0.01),
                std::vector<double>(100,0.01),
                std::vector<double>(10,0.01),
                environment,workload);
            (void)unsafe;
        } catch(const std::invalid_argument& error) {
            allocation_rejected=std::string(error.what()).find(
                "before cell allocation") != std::string::npos;
        }
        assert(allocation_rejected);
    }

    {
        std::string malformed=read_file(
            "library/models/new_model_updated_native_regression.toml");
        const std::string valid=
            "native_output_directory = \"validation/"
            "revised_native_regression_2026-08-26/run_002\"";
        const std::size_t at=malformed.find(valid);
        assert(at != std::string::npos);
        malformed.replace(at,valid.size(),
                          "native_output_directory = 42");
        const auto malformed_path=
            test_root/"malformed_native_output_directory.toml";
        std::ofstream(malformed_path) << malformed;
        bool rejected=false;
        try {
            ModelLoader malformed_loader;
            malformed_loader.load_model(malformed_path);
        } catch(const std::runtime_error& error) {
            rejected=std::string(error.what()).find(
                "native_output_directory must be a non-empty string") !=
                std::string::npos;
        }
        assert(rejected);
    }

    // Forcing the native backend on an otherwise enabled profile likewise
    // retains the model's root mesh instead of the OpenFOAM profile mesh.
    ModelLoader forced_native_loader;
    forced_native_loader.load_model(
        "library/tests/openfoam_model.toml",true);
    assert(!forced_native_loader.model.mesh.adaptive);
    assert(forced_native_loader.model.mesh.dx == 0.05);
    assert(forced_native_loader.model.mesh.dy == 0.05);
    assert(forced_native_loader.model.mesh.dz == 0.05);

    ModelLoader legacy_loader;
    legacy_loader.load_model("library/tests/valid_model.toml");
    assert(!legacy_loader.model.openfoam_solver.enabled);

    // Native geometry output is isolated, and a no-overwrite run refuses a
    // populated evidence directory before truncating the existing artifact.
    {
        const auto native_output=test_root/"isolated_native_output";
        std::string isolated=read_file("library/tests/valid_model.toml");
        const std::string simulation_header="[simulation]\n";
        const std::size_t at=isolated.find(simulation_header);
        assert(at != std::string::npos);
        isolated.insert(
            at+simulation_header.size(),
            "native_output_directory = \""+
            native_output.generic_string()+"\"\n"
            "native_overwrite = false\n");
        const auto isolated_model=test_root/"isolated_native_model.toml";
        std::ofstream(isolated_model) << isolated;

        ModelLoader isolated_loader;
        isolated_loader.load_model(isolated_model);
        isolated_loader.run(true);
        const auto geometry=native_output/"output.txt";
        assert(std::filesystem::is_regular_file(geometry));
        const std::string preserved_geometry=read_file(geometry);
        assert(!preserved_geometry.empty());

        bool overwrite_rejected=false;
        try {
            ModelLoader rerun_loader;
            rerun_loader.load_model(isolated_model);
            rerun_loader.run(true);
        } catch(const std::runtime_error& error) {
            overwrite_rejected=std::string(error.what()).find(
                "native_overwrite=false") != std::string::npos;
        }
        assert(overwrite_rejected);
        assert(read_file(geometry) == preserved_geometry);
    }
    assert(!legacy_loader.model.openfoam_solver.template_file.has_value());

    ModelLoader homogeneous_loader;
    homogeneous_loader.load_model(
        "library/tests/minimal_geometry_repro.toml");
    assert(homogeneous_loader.model.components.size() == 1);
    assert(homogeneous_loader.model.components[0].internal_regions.empty());

    ComponentLoader material_reference_loader;
    material_reference_loader.load_component(
        "library/tests/material_reference_component.toml");
    assert(material_reference_loader.model.material.density == 2700.0);
    assert(material_reference_loader.model.material.cp == 900.0);
    assert(material_reference_loader.model.material.k == 205.0);
    assert(material_reference_loader.model.internal_regions.size() == 1);
    const MaterialInput& referenced_internal_material =
        material_reference_loader.model.internal_regions[0].material;
    assert(referenced_internal_material.density == 1200.0);
    assert(referenced_internal_material.cp == 800.0);
    assert(referenced_internal_material.k == 10.0);

    ModelLoader foam_loader;
    foam_loader.load_model("library/tests/openfoam_model.toml");
    assert(foam_loader.model.porous_regions.size()==1);
    assert(foam_loader.model.porous_regions[0].free_area_ratio.value()==0.40);
    {
        std::string legacy_text=read_file("library/tests/openfoam_model.toml");
        const std::string percent_line="porosity_percent = 40.0";
        const auto percent_at=legacy_text.find(percent_line);
        assert(percent_at!=std::string::npos);
        legacy_text.replace(percent_at,percent_line.size(),
                            "free_area_ratio = 0.40");
        const auto legacy_path=test_root/"legacy_porous_ratio.toml";
        std::ofstream(legacy_path) << legacy_text;
        ModelLoader legacy_porous_loader;
        legacy_porous_loader.load_model(legacy_path.string());
        assert(legacy_porous_loader.model.porous_regions[0]
                   .free_area_ratio.value()==0.40);

        std::string ambiguous_text=legacy_text;
        const auto ratio_at=ambiguous_text.find("free_area_ratio = 0.40");
        assert(ratio_at!=std::string::npos);
        ambiguous_text.insert(ratio_at,"porosity_percent = 40.0\n");
        const auto ambiguous_path=test_root/"ambiguous_porosity.toml";
        std::ofstream(ambiguous_path) << ambiguous_text;
        bool rejected_ambiguous=false;
        try {
            ModelLoader ambiguous_loader;
            ambiguous_loader.load_model(ambiguous_path.string());
        } catch(const std::runtime_error& error) {
            rejected_ambiguous=std::string(error.what()).find(
                "specify only porosity_percent")!=std::string::npos;
        }
        assert(rejected_ambiguous);

        const auto load_percent_case=[&](const std::string& value,
                                         const std::string& filename) {
            std::string text=read_file("library/tests/openfoam_model.toml");
            const std::string original="porosity_percent = 40.0";
            const auto at=text.find(original);
            assert(at!=std::string::npos);
            text.replace(at,original.size(),"porosity_percent = "+value);
            const auto path=test_root/filename;
            std::ofstream(path) << text;
            ModelLoader parsed;
            parsed.load_model(path.string());
            return parsed.model.porous_regions[0].free_area_ratio.value();
        };
        assert(load_percent_case("100.0","full_porosity.toml")==1.0);
        for(const auto& invalid : {
                std::pair<std::string,std::string>{"0.0","zero_porosity.toml"},
                {"100.1","excess_porosity.toml"},
                {"-1.0","negative_porosity.toml"}}) {
            bool rejected=false;
            try {
                (void)load_percent_case(invalid.first,invalid.second);
            } catch(const std::runtime_error& error) {
                rejected=std::string(error.what()).find(
                    "porosity_percent must be in (0,100]")!=std::string::npos;
            }
            assert(rejected);
        }
    }
    const PorousRegion parsed_porous=build_porous_region(
        foam_loader.model.porous_regions[0]);
    assert(std::abs(parsed_porous.forchheimer-
        1.0/(0.65*0.65*0.40*0.40*0.05))<1e-10);
    assert(foam_loader.model.openfoam_solver.case_directory ==
           std::filesystem::path("openfoam_cases/openfoam_model"));
    foam_loader.model.openfoam_solver.case_directory=
        test_root/"toml_export";
    const ModelInput& foam=foam_loader.model;
    assert(foam.openfoam_solver.enabled);
    assert(foam.openfoam_solver.template_file ==
           "library/openfoam_cfg/default_foam_cfg.toml");
    // Inline model values override the reusable template.
    assert(foam.openfoam_solver.parallel_processes == 2);
    assert(foam.openfoam_solver.field_write_interval == 5.0);
    assert(foam.openfoam_solver.saved_time_directories == 3);
    // Unspecified values are inherited from the template.
    assert(foam.openfoam_solver.overwrite);
    assert(!foam.openfoam_solver.allow_determinant_warnings);
    assert(foam.openfoam_solver.temperature_dependent_air);
    assert(foam.openfoam_solver.maximum_courant_number == 1.0);
    assert(foam.openfoam_solver.use_fan_startup_ramp);
    assert(foam.openfoam_solver.fan_startup_ramp_time == 0.05);
    assert(foam.openfoam_solver.fan_startup_ramp_steps == 5);
    assert(foam.openfoam_solver.initial_airflow_check_interval == 0.01);
    assert(foam.openfoam_solver.minimum_initial_airflow_duration == 0.30);
    assert(foam.openfoam_solver.airflow_maximum_time_step == 0.001);
    assert(foam.openfoam_solver.thermal_only_maximum_time_step == 5.0);
    assert(foam.openfoam_solver.airflow_refresh_check_interval == 0.01);
    assert(
        foam.openfoam_solver.airflow_refresh_maximum_courant_number == 10.0);
    assert(foam.openfoam_solver.stop_when_thermally_converged);
    assert(foam.openfoam_solver.minimum_thermal_convergence_time == 3600.0);
    assert(
        foam.openfoam_solver.thermal_convergence_reference_interval == 300.0);
    assert(foam.openfoam_solver.maximum_temperature_change == 0.1);
    assert(
        foam.openfoam_solver.maximum_component_average_temperature_change ==
        0.05);
    assert(
        foam.openfoam_solver.thermal_convergence_required_checkpoints == 2);
    assert(std::abs(foam.openfoam_solver.gravity.z+9.80665) < 1e-12);
    // Simulation time remains authoritative in the model, not the template.
    assert(foam.simulation.duration == 10.0);

    const auto load_fidelity_profile=[&](
        const std::string& profile_name) {
        std::string profile_model=
            read_file("library/tests/openfoam_model.toml");
        const std::string default_profile=
            "library/openfoam_cfg/default_foam_cfg.toml";
        const std::string selected_profile=
            "library/openfoam_cfg/"+profile_name+"_foam_cfg.toml";
        const std::size_t profile_at=profile_model.find(default_profile);
        assert(profile_at != std::string::npos);
        profile_model.replace(
            profile_at,default_profile.size(),selected_profile);
        const std::filesystem::path profile_path=
            test_root/(profile_name+"_profile_model.toml");
        std::filesystem::create_directories(profile_path.parent_path());
        std::ofstream output(profile_path);
        output << profile_model;
        output.close();
        ModelLoader loader;
        loader.load_model(profile_path);
        return loader.model;
    };
    const ModelInput screening=load_fidelity_profile("screening");
    assert(screening.mesh.adaptive);
    assert(screening.mesh.fine_dx == 0.02);
    assert(screening.mesh.coarse_dx == 0.10);
    assert(screening.mesh.refinement_margin == 0.02);
    assert(
        screening.openfoam_solver.thermal_only_maximum_time_step == 20.0);
    assert(screening.openfoam_solver.pimple_outer_correctors == 3);
    assert(
        screening.openfoam_solver.thermal_only_pimple_outer_correctors == 2);
    assert(screening.openfoam_solver.airflow_refresh_duration == 0.02);
    assert(screening.openfoam_solver.allow_determinant_warnings);
    assert(screening.openfoam_solver.airflow_maximum_time_step == 0.0005);
    assert(
        screening.openfoam_solver.airflow_refresh_maximum_courant_number ==
        2.0);
    assert(
        screening.openfoam_solver.maximum_device_flow_change_fraction == 0.01);
    assert(
        screening.openfoam_solver.minimum_tracked_boundary_flow_fraction ==
        0.001);
    // Inline test-model values remain authoritative over profile defaults.
    assert(screening.openfoam_solver.airflow_refresh_interval == 5.0);
    assert(screening.openfoam_solver.report_interval == 1.0);
    assert(screening.openfoam_solver.maximum_temperature_change == 0.25);

    // A model-local strict override remains authoritative even when the
    // reusable screening profile is explicitly permissive.
    {
        std::string strict_screening_text=read_file(
            test_root/"screening_profile_model.toml");
        const std::string enabled_line="enabled = true\n";
        const std::size_t solver_table=
            strict_screening_text.find("[openfoam_solver]");
        assert(solver_table != std::string::npos);
        const std::size_t enabled_at=
            strict_screening_text.find(enabled_line,solver_table);
        assert(enabled_at != std::string::npos);
        strict_screening_text.insert(
            enabled_at+enabled_line.size(),
            "allow_determinant_warnings = false\n");
        const std::filesystem::path strict_screening_path=
            test_root/"strict_screening_profile_model.toml";
        std::ofstream(strict_screening_path) << strict_screening_text;
        ModelLoader strict_screening_loader;
        strict_screening_loader.load_model(strict_screening_path);
        assert(!strict_screening_loader.model.openfoam_solver
                    .allow_determinant_warnings);
    }

    const ModelInput indepth=load_fidelity_profile("indepth");
    assert(indepth.mesh.adaptive);
    assert(indepth.mesh.fine_dx == 0.015);
    assert(indepth.mesh.coarse_dx == 0.10);
    assert(indepth.mesh.refinement_margin == 0.02);
    assert(screening.mesh.fine_dx > indepth.mesh.fine_dx);
    assert(screening.mesh.coarse_dx == indepth.mesh.coarse_dx);
    assert(screening.mesh.refinement_margin == indepth.mesh.refinement_margin);
    assert(indepth.openfoam_solver.thermal_only_maximum_time_step == 30.0);
    assert(indepth.openfoam_solver.thermal_only_pimple_outer_correctors == 0);
    assert(indepth.openfoam_solver.minimum_initial_airflow_duration == 0.30);
    assert(!indepth.openfoam_solver.allow_determinant_warnings);
    assert(
        indepth.openfoam_solver.maximum_device_flow_change_fraction == 0.01);
    assert(
        indepth.openfoam_solver.minimum_tracked_boundary_flow_fraction ==
        0.001);
    assert(indepth.openfoam_solver.airflow_maximum_time_step == 0.001);
    assert(indepth.openfoam_solver.maximum_temperature_change == 0.10);

    ModelLoader unsafe_path_loader;
    unsafe_path_loader.load_model("library/tests/openfoam_model.toml");
    unsafe_path_loader.model.openfoam_solver.case_directory=
        test_root/"unsafe case";
    bool rejected_unsafe_path=false;
    try {
        unsafe_path_loader.run();
    } catch(const std::runtime_error& error) {
        rejected_unsafe_path=
            std::string(error.what()).find(
                "OpenFOAM MPI does not support") != std::string::npos;
    }
    assert(rejected_unsafe_path);

    // A leftover transaction file signals a prior interrupted geometry swap.
    // Refuse before overwrite export and preserve existing solution evidence.
    {
        ModelLoader recovery_guard_loader;
        recovery_guard_loader.load_model(
            "library/tests/openfoam_model.toml");
        const std::filesystem::path guarded_case=
            test_root/"geometry_recovery_guard";
        recovery_guard_loader.model.openfoam_solver.case_directory=
            guarded_case;
        std::filesystem::create_directories(guarded_case/"0.25");
        const std::filesystem::path checkpoint=
            guarded_case/"0.25"/"checkpoint_sentinel.txt";
        const std::string checkpoint_bytes="preserve checkpoint\n";
        std::ofstream(checkpoint,std::ios::binary) << checkpoint_bytes;
        std::ofstream(
            guarded_case/".thermal_sim_geometry_previous.txt",
            std::ios::binary) << "prior geometry backup\n";
        bool recovery_guard_rejected=false;
        try {
            recovery_guard_loader.run();
        } catch(const std::runtime_error& error) {
            recovery_guard_rejected=std::string(error.what()).find(
                "geometry transaction recovery file already exists") !=
                std::string::npos;
        }
        assert(recovery_guard_rejected);
        assert(read_file(checkpoint)==checkpoint_bytes);
    }

    // The exporter's read-only preflight, including overwrite=false, must run
    // before the geometry transaction. A routine refusal therefore leaves the
    // prior sidecar and solution evidence byte-for-byte untouched.
    {
        ModelLoader no_overwrite_loader;
        no_overwrite_loader.load_model(
            "library/tests/openfoam_model.toml");
        const std::filesystem::path guarded_case=
            test_root/"geometry_no_overwrite_guard";
        no_overwrite_loader.model.openfoam_solver.case_directory=
            guarded_case;
        no_overwrite_loader.model.openfoam_solver.overwrite=false;
        std::filesystem::create_directories(
            guarded_case/"constant"/"polyMesh");
        std::filesystem::create_directories(guarded_case/"0.25");
        const std::filesystem::path geometry=guarded_case/"geometry.txt";
        const std::filesystem::path checkpoint=
            guarded_case/"0.25"/"checkpoint_sentinel.txt";
        const std::string geometry_bytes="preserved geometry\n";
        const std::string checkpoint_bytes="preserved checkpoint\n";
        std::ofstream(geometry,std::ios::binary) << geometry_bytes;
        std::ofstream(checkpoint,std::ios::binary) << checkpoint_bytes;
        bool no_overwrite_rejected=false;
        try {
            no_overwrite_loader.run();
        } catch(const std::runtime_error& error) {
            no_overwrite_rejected=std::string(error.what()).find(
                "Set overwrite=true to replace files") !=
                std::string::npos;
        }
        assert(no_overwrite_rejected);
        assert(read_file(geometry)==geometry_bytes);
        assert(read_file(checkpoint)==checkpoint_bytes);
        assert(!std::filesystem::exists(
            guarded_case/".thermal_sim_geometry_staging.txt"));
        assert(!std::filesystem::exists(
            guarded_case/".thermal_sim_geometry_previous.txt"));
    }

    const std::filesystem::path case_directory=
        foam.openfoam_solver.case_directory;
    std::filesystem::create_directories(case_directory);
    {
        std::ofstream stale_marker(
            case_directory/".openfoam_regions_prepared");
        stale_marker << "stale\n";
        std::ofstream stale_thermal_state(
            case_directory/".thermal_convergence_state");
        stale_thermal_state << "stale\n";
        std::ofstream stale_thermal_streak(
            case_directory/".thermal_convergence_streak");
        stale_thermal_streak << "9\n";
        std::ofstream stale_initial_airflow(
            case_directory/".initial_airflow_converged");
        stale_initial_airflow << "stale\n";
        std::ofstream stale_refresh(
            case_directory/".airflow_refresh_pending");
        stale_refresh << "stale\n";
        std::ofstream stale_airflow_state(
            case_directory/".airflow_convergence_state");
        stale_airflow_state << "0 0\n";
        for(const char* marker : {
                ".fan_ramp_complete",
                ".mapped_initial_state",
                ".initial_airflow_pending",
                ".initial_air_exchange_state",
                ".initial_airflow_physical_settling",
                ".velocity_convergence_state",
                ".openfoam_mesh_determinant_warning"})
            std::ofstream(case_directory/marker) << "stale\n";
        for(const char* directory : {
                ".accepted_airflow_reference",
                ".accepted_airflow_reference.tmp",
                ".stage_velocity_reference",
                ".stage_velocity_reference.tmp.456"}) {
            std::filesystem::create_directories(case_directory/directory);
            std::ofstream(case_directory/directory/"stale") << "stale\n";
        }
    }
    std::filesystem::create_directories(case_directory/"0.25");
    std::filesystem::create_directories(case_directory/"processor0");
    std::filesystem::create_directories(case_directory/"postProcessing");
    std::ostringstream export_output;
    std::streambuf* original_output=std::cout.rdbuf(export_output.rdbuf());
    foam_loader.run();
    std::cout.rdbuf(original_output);
    assert(!std::filesystem::exists(
        case_directory/".thermal_sim_geometry_staging.txt"));
    assert(!std::filesystem::exists(
        case_directory/".thermal_sim_geometry_previous.txt"));
    assert(export_output.str().find(
        "Run from a WSL terminal with:") != std::string::npos);
    assert(export_output.str().find(
        "./run_parallel.sh 2 --multirate 10") != std::string::npos);
    assert(export_output.str().find(
        "./run_parallel.sh 2 --multirate 18000") != std::string::npos);
    assert(export_output.str().find(
        "./run_parallel.sh 2 --multirate 100000 10000") != std::string::npos);
    assert(export_output.str().find(
        "Plot the latest temperature cut plane interactively") !=
        std::string::npos);
    assert(export_output.str().find(
        "Generate the signed-flow and hot-air recirculation PNG") !=
        std::string::npos);
    assert(export_output.str().find(
        "plot/recirculation_report.py") != std::string::npos ||
           export_output.str().find(
        "plot\\recirculation_report.py") != std::string::npos);
    assert(export_output.str().find(
        "Plot the complete 3D rack temperature field interactively") !=
        std::string::npos);
    assert(export_output.str().find(
        "Save the complete 3D rack temperature plot to PNG") !=
        std::string::npos);
    assert(export_output.str().find(
        "Animate all written full-rack temperature results to MP4") !=
        std::string::npos);
    assert(export_output.str().find(
        "plot/heat_animation.py") != std::string::npos ||
           export_output.str().find(
        "plot\\heat_animation.py") != std::string::npos);
    assert(export_output.str().find(
        "--format openfoam --case") != std::string::npos);
    assert(export_output.str().find(
        "--time latest --slice-axis y --temperature-units C") !=
        std::string::npos);
    assert(export_output.str().find(
        "--time latest --slice-axis none --opacity 0.35 "
        "--temperature-units C") != std::string::npos);
    assert(export_output.str().find(
        "temperature_latest_full_rack.png") != std::string::npos);
    assert(export_output.str().find(
        "--animate --slice-axis none --opacity 0.35") !=
        std::string::npos);
    assert(export_output.str().find(
        "temperature_full_rack.mp4") != std::string::npos);
    assert(export_output.str().find(
        "Generate the temperature convergence PNG and CSV") !=
        std::string::npos);
    assert(export_output.str().find(
        "--convergence-report --temperature-units C --skip 1") !=
        std::string::npos);
    assert(export_output.str().find(
        "temperature_convergence.png") != std::string::npos);
    assert(export_output.str().find("--save") != std::string::npos);
#ifdef _WIN32
    const std::size_t wsl_command=export_output.str().find("  cd '/mnt/");
    assert(wsl_command != std::string::npos);
    const std::size_t wsl_command_end=
        export_output.str().find('\n',wsl_command);
    assert(export_output.str().substr(
        wsl_command,wsl_command_end-wsl_command).find('\\') ==
        std::string::npos);
    const std::size_t plot_command=export_output.str().find(
        "  python '",wsl_command_end);
    assert(plot_command != std::string::npos);
    const std::size_t plot_command_end=
        export_output.str().find('\n',plot_command);
    assert(export_output.str().substr(
        plot_command,plot_command_end-plot_command).find('\\') ==
        std::string::npos);
#endif
    assert(!std::filesystem::exists(
        case_directory/".openfoam_regions_prepared"));
    assert(!std::filesystem::exists(
        case_directory/".thermal_convergence_state"));
    assert(!std::filesystem::exists(
        case_directory/".thermal_convergence_streak"));
    assert(!std::filesystem::exists(
        case_directory/".initial_airflow_converged"));
    assert(!std::filesystem::exists(
        case_directory/".airflow_refresh_pending"));
    assert(!std::filesystem::exists(
        case_directory/".airflow_convergence_state"));
    for(const char* state : {
            ".fan_ramp_complete",
            ".mapped_initial_state",
            ".initial_airflow_pending",
            ".initial_air_exchange_state",
            ".initial_airflow_physical_settling",
            ".velocity_convergence_state",
            ".openfoam_mesh_determinant_warning",
            ".accepted_airflow_reference",
            ".accepted_airflow_reference.tmp",
            ".stage_velocity_reference",
            ".stage_velocity_reference.tmp.456"})
        assert(!std::filesystem::exists(case_directory/state));
    assert(!std::filesystem::exists(case_directory/"0.25"));
    assert(!std::filesystem::exists(case_directory/"processor0"));
    assert(!std::filesystem::exists(case_directory/"postProcessing"));
    assert(std::filesystem::is_directory(case_directory/"0"));
    assert(std::filesystem::is_regular_file(
        case_directory/"system/controlDict"));
    assert(std::filesystem::is_regular_file(
        case_directory/"prepare_regions.sh"));
    assert(std::filesystem::is_regular_file(
        case_directory/"geometry.txt"));
    assert(std::filesystem::is_regular_file(
        case_directory/"provenance/model.toml"));
    assert(!std::filesystem::exists(
        case_directory/"provenance/fan_curves.toml"));
    const std::string geometry=
        read_file(case_directory/"geometry.txt");
    assert(geometry.find("Rack dimensions:") != std::string::npos);
    assert(geometry.find("Component 1: heated block") != std::string::npos);
    const std::string control=
        read_file(case_directory/"system/controlDict");
    const std::string decomposition=
        read_file(case_directory/"system/decomposeParDict");
    const std::string gravity=
        read_file(case_directory/"constant/g");
    const std::string fluid_solution=
        read_file(case_directory/"system/fluid/fvSolution");
    const std::string solid_temperature=
        read_file(case_directory/"0/heated_block_0/T");
    const std::string run_parallel=
        read_file(case_directory/"run_parallel.sh");
    const std::string prepare_regions=
        read_file(case_directory/"prepare_regions.sh");
    assert(prepare_regions.find("checkMesh.prepare.log") !=
           std::string::npos);
    assert(prepare_regions.find(
        "Failed [1-9][0-9]* mesh checks") != std::string::npos);
    assert(prepare_regions.find(
        "the solver will not run") != std::string::npos);
    assert(prepare_regions.find(
        "allow_determinant_warnings=\"false\"") != std::string::npos);
    assert(prepare_regions.find(
        "mesh quality policy rejects determinant warnings") !=
           std::string::npos);
    assert(prepare_regions.find(
        "failed_checks != determinant_failures") != std::string::npos);
    const std::size_t determinant_rejection=prepare_regions.find(
        "mesh quality policy rejects determinant warnings");
    const std::size_t prepared_marker=prepare_regions.find(
        "touch \"$case_dir/.openfoam_regions_prepared\"");
    assert(determinant_rejection < prepared_marker);
    const std::string run_serial=
        read_file(case_directory/"run_cht.sh");
    assert(run_parallel.find(
        "Process count must be an integer of at least two") !=
           std::string::npos);
    assert(run_parallel.find(
        "awk -v v=\"$requested_end\"") != std::string::npos);
    assert(control.find("endTime         10") != std::string::npos);
    assert(control.find("deltaT          0.01") != std::string::npos);
    assert(control.find("purgeWrite      3") != std::string::npos);
    assert(control.find("writeFormat     binary") != std::string::npos);
    assert(control.find("timePrecision   17") != std::string::npos);
    assert(control.find(
        "internal_Passive_test_vent_0_temperature_average") !=
        std::string::npos);
    assert(control.find("regionType cellZone") != std::string::npos);
    assert(control.find(
        "name internal_Passive_test_vent_0") != std::string::npos);
    assert(decomposition.find("numberOfSubdomains 2") != std::string::npos);
    assert(gravity.find("(0 0 -9.80665)") != std::string::npos);
    assert(fluid_solution.find("pRefCell") != std::string::npos);
    assert(solid_temperature.find("\".*\"") != std::string::npos);
    assert(solid_temperature.find("type zeroGradient") != std::string::npos);
    assert(std::filesystem::is_regular_file(
        case_directory/"constant/fluid/fvOptions.fullFan"));
    assert(run_parallel.find("run_fan_ramp") != std::string::npos);
    assert(run_parallel.find("flock -n 9") != std::string::npos);
    assert(run_parallel.find("exec 9>>\"$run_lock\"") !=
           std::string::npos);
    assert(run_parallel.find("command -v flock") != std::string::npos);
    assert(run_parallel.find("Another thermal solver is already writing") !=
           std::string::npos);
    assert(run_parallel.find(
        "Ambiguous duplicate OpenFOAM root times") != std::string::npos);
    assert(run_parallel.find("root_time_dirs") != std::string::npos);
    assert(run_parallel.find("d<=1e-12*s") != std::string::npos);
    assert(run_parallel.find("processor_time_complete()") !=
           std::string::npos);
    assert(run_parallel.find(
        "for field in T U p p_rgh phi rho k omega nut alphat") !=
           std::string::npos);
    assert(run_parallel.find("latest_complete_processor_time()") !=
           std::string::npos);
    assert(run_parallel.find("preflight_warm_start_state()") !=
           std::string::npos);
    assert(run_parallel.find(
        "common_time=$(latest_complete_processor_time \"$expected\")") !=
           std::string::npos);
    assert(run_parallel.find(
        "case \"$configured_start_from\" in") != std::string::npos);
    assert(run_parallel.find(
        "startTime|latestTime|firstTime) ;;") != std::string::npos);
    assert(run_parallel.find(
        "[[ \"$configured_start_from\" == startTime ]] && awk -v configured=") !=
           std::string::npos);
    assert(run_parallel.find(
        "latest common complete processor checkpoint $common_time") !=
           std::string::npos);
    assert(run_parallel.find(
        "only the decomposed t=0 initial state; no completed solver checkpoint exists yet") !=
           std::string::npos);
    assert(run_parallel.find(
        "postProcessing time directories newer than the latest common complete") !=
           std::string::npos);
    assert(run_parallel.find(
        "data files containing first-column time samples newer than") !=
           std::string::npos);
    assert(run_parallel.find(
        "find \"$case_dir/postProcessing\" -type f \\( -name '*.dat' -o -name '*.csv' \\) -exec awk") !=
           std::string::npos);
    assert(run_parallel.find(
        "print FILENAME \"\\t\" value") !=
           std::string::npos);
    assert(run_parallel.find("nextfile") != std::string::npos);
    assert(run_parallel.find(
        "Quarantine or remove rejected/future postProcessing data") !=
           std::string::npos);
    const std::size_t warm_preflight=run_parallel.find(
        "preflight_warm_start_state false true || exit $?");
    assert(warm_preflight != std::string::npos);
    assert(warm_preflight < run_parallel.find("reuse_decomposition=false"));
    assert(run_parallel.find(
        "preflight_warm_start_state true false || exit $?",
        warm_preflight) != std::string::npos);
    assert(run_parallel.find("discard_incomplete_processor_tail()") !=
           std::string::npos);
    assert(run_parallel.find(
        "Discarded incomplete processor${rank} checkpoint") !=
           std::string::npos);
    assert(run_parallel.find("mapped_phi_present=false") !=
           std::string::npos);
    assert(run_parallel.find(
        "has no face-flux field fluid/phi") != std::string::npos);
    assert(run_parallel.find(
        "before --multirate") != std::string::npos);
    const std::size_t duplicate_time_check=run_parallel.find(
        "Ambiguous duplicate OpenFOAM root times");
    const std::size_t decomposition_setup=run_parallel.find(
        "reuse_decomposition=false");
    assert(duplicate_time_check<decomposition_setup);
    assert(run_serial.find(
        "Serial run_cht.sh is disabled for this multirate export") !=
           std::string::npos);
    assert(run_serial.find("flock -n 9") == std::string::npos);
    assert(run_parallel.find("Fan ramp stage") != std::string::npos);
    assert(run_parallel.find("adaptive_initial_airflow") !=
           std::string::npos);
    const std::size_t adaptive_refresh_start=
        run_parallel.find("adaptive_airflow_refresh()");
    const std::size_t adaptive_initial_start=
        run_parallel.find("adaptive_initial_airflow()", adaptive_refresh_start);
    assert(adaptive_refresh_start != std::string::npos);
    assert(adaptive_initial_start != std::string::npos);
    const std::string adaptive_refresh=run_parallel.substr(
        adaptive_refresh_start, adaptive_initial_start-adaptive_refresh_start);
    assert(adaptive_refresh.find("previous_flows=()") == std::string::npos);
    assert(adaptive_refresh.find("Retain the last accepted operating point") !=
           std::string::npos);
    assert(run_parallel.find("previous_flows=()", adaptive_initial_start) !=
           std::string::npos);
    const std::size_t empty_initial_marker=run_parallel.find(
        "if [[ -f \"$initial_pending_marker\" && "
        "! -s \"$initial_pending_marker\" ]]",
        adaptive_initial_start);
    const std::size_t resumable_initial_marker=run_parallel.find(
        "if [[ -s \"$initial_pending_marker\" ]]",
        empty_initial_marker);
    const std::size_t new_initial_window=run_parallel.find(
        "if [[ ! -f \"$initial_pending_marker\" ]]",
        resumable_initial_marker);
    const std::size_t initial_marker_write=run_parallel.find(
        "printf '%s\\n' \"$initial_start\" > \"$initial_pending_tmp\"",
        new_initial_window);
    const std::size_t initial_marker_publish=run_parallel.find(
        "mv -f \"$initial_pending_tmp\" \"$initial_pending_marker\"",
        initial_marker_write);
    const std::size_t initial_flow_reset=run_parallel.find(
        "previous_flows=()", adaptive_initial_start);
    const std::size_t initial_state_reset=run_parallel.find(
        "rm -f \"$velocity_convergence_state\" "
        "\"$airflow_convergence_state\"",initial_marker_publish);
    const std::size_t initial_limit_assignment=run_parallel.find(
        "initial_limit=$(awk", adaptive_initial_start);
    assert(empty_initial_marker != std::string::npos);
    assert(resumable_initial_marker>empty_initial_marker);
    assert(new_initial_window>resumable_initial_marker);
    assert(initial_marker_write>new_initial_window);
    assert(initial_marker_publish>initial_marker_write);
    assert(new_initial_window<initial_flow_reset);
    assert(initial_state_reset>initial_marker_publish);
    assert(initial_flow_reset<initial_limit_assignment);
    assert(run_parallel.find(".initial_airflow_pending") !=
           std::string::npos);
    assert(run_parallel.find(
        "Resuming initial airflow observation window",
        adaptive_initial_start) != std::string::npos);
    assert(run_parallel.find(
        "Discarding incompatible initial-airflow pending state",
        adaptive_initial_start) != std::string::npos);
    assert(run_parallel.find(
        "rm -f \"$initial_pending_marker\"",
        adaptive_initial_start) != std::string::npos);
    assert(run_parallel.find(
        "initial_limit=$(awk -v start=\"$initial_start\"",
        adaptive_initial_start) != std::string::npos);
    assert(run_parallel.find("limit=start+maximum", adaptive_initial_start) !=
           std::string::npos);
    const std::size_t acceptance_reference_guard=run_parallel.find(
        "acceptance_reference_complete=true");
    const std::size_t acceptance_time_check=run_parallel.find(
        "[[ -f \"$accepted_airflow_reference/time\" ]]",
        acceptance_reference_guard);
    const std::size_t acceptance_rank_check=run_parallel.find(
        "[[ -f \"$accepted_airflow_reference/processor${rank}/U\" ]]",
        acceptance_time_check);
    const std::size_t incomplete_acceptance=run_parallel.find(
        "if [[ \"$acceptance_reference_complete\" != true ]]",
        acceptance_rank_check);
    const std::size_t revoke_initial_acceptance=run_parallel.find(
        "rm -f -- \"$initial_convergence_marker\"",incomplete_acceptance);
    const std::size_t remove_incomplete_reference=run_parallel.find(
        "rm -rf -- \"$accepted_airflow_reference\"",
        revoke_initial_acceptance);
    const std::size_t force_initial_revalidation=run_parallel.find(
        "if [[ ! -f \"$initial_convergence_marker\" ]]",
        remove_incomplete_reference);
    assert(acceptance_reference_guard != std::string::npos);
    assert(acceptance_time_check>acceptance_reference_guard);
    assert(acceptance_rank_check>acceptance_time_check);
    assert(incomplete_acceptance>acceptance_rank_check);
    assert(revoke_initial_acceptance>incomplete_acceptance);
    assert(remove_incomplete_reference>revoke_initial_acceptance);
    assert(force_initial_revalidation>remove_incomplete_reference);
    assert(run_parallel.find(
        "Initial-airflow acceptance evidence is incomplete; revoking the "
        "marker and requiring full revalidation.") != std::string::npos);
    assert(run_parallel.find("internal_fan_names") != std::string::npos);
    assert(run_parallel.find("previous_smoothed_internal_flows") !=
           std::string::npos);
    assert(run_parallel.find(".airflow_convergence_state") !=
           std::string::npos);
    assert(run_parallel.find(
        "Restored airflow convergence baseline from t=$state_time") !=
           std::string::npos);
    assert(run_parallel.find(
        "Ignoring incompatible, malformed, or future airflow convergence state") !=
           std::string::npos);
    assert(run_parallel.find(
        "mv -f \"$airflow_state_tmp\" \"$airflow_convergence_state\"") !=
           std::string::npos);
    assert(run_parallel.find("0.5*(a+b)") != std::string::npos);
    assert(run_parallel.find("maxFlowDevice=") != std::string::npos);
    assert(run_parallel.find("boundary_flow_lookup") != std::string::npos);
    assert(run_parallel.find("boundary_flow_floor") != std::string::npos);
    assert(run_parallel.find(
        "floor>0 && aa<floor && bb<floor") != std::string::npos);
    assert(run_parallel.find("boundaryFlowFloor=") != std::string::npos);
    assert(run_parallel.find("fan_curve_domain_rules") !=
           std::string::npos);
    assert(run_parallel.find("fan_domain_warning_fraction=0.9") !=
           std::string::npos);
    assert(run_parallel.find(
        "Fan outside signed curve domain") != std::string::npos);
    assert(run_parallel.find(
        "Fan on signed assisted-flow branch") != std::string::npos);
    assert(run_parallel.find(
        "Fan near positive-pressure curve limit") != std::string::npos);
    assert(run_parallel.find("fanDomainOK=") != std::string::npos);
    assert(run_parallel.find(
        "if [[ \"$fan_domain_ok\" != 1 ]]; then stable=0; fi") !=
           std::string::npos);
    assert(run_parallel.find("estimatedAirExchangeTime=") !=
           std::string::npos);
    assert(run_parallel.find("maxima+=(\"$value\")") != std::string::npos);
    assert(run_parallel.find("maximum_peak_delta") != std::string::npos);
    assert(run_parallel.find("airflow_refresh_validated=0") !=
           std::string::npos);
    assert(run_parallel.find("airflow_refresh_validated=1") !=
           std::string::npos);
    assert(run_parallel.find(
        "checkpoint remains unvalidated") != std::string::npos);
    assert(run_parallel.find(
        "pending refresh will resume before the next thermal-only stage") !=
           std::string::npos);
    const std::size_t pending_refresh_message=run_parallel.find(
        "pending refresh will resume before the next thermal-only stage");
    const std::size_t endpoint_refresh_condition=run_parallel.rfind(
        "if ! awk -v a=\"$current\"",pending_refresh_message);
    assert(endpoint_refresh_condition != std::string::npos);
    assert(run_parallel.substr(
        endpoint_refresh_condition,
        pending_refresh_message-endpoint_refresh_condition).find(
            "rm -f \"$refresh_pending_marker\"") == std::string::npos);
    assert(run_parallel.substr(
        endpoint_refresh_condition,
        pending_refresh_message-endpoint_refresh_condition).find(
            "tol=1e-9*s; if(tol>1e-8)tol=1e-8") !=
           std::string::npos);
    const std::size_t exact_endpoint_helper=run_parallel.find(
        "require_exact_endpoint()");
    const std::size_t stage_interval_helper=run_parallel.find(
        "classify_stage_interval()",exact_endpoint_helper);
    assert(exact_endpoint_helper != std::string::npos);
    assert(stage_interval_helper>exact_endpoint_helper);
    assert(run_parallel.substr(
        exact_endpoint_helper,
        stage_interval_helper-exact_endpoint_helper).find(
            "if(tolerance>1e-8)tolerance=1e-8") != std::string::npos);
    const std::size_t refresh_writer=run_parallel.find(
        "write_airflow_refresh_state()");
    const std::size_t refresh_committer=run_parallel.find(
        "commit_airflow_refresh_state()",refresh_writer);
    const std::size_t refresh_normalizer=run_parallel.find(
        "normalize_airflow_refresh_journal()",refresh_committer);
    const std::size_t refresh_temp=run_parallel.find(
        "temporary=\"${refresh_pending_marker}.tmp.$$\"",refresh_writer);
    const std::size_t refresh_publish=run_parallel.find(
        "mv -f -- \"$temporary\" \"$refresh_pending_marker\"",
        refresh_temp);
    const std::size_t owed_refresh=run_parallel.find(
        "write_airflow_refresh_state owed \"$current\" "
        "\"$frozen_target\"");
    const std::size_t thermal_only_stage=run_parallel.find(
        "stage true \"$frozen_target\"",owed_refresh);
    assert(refresh_writer != std::string::npos);
    assert(refresh_committer>refresh_writer);
    assert(refresh_normalizer>refresh_committer);
    assert(refresh_temp>refresh_writer);
    assert(refresh_publish>refresh_temp);
    const std::string refresh_commit_body=run_parallel.substr(
        refresh_committer,refresh_normalizer-refresh_committer);
    assert(refresh_commit_body.find("if [[ \"$state\" != owed ]]") !=
           std::string::npos);
    assert(refresh_commit_body.find("[[ ! \"$start\" =~ ^[0-9]+") !=
           std::string::npos);
    assert(refresh_commit_body.find("[[ ! \"$target\" =~ ^[0-9]+") !=
           std::string::npos);
    assert(refresh_commit_body.find(
        "require_exact_endpoint \"$start\" \"$expected_start\"") !=
           std::string::npos);
    assert(refresh_commit_body.find(
        "require_exact_endpoint \"$target\" \"$expected_target\"") !=
           std::string::npos);
    assert(refresh_commit_body.find(
        "require_exact_endpoint \"$actual\" \"$expected_target\"") !=
           std::string::npos);
    assert(refresh_commit_body.find(
        "write_airflow_refresh_state active \"$actual\"") !=
           std::string::npos);
    assert(owed_refresh != std::string::npos);
    assert(thermal_only_stage>owed_refresh);
    const std::size_t stage_function=run_parallel.find("stage()\n");
    const std::size_t stage_endpoint_check=run_parallel.find(
        "require_exact_endpoint \"$actual_time\" \"$target\"",
        stage_function);
    const std::size_t stage_thermal_source_restore=run_parallel.find(
        "if [[ \"$thermal_only\" == \"true\" && ",stage_endpoint_check);
    const std::size_t active_refresh_commit=run_parallel.find(
        "commit_airflow_refresh_state \"$actual_time\" "
        "\"$current\" \"$target\"",
        stage_thermal_source_restore);
    const std::size_t stage_prune=run_parallel.find(
        "prune_processor_times",active_refresh_commit);
    const std::size_t stage_summary=run_parallel.find(
        "summary \"stage label=$label",active_refresh_commit);
    const std::size_t stage_current_commit=run_parallel.find(
        "current=\"$actual_time\"",active_refresh_commit);
    assert(stage_function != std::string::npos);
    assert(stage_endpoint_check>stage_function);
    assert(stage_thermal_source_restore>stage_endpoint_check);
    assert(active_refresh_commit>stage_thermal_source_restore);
    assert(stage_prune>active_refresh_commit);
    assert(stage_summary>active_refresh_commit);
    assert(stage_current_commit>stage_summary);
    assert(run_parallel.find(
        "An uncommitted thermal-only checkpoint advanced from") !=
           std::string::npos);
    assert(run_parallel.find(
        "refusing automatic recovery.") != std::string::npos);
    assert(run_parallel.find(
        "Recovered completed or partial thermal-only checkpoint") ==
           std::string::npos);
    assert(run_parallel.find(
        "airflow_validated=\"$airflow_refresh_validated\"") !=
           std::string::npos);
    assert(control.find("fluid_temperature_internal_maximum") !=
           std::string::npos);
    assert(control.find("operation max;") != std::string::npos);
    assert(run_parallel.find("maxInternalCellChange=") != std::string::npos);
    assert(run_parallel.find(
        "fluid report does not match the current solver checkpoint") !=
           std::string::npos);
    assert(run_parallel.find(
        "maximum for component region $region is stale") !=
           std::string::npos);
    assert(run_parallel.find(
        "average for component region $region is stale") !=
           std::string::npos);
    assert(run_parallel.find(
        "if(tolerance>1e-8)tolerance=1e-8; "
        "exit !(delta<=tolerance)") != std::string::npos);
    assert(run_parallel.find("new per-component peak baseline") !=
           std::string::npos);
    assert(run_parallel.find("volume*rho/one_way") != std::string::npos);
    assert(run_parallel.find("${#boundary_flow_names[@]} -eq 0") !=
           std::string::npos);
    assert(run_parallel.find("imbalance=0") != std::string::npos);
    assert(run_parallel.find("Properties") != std::string::npos);
    const std::size_t internal_fan_check=
        run_parallel.find("Internal fan not producing positive through-flow");
    assert(internal_fan_check != std::string::npos);
    assert(internal_fan_check >
           run_parallel.find("for name in \"${internal_fan_names[@]}\""));
    assert(run_parallel.find(
        "Initial airflow failed to converge") != std::string::npos);
    assert(run_parallel.find(
        "printf \"%.17g %d\", remaining/n,n") != std::string::npos);
    assert(run_parallel.find(
        "-funcs '(CourantNo fieldMinMax(Co))'") != std::string::npos);
    assert(run_parallel.find(
        "Courant preflight: predictedMaxCo=") != std::string::npos);
    assert(run_parallel.find(
        "dt*0.5*limit/observed") != std::string::npos);
    assert(run_parallel.find(
        "safe=(observed>0?dt*0.5*limit/observed:hard)") !=
           std::string::npos);
    assert(run_parallel.find(
        "Courant postflight: actualMaxCo=") != std::string::npos);
    assert(run_parallel.find(
        "Live-flow Courant limit exceeded") != std::string::npos);
    assert(run_parallel.find(
        "scale=(co<2?co/2:1)") != std::string::npos);
    assert(run_parallel.find(
        "Startup has no established flow field") != std::string::npos);
    assert(run_parallel.find(
        "-entry writeInterval -set \"$ramp_steps\"") !=
           std::string::npos);
    assert(run_parallel.find(
        "-entry maxDeltaT -set \"$ramp_dt\"") != std::string::npos);
    assert(run_parallel.find(
        "-entry writeControl -set timeStep") !=
           std::string::npos);
    assert(run_parallel.find(
        "-entry adjustTimeStep -set false") != std::string::npos);
    assert(run_parallel.find(
        "deltaT=$ramp_dt, steps=$ramp_steps") != std::string::npos);
    assert(run_parallel.find(
        "$label Courant limit exceeded or unavailable") !=
           std::string::npos);
    assert(run_parallel.find(
        "-entry writeControl -set \"$stage_write_control\"") !=
           std::string::npos);
    assert(run_parallel.find(
        "-entry writeInterval -set \"$stage_write_interval\"") !=
           std::string::npos);
    assert(run_parallel.find(
        "for field in U p p_rgh phi rho k omega nut alphat") !=
        std::string::npos);
    assert(run_parallel.find(
        "cp -p \"$source_field\" \"$target_field\"") !=
        std::string::npos);
    assert(run_parallel.find(
        "printf \"%.17g\", x") != std::string::npos);
    assert(run_parallel.find(
        "if(d < -tolerance) print \"reversed\"") !=
           std::string::npos);
    assert(run_parallel.find(
        "x=(int(a/d)+1)*d") != std::string::npos);
    assert(run_parallel.find(
        "if(x<=a+1e-9)x+=d") != std::string::npos);
    assert(run_parallel.find(
        "stage_max_dt=$(awk") != std::string::npos);
    assert(run_parallel.find(
        "Thermal-only maxCo=$max_co is diagnostic") !=
        std::string::npos);
    assert(run_parallel.find(
        "-v flow_max=\"0.001") != std::string::npos);
    assert(run_parallel.find(
        "adjust_time_step=false") != std::string::npos);
    assert(run_parallel.find(
        "stage_write_control=timeStep") != std::string::npos);
    assert(run_parallel.find(
        "-entry maxDeltaT -set \"$stage_max_dt\"") != std::string::npos);
    assert(run_parallel.find(
        "-entry index -set 0") != std::string::npos);
    assert(run_parallel.find(
        "Normalized legacy checkpoint directory") != std::string::npos);
    assert(run_parallel.find(
        "printf \"%.17g\", t") != std::string::npos);
    assert(run_parallel.find(
        "${saved_time}/uniform/time") != std::string::npos);
    assert(run_parallel.find(
        "restart_dt=\"$stage_dt\"") != std::string::npos);
    assert(run_parallel.find(
        "-entry deltaT0") != std::string::npos);
    assert(run_parallel.find(
        "require_exact_endpoint()") != std::string::npos);
    assert(run_parallel.find(
        "tolerance=1e-9*scale; if(tolerance>1e-8)tolerance=1e-8") !=
           std::string::npos);
    assert(run_parallel.find(
        "delta<=tolerance") != std::string::npos);
    assert(run_parallel.find(
        "terminal airflow validation remains pending") !=
           std::string::npos);
    assert(run_parallel.find(
        "terminal_requested_end") == std::string::npos);
    assert(run_parallel.find(
        "Conventional run mode is disabled for this multirate export") !=
           std::string::npos);
    assert(run_parallel.find(
        "mode=\"${2:---multirate}\"") != std::string::npos);
    assert(run_parallel.find(
        "-entry startTime -set \"$current\"") != std::string::npos);
    assert(run_parallel.find(
        "foamDictionary -precision 17") != std::string::npos);
    assert(run_parallel.find(
        "-allRegions -time \"$reconstruct_time\"") != std::string::npos);
    assert(run_parallel.find(
        ".airflow_refresh_pending") != std::string::npos);
    assert(run_parallel.find(
        "Retrying interrupted airflow refresh") != std::string::npos);
    assert(run_parallel.find(
        "thermal_metrics_converged") != std::string::npos);
    assert(run_parallel.find(
        ".thermal_convergence_state") != std::string::npos);
    assert(run_parallel.find(
        "Discarding future thermal-convergence state") !=
        std::string::npos);
    assert(run_parallel.find(
        "-name 'volFieldValue*.dat'") != std::string::npos);
    assert(run_parallel.find(
        "sort -g -k1,1 | tail -1") != std::string::npos);
    assert(run_parallel.find(
        "maxInternalCellChange=$scaled_delta K/300s") != std::string::npos);
    assert(run_parallel.find(
        "Thermal convergence checkpoint $streak/2 accepted") !=
        std::string::npos);
    assert(run_parallel.find(
        "elif [[ \"$airflow_validated\" == 1 ]]") !=
        std::string::npos);
    assert(run_parallel.find(
        "Preserving thermal convergence streak $streak") !=
        std::string::npos);
    assert(run_parallel.find(
        "\"$airflow_validated\" == 1") != std::string::npos);
    assert(run_parallel.find(
        "Thermal and airflow convergence criteria satisfied") !=
        std::string::npos);
    assert(run_parallel.find(
        "Reusing $processes valid processor partitions") !=
        std::string::npos);
    const auto interrupted_reconstruction = run_parallel.find(
        "Reconstructing interrupted parallel time");
    const auto stale_partition_cleanup = run_parallel.find(
        "rm -rf -- \"$processor_dir\"");
    const auto redecomposition = run_parallel.find(
        "decomposePar -case \"$case_dir\"");
    assert(interrupted_reconstruction != std::string::npos);
    assert(stale_partition_cleanup != std::string::npos);
    assert(redecomposition != std::string::npos);
    assert(interrupted_reconstruction < stale_partition_cleanup);
    assert(stale_partition_cleanup < redecomposition);
    assert(run_parallel.find(
        "system/controlDict\" -entry deltaT -set") != std::string::npos);
    assert(run_parallel.find(
        "system/controlDict\" -entry writeInterval -set") !=
        std::string::npos);
    const std::string production_write_control =
        "system/controlDict\" -entry writeControl -set "
        "adjustableRunTime";
    const auto warm_start_position = run_parallel.find(
        "if [[ \"$mode\" == \"--warm-start\" ]]");
    const auto warm_interval_position = run_parallel.find(
        "-entry writeInterval -set \"$warm_restart_steps\"",
        warm_start_position);
    const auto warm_write_control_position = run_parallel.find(
        "-entry writeControl -set timeStep",warm_start_position);
    assert(warm_start_position != std::string::npos);
    assert(warm_write_control_position != std::string::npos);
    assert(warm_interval_position != std::string::npos);
    assert(warm_write_control_position < warm_interval_position);
    assert(run_parallel.find(
        "foamDictionary -precision 17 \"$case_dir/system/controlDict\" "
        "-entry endTime -set \"$warm_window_target\"") !=
        std::string::npos);
    assert(run_parallel.find(
        "warm_window_target=$(next_warm_start_window") !=
        std::string::npos);
    assert(run_parallel.find(
        "foamDictionary -precision 17 \"$case_dir/system/controlDict\" "
        "-entry stopAt -set endTime", warm_start_position) != std::string::npos);
    assert(run_parallel.find(
        "foamDictionary -precision 17 \"$case_dir/system/controlDict\" "
        "-entry writeInterval -set \"$warm_restart_steps\"") !=
        std::string::npos);
    assert(run_parallel.find(
        "foamDictionary -precision 17 \"$case_dir/system/controlDict\" "
        "-entry startTime -set \"$reconstruct_time\"") !=
        std::string::npos);
    assert(run_parallel.find(
        "warm_restart_plan=$(awk -v maximum=\"$warm_start_maximum_time_step\"") !=
        std::string::npos);
    assert(run_parallel.find(
        "-entry deltaT -set \"$warm_restart_dt\"") !=
        std::string::npos);
    assert(run_parallel.find(
        "foamDictionary -precision 17 \"$case_dir/system/controlDict\" "
        "-entry adjustTimeStep -set false") != std::string::npos);
    assert(run_parallel.find(
        "require_exact_endpoint \"$warm_actual\" \"$warm_window_target\"") !=
        std::string::npos);
    assert(run_parallel.find(
        "summary \"warm_start_window index=$warm_window_index") !=
        std::string::npos);
    const auto warm_windows_function=run_parallel.find(
        "run_warm_start_windows()");
    const auto warm_windows_call=run_parallel.find(
        "    run_warm_start_windows\n",warm_windows_function);
    const auto warm_checkpoint_preflight=run_parallel.find(
        "        preflight_checkpoint_space || exit $?",warm_start_position);
    const auto warm_acceptance_invalidation=run_parallel.find(
        "        invalidate_airflow_acceptance_state\n",
        warm_checkpoint_preflight);
    const auto warm_fan_ramp=run_parallel.find(
        "        run_fan_ramp ",warm_acceptance_invalidation);
    assert(warm_windows_function != std::string::npos);
    assert(warm_windows_call>warm_windows_function);
    assert(warm_checkpoint_preflight != std::string::npos);
    assert(warm_acceptance_invalidation>warm_checkpoint_preflight);
    assert(warm_fan_ramp>warm_acceptance_invalidation);
    assert(warm_windows_call>warm_acceptance_invalidation);
    const auto restart_courant_helper=run_parallel.find(
        "validate_latest_airflow_courant()");
    const auto restart_courant_resume=run_parallel.find(
        "validate_latest_airflow_courant \"$warm_current\"",
        warm_acceptance_invalidation);
    assert(restart_courant_helper != std::string::npos);
    assert(restart_courant_resume>warm_acceptance_invalidation);
    assert(restart_courant_resume<warm_fan_ramp);
    const auto fan_ramp_restart_helper=run_parallel.find(
        "validate_interrupted_fan_ramp_checkpoint()");
    const auto multirate_fan_ramp_restart=run_parallel.find(
        "validate_interrupted_fan_ramp_checkpoint \"$current\"");
    const auto warm_fan_ramp_restart=run_parallel.find(
        "validate_interrupted_fan_ramp_checkpoint \"$warm_current\"",
        warm_acceptance_invalidation);
    assert(fan_ramp_restart_helper != std::string::npos);
    assert(multirate_fan_ramp_restart != std::string::npos);
    assert(warm_fan_ramp_restart>warm_acceptance_invalidation);
    assert(warm_fan_ramp_restart<restart_courant_resume);
    assert(run_parallel.find(
        "checkpoint before Courant validation",restart_courant_helper) !=
        std::string::npos);
    assert(run_parallel.find(
        "checkpoint after Courant validation",restart_courant_helper) !=
        std::string::npos);
    const auto restored_write_control_position = run_parallel.find(
        production_write_control,warm_interval_position);
    assert(restored_write_control_position != std::string::npos);
    assert(run_parallel.find(
        production_write_control,restored_write_control_position + 1) !=
        std::string::npos);
    const auto restore_initial_full_sources=run_parallel.find(
        "restore_full_fan_options\n"
        "                    echo \"Restored full fluid heat sources");
    const auto publish_initial_convergence=run_parallel.find(
        "touch \"$initial_convergence_marker\"",restore_initial_full_sources);
    assert(restore_initial_full_sources != std::string::npos);
    assert(publish_initial_convergence > restore_initial_full_sources);
    assert(run_parallel.find(
        "[[ ! -f \"$initial_convergence_marker\" ]]") !=
        std::string::npos);
    const auto restore_production_controls=run_parallel.find(
        "restore_production_solver_controls()");
    const auto restore_run_state=run_parallel.find("restore_run_state()");
    const auto tracked_runner=run_parallel.find("run_tracked()");
    const auto tracked_capture=run_parallel.find(
        "run_tracked_capture()",tracked_runner);
    const auto terminate_run=run_parallel.find("terminate_run()");
    const auto bootstrap_exit_trap=run_parallel.find(
        "trap cleanup_script_snapshot EXIT",terminate_run);
    const auto interrupt_trap=run_parallel.find(
        "trap 'terminate_run INT' INT",bootstrap_exit_trap);
    const auto terminate_trap=run_parallel.find(
        "trap 'terminate_run TERM' TERM",interrupt_trap);
    const auto exit_trap=run_parallel.find(
        "trap restore_run_state EXIT",restore_run_state);
    assert(tracked_runner != std::string::npos);
    assert(tracked_capture>tracked_runner);
    assert(terminate_run>tracked_capture);
    const std::string tracked_body=run_parallel.substr(
        tracked_runner,tracked_capture-tracked_runner);
    const auto deferred_int=tracked_body.find(
        "trap 'pending_termination_signal=INT' INT");
    const auto deferred_term=tracked_body.find(
        "trap 'pending_termination_signal=TERM' TERM",deferred_int);
    const auto setsid_launch=tracked_body.find("setsid -- \"$@\" &");
    const auto job_control_fallback=tracked_body.find("set -m",setsid_launch);
    const auto job_control_reset=tracked_body.find(
        "set +m",job_control_fallback);
    const auto child_registration=tracked_body.find(
        "active_child_pid=$!",job_control_reset);
    const auto restored_int=tracked_body.find(
        "trap 'terminate_run INT' INT",child_registration);
    const auto restored_term=tracked_body.find(
        "trap 'terminate_run TERM' TERM",restored_int);
    const auto pending_delivery=tracked_body.find(
        "terminate_run \"$pending_termination_signal\"",restored_term);
    assert(deferred_int != std::string::npos);
    assert(deferred_term>deferred_int);
    assert(setsid_launch>deferred_term);
    assert(job_control_fallback>setsid_launch);
    assert(job_control_reset>job_control_fallback);
    assert(child_registration>job_control_reset);
    assert(restored_int>child_registration);
    assert(restored_term>restored_int);
    assert(pending_delivery>restored_term);
    assert(bootstrap_exit_trap>terminate_run);
    assert(interrupt_trap>bootstrap_exit_trap);
    assert(terminate_trap>interrupt_trap);
    const auto script_snapshot_gate=run_parallel.find(
        "if [[ \"${THERMAL_SOLVER_SCRIPT_SNAPSHOT:-0}\" != 1 ]]",
        terminate_trap);
    const auto tracked_preparation=run_parallel.find(
        "run_tracked bash \"$case_dir/prepare_regions_low_memory.sh\"",
        script_snapshot_gate);
    assert(script_snapshot_gate>terminate_trap);
    assert(tracked_preparation>script_snapshot_gate);
    assert(restore_production_controls != std::string::npos);
    assert(restore_run_state>restore_production_controls);
    assert(exit_trap>restore_run_state);
    const std::string production_restore_body=run_parallel.substr(
        restore_production_controls,
        restore_run_state-restore_production_controls);
    for(const char* expected : {
            "restore_live_outer_correctors || true",
            "PIMPLE/frozenFlow -set false",
            "PIMPLE/semiFrozenFlow -set false",
            "PIMPLE/thermalOnlyFlow -set false",
            "PIMPLE/momentumPredictor -set true",
            "startFrom -set latestTime",
            "stopAt -set endTime",
            "adjustTimeStep -set true",
            "-entry deltaT -set ",
            "writeControl -set adjustableRunTime"})
        assert(production_restore_body.find(expected) != std::string::npos);
    const std::string restore_state_body=run_parallel.substr(
        restore_run_state,exit_trap-restore_run_state);
    assert(restore_state_body.find("restore_full_fan_options || true") !=
           std::string::npos);
    assert(restore_state_body.find(
        "restore_production_solver_controls || true") != std::string::npos);
    assert(restore_state_body.find("cleanup_script_snapshot || true") !=
           std::string::npos);
    const std::string terminate_body=run_parallel.substr(
        terminate_run,bootstrap_exit_trap-terminate_run);
    assert(terminate_body.find("trap - EXIT") != std::string::npos);
    assert(terminate_body.find("trap '' INT TERM") != std::string::npos);
    assert(terminate_body.find("declare -F restore_run_state") !=
           std::string::npos);
    assert(terminate_body.find("cleanup_script_snapshot") !=
           std::string::npos);
    assert(terminate_body.find(
        "kill -s \"$signal\" -- \"-$child_pid\"") !=
           std::string::npos);
    assert(terminate_body.find(
        "kill -s \"$signal\" -- \"$child_pid\"") !=
           std::string::npos);
    const auto first_watchdog=terminate_body.find(
        "for ((attempt=0; attempt<50; ++attempt))");
    const auto watchdog_term=terminate_body.find(
        "kill -s TERM -- \"-$child_pid\"",first_watchdog);
    const auto second_watchdog=terminate_body.find(
        "for ((attempt=0; attempt<20; ++attempt))",watchdog_term);
    const auto watchdog_kill=terminate_body.find(
        "kill -s KILL -- \"-$child_pid\"",second_watchdog);
    const auto watchdog_wait=terminate_body.find(
        "wait \"$watchdog_pid\"",watchdog_kill);
    assert(first_watchdog != std::string::npos);
    assert(watchdog_term>first_watchdog);
    assert(second_watchdog>watchdog_term);
    assert(watchdog_kill>second_watchdog);
    assert(watchdog_wait>watchdog_kill);
    assert(terminate_body.find(
        "declare -F restore_preparation_controls") != std::string::npos);
    assert(terminate_body.find("exit \"$status\"") != std::string::npos);
    assert(run_parallel.find("trap restore_run_state EXIT INT TERM") ==
           std::string::npos);
    assert(run_parallel.find(
        "run_tracked \"$foam_launcher\" decomposePar") !=
           std::string::npos);
    assert(run_parallel.find(
        "run_tracked \"$foam_launcher\" reconstructPar") !=
           std::string::npos);
    assert(run_parallel.find(
        "run_tracked \"$foam_launcher\" mpirun -np \"$processes\"") !=
           std::string::npos);
    assert(run_parallel.find(
        "run_tracked_capture postflight_output") != std::string::npos);
    assert(run_parallel.find(
        "\"$processor_dir/constant/fluid/fvOptions\"") !=
        std::string::npos);
    assert(run_parallel.find("gsub(/[()\\r]/") != std::string::npos);
    assert(run_parallel.find("Fan ramp scaling verification failed") !=
        std::string::npos);
    const std::size_t prune_function=
        run_parallel.find("prune_processor_times()");
    const std::size_t ramp_prune_call=run_parallel.find(
        "        prune_processor_times\n",
        run_parallel.find("Fan ramp stage"));
    const std::size_t stage_flow_copy=
        run_parallel.find("cp -p \"$source_field\"");
    const std::size_t stage_prune_call=run_parallel.find(
        "        prune_processor_times\n",stage_flow_copy);
    assert(prune_function!=std::string::npos);
    assert(prune_function<run_parallel.find("run_fan_ramp()"));
    assert(run_parallel.find("local keep=\"3\"",prune_function)!=
        std::string::npos);
    assert(run_parallel.find("$0 != \"0\"",prune_function)!=
        std::string::npos);
    assert(run_parallel.find(
        "rm -rf -- \"$target\"",prune_function)!=std::string::npos);
    assert(ramp_prune_call!=std::string::npos);
    assert(stage_flow_copy!=std::string::npos);
    assert(stage_prune_call!=std::string::npos);
    assert(stage_prune_call>stage_flow_copy);
    assert(run_parallel.find(
        "x=duration*i/n; printf \"%.17g\", (x<limit?x:limit)") !=
        std::string::npos);
    assert(run_parallel.find("then continue; fi") != std::string::npos);
    assert(run_parallel.find(
        "\"$saved_time_file\" -entry index -set 0") !=
        std::string::npos);
    assert(run_parallel.find("x=target/duration") != std::string::npos);
    assert(run_parallel.find("-latestTime -withZero") ==
           std::string::npos);

    Rack planner_rack=Rack::from_meters(0.4,0.4,0.4,"planner test");
    Component nearly_aligned=
        Component::from_meters(0.1,0.1,0.1,"nearly aligned");
    nearly_aligned.set_coords_m(0.1000196,0.1,0.1);
    const MeshRefinementPlan refinement=MeshRefinementPlanner::plan(
        planner_rack,{nearly_aligned},{},{},0.02,0.10,0.02);
    const auto minimum_width=[](const std::vector<double>& widths) {
        return *std::min_element(widths.begin(),widths.end());
    };
    assert(minimum_width(refinement.dxs)>=0.005-1e-12);
    assert(minimum_width(refinement.dys)>=0.005-1e-12);
    assert(minimum_width(refinement.dzs)>=0.005-1e-12);
    const auto maximum_adjacent_ratio=[](
        const std::vector<double>& widths) {
        double ratio=1.0;
        for(std::size_t i=1;i<widths.size();++i)
            ratio=std::max(
                ratio,std::max(widths[i-1],widths[i])/
                    std::min(widths[i-1],widths[i]));
        return ratio;
    };
    assert(maximum_adjacent_ratio(refinement.dxs)<=4.0+1e-12);
    assert(maximum_adjacent_ratio(refinement.dys)<=4.0+1e-12);
    assert(maximum_adjacent_ratio(refinement.dzs)<=4.0+1e-12);

    // Full-face openings have no interior tangential edges. They must retain
    // fine cells near their normal boundary without forcing every x/z cell
    // to fine_dx merely because the footprint spans the complete rack face.
    Fan full_face_inlet(
        "full face",15.0,0.0,{0.4,0.0,0.4},{0.2,0.0,0.2},
        {0.0,1.0,0.0},FlowType::Intake,ShapeType::Rectangular);
    const MeshRefinementPlan component_only=MeshRefinementPlanner::plan(
        planner_rack,{nearly_aligned},{},{},0.02,0.10,0.02);
    const MeshRefinementPlan full_face=MeshRefinementPlanner::plan(
        planner_rack,{nearly_aligned},{full_face_inlet},{},
        0.02,0.10,0.02);
    assert(full_face.dxs==component_only.dxs);
    assert(full_face.dzs==component_only.dzs);
    assert(full_face.dys.size()>=component_only.dys.size());

    Fan partial_face_inlet(
        "partial face",15.0,0.0,{0.2,0.0,0.2},{0.2,0.0,0.2},
        {0.0,1.0,0.0},FlowType::Intake,ShapeType::Rectangular);
    const MeshRefinementPlan partial_face=MeshRefinementPlanner::plan(
        planner_rack,{nearly_aligned},{partial_face_inlet},{},
        0.02,0.10,0.02);
    assert(partial_face.dxs.size()>component_only.dxs.size());
    assert(partial_face.dzs.size()>component_only.dzs.size());

    // Refinement-band edges must never displace required component or
    // internal-region boundaries. Otherwise changing only margin/coarse_dx
    // changes the represented solid and air volumes.
    Component cut_component=
        Component::from_meters(0.20,0.20,0.20,"cut priority");
    cut_component.set_coords_m(0.15,0.15,0.15);
    cut_component.add_region(InternalRegion(
        "interior air",{0.17,0.17,0.17},{0.015,0.015,0.015}));
    // This lower-priority feature plane is only 3 mm from the 150 mm
    // component face in a 20 mm mesh. Sliver suppression must retain the
    // material boundary, not whichever coordinate sorts first.
    cut_component.add_region(InternalRegion(
        "near-wall feature",{0.05,0.05,0.05},{0.003,0.003,0.003}));
    cut_component.add_region(InternalRegion(
        "minimum-resolved feature",{0.05,0.05,0.05},{0.005,0.005,0.005}));
    const MeshRefinementPlan narrow_band=MeshRefinementPlanner::plan(
        planner_rack,{cut_component},{},{},0.02,0.20,0.005);
    const MeshRefinementPlan wide_band=MeshRefinementPlanner::plan(
        planner_rack,{cut_component},{},{},0.02,0.10,0.02);
    const auto has_boundary=[](
        const std::vector<double>& widths,double target) {
        double coordinate=0.0;
        for(double width : widths) {
            coordinate+=width;
            if(std::abs(coordinate-target)<1e-12) return true;
        }
        return target==0.0;
    };
    for(const auto* plan : {&narrow_band,&wide_band}) {
        for(const auto* widths : {&plan->dxs,&plan->dys,&plan->dzs}) {
            assert(has_boundary(*widths,0.15));
            assert(!has_boundary(*widths,0.153));
            assert(has_boundary(*widths,0.155));
            assert(has_boundary(*widths,0.165));
            assert(has_boundary(*widths,0.335));
            assert(has_boundary(*widths,0.35));
        }
    }

    Component thin_layer_component=
        Component::from_meters(0.20,0.20,0.20,"thin layers");
    thin_layer_component.set_coords_m(0.10,0.10,0.10);
    thin_layer_component.add_region(InternalRegion(
        "air",{0.19,0.19,0.19},{0.005,0.005,0.005}));
    thin_layer_component.add_region(InternalRegion(
        "thin heat block",{0.02,0.02,0.02},{0.09,0.09,0.09},
        800.0,1200.0,10.0,100.0));
    const MeshRefinementPlan thin_layer_plan=MeshRefinementPlanner::plan(
        planner_rack,{thin_layer_component},{},{},0.02,0.10,0.02);
    for(const auto* widths : {
            &thin_layer_plan.dxs,&thin_layer_plan.dys,
            &thin_layer_plan.dzs}) {
        // Two cells through the 5 mm chassis and the 20 mm heat block.
        assert(has_boundary(*widths,0.1025));
        assert(has_boundary(*widths,0.20));
        assert(has_boundary(*widths,0.2975));
    }

    // Cumulative width sums are not bit-identical to the source geometry
    // coordinates. Both profiles must still stamp exactly the same component
    // volume when its faces coincide with planned mesh boundaries.
    Component volume_component=
        Component::from_meters(0.20,0.20,0.20,"volume invariant");
    volume_component.set_coords_m(0.15,0.15,0.15);
    Environment mesh_environment(
        30.0,5800.0,20.0,1005.0,0.02587,0.000018,0.71,1.225);
    Workload mesh_workload(100000,10000000,1000000,100);
    const auto stamped_solid_volume=[&](const MeshRefinementPlan& plan) {
        Mesh mesh=Mesh().build_adaptive_mesh(
            planner_rack,plan.dxs,plan.dys,plan.dzs,
            mesh_environment,mesh_workload);
        mesh.stamp_component_adaptive(volume_component);
        double volume=0.0;
        for(const Cell& cell : mesh.get_cells())
            if(cell.is_solid()) volume+=cell.volume();
        return volume;
    };
    const double narrow_volume=stamped_solid_volume(narrow_band);
    const double wide_volume=stamped_solid_volume(wide_band);
    assert(std::abs(narrow_volume-0.008)<1e-12);
    assert(std::abs(wide_volume-0.008)<1e-12);
    assert(std::abs(narrow_volume-wide_volume)<1e-12);

    std::cout << "model_config_test PASSED\n";
}
