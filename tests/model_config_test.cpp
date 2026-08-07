#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../src/grapher.hpp"
#include "../src/input/model_loader.hpp"

int main() {
    const std::filesystem::path test_root=
        std::filesystem::temp_directory_path()/
        "thermal_sim_model_config_test";
    std::filesystem::remove_all(test_root);
    std::filesystem::create_directories(test_root);
    auto read_file=[](const std::filesystem::path& path) {
        std::ifstream input(path);
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    };

    ModelLoader loader;
    loader.load_fan_curves("library/fan_curves/fan_curves.toml");
    loader.load_model("library/models/model.toml");

    const ModelInput& model = loader.model;
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
        "curve = \"generic_80mm_low_speed\"")==1);
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

    ModelLoader legacy_loader;
    legacy_loader.load_model("library/tests/valid_model.toml");
    assert(!legacy_loader.model.openfoam_solver.enabled);
    assert(!legacy_loader.model.openfoam_solver.template_file.has_value());

    ModelLoader homogeneous_loader;
    homogeneous_loader.load_model(
        "library/tests/minimal_geometry_repro.toml");
    assert(homogeneous_loader.model.components.size() == 1);
    assert(homogeneous_loader.model.components[0].internal_regions.empty());

    ModelLoader foam_loader;
    foam_loader.load_model("library/tests/openfoam_model.toml");
    assert(foam_loader.model.openfoam_solver.case_directory ==
           std::filesystem::path("openfoam_cases/openfoam_model"));
    foam_loader.model.openfoam_solver.case_directory=
        test_root/"toml_export";
#ifdef _WIN32
    {
        const std::string resolved_test_case=std::filesystem::absolute(
            foam_loader.model.openfoam_solver.case_directory).string();
        if(std::any_of(
               resolved_test_case.begin(),resolved_test_case.end(),
               [](const unsigned char character) {
                   return std::isspace(character)!=0;
               }))
            foam_loader.model.openfoam_solver.case_directory=
                std::filesystem::temp_directory_path()/
                "thermal_sim_openfoam_test";
    }
#endif
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
        screening.openfoam_solver.thermal_only_maximum_time_step == 10.0);
    assert(screening.openfoam_solver.airflow_refresh_duration == 0.02);
    assert(screening.openfoam_solver.airflow_maximum_time_step == 0.001);
    assert(
        screening.openfoam_solver.maximum_device_flow_change_fraction == 0.01);
    assert(
        screening.openfoam_solver.minimum_tracked_boundary_flow_fraction ==
        0.0001);
    // Inline test-model values remain authoritative over profile defaults.
    assert(screening.openfoam_solver.airflow_refresh_interval == 5.0);
    assert(screening.openfoam_solver.report_interval == 1.0);
    assert(screening.openfoam_solver.maximum_temperature_change == 0.25);

    const ModelInput indepth=load_fidelity_profile("indepth");
    assert(indepth.mesh.adaptive);
    assert(indepth.mesh.fine_dx == 0.02);
    assert(indepth.mesh.coarse_dx == 0.10);
    assert(indepth.mesh.refinement_margin == 0.02);
    assert(screening.mesh.fine_dx == indepth.mesh.fine_dx);
    assert(screening.mesh.coarse_dx == indepth.mesh.coarse_dx);
    assert(screening.mesh.refinement_margin == indepth.mesh.refinement_margin);
    assert(indepth.openfoam_solver.thermal_only_maximum_time_step == 5.0);
    assert(indepth.openfoam_solver.minimum_initial_airflow_duration == 0.30);
    assert(
        indepth.openfoam_solver.maximum_device_flow_change_fraction == 0.01);
    assert(
        indepth.openfoam_solver.minimum_tracked_boundary_flow_fraction ==
        0.0001);
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
    }
    std::filesystem::create_directories(case_directory/"0.25");
    std::filesystem::create_directories(case_directory/"processor0");
    std::filesystem::create_directories(case_directory/"postProcessing");
    std::ostringstream export_output;
    std::streambuf* original_output=std::cout.rdbuf(export_output.rdbuf());
    foam_loader.run();
    std::cout.rdbuf(original_output);
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
    const std::string run_serial=
        read_file(case_directory/"run_cht.sh");
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
    assert(run_serial.find("flock -n 9") != std::string::npos);
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
    assert(run_parallel.find("internal_fan_names") != std::string::npos);
    assert(run_parallel.find("boundary_flow_lookup") != std::string::npos);
    assert(run_parallel.find("boundary_flow_floor") != std::string::npos);
    assert(run_parallel.find(
        "floor>0 && aa<floor && bb<floor") != std::string::npos);
    assert(run_parallel.find("boundaryFlowFloor=") != std::string::npos);
    assert(run_parallel.find("estimatedAirExchangeTime=") !=
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
        "dt*0.8*limit/observed") != std::string::npos);
    assert(run_parallel.find(
        "safe=(observed>0?dt*0.8*limit/observed:hard)") !=
           std::string::npos);
    assert(run_parallel.find(
        "Courant postflight: actualMaxCo=") != std::string::npos);
    assert(run_parallel.find(
        "Live-flow Courant limit exceeded") != std::string::npos);
    assert(run_parallel.find(
        "scale=(co<10?co/10:1)") != std::string::npos);
    assert(run_parallel.find(
        "Startup has no established flow field") != std::string::npos);
    assert(run_parallel.find(
        "-entry writeInterval -set \"$ramp_steps\"") !=
           std::string::npos);
    assert(run_parallel.find(
        "deltaT=$ramp_dt, steps=$ramp_steps") != std::string::npos);
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
        "d<=1e-9*s") != std::string::npos);
    assert(run_parallel.find(
        "x=(int(a/d)+1)*d") != std::string::npos);
    assert(run_parallel.find(
        "if(x<=a+1e-9)x+=d") != std::string::npos);
    assert(run_parallel.find(
        "stage_max_dt=$(awk") != std::string::npos);
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
        "Solver stage failed to reach target time") != std::string::npos);
    assert(run_parallel.find(
        "tolerance=1e-6*scale") != std::string::npos);
    assert(run_parallel.find(
        "-entry startTime -set \"$current\"") != std::string::npos);
    assert(run_parallel.find(
        "foamDictionary -precision 16") != std::string::npos);
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
        "peakChange=$scaled_delta K/300s") != std::string::npos);
    assert(run_parallel.find(
        "Thermal convergence checkpoint $streak/2 accepted") !=
        std::string::npos);
    assert(run_parallel.find(
        "\"$airflow_validated\" == 1") != std::string::npos);
    assert(run_parallel.find(
        "Thermal and airflow convergence criteria satisfied") !=
        std::string::npos);
    assert(run_parallel.find(
        "Reusing $processes valid processor partitions") !=
        std::string::npos);
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
        "-entry writeInterval -set \"$warm_interval\"",
        warm_start_position);
    const auto warm_write_control_position = run_parallel.find(
        production_write_control,warm_start_position);
    assert(warm_start_position != std::string::npos);
    assert(warm_write_control_position != std::string::npos);
    assert(warm_interval_position != std::string::npos);
    assert(warm_write_control_position < warm_interval_position);
    const auto restored_write_control_position = run_parallel.find(
        production_write_control,warm_interval_position);
    assert(restored_write_control_position != std::string::npos);
    assert(run_parallel.find(
        production_write_control,restored_write_control_position + 1) !=
        std::string::npos);
    assert(run_parallel.find(
        "touch \"$initial_convergence_marker\"") != std::string::npos);
    assert(run_parallel.find(
        "[[ ! -f \"$initial_convergence_marker\" ]]") !=
        std::string::npos);
    assert(run_parallel.find(
        "trap restore_full_fan_options EXIT INT TERM") !=
        std::string::npos);
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
        "x=duration*i/n; print (x<limit?x:limit)") !=
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

    std::filesystem::remove_all(test_root);
    std::cout << "model_config_test PASSED\n";
}
