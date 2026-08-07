#ifndef INPUT_TYPES_HPP
#define INPUT_TYPES_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct PositionInput {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::optional<std::string> units;
};

struct SizeInput {
    double width = 0.0;
    double depth = 0.0;
    double height = 0.0;
    std::optional<std::string> units;
};

struct MaterialInput {
    double density = 0.0;
    double cp = 0.0;
    double k = 0.0;
};

struct DirectionInput {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// -------------------------------------------------------------
// Simulation and mesh
// -------------------------------------------------------------

struct SimulationInput {
    double dt = 0.0;
    double duration = 0.0;
    int output_interval = 0.0;
    int max_timesteps = 0;
    int max_updates = 0;
    int max_cell_count = 0;
    int max_megabyte_usage = 0;
    std::optional<int> update_flow_interval = 0;
    bool advection_subcycling = false;
    double advection_cfl_target = 0.8;
    int max_advection_substeps = 10000;
};

struct FlowSolverInput {
    bool enable_flow_solver = false;
    std::string pressure_method = "sor";
    std::optional<double> resistivity = 0.0;
    std::optional<double> tolerance = 0.0;
    std::optional<int> max_iterations = 0;
    std::optional<double> sor_omega = 0.0;
    std::optional<int> max_outer_iters = 0;
    std::optional<double> flow_tolerance = 0.0;
};

struct LoggerSummaryInput {
    std::optional<std::string> name;
    std::optional<LogVariable> variable;
    std::optional<CellSelection> selection;
    std::optional<bool> log_min;
    std::optional<bool> log_max;
    std::optional<bool> log_average;
    std::optional<bool> log_rms;
    std::optional<bool> log_standard_deviation;
};

struct LoggerProbeInput {
    std::optional<std::string> name;
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> z;
    std::optional<std::vector<LogVariable>> variables;
};

struct LoggerInput {
    std::optional<std::string> output_directory;
    std::optional<bool> enable_field_logging;
    std::optional<bool> enable_summary_logging;
    std::optional<bool> enable_probe_logging;

    std::optional<int> field_interval;
    std::optional<int> summary_interval;
    std::optional<int> probe_interval;

    std::optional<std::vector<LogVariable>> field_variables;
    std::optional<std::vector<LoggerSummaryInput>> summary_requests;
    std::optional<std::vector<LoggerProbeInput>> probes;

    std::optional<std::string> template_file;
};

struct EnvironmentInput {
    double humidity = 0.0;
    double elevation = 0.0;
    double T_ambient = 0.0;
    double cp = 0.0;
    double k = 0.0;
    double mu = 0.0;
    double pr = 0.0;
    double rho = 0.0;
};


struct MeshInput {
    bool adaptive = false;
    double dx = 0.0;
    double dy = 0.0;
    double dz = 0.0;
    double fine_dx = 0.0;
    double coarse_dx = 0.0;
    double refinement_margin = 0.0;
};
struct MultiStageInput {
    bool enabled = false;
    double coarse_dt = 0.0;
    double coarse_duration = 0.0;
    int coarse_update_flow_interval = -1;
    MeshInput coarse_mesh;
};

struct OpenFoamSolverInput {
    bool enabled = false;
    std::optional<std::string> template_file;
    std::filesystem::path case_directory = "openfoam_cases/model";
    bool overwrite = false;
    int parallel_processes = 4;
    double maximum_time_step = 1.0;
    double maximum_courant_number = 1.0;
    double field_write_interval = 60.0;
    int saved_time_directories = 3;
    double report_interval = 10.0;
    bool use_k_omega_sst = true;
    double inlet_turbulence_intensity = 0.05;
    double turbulence_length_scale = 0.01;
    double turbulent_prandtl_number = 0.85;
    bool temperature_dependent_air = true;
    DirectionInput gravity{0.0,0.0,-9.80665};
    double sutherland_temperature = 110.4;
    bool use_vent_pressure_loss = true;
    bool use_fan_curves = true;
    // Zero selects the established automatic policy: fan-curve cases use
    // 3x3 PIMPLE correction and fixed-flow cases use 1x2.
    int pimple_outer_correctors = 0;
    int pimple_pressure_correctors = 0;
    double fan_curve_extension_multiplier = 2.0;
    bool use_multirate_thermal = true;
    double airflow_warmup_time = 5.0;
    bool use_fan_startup_ramp = true;
    double fan_startup_ramp_time = 0.05;
    int fan_startup_ramp_steps = 5;
    double initial_airflow_check_interval = 0.01;
    double minimum_initial_airflow_duration = 0.02;
    double airflow_maximum_time_step = 0.001;
    double thermal_only_maximum_time_step = 1.0;
    // Diagnostic maxCo written for frozen-flow energy stages. These stages
    // use fixed, fully implicit timesteps; accuracy is controlled by
    // thermal_only_maximum_time_step rather than an explicit CFL clamp.
    double thermal_only_maximum_courant_number = 1000.0;
    double airflow_refresh_interval = 300.0;
    double airflow_refresh_duration = 1.0;
    bool use_adaptive_airflow_refresh = true;
    double airflow_refresh_maximum_courant_number = 10.0;
    double airflow_refresh_check_interval = 0.01;
    double maximum_airflow_refresh_duration = 0.2;
    double maximum_mass_imbalance_fraction = 0.01;
    double maximum_device_flow_change_fraction = 0.02;
    double minimum_tracked_boundary_flow_fraction = 1e-4;
    bool stop_when_thermally_converged = true;
    double minimum_thermal_convergence_time = 3600.0;
    double thermal_convergence_reference_interval = 300.0;
    double maximum_temperature_change = 0.1;
    double maximum_component_average_temperature_change = 0.05;
    int thermal_convergence_required_checkpoints = 2;
};

// -------------------------------------------------------------
// Rack
// -------------------------------------------------------------

struct RackInput {
    std::string name = "rack";
    SizeInput size;
};

// -------------------------------------------------------------
// Fan and vents
// -------------------------------------------------------------

struct FanCurveInput {
    std::string name;
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;
    double rho_rated = 1.2;
};

enum class FanShape {
    Circular,
    Rectangular
};

enum class FanFlowType {
    Intake,
    Exhaust
};

struct FanInput {
    std::string name;

    FanShape shape{FanShape::Rectangular};
    FanFlowType flow_type{FanFlowType::Intake};

    PositionInput position;
    DirectionInput direction;

    // used by rectangular fan
    std::optional<SizeInput> size;
    // used by circular fan
    std::optional<double> diameter;
    std::optional<std::string> diameter_units;

    std::optional<std::string> curve_name;

    double cfm = 0.0;
};

enum class VentShape {
    Circular,
    Rectangular
};

struct VentInput {
    std::string name;
    
    VentShape shape{VentShape::Rectangular};

    PositionInput position;
    DirectionInput direction;
    
    double cd = 0.0;

    // used by rectangular vent
    std::optional<SizeInput> size;
    // used by circular vent
    std::optional<double> diameter;
    std::optional<std::string> diameter_units;

    double free_area_ratio = 0.0;
};

// -------------------------------------------------------------
// Internal Regions
// -------------------------------------------------------------

enum class RegionState {
    Solid,
    Air,
    Vent,
    Fan
};

struct InternalRegionInput {
    std::string name;

    RegionState state{RegionState::Solid};

    // Used by solid and air regions. For fan/vent regions, the position and
    // geometry are stored in the parsed FanInput or VentInput below.
    PositionInput local_position;
    SizeInput size;

    MaterialInput material;
    double watts = 0.0;

    std::optional<FanInput> fan;
    std::optional<VentInput> vent;
};

// -------------------------------------------------------------
// Components
// -------------------------------------------------------------

struct ComponentInput {
    std::string name;
    PositionInput position;
    SizeInput size;
    MaterialInput material;
    std::vector<InternalRegionInput> internal_regions;
    double watts = 0.0;
    std::optional<std::string> template_path;
};


// -------------------------------------------------------------
// Model
// -------------------------------------------------------------

struct ModelInput {
    std::string name;

    SimulationInput simulation;
    EnvironmentInput environment;
    FlowSolverInput flow_solver;
    MeshInput mesh;
    MultiStageInput multistage;
    OpenFoamSolverInput openfoam_solver;
    RackInput rack;

    std::vector<ComponentInput> components;
    std::vector<FanInput> fans;
    std::vector<VentInput> vents;
};

#endif
