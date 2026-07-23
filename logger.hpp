#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mesh.hpp"
#include "cell.hpp"

// ============================================================
// LOGGABLE VARIABLES
// ============================================================

enum class LogVariable {
    Temperature,
    Pressure,

    VelocityX,
    VelocityY,
    VelocityZ,
    VelocityMagnitude,

    Density,
    SpecificHeat,
    Conductivity,
    HeatGeneration,

    ReynoldsNumber,
    ConvectionCoefficient
};

// ============================================================
// CELL SELECTIONS FOR SUMMARY STATISTICS
// ============================================================

enum class CellSelection {
    All,
    AirOnly,
    SolidOnly,
    FluidOnly,
    HeatGeneratingOnly
};

// ============================================================
// SUMMARY REQUEST
// ============================================================

struct SummaryRequest {
    std::string name;

    LogVariable variable;
    CellSelection selection = CellSelection::All;

    bool log_min = true;
    bool log_max = true;
    bool log_average = true;
    bool log_rms = false;
    bool log_standard_deviation = false;
};

// ============================================================
// RUNNING SUMMARY STATISTICS
// ============================================================

struct SummaryStatistics {
    std::size_t count = 0;

    double minimum =
        std::numeric_limits<double>::infinity();

    double maximum =
        -std::numeric_limits<double>::infinity();

    double sum = 0.0;
    double sum_squares = 0.0;

    void add(double value) {
        if(!std::isfinite(value)) {
            return;
        }

        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);

        sum += value;
        sum_squares += value * value;

        ++count;
    }

    double average() const {
        if(count == 0) {
            return 0.0;
        }

        return sum / static_cast<double>(count);
    }

    double rms() const {
        if(count == 0) {
            return 0.0;
        }

        return std::sqrt(
            sum_squares / static_cast<double>(count)
        );
    }

    double standard_deviation() const {
        if(count == 0) {
            return 0.0;
        }

        const double mean = average();

        const double variance =
            sum_squares / static_cast<double>(count)
            - mean * mean;

        return std::sqrt(
            std::max(0.0, variance)
        );
    }
};

// ============================================================
// PROBE CONFIGURATION
// ============================================================

struct Probe {
    std::string name;

    // Physical coordinates in meters
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    std::vector<LogVariable> variables;
};

struct ResolvedProbe {
    std::string name;

    double requested_x = 0.0;
    double requested_y = 0.0;
    double requested_z = 0.0;

    int i = 0;
    int j = 0;
    int k = 0;

    std::vector<LogVariable> variables;
};

// ============================================================
// LOGGER CONFIGURATION
// ============================================================

struct LoggingConfig {
    std::filesystem::path output_directory =
        "simulation_output";

    bool enable_field_logging = true;
    bool enable_summary_logging = true;
    bool enable_probe_logging = true;

    int field_interval = 100;
    int summary_interval = 10;
    int probe_interval = 1;

    // Logged once for every selected cell.
    std::vector<LogVariable> field_variables;

    // Reduced to one set of statistics per timestep.
    std::vector<SummaryRequest> summary_requests;

    // Logged at named physical locations.
    std::vector<Probe> probes;
};

// ============================================================
// SIMULATION LOGGER
// ============================================================

class SimulationLogger {
public:
    explicit SimulationLogger(LoggingConfig config)
        : config_(std::move(config)) {
    }

    ~SimulationLogger() {
        close();
    }

    SimulationLogger(const SimulationLogger&) = delete;

    SimulationLogger& operator=(
        const SimulationLogger&
    ) = delete;

    void initialize(const Mesh& mesh) {
        if(initialized_) {
            throw std::runtime_error(
                "SimulationLogger is already initialized."
            );
        }

        validate_config();

        std::filesystem::create_directories(
            config_.output_directory
        );

        resolved_probes_.clear();

        for(const Probe& probe : config_.probes) {
            resolved_probes_.push_back(
                resolve_probe(mesh, probe)
            );
        }

        open_files();
        initialized_ = true;
    }

    void log(
        const Mesh& mesh,
        int timestep,
        double time
    ) {
        if(!initialized_) {
            throw std::runtime_error(
                "SimulationLogger::initialize() must be "
                "called before log()."
            );
        }

        if(
            config_.enable_field_logging &&
            config_.field_interval > 0 &&
            timestep % config_.field_interval == 0
        ) {
            log_field(mesh, timestep, time);
        }

        if(
            config_.enable_summary_logging &&
            config_.summary_interval > 0 &&
            timestep % config_.summary_interval == 0
        ) {
            log_summary(mesh, timestep, time);
        }

        if(
            config_.enable_probe_logging &&
            config_.probe_interval > 0 &&
            timestep % config_.probe_interval == 0
        ) {
            log_probes(mesh, timestep, time);
        }
    }

    void close() {
        if(field_file_.is_open()) {
            field_file_.close();
        }

        if(summary_file_.is_open()) {
            summary_file_.close();
        }

        for(auto& entry : probe_files_) {
            if(entry.second.is_open()) {
                entry.second.close();
            }
        }

        probe_files_.clear();
        resolved_probes_.clear();

        initialized_ = false;
    }

    bool is_initialized() const {
        return initialized_;
    }

private:
    LoggingConfig config_;

    bool initialized_ = false;

    std::ofstream field_file_;
    std::ofstream summary_file_;

    std::vector<ResolvedProbe> resolved_probes_;

    std::unordered_map<
        std::string,
        std::ofstream
    > probe_files_;

    // ========================================================
    // CONFIGURATION VALIDATION
    // ========================================================

    void validate_config() const {
        if(
            config_.enable_field_logging &&
            config_.field_interval <= 0
        ) {
            throw std::invalid_argument(
                "Field logging interval must be greater than zero."
            );
        }

        if(
            config_.enable_summary_logging &&
            config_.summary_interval <= 0
        ) {
            throw std::invalid_argument(
                "Summary logging interval must be greater than zero."
            );
        }

        if(
            config_.enable_probe_logging &&
            config_.probe_interval <= 0
        ) {
            throw std::invalid_argument(
                "Probe logging interval must be greater than zero."
            );
        }

        for(const Probe& probe : config_.probes) {
            if(probe.name.empty()) {
                throw std::invalid_argument(
                    "Probe name cannot be empty."
                );
            }
        }
    }

    // ========================================================
    // VARIABLE NAMES
    // ========================================================

    static std::string variable_name(
        LogVariable variable
    ) {
        switch(variable) {
            case LogVariable::Temperature:
                return "T";

            case LogVariable::Pressure:
                return "pressure";

            case LogVariable::VelocityX:
                return "vx";

            case LogVariable::VelocityY:
                return "vy";

            case LogVariable::VelocityZ:
                return "vz";

            case LogVariable::VelocityMagnitude:
                return "velocity_magnitude";

            case LogVariable::Density:
                return "rho";

            case LogVariable::SpecificHeat:
                return "cp";

            case LogVariable::Conductivity:
                return "k";

            case LogVariable::HeatGeneration:
                return "qdot";

            case LogVariable::ReynoldsNumber:
                return "Re";

            case LogVariable::ConvectionCoefficient:
                return "h";
        }

        throw std::runtime_error(
            "Unknown LogVariable."
        );
    }

    // ========================================================
    // READ VARIABLE FROM CELL
    // ========================================================

    static double get_log_value(
        const Cell& cell,
        LogVariable variable
    ) {
        switch(variable) {
            case LogVariable::Temperature:
                return cell.get_T();

            case LogVariable::Pressure:
                return cell.get_pressure();

            case LogVariable::VelocityX:
                return cell.get_vx();

            case LogVariable::VelocityY:
                return cell.get_vy();

            case LogVariable::VelocityZ:
                return cell.get_vz();

            case LogVariable::VelocityMagnitude: {
                const double vx = cell.get_vx();
                const double vy = cell.get_vy();
                const double vz = cell.get_vz();

                return std::sqrt(
                    vx * vx +
                    vy * vy +
                    vz * vz
                );
            }

            case LogVariable::Density:
                return cell.get_rho();

            case LogVariable::SpecificHeat:
                return cell.get_cp();

            case LogVariable::Conductivity:
                return cell.get_k();

            case LogVariable::HeatGeneration:
                return cell.get_qdot();

            case LogVariable::ReynoldsNumber:
                return cell.get_reynolds_number();

            case LogVariable::ConvectionCoefficient:
                return cell.get_h();
        }

        throw std::runtime_error(
            "Unsupported logging variable."
        );
    }

    // ========================================================
    // CELL SELECTION
    // ========================================================

    static bool cell_matches_selection(
        const Cell& cell,
        CellSelection selection
    ) {
        switch(selection) {
            case CellSelection::All:
                return true;

            case CellSelection::AirOnly:
                return (
                    cell.get_state() ==
                    Cell::State::Air
                );

            case CellSelection::SolidOnly:
                return (
                    cell.get_state() ==
                    Cell::State::Component
                );

            case CellSelection::FluidOnly:
                return (
                    cell.get_state() ==
                        Cell::State::Air ||

                    cell.get_state() ==
                        Cell::State::Fan ||

                    cell.get_state() ==
                        Cell::State::Vent
                );

            case CellSelection::HeatGeneratingOnly:
                return cell.get_qdot() > 0.0;
        }

        return false;
    }

    // ========================================================
    // SUMMARY CALCULATION
    // ========================================================

    static SummaryStatistics compute_summary(
        const Mesh& mesh,
        const SummaryRequest& request
    ) {
        SummaryStatistics statistics;

        for(const Cell& cell : mesh.get_cells()) {
            if(!cell_matches_selection(
                cell,
                request.selection
            )) {
                continue;
            }

            statistics.add(
                get_log_value(
                    cell,
                    request.variable
                )
            );
        }

        return statistics;
    }

    // ========================================================
    // PROBE RESOLUTION
    // ========================================================

    static ResolvedProbe resolve_probe(
        const Mesh& mesh,
        const Probe& probe
    ) {
        const int i = static_cast<int>(
            std::floor(
                probe.x / mesh.get_dx()
            )
        );

        const int j = static_cast<int>(
            std::floor(
                probe.y / mesh.get_dy()
            )
        );

        const int k = static_cast<int>(
            std::floor(
                probe.z / mesh.get_dz()
            )
        );

        if(!mesh.in_bounds(i, j, k)) {
            throw std::out_of_range(
                "Probe '" + probe.name +
                "' lies outside the mesh."
            );
        }

        return {
            probe.name,

            probe.x,
            probe.y,
            probe.z,

            i,
            j,
            k,

            probe.variables
        };
    }

    // ========================================================
    // OPEN FILES
    // ========================================================

    void open_files() {
        if(config_.enable_field_logging) {
            const std::filesystem::path path =
                config_.output_directory /
                "field.csv";

            field_file_.open(path);

            if(!field_file_) {
                throw std::runtime_error(
                    "Could not open field log: " +
                    path.string()
                );
            }

            write_field_header();
        }

        if(config_.enable_summary_logging) {
            const std::filesystem::path path =
                config_.output_directory /
                "summary.csv";

            summary_file_.open(path);

            if(!summary_file_) {
                throw std::runtime_error(
                    "Could not open summary log: " +
                    path.string()
                );
            }

            write_summary_header();
        }

        if(config_.enable_probe_logging) {
            open_probe_files();
            write_probe_headers();
        }
    }

    void open_probe_files() {
        for(const ResolvedProbe& probe :
            resolved_probes_) {

            if(probe_files_.contains(probe.name)) {
                throw std::runtime_error(
                    "Duplicate probe name: " +
                    probe.name
                );
            }

            const std::filesystem::path path =
                config_.output_directory /
                ("probe_" + probe.name + ".csv");

            std::ofstream file(path);

            if(!file) {
                throw std::runtime_error(
                    "Could not open probe log: " +
                    path.string()
                );
            }

            probe_files_.emplace(
                probe.name,
                std::move(file)
            );
        }
    }

    // ========================================================
    // HEADERS
    // ========================================================

    void write_field_header() {
        field_file_
            << "step,time,i,j,k,x,y,z,state";

        for(LogVariable variable :
            config_.field_variables) {

            field_file_
                << ','
                << variable_name(variable);
        }

        field_file_ << '\n';
    }

    void write_summary_header() {
        summary_file_ << "step,time";

        for(const SummaryRequest& request :
            config_.summary_requests) {

            const std::string prefix =
                request.name.empty()
                    ? variable_name(request.variable)
                    : request.name;

            if(request.log_min) {
                summary_file_
                    << ',' << prefix << "_min";
            }

            if(request.log_max) {
                summary_file_
                    << ',' << prefix << "_max";
            }

            if(request.log_average) {
                summary_file_
                    << ',' << prefix << "_avg";
            }

            if(request.log_rms) {
                summary_file_
                    << ',' << prefix << "_rms";
            }

            if(request.log_standard_deviation) {
                summary_file_
                    << ','
                    << prefix
                    << "_stddev";
            }
        }

        summary_file_ << '\n';
    }

    void write_probe_headers() {
        for(const ResolvedProbe& probe :
            resolved_probes_) {

            std::ofstream& file =
                probe_files_.at(probe.name);

            file
                << "step,time"
                << ",requested_x"
                << ",requested_y"
                << ",requested_z"
                << ",i,j,k";

            for(LogVariable variable :
                probe.variables) {

                file
                    << ','
                    << variable_name(variable);
            }

            file << '\n';
        }
    }

    // ========================================================
    // FIELD LOGGING
    // ========================================================

    void log_field(
        const Mesh& mesh,
        int timestep,
        double time
    ) {
        for(int i = 0; i < mesh.get_nx(); ++i) {
            for(int j = 0; j < mesh.get_ny(); ++j) {
                for(int k = 0; k < mesh.get_nz(); ++k) {
                    const Cell& cell =
                        mesh.at(i, j, k);

                    const double x =
                        (
                            static_cast<double>(i)
                            + 0.5
                        ) * mesh.get_dx();

                    const double y =
                        (
                            static_cast<double>(j)
                            + 0.5
                        ) * mesh.get_dy();

                    const double z =
                        (
                            static_cast<double>(k)
                            + 0.5
                        ) * mesh.get_dz();

                    field_file_
                        << timestep << ','
                        << time << ','
                        << i << ','
                        << j << ','
                        << k << ','
                        << x << ','
                        << y << ','
                        << z << ','
                        << static_cast<int>(
                            cell.get_state()
                        );

                    for(LogVariable variable :
                        config_.field_variables) {

                        field_file_
                            << ','
                            << get_log_value(
                                cell,
                                variable
                            );
                    }

                    field_file_ << '\n';
                }
            }
        }
    }

    // ========================================================
    // SUMMARY LOGGING
    // ========================================================

    void log_summary(
        const Mesh& mesh,
        int timestep,
        double time
    ) {
        summary_file_
            << timestep << ','
            << time;

        for(const SummaryRequest& request :
            config_.summary_requests) {

            const SummaryStatistics statistics =
                compute_summary(mesh, request);

            if(request.log_min) {
                summary_file_
                    << ','
                    << (
                        statistics.count > 0
                            ? statistics.minimum
                            : 0.0
                    );
            }

            if(request.log_max) {
                summary_file_
                    << ','
                    << (
                        statistics.count > 0
                            ? statistics.maximum
                            : 0.0
                    );
            }

            if(request.log_average) {
                summary_file_
                    << ','
                    << statistics.average();
            }

            if(request.log_rms) {
                summary_file_
                    << ','
                    << statistics.rms();
            }

            if(request.log_standard_deviation) {
                summary_file_
                    << ','
                    << statistics.standard_deviation();
            }
        }

        summary_file_ << '\n';
    }

    // ========================================================
    // PROBE LOGGING
    // ========================================================

    void log_probes(
        const Mesh& mesh,
        int timestep,
        double time
    ) {
        for(const ResolvedProbe& probe :
            resolved_probes_) {

            const Cell& cell =
                mesh.at(
                    probe.i,
                    probe.j,
                    probe.k
                );

            std::ofstream& file =
                probe_files_.at(probe.name);

            file
                << timestep << ','
                << time << ','
                << probe.requested_x << ','
                << probe.requested_y << ','
                << probe.requested_z << ','
                << probe.i << ','
                << probe.j << ','
                << probe.k;

            for(LogVariable variable :
                probe.variables) {

                file
                    << ','
                    << get_log_value(
                        cell,
                        variable
                    );
            }

            file << '\n';
        }
    }
};
