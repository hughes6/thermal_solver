#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

#include "../src/solver.hpp"

static Mesh make_mesh(
    std::size_t max_updates = 1000000,
    std::size_t max_timesteps = 10000) {
    Environment env(30.0,0.0,20.0,1005.0,0.02587,
                    0.000018,0.71,1.225);
    Workload load(max_timesteps,max_updates,100000,100);
    Rack rack=Rack::from_meters(0.3,0.1,0.1);
    rack.set_t(20.0); rack.set_cp(1005.0);
    rack.set_k(0.02587); rack.set_rho(1.225);
    Mesh mesh=Mesh().build_mesh(rack,0.1,0.1,0.1,env,load);
    mesh.at(0,0,0).set_T(40.0);
    for(int x=0; x<3; ++x) {
        mesh.at(x,0,0).set_vx(1.0);
        // Isolate the advection comparison from fluid conduction.
        mesh.at(x,0,0).set_k(1e-12);
    }
    return mesh;
}

int main() {
    const std::filesystem::path temporary_directory =
        std::filesystem::temp_directory_path() /
        ("thermal_solver_advection_workload_" +
         std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch().count()));
    std::filesystem::create_directories(temporary_directory);

    Mesh legacy_mesh=make_mesh();
    Solver legacy_default(
        legacy_mesh,0.01,0.01,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,false,0.8,10000,
        (temporary_directory / "legacy_default.csv").string());
    Solver legacy_explicit(
        legacy_mesh,0.01,0.01,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,false,0.8,10000,
        (temporary_directory / "legacy_explicit.csv").string());
    legacy_default.solve();
    legacy_explicit.solve();
    for(int x=0; x<3; ++x)
        assert(legacy_default.get_mesh().at(x,0,0).get_T() ==
               legacy_explicit.get_mesh().at(x,0,0).get_T());

    Mesh reference_mesh=make_mesh();
    Mesh subcycled_mesh=make_mesh();
    Solver reference(
        reference_mesh,0.05,0.20,false,4,-1,
        4.5,1e-3,10,1.3,2,1e-2,false,0.8,10000,
        (temporary_directory / "reference.csv").string());
    Solver subcycled(
        subcycled_mesh,0.20,0.20,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,true,0.5,100,
        (temporary_directory / "subcycled.csv").string());
    reference.solve();
    subcycled.solve();
    for(int x=0; x<3; ++x)
        assert(std::abs(reference.get_mesh().at(x,0,0).get_T()-
                        subcycled.get_mesh().at(x,0,0).get_T()) < 1e-9);

    bool invalid_target_threw=false;
    try {
        Solver invalid(
            make_mesh(),0.1,0.1,false,1,-1,
            4.5,1e-3,10,1.3,2,1e-2,true,1.1,100,
            (temporary_directory / "invalid.csv").string());
        (void)invalid;
    } catch(const std::invalid_argument&) {
        invalid_target_threw=true;
    }
    assert(invalid_target_threw);

    bool nan_target_threw=false;
    try {
        Solver invalid(
            make_mesh(),0.1,0.1,false,1,-1,
            4.5,1e-3,10,1.3,2,1e-2,true,
            std::numeric_limits<double>::quiet_NaN(),100,
            (temporary_directory / "nan_target.csv").string());
        (void)invalid;
    } catch(const std::invalid_argument&) {
        nan_target_threw=true;
    }
    assert(nan_target_threw);

    bool substep_limit_threw=false;
    try {
        Solver limited(
            make_mesh(),0.2,0.2,false,1,-1,
            4.5,1e-3,10,1.3,2,1e-2,true,0.5,1,
            (temporary_directory / "substep_limited.csv").string());
        limited.solve();
    } catch(const std::runtime_error&) {
        substep_limit_threw=true;
    }
    assert(substep_limit_threw);

    // The old guard counted only three cells x one global step and therefore
    // allowed this run. The real loop performs one non-advection pass and four
    // advection passes: 3 * (1 + 4) = 15 cell visits. Refuse it before the
    // first thermal update when the configured budget is 14.
    bool inclusive_workload_threw=false;
    try {
        Solver limited_workload(
            make_mesh(14),0.2,0.2,false,1,-1,
            4.5,1e-3,10,1.3,2,1e-2,true,0.5,100,
            (temporary_directory / "limited.csv").string());
        limited_workload.solve();
    } catch(const std::runtime_error& error) {
        const std::string message=error.what();
        inclusive_workload_threw =
            message.find(
                "projected advection-inclusive cell visits (15)") !=
                std::string::npos &&
            message.find("max_updates (14)") != std::string::npos;
    }
    assert(inclusive_workload_threw);

    // The limit is inclusive: the exact 15-visit budget completes.
    Solver exact_workload(
        make_mesh(15),0.2,0.2,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,true,0.5,100,
        (temporary_directory / "exact.csv").string());
    exact_workload.solve();

    // Timestep counts that cannot be represented by solve() fail closed
    // instead of narrowing to int and making an enormous run appear cheap.
    bool timestep_range_threw=false;
    try {
        const std::size_t maximum =
            std::numeric_limits<std::size_t>::max();
        Solver overflow(
            make_mesh(maximum,maximum),1.0,
            static_cast<double>(maximum/2),false,1,-1,
            4.5,1e-3,10,1.3,2,1e-2,false,0.8,100,
            (temporary_directory / "overflow.csv").string());
        (void)overflow;
    } catch(const std::invalid_argument& error) {
        timestep_range_threw=std::string(error.what()).find(
            "timestep count exceeds the implementation limit") !=
            std::string::npos;
    }
    assert(timestep_range_threw);

    bool partial_step_threw=false;
    try {
        Solver partial(
            make_mesh(),0.1,0.15,false,1,-1,
            4.5,1e-3,10,1.3,2,1e-2,false,0.8,100,
            (temporary_directory / "partial.csv").string());
        (void)partial;
    } catch(const std::invalid_argument& error) {
        partial_step_threw=std::string(error.what()).find(
            "integer multiple of dt") != std::string::npos;
    }
    assert(partial_step_threw);

    bool zero_flow_interval_threw=false;
    try {
        Solver invalid_interval(
            make_mesh(),0.1,0.1,false,1,0,
            4.5,1e-3,10,1.3,2,1e-2,false,0.8,100,
            (temporary_directory / "invalid_interval.csv").string());
        (void)invalid_interval;
    } catch(const std::invalid_argument& error) {
        zero_flow_interval_threw=std::string(error.what()).find(
            "update_flow_interval must be -1 or >= 1") !=
            std::string::npos;
    }
    assert(zero_flow_interval_threw);

    // A completed multistage field can be moved out without a third Cell
    // payload. The transfer is deliberately terminal and cannot happen before
    // a successful solve or more than once.
    Mesh release_source=make_mesh();
    Solver release_solver(
        std::move(release_source),0.01,0.01,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,false,0.8,100,
        (temporary_directory / "released.csv").string());
    bool premature_release_threw=false;
    try {
        (void)release_solver.release_completed_mesh();
    } catch(const std::logic_error&) {
        premature_release_threw=true;
    }
    assert(premature_release_threw);
    release_solver.solve();
    assert(release_solver.completed_native_cell_visits()==3u);
    Mesh released=release_solver.release_completed_mesh();
    assert(released.get_cell_count()==3u);
    const auto require_released_guard=[&](auto&& operation) {
        bool threw=false;
        try {
            operation();
        } catch(const std::logic_error&) {
            threw=true;
        }
        assert(threw);
    };
    require_released_guard([&] { (void)release_solver.get_mesh(); });
    require_released_guard([&] {
        release_solver.validate_computational_workload();
    });
    require_released_guard([&] {
        (void)release_solver.get_total_cell_updates();
    });
    require_released_guard([&] {
        release_solver.apply_bulk_velocity(1.0,0.0,0.0);
    });
    require_released_guard([&] {
        release_solver.check_advection_stability();
    });
    require_released_guard([&] {
        release_solver.check_advection_stability_adaptive();
    });
    require_released_guard([&] {
        release_solver.check_conduction_stability();
    });
    require_released_guard([&] {
        release_solver.check_conduction_stability_adaptive();
    });
    require_released_guard([&] {
        release_solver.check_convection_stability();
    });
    require_released_guard([&] { release_solver.initialize_flow(); });
    require_released_guard([&] {
        (void)release_solver.has_face_flux_solution();
    });
    require_released_guard([&] {
        (void)release_solver.relative_flow_mass_imbalance();
    });
    require_released_guard([&] {
        (void)release_solver.x_face_flux_m3s(0,0,0);
    });
    require_released_guard([&] {
        (void)release_solver.y_face_flux_m3s(0,0,0);
    });
    require_released_guard([&] {
        (void)release_solver.z_face_flux_m3s(0,0,0);
    });
    require_released_guard([&] {
        (void)release_solver.ambient_flow_into_cell_m3s(0,0,0);
    });
    require_released_guard([&] {
        (void)release_solver.maximum_flow_continuity_residual_m3s();
    });
    require_released_guard([&] {
        (void)release_solver.planned_advection_substeps_for_current_flow();
    });
    SimulationLogger released_logger(LoggingConfig{});
    require_released_guard([&] {
        release_solver.set_logger(released_logger);
    });
    bool repeated_release_threw=false;
    try {
        (void)release_solver.release_completed_mesh();
    } catch(const std::logic_error&) {
        repeated_release_threw=true;
    }
    assert(repeated_release_threw);

    std::error_code cleanup_error;
    std::filesystem::remove_all(temporary_directory,cleanup_error);

    std::cout << "advection_subcycling_test PASSED\n";
}
