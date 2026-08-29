#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "../src/solver.hpp"

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

std::vector<std::string> split_csv_row(const std::string& row) {
    std::vector<std::string> fields;
    std::istringstream input(row);
    std::string field;
    while(std::getline(input, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

Mesh make_single_cell_mesh() {
    Environment environment(
        30.0, 0.0, 20.0, 1005.0, 0.02587, 0.000018, 0.71, 1.225);
    Workload workload(10, 100, 10, 16);
    Rack rack = Rack::from_meters(0.1, 0.1, 0.1);
    rack.set_t(20.0);
    rack.set_cp(1005.0);
    rack.set_k(0.02587);
    rack.set_rho(1.225);
    return Mesh().build_mesh(
        rack, 0.1, 0.1, 0.1, environment, workload);
}

} // namespace

int main() {
    const auto unique_suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryDirectory temporary{
        std::filesystem::temp_directory_path() /
        ("thermal_solver_logger_timestep_test_" + unique_suffix)};
    std::filesystem::create_directories(temporary.path);

    Mesh mesh = make_single_cell_mesh();
    mesh.at(0, 0, 0).set_qdot(1000.0);
    LoggingConfig logging;
    logging.output_directory = temporary.path / "structured";
    logging.enable_field_logging = false;
    logging.enable_summary_logging = true;
    logging.enable_probe_logging = false;
    logging.summary_interval = 1;
    logging.summary_requests = {{
        "temperature",
        LogVariable::Temperature,
        CellSelection::All,
        false,
        false,
        true,
        false,
        false}};

    SimulationLogger logger(logging);
    logger.initialize(mesh);
    assert(!std::filesystem::exists(logging.output_directory));

    constexpr double dt = 0.125;
    Solver solver(
        mesh,
        dt,
        2.0 * dt,
        false,
        2,
        -1,
        4.5,
        1e-3,
        10,
        1.3,
        2,
        1e-2,
        false,
        0.8,
        10000,
        (temporary.path / "legacy.csv").string());
    solver.set_logger(logger);
    solver.solve();
    logger.close();

    std::string row;
    std::ifstream legacy(temporary.path / "legacy.csv");
    assert(legacy.is_open());
    std::vector<int> legacy_steps;
    while(std::getline(legacy, row)) {
        const auto fields = split_csv_row(row);
        if(fields.size() == 15 && fields[0] != "step") {
            legacy_steps.push_back(std::stoi(fields[0]));
        }
    }
    assert((legacy_steps == std::vector<int>{0, 2}));

    std::ifstream summary(temporary.path / "structured" / "summary.csv");
    assert(summary.is_open());

    std::vector<std::string> rows;
    while(std::getline(summary, row)) {
        rows.push_back(row);
    }
    assert(rows.size() == 4);
    assert(rows[0] == "step,time,temperature_avg");

    const int expected_steps[] = {0, 1, 2};
    const double expected_times[] = {0.0, dt, 2.0 * dt};
    std::vector<double> logged_temperatures;
    for(std::size_t index = 0; index < 3; ++index) {
        const auto fields = split_csv_row(rows[index + 1]);
        assert(fields.size() == 3);
        assert(std::stoi(fields[0]) == expected_steps[index]);
        assert(std::abs(std::stod(fields[1]) - expected_times[index]) < 1e-15);
        assert(std::isfinite(std::stod(fields[2])));
        logged_temperatures.push_back(std::stod(fields[2]));
    }
    assert(std::abs(logged_temperatures[0] - 20.0) < 1e-12);
    assert(logged_temperatures[1] > logged_temperatures[0]);
    assert(logged_temperatures[2] > logged_temperatures[1]);

    // These explicit boundary assertions document the regression: the
    // initial label appears once and the completed two-step state is present.
    assert(std::stod(split_csv_row(rows[1])[1]) == 0.0);
    assert(std::stod(split_csv_row(rows[2])[1]) != 0.0);
    assert(std::stod(split_csv_row(rows[3])[1]) == 2.0 * dt);

    // Preparing structured logging and attaching it to a Solver must not
    // alter any prior output. Exact CFL/workload preflight intentionally
    // rejects this run before either the legacy or structured writers open.
    const std::filesystem::path refused_root=temporary.path/"refused";
    const std::filesystem::path refused_structured=refused_root/"structured";
    std::filesystem::create_directories(refused_structured);
    const std::filesystem::path refused_legacy=refused_root/"legacy.csv";
    const std::filesystem::path refused_field=refused_structured/"field.csv";
    const std::filesystem::path refused_summary=
        refused_structured/"summary.csv";
    const std::filesystem::path refused_probe=
        refused_structured/"probe_center.csv";
    const std::string legacy_sentinel="preserved legacy bytes\n";
    const std::string field_sentinel="preserved field bytes\n";
    const std::string summary_sentinel="preserved summary bytes\n";
    const std::string probe_sentinel="preserved probe bytes\n";
    std::ofstream(refused_legacy,std::ios::binary) << legacy_sentinel;
    std::ofstream(refused_field,std::ios::binary) << field_sentinel;
    std::ofstream(refused_summary,std::ios::binary) << summary_sentinel;
    std::ofstream(refused_probe,std::ios::binary) << probe_sentinel;

    Mesh refused_mesh=make_single_cell_mesh();
    LoggingConfig refused_logging;
    refused_logging.output_directory=refused_structured;
    refused_logging.enable_field_logging=true;
    refused_logging.enable_summary_logging=true;
    refused_logging.enable_probe_logging=true;
    refused_logging.field_interval=1;
    refused_logging.summary_interval=1;
    refused_logging.probe_interval=1;
    refused_logging.field_variables={LogVariable::Temperature};
    refused_logging.summary_requests={{
        "temperature",LogVariable::Temperature,CellSelection::All,
        false,false,true,false,false}};
    refused_logging.probes={{
        "center",0.05,0.05,0.05,{LogVariable::Temperature}}};
    SimulationLogger refused_logger(refused_logging);
    refused_logger.initialize(refused_mesh);
    assert(read_file(refused_legacy)==legacy_sentinel);
    assert(read_file(refused_field)==field_sentinel);
    assert(read_file(refused_summary)==summary_sentinel);
    assert(read_file(refused_probe)==probe_sentinel);

    Solver refused_solver(
        std::move(refused_mesh),0.1,0.1,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,true,0.5,1,
        refused_legacy.string());
    refused_solver.apply_bulk_velocity(100.0,0.0,0.0);
    refused_solver.set_logger(refused_logger);
    bool exact_preflight_rejected=false;
    try {
        refused_solver.solve();
    } catch(const std::runtime_error& error) {
        exact_preflight_rejected=std::string(error.what()).find(
            "required advection substeps")!=std::string::npos;
    }
    assert(exact_preflight_rejected);
    assert(read_file(refused_legacy)==legacy_sentinel);
    assert(read_file(refused_field)==field_sentinel);
    assert(read_file(refused_summary)==summary_sentinel);
    assert(read_file(refused_probe)==probe_sentinel);

    // Invalid probe configuration is rejected during initialize(), before
    // any legacy or structured output can be opened or truncated.
    const auto assert_probe_rejection_preserves_outputs = [
        &temporary
    ](const std::string& case_name,
      const std::vector<Probe>& probes,
      const std::string& expected_message) {
        assert(!probes.empty());
        const std::filesystem::path case_root=
            temporary.path/("probe_rejection_"+case_name);
        const std::filesystem::path structured=case_root/"structured";
        std::filesystem::create_directories(structured);
        const std::filesystem::path field=structured/"field.csv";
        const std::filesystem::path summary=structured/"summary.csv";
        const std::filesystem::path probe_file=
            structured/"probe_preserved.csv";
        const std::string field_bytes="preserved field "+case_name+"\n";
        const std::string summary_bytes="preserved summary "+case_name+"\n";
        const std::string probe_bytes="preserved probe "+case_name+"\n";
        std::ofstream(field,std::ios::binary) << field_bytes;
        std::ofstream(summary,std::ios::binary) << summary_bytes;
        std::ofstream(probe_file,std::ios::binary) << probe_bytes;

        LoggingConfig invalid;
        invalid.output_directory=structured;
        invalid.enable_field_logging=true;
        invalid.enable_summary_logging=true;
        invalid.enable_probe_logging=true;
        invalid.field_interval=1;
        invalid.summary_interval=1;
        invalid.probe_interval=1;
        invalid.field_variables={LogVariable::Temperature};
        invalid.summary_requests={{
            "temperature",LogVariable::Temperature,CellSelection::All,
            false,false,true,false,false}};
        invalid.probes=probes;

        Mesh probe_mesh=make_single_cell_mesh();
        SimulationLogger invalid_logger(invalid);
        bool rejected=false;
        try {
            invalid_logger.initialize(probe_mesh);
        } catch(const std::exception& error) {
            rejected=std::string(error.what()).find(expected_message) !=
                     std::string::npos;
        }
        assert(rejected);
        assert(!invalid_logger.is_initialized());
        assert(read_file(field)==field_bytes);
        assert(read_file(summary)==summary_bytes);
        assert(read_file(probe_file)==probe_bytes);
    };

    assert_probe_rejection_preserves_outputs(
        "duplicate",
        {{"duplicate",0.05,0.05,0.05,{LogVariable::Temperature}},
         {"duplicate",0.05,0.05,0.05,{LogVariable::Pressure}}},
        "Duplicate probe output filename");
    assert_probe_rejection_preserves_outputs(
        "case_collision",
        {{"Center",0.05,0.05,0.05,{LogVariable::Temperature}},
         {"center",0.05,0.05,0.05,{LogVariable::Pressure}}},
        "Duplicate probe output filename");
    assert_probe_rejection_preserves_outputs(
        "path_separator",
        {{"rack/center",0.05,0.05,0.05,{LogVariable::Temperature}}},
        "portable filename-safe characters");
    assert_probe_rejection_preserves_outputs(
        "windows_invalid_character",
        {{"rack:center",0.05,0.05,0.05,{LogVariable::Temperature}}},
        "portable filename-safe characters");

    const double not_a_number=std::numeric_limits<double>::quiet_NaN();
    assert_probe_rejection_preserves_outputs(
        "nan_x",
        {{"nan_x",not_a_number,0.05,0.05,{LogVariable::Temperature}}},
        "x coordinate must be finite");
    assert_probe_rejection_preserves_outputs(
        "nan_y",
        {{"nan_y",0.05,not_a_number,0.05,{LogVariable::Temperature}}},
        "y coordinate must be finite");
    assert_probe_rejection_preserves_outputs(
        "nan_z",
        {{"nan_z",0.05,0.05,not_a_number,{LogVariable::Temperature}}},
        "z coordinate must be finite");

    Mesh bounds_mesh=make_single_cell_mesh();
    const double x_upper=bounds_mesh.get_x_bounds().back();
    const double y_upper=bounds_mesh.get_y_bounds().back();
    const double z_upper=bounds_mesh.get_z_bounds().back();
    assert_probe_rejection_preserves_outputs(
        "above_upper",
        {{"above_upper",
          std::nextafter(x_upper,std::numeric_limits<double>::infinity()),
          0.05,0.05,{LogVariable::Temperature}}},
        "lies outside mesh bounds");

    // Exact upper faces remain valid by the Mesh coordinate convention and
    // resolve to the final cell, while even the next representable value above
    // an upper face is rejected by the preceding test.
    LoggingConfig upper_bound_logging;
    upper_bound_logging.output_directory=
        temporary.path/"upper_bound_structured";
    upper_bound_logging.enable_field_logging=false;
    upper_bound_logging.enable_summary_logging=false;
    upper_bound_logging.enable_probe_logging=true;
    upper_bound_logging.probe_interval=1;
    upper_bound_logging.probes={{
        "upper_bound",x_upper,y_upper,z_upper,{LogVariable::Temperature}}};
    SimulationLogger upper_bound_logger(upper_bound_logging);
    upper_bound_logger.initialize(bounds_mesh);
    assert(!std::filesystem::exists(upper_bound_logging.output_directory));
    upper_bound_logger.log(bounds_mesh,0,0.0);
    upper_bound_logger.close();
    std::ifstream upper_bound_file(
        upper_bound_logging.output_directory/"probe_upper_bound.csv");
    assert(upper_bound_file.is_open());
    std::string upper_header;
    std::string upper_row;
    assert(static_cast<bool>(std::getline(upper_bound_file,upper_header)));
    assert(static_cast<bool>(std::getline(upper_bound_file,upper_row)));
    const auto upper_fields=split_csv_row(upper_row);
    assert(upper_fields.size()==9);
    assert(std::stod(upper_fields[2])==x_upper);
    assert(std::stod(upper_fields[3])==y_upper);
    assert(std::stod(upper_fields[4])==z_upper);
    assert(std::stoi(upper_fields[5])==0);
    assert(std::stoi(upper_fields[6])==0);
    assert(std::stoi(upper_fields[7])==0);

    // Logger lifecycle mistakes are rejected before the legacy writer can
    // truncate a prior result.
    LoggingConfig lifecycle_logging;
    lifecycle_logging.output_directory=refused_root/"lifecycle_structured";
    lifecycle_logging.enable_field_logging=false;
    lifecycle_logging.enable_summary_logging=false;
    lifecycle_logging.enable_probe_logging=false;
    SimulationLogger uninitialized_logger(lifecycle_logging);
    const std::filesystem::path uninitialized_legacy=
        refused_root/"uninitialized_legacy.csv";
    const std::string uninitialized_sentinel="uninitialized sentinel\n";
    std::ofstream(uninitialized_legacy,std::ios::binary)
        << uninitialized_sentinel;
    Solver uninitialized_solver(
        make_single_cell_mesh(),0.1,0.1,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,false,0.8,100,
        uninitialized_legacy.string());
    bool uninitialized_attach_rejected=false;
    try {
        uninitialized_solver.set_logger(uninitialized_logger);
    } catch(const std::logic_error&) {
        uninitialized_attach_rejected=true;
    }
    assert(uninitialized_attach_rejected);
    assert(read_file(uninitialized_legacy)==uninitialized_sentinel);

    Mesh closed_mesh=make_single_cell_mesh();
    SimulationLogger closed_logger(lifecycle_logging);
    closed_logger.initialize(closed_mesh);
    const std::filesystem::path closed_legacy=
        refused_root/"closed_legacy.csv";
    const std::string closed_sentinel="closed sentinel\n";
    std::ofstream(closed_legacy,std::ios::binary) << closed_sentinel;
    Solver closed_solver(
        std::move(closed_mesh),0.1,0.1,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,false,0.8,100,
        closed_legacy.string());
    closed_solver.set_logger(closed_logger);
    closed_logger.close();
    bool closed_before_solve_rejected=false;
    try {
        closed_solver.solve();
    } catch(const std::logic_error& error) {
        closed_before_solve_rejected=std::string(error.what()).find(
            "no longer initialized")!=std::string::npos;
    }
    assert(closed_before_solve_rejected);
    assert(read_file(closed_legacy)==closed_sentinel);
}
