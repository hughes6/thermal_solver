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
};

struct FlowSolverInput {
    bool enable_flow_solver = false;
    std::optional<double> resistivity = 0.0;
    std::optional<double> tolerance = 0.0;
    std::optional<int> max_iterations = 0;
    std::optional<double> sor_omega = 0.0;
    std::optional<int> max_outer_iters = 0;
    std::optional<double> flow_tolerance = 0.0;
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
    double dx = 0.0;
    double dy = 0.0;
    double dz = 0.0;
};

// -------------------------------------------------------------
// Rack
// -------------------------------------------------------------

struct AmbientInput {
    double temperature = 20.0;
    double pressure = 101325.0;
    double rho = 0.0;
    double cp = 0.0;
    double k = 0.0;
    double h = 0.0;
};

struct RackInput {
    std::string name = "rack";
    SizeInput size;
    AmbientInput ambient;
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

    // local to component
    PositionInput local_position;
    SizeInput size;


    MaterialInput material;
    double watts = 0.0;
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
    RackInput rack;

    std::vector<ComponentInput> components;
    std::vector<FanInput> fans;
    std::vector<VentInput> vents;
};

#endif