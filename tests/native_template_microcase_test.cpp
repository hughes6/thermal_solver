#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "../src/collision.hpp"
#include "../src/grapher.hpp"
#include "../src/input/model_loader.hpp"

namespace {

constexpr double kAmbientTemperatureC = 20.0;
constexpr double kBoundarySourceM3s = 5.0e-4;
constexpr std::size_t kPeakBudgetBytes = 256u * 1024u * 1024u;
// FlowSolver's vectors and adjacency lists dominate its one-mesh phase;
// Solver's two-mesh thermal phase is smaller. This deliberately conservative
// guard is checked before allocation, so no individual microcase can turn into
// another full-rack memory probe.
constexpr std::size_t kConservativePeakBytesPerCell = 4096u;
constexpr auto kCampaignLimit = std::chrono::minutes(10);

struct TemplateSpec {
    std::string path;
    std::array<double, 3> size_m;
    double watts;
    std::array<int, 4> region_counts; // air, solid, vent, fan
    std::array<double, 3> maximum_cell_width_m;
    std::size_t planning_estimate;
    bool active_rack_template = true;
    std::string expected_rejection;
};

const std::vector<TemplateSpec> kSpecs{
    {"library/components/updated_eaton_UPS.toml",
     {0.4400, 0.6050, 0.08650}, 120.0, {2, 3, 2, 1},
     {0.020, 0.020, 0.010}, 18720},
    {"library/components/updated_DELL_R470.toml",
     {0.4820, 0.8170, 0.04300}, 1000.0, {1, 4, 2, 6},
     {0.020, 0.025, 0.010}, 22360},
    {"library/components/updated_Keysight_N5766A.toml",
     {0.4228, 0.4328, 0.04360}, 270.0, {6, 10, 4, 8},
     {0.020, 0.020, 0.010}, 17136},
    {"library/components/updated_keysight_N6701C.toml",
     {0.4250, 0.5497, 0.04445}, 270.0, {6, 10, 4, 8},
     {0.020, 0.020, 0.010}, 17138},
    {"library/components/updated_trenton_3u_bam.toml",
     {0.4826, 0.5000, 0.13335}, 370.0, {7, 16, 4, 6},
     {0.020, 0.020, 0.020}, 40800},
    {"library/components/eaton_KVM.toml",
     {0.4800, 0.6850, 0.04400}, 20.0, {1, 2, 0, 0},
     {0.025, 0.025, 0.010}, 8500},
    {"library/components/updated_Thruster_Load_Box.toml",
     {0.4826, 0.2286, 0.08890}, 10.0, {1, 1, 2, 2},
     {0.020, 0.020, 0.020}, 7854},
    {"library/components/updated_cisco_catalyst_9300_24.toml",
     {0.4450, 0.4880, 0.04400}, 150.0, {1, 3, 3, 4},
     {0.020, 0.020, 0.010}, 14280},
    {"library/components/EATON_PDU_PDUMNH30.toml",
     {0.4445, 0.3175, 0.08890}, 10.0, {1, 1, 0, 0},
     {0.025, 0.025, 0.015}, 5760},
    {"library/components/Fan_control_kit_PS.toml",
     {0.0400, 0.1135, 0.12520}, 15.0, {1, 1, 2, 0},
     {0.010, 0.020, 0.020}, 1404},
    {"library/components/updated_NI_PXIe_Chassis.toml",
     {0.3556, 0.2142, 0.17720}, 65.0, {2, 7, 4, 3},
     {0.020, 0.020, 0.020}, 14520},
    {"library/components/updated_NI_PXIe_Chassis_minimum_separator_sensitivity.toml",
     {0.3556, 0.2142, 0.17720}, 65.0, {2, 8, 4, 3},
     {0.020, 0.020, 0.020}, 14520, false,
     "explicit solid region 'Card Slot wall 1' changed volume after "
     "overlay/opening stamping"},
};

// The shelf is defined inline in the canonical model, so it cannot be loaded
// through ComponentLoader like the reusable templates above.  Keep a separate
// isolated-case contract pinned to that exact model object.  This is a
// numerical topology/material-participation screen; the provisional envelope
// and zero-watt load are not presented as measured physical calibration.
constexpr const char* kInlineShelfName = "3U storage Shelf";
const TemplateSpec kInlineShelfSpec{
    "inline_storage_shelf",
    {0.44450, 0.413385, 0.13335}, 0.0, {0, 0, 0, 0},
    {0.020, 0.020, 0.020}, 7425};

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if(!condition) fail(message);
}

bool close(double actual, double expected, double scale = 1.0) {
    return std::abs(actual - expected) <=
           1.0e-10 * std::max({scale, std::abs(actual), std::abs(expected)});
}

std::string case_label(const TemplateSpec& spec) {
    return std::filesystem::path(spec.path).stem().string();
}

double parsed_watts(const ComponentInput& component) {
    double watts = component.watts;
    for(const InternalRegionInput& region : component.internal_regions)
        watts += region.watts;
    return watts;
}

int region_slot(RegionState state) {
    switch(state) {
        case RegionState::Air: return 0;
        case RegionState::Solid: return 1;
        case RegionState::Vent: return 2;
        case RegionState::Fan: return 3;
    }
    return -1;
}

int normal_axis(const std::array<double, 3>& direction) {
    const double ax = std::abs(direction[0]);
    const double ay = std::abs(direction[1]);
    const double az = std::abs(direction[2]);
    if(az >= ax && az >= ay) return 2;
    if(ay >= ax && ay >= az) return 1;
    return 0;
}

std::vector<double> split_at_features(
    std::vector<double> features, double extent, double maximum_width) {
    require(std::isfinite(extent) && extent > 0.0,
            "invalid microcase extent");
    require(std::isfinite(maximum_width) && maximum_width > 0.0,
            "invalid microcase cell-width cap");
    features.push_back(0.0);
    features.push_back(extent);
    for(double& value : features) {
        require(std::isfinite(value), "non-finite feature coordinate");
        if(std::abs(value) < 1.0e-12) value = 0.0;
        if(std::abs(value - extent) < 1.0e-12) value = extent;
        require(value >= 0.0 && value <= extent,
                "feature coordinate outside microcase rack");
    }
    std::sort(features.begin(), features.end());
    features.erase(
        std::unique(features.begin(), features.end(),
                    [](double left, double right) {
                        return std::abs(left - right) < 1.0e-12;
                    }),
        features.end());

    std::vector<double> widths;
    for(std::size_t index = 1; index < features.size(); ++index) {
        const double span = features[index] - features[index - 1];
        if(span <= 1.0e-12) continue;
        const int pieces = std::max(
            1, static_cast<int>(std::ceil(span / maximum_width)));
        const double width = span / static_cast<double>(pieces);
        for(int piece = 0; piece < pieces; ++piece)
            widths.push_back(width);
    }
    require(!widths.empty(), "microcase produced an empty mesh axis");
    return widths;
}

void add_component_features(
    const Component& component,
    std::array<std::vector<double>, 3>& features) {
    const auto component_position = component.get_coords();
    const std::array<double, 3> component_size{
        component.get_width_m(), component.get_depth_m(),
        component.get_height_m()};
    for(int axis = 0; axis < 3; ++axis) {
        features[axis].push_back(component_position[axis]);
        features[axis].push_back(
            component_position[axis] + component_size[axis]);
    }

    for(const InternalRegion& region : component.get_regions()) {
        const auto position = region.get_global_position();
        const auto size = region.get_size_m();
        if(region.get_region_type() == RegionType::Fan ||
           region.get_region_type() == RegionType::Vent) {
            const int axis = normal_axis(region.get_direction());
            for(int dimension = 0; dimension < 3; ++dimension) {
                if(dimension == axis) {
                    features[dimension].push_back(position[dimension]);
                    continue;
                }
                const double half_extent = region.is_circular()
                    ? 0.5 * region.get_diameter()
                    : 0.5 * size[dimension];
                features[dimension].push_back(
                    position[dimension] - half_extent);
                features[dimension].push_back(
                    position[dimension] + half_extent);
            }
        } else {
            for(int axis = 0; axis < 3; ++axis) {
                features[axis].push_back(position[axis]);
                features[axis].push_back(position[axis] + size[axis]);
            }
        }
    }
}

Component build_component(
    const ComponentInput& input,
    const std::unordered_map<std::string, FanCurveInput>& curves,
    const std::array<double, 3>& position) {
    const auto size = size_to_meters(input.size, "microcase component.size");
    Component component = Component::from_meters(
        size[0], size[1], size[2], input.name);
    component.set_coords_m(position[0], position[1], position[2]);
    component.set_cp(input.material.cp);
    component.set_rho_solid(input.material.density);
    component.set_k_solid(input.material.k);
    component.set_watts(input.watts);
    for(const InternalRegionInput& region : input.internal_regions)
        component.add_region(build_internal_region(region, &curves));
    component.order_internal_regions();
    return component;
}

bool expected_state(const Cell& cell, RegionType type) {
    switch(type) {
        case RegionType::Air:
            // A one-cell air duct may deliberately become the fan or vent
            // plane stamped on top of it. Fan/Vent remain fluid states and
            // are the intended topology; requiring literal Air would report
            // those feature-aligned ducts as missing.
            return cell.is_fluid();
        case RegionType::HeatSource:
            return cell.is_solid();
        case RegionType::Vent:
            return cell.get_state() == Cell::State::Vent;
        case RegionType::Fan:
            return cell.get_state() == Cell::State::Fan;
        default:
            return false;
    }
}

struct RegionCellAudit {
    std::size_t count = 0;
    double geometric_measure = 0.0;
    double vent_conductance = 0.0;
    double integrated_watts = 0.0;
};

RegionCellAudit audit_region_cells(
    const Mesh& mesh, const InternalRegion& region) {
    const auto position = region.get_global_position();
    const auto size = region.get_size_m();
    const bool planar =
        region.get_region_type() == RegionType::Fan ||
        region.get_region_type() == RegionType::Vent;
    const int plane_axis = planar ? normal_axis(region.get_direction()) : -1;
    const int plane_index = !planar ? -1 :
        (plane_axis == 0 ? mesh.index_x(position[0]) :
         (plane_axis == 1 ? mesh.index_y(position[1]) :
                            mesh.index_z(position[2])));

    RegionCellAudit audit;
    for(int x = 0; x < mesh.get_nx(); ++x) {
        for(int y = 0; y < mesh.get_ny(); ++y) {
            for(int z = 0; z < mesh.get_nz(); ++z) {
                const Cell& cell = mesh.at(x, y, z);
                if(!expected_state(cell, region.get_region_type())) continue;
                const int indices[3]{x, y, z};
                const double center[3]{
                    mesh.cell_center_x(x), mesh.cell_center_y(y),
                    mesh.cell_center_z(z)};
                bool selected = true;
                if(planar) {
                    selected = indices[plane_axis] == plane_index;
                    for(int axis = 0; axis < 3 && selected; ++axis) {
                        if(axis == plane_axis) continue;
                        if(region.is_circular()) {
                            // Circular membership is checked jointly below.
                            continue;
                        }
                        const double half = 0.5 * size[axis];
                        selected = center[axis] >= position[axis] - half - 1.0e-12 &&
                                   center[axis] <= position[axis] + half + 1.0e-12;
                    }
                    if(selected && region.is_circular()) {
                        double distance_squared = 0.0;
                        for(int axis = 0; axis < 3; ++axis) {
                            if(axis == plane_axis) continue;
                            const double delta = center[axis] - position[axis];
                            distance_squared += delta * delta;
                        }
                        const double radius = 0.5 * region.get_diameter();
                        selected = distance_squared <= radius * radius + 1.0e-15;
                    }
                } else {
                    for(int axis = 0; axis < 3 && selected; ++axis)
                        selected = center[axis] >= position[axis] - 1.0e-12 &&
                                   center[axis] <=
                                       position[axis] + size[axis] + 1.0e-12;
                }
                if(selected) {
                    ++audit.count;
                    audit.geometric_measure += planar
                        ? (plane_axis == 0 ? cell.area_x() :
                           (plane_axis == 1 ? cell.area_y() : cell.area_z()))
                        : cell.volume();
                    if(region.get_region_type() == RegionType::Vent)
                        audit.vent_conductance +=
                            cell.get_vent_conductance();
                    audit.integrated_watts += cell.get_qdot() * cell.volume();
                }
            }
        }
    }
    return audit;
}

double integrated_watts(const Mesh& mesh) {
    double watts = 0.0;
    for(const Cell& cell : mesh.get_cells())
        watts += cell.get_qdot() * cell.volume();
    return watts;
}

double average_powered_temperature(const Mesh& mesh) {
    double sum = 0.0;
    std::size_t count = 0;
    for(const Cell& cell : mesh.get_cells()) {
        if(cell.get_qdot() <= 0.0) continue;
        sum += cell.get_T();
        ++count;
    }
    require(count > 0, "no powered cells survived component stamping");
    return sum / static_cast<double>(count);
}

double average_solid_temperature(const Mesh& mesh) {
    double sum = 0.0;
    std::size_t count = 0;
    for(const Cell& cell : mesh.get_cells()) {
        if(!cell.is_solid()) continue;
        sum += cell.get_T();
        ++count;
    }
    require(count > 0, "no solid cells survived component stamping");
    return sum / static_cast<double>(count);
}

void check_elapsed(
    const std::chrono::steady_clock::time_point start,
    const std::string& stage) {
    if(std::chrono::steady_clock::now() - start > kCampaignLimit)
        fail("native template campaign exceeded 10 minutes after " + stage);
}

void verify_inventory() {
    ModelLoader model_loader;
    model_loader.load_fan_curves("library/fan_curves/fan_curves.toml");
    model_loader.load_model("library/models/new_model_updated.toml");

    std::map<std::string, int> placements;
    int inline_components = 0;
    for(const ComponentInput& component : model_loader.model.components) {
        if(component.template_path)
            ++placements[*component.template_path];
        else
            ++inline_components;
    }
    const std::size_t active_template_count = static_cast<std::size_t>(
        std::count_if(
            kSpecs.begin(),kSpecs.end(),
            [](const TemplateSpec& spec) { return spec.active_rack_template; }));
    require(placements.size() == active_template_count,
            "updated rack does not contain exactly 11 unique templates");
    require(inline_components == 1,
            "updated rack must retain exactly one inline storage shelf");

    double unique_watts = 0.0;
    double rack_watts = 0.0;
    for(const TemplateSpec& spec : kSpecs) {
        const auto found = placements.find(spec.path);
        if(!spec.active_rack_template) {
            require(found == placements.end(),
                    "provisional sensitivity became active in the canonical rack: " +
                    spec.path);
            continue;
        }
        require(found != placements.end(),
                "updated rack is missing template " + spec.path);
        const int expected_placements =
            spec.path == "library/components/Fan_control_kit_PS.toml" ? 2 : 1;
        require(found->second == expected_placements,
                spec.path + " placement multiplicity changed");
        unique_watts += spec.watts;
        rack_watts += spec.watts * expected_placements;
    }
    for(const ComponentInput& component : model_loader.model.components)
        if(!component.template_path) rack_watts += parsed_watts(component);
    require(close(unique_watts, 2300.0),
            "unique-template heat inventory is not 2300 W");
    require(close(rack_watts, 2315.0),
            "updated rack heat inventory is not 2315 W");
}

void run_microcase(
    const TemplateSpec& spec,
    const ComponentInput& input,
    const std::unordered_map<std::string, FanCurveInput>& curves,
    const std::filesystem::path& temporary_root,
    const std::chrono::steady_clock::time_point campaign_start,
    int maximum_outer_iterations) {
    const std::string label = case_label(spec);

    const auto parsed_size = size_to_meters(input.size, label + ".size");
    for(int axis = 0; axis < 3; ++axis)
        require(close(parsed_size[axis], spec.size_m[axis]),
                label + " parsed dimensions changed");
    require(close(parsed_watts(input), spec.watts),
            label + " parsed watts changed");
    std::array<int, 4> parsed_region_counts{};
    for(const InternalRegionInput& region : input.internal_regions) {
        const int slot = region_slot(region.state);
        require(slot >= 0, label + " has an unrecognized region state");
        ++parsed_region_counts[static_cast<std::size_t>(slot)];
    }
    require(parsed_region_counts == spec.region_counts,
            label + " parsed region inventory changed");

    const std::array<double, 3> component_position{
        2.0 * spec.maximum_cell_width_m[0],
        2.0 * spec.maximum_cell_width_m[1],
        2.0 * spec.maximum_cell_width_m[2]};
    const std::array<double, 3> rack_size{
        parsed_size[0] + 4.0 * spec.maximum_cell_width_m[0],
        parsed_size[1] + 4.0 * spec.maximum_cell_width_m[1],
        parsed_size[2] + 4.0 * spec.maximum_cell_width_m[2]};
    const bool inline_shelf = spec.path == kInlineShelfSpec.path;
    Component component = build_component(
        input, curves, component_position);
    if(inline_shelf) {
        // A small numerical initial-condition perturbation exercises the
        // unpowered shelf's thermal transport and heat capacity without
        // inventing a shelf heat load. It is a software-path check only.
        component.set_t(kAmbientTemperatureC + 5.0);
    }

    Rack rack = Rack::from_meters(
        rack_size[0], rack_size[1], rack_size[2], label + " microcase");
    rack.set_t(kAmbientTemperatureC);
    rack.set_cp(1005.5);
    rack.set_k(0.02587);
    rack.set_rho(0.9833);
    const std::vector<Component> bounded_components{component};
    const std::vector<Fan> no_fans;
    const std::vector<Vent> no_vents;
    RackBoundsChecker::check_all(
        rack, bounded_components, no_fans, no_vents);
    CollisionChecker::check_all(bounded_components, no_fans, no_vents);

    std::array<std::vector<double>, 3> features;
    add_component_features(component, features);
    // Explicit patch edges exercise the boundary fan/vent footprint planner
    // as well as component-owned openings. Both patches span the component's
    // projected x-z envelope and sit on opposite y boundaries.
    features[0].push_back(component_position[0]);
    features[0].push_back(component_position[0] + parsed_size[0]);
    features[2].push_back(component_position[2]);
    features[2].push_back(component_position[2] + parsed_size[2]);

    const auto dxs = split_at_features(
        features[0], rack_size[0], spec.maximum_cell_width_m[0]);
    const auto dys = split_at_features(
        features[1], rack_size[1], spec.maximum_cell_width_m[1]);
    const auto dzs = split_at_features(
        features[2], rack_size[2], spec.maximum_cell_width_m[2]);
    const std::size_t cell_count =
        dxs.size() * dys.size() * dzs.size();
    const std::size_t guarded_peak =
        cell_count * kConservativePeakBytesPerCell;
    require(guarded_peak <= kPeakBudgetBytes,
            label + " exceeds the conservative 256 MiB per-case guard before allocation");
    require(cell_count <= 62500,
            label + " exceeds the explicit 62,500-cell cap");
    if(spec.path == kInlineShelfSpec.path)
        require(cell_count == spec.planning_estimate,
                "inline shelf feature-aligned mesh cell count changed");

    Environment environment(
        30.0, 5500.0, kAmbientTemperatureC, 1005.5,
        0.02587, 1.81e-5, 0.71, 0.9833);
    Workload workload(
        2, 4 * cell_count, static_cast<int>(cell_count), 128);
    Mesh mesh = Mesh().build_adaptive_mesh(
        rack, dxs, dys, dzs, environment, workload);
    mesh.stamp_component_adaptive(component);

    std::size_t solid_cells = 0;
    std::size_t internal_air_cells = 0;
    std::size_t fan_cells = 0;
    std::size_t vent_cells = 0;
    double solid_volume_m3 = 0.0;
    double solid_thermal_capacity_j_per_k = 0.0;
    for(const Cell& cell : mesh.get_cells()) {
        if(cell.is_solid()) {
            ++solid_cells;
            solid_volume_m3 += cell.volume();
            solid_thermal_capacity_j_per_k +=
                cell.get_rho() * cell.get_cp() * cell.volume();
        }
        if(cell.get_state() == Cell::State::Air) ++internal_air_cells;
        if(cell.get_state() == Cell::State::Fan) ++fan_cells;
        if(cell.get_state() == Cell::State::Vent) ++vent_cells;
    }
    require(solid_cells > 0, label + " produced no solid topology");
    require(internal_air_cells > 0, label + " produced no air topology");

    if(inline_shelf) {
        require(input.name == kInlineShelfName,
                "inline shelf case selected a different model component");
        require(input.internal_regions.empty(),
                "inline shelf unexpectedly gained internal regions");
        require(fan_cells == 0 && vent_cells == 0,
                "inline shelf unexpectedly stamps an opening or active device");
        const double requested_volume =
            parsed_size[0] * parsed_size[1] * parsed_size[2];
        require(std::abs(solid_volume_m3 - requested_volume) <=
                    std::max(1.0e-12, 1.0e-9 * requested_volume),
                "inline shelf obstruction does not preserve its solid envelope volume");
        const double requested_capacity = input.material.density *
            input.material.cp * requested_volume;
        require(std::abs(solid_thermal_capacity_j_per_k - requested_capacity) <=
                    1.0e-9 * std::max(1.0, requested_capacity),
                "inline shelf does not preserve rho*Cp*volume thermal mass");
        for(const Cell& cell : mesh.get_cells()) {
            if(!cell.is_solid()) continue;
            require(close(cell.get_rho(), input.material.density) &&
                        close(cell.get_cp(), input.material.cp) &&
                        close(cell.get_k(), input.material.k),
                    "inline shelf solid cells do not preserve material properties");
        }
    }

    const double watt_tolerance = 1.0e-8 * std::max(1.0, spec.watts);
    std::size_t region_fan_cells = 0;
    std::size_t region_vent_cells = 0;
    for(const InternalRegion& region : component.get_regions()) {
        const RegionCellAudit audit = audit_region_cells(mesh, region);
        require(audit.count > 0,
                 label + " region '" + region.get_name() +
                     "' retained no cells of its requested state");
        if(region.get_region_type() == RegionType::HeatSource) {
            const double requested_volume = region.volume();
            const double tolerance =
                std::max(1.0e-12, 1.0e-9 * requested_volume);
            require(
                std::abs(audit.geometric_measure - requested_volume) <=
                    tolerance,
                label + " explicit solid region '" + region.get_name() +
                    "' changed volume after overlay/opening stamping: " +
                    "requested=" + std::to_string(requested_volume) +
                    " m^3, realized=" +
                    std::to_string(audit.geometric_measure) + " m^3");
        }
        if(region.get_region_type() == RegionType::Fan ||
           region.get_region_type() == RegionType::Vent) {
            const double requested_area = region.area();
            const double tolerance = region.is_circular()
                ? 0.05 * requested_area + 1.0e-12
                : std::max(1.0e-12, 1.0e-9 * requested_area);
            require(
                std::abs(audit.geometric_measure - requested_area) <=
                    tolerance,
                label + " opening region '" + region.get_name() +
                    "' does not retain its requested face area");
            if(region.get_region_type() == RegionType::Vent) {
                const double requested_conductance =
                    region.get_cd() * region.free_area();
                require(
                    std::abs(
                        audit.vent_conductance - requested_conductance) <=
                        std::max(
                            1.0e-12,
                            1.0e-9 * requested_conductance),
                    label + " vent region '" + region.get_name() +
                        "' does not conserve Cd*A_free after stamping");
            }
        }
        if((region.get_region_type() == RegionType::HeatSource ||
            region.get_region_type() == RegionType::Air) &&
           std::abs(region.get_watts()) > 1.0e-12) {
            require(
                std::abs(audit.integrated_watts - region.get_watts()) <=
                    1.0e-8 * std::max(1.0, std::abs(region.get_watts())),
                label + " powered region '" + region.get_name() +
                    "' does not conserve its requested heat after stamping");
        }
        if(region.get_region_type() == RegionType::Fan)
            region_fan_cells += audit.count;
        if(region.get_region_type() == RegionType::Vent)
            region_vent_cells += audit.count;
    }
    require(region_fan_cells == fan_cells,
            label + " fan footprints overlap or lost stamped cells");
    require(region_vent_cells == vent_cells,
            label + " vent footprints overlap or lost stamped cells");
    require(mesh.get_internal_fans().size() == fan_cells,
            label + " internal fan interface count does not match fan cells");

    // Establish the complete and per-region power inventory even when a later
    // fan-domain or nonlinear-flow gate intentionally stops this case before
    // thermal integration.
    const double stamped_watts = integrated_watts(mesh);
    require(std::abs(stamped_watts - spec.watts) <= watt_tolerance,
            label + " does not conserve requested heat after stamping");

    std::size_t bootstrap_curve_overrun_fans = 0;
    double maximum_bootstrap_overrun_percent = 0.0;
    for(const Mesh::InternalFanInterface& fan : mesh.get_internal_fans()) {
        require(mesh.in_bounds(fan.upstream[0], fan.upstream[1], fan.upstream[2]),
                label + " internal fan upstream link is out of bounds");
        require(mesh.in_bounds(
                    fan.downstream[0], fan.downstream[1], fan.downstream[2]),
                label + " internal fan downstream link is out of bounds");
        require(mesh.at(fan.upstream[0], fan.upstream[1], fan.upstream[2]).is_fluid(),
                label + " internal fan upstream link is not fluid");
        require(mesh.at(
                    fan.downstream[0], fan.downstream[1],
                    fan.downstream[2]).is_fluid(),
                label + " internal fan downstream link is not fluid");
        require(std::isfinite(fan.flow_m3s) && fan.flow_m3s > 0.0,
                label + " internal fan reference flow is invalid");
        require(std::isfinite(fan.q_ref) && fan.q_ref > 0.0,
                label + " internal fan operating flow is invalid");
        if(fan.has_curve()) {
            const double zero = fan_curve_first_positive_zero(
                fan.curve_a, fan.curve_b, fan.curve_c);
            require(std::isfinite(zero) && zero > 0.0,
                    label + " internal fan has no positive curve zero");
            if(fan.q_ref > zero * (1.0 + 1.0e-9)) {
                ++bootstrap_curve_overrun_fans;
                maximum_bootstrap_overrun_percent = std::max(
                    maximum_bootstrap_overrun_percent,
                    100.0 * (fan.q_ref / zero - 1.0));
            }
        }
    }

    const double boundary_cfm = kBoundarySourceM3s / Fan::CFM_TO_M3S;
    Fan inlet(
        label + " controlled inlet", boundary_cfm, 0.0,
        {parsed_size[0], 0.0, parsed_size[2]},
        {component_position[0] + 0.5 * parsed_size[0], 0.0,
         component_position[2] + 0.5 * parsed_size[2]},
        {0.0, 1.0, 0.0}, FlowType::Intake, ShapeType::Rectangular);
    Vent outlet(
        label + " controlled outlet",
        {parsed_size[0], 0.0, parsed_size[2]}, 1.0, 0.0, 0.82,
        {component_position[0] + 0.5 * parsed_size[0], rack_size[1],
         component_position[2] + 0.5 * parsed_size[2]},
        {0.0, 1.0, 0.0}, VentShapeType::Rectangular);
    const std::vector<Fan> boundary_fans{inlet};
    const std::vector<Vent> boundary_vents{outlet};
    RackBoundsChecker::check_all(
        rack, bounded_components, boundary_fans, boundary_vents);
    CollisionChecker::check_all(
        bounded_components, boundary_fans, boundary_vents);
    mesh.stamp_fan_adaptive(inlet);
    mesh.stamp_vent_adaptive(outlet);

    int flow_outer_iterations = 0;
    double maximum_local_continuity_residual =
        std::numeric_limits<double>::infinity();
    {
        FlowSolver flow(
            mesh, 4.6, 1.0e-8, 1000, 1.1,
            maximum_outer_iterations, 1.0e-3, "pcg");
        flow.solve();
        flow_outer_iterations = flow.outer_iterations();
        require(flow.converged(),
                label + " PCG/nonlinear flow did not converge within " +
                    std::to_string(maximum_outer_iterations) +
                    " outer iterations");
        require(flow_outer_iterations <= 40,
                label + " flow solve exceeds the 40-iteration readiness "
                    "ceiling under the 50-iteration default campaign cap");
        require(std::abs(flow.total_source_m3s() - kBoundarySourceM3s) <= 1.0e-8,
                label + " controlled source is not 5e-4 m^3/s");
        require(std::abs(flow.mass_imbalance_m3s()) <=
                    std::max(1.0e-8, 0.01 * kBoundarySourceM3s),
                label + " flow mass imbalance exceeds 1% of source flow");
        require(flow.has_face_flux_solution(),
                label + " did not publish an exact face-flux solution");
        maximum_local_continuity_residual =
            flow.maximum_realized_continuity_residual_m3s();
        require(std::isfinite(maximum_local_continuity_residual) &&
                    maximum_local_continuity_residual <= 1.01e-8,
                label + " exact face-flux field violates local continuity");
        for(const Mesh::InternalFanInterface& fan : mesh.get_internal_fans()) {
            const double published = flow.internal_fan_face_flux_m3s(
                fan.upstream, fan.downstream);
            const double expected = fan.has_curve() ? fan.q_ref : fan.flow_m3s;
            require(std::isfinite(published) &&
                        std::abs(published - expected) <=
                            1.0e-12 * std::max(1.0, std::abs(expected)),
                    label + " internal fan interface does not publish its "
                        "solved operating flow");
        }
    }

    double maximum_speed = 0.0;
    double maximum_inline_shelf_bypass_speed = 0.0;
    std::size_t inline_shelf_bypass_cells = 0;
    const double component_x_max = component_position[0] + parsed_size[0];
    const double component_y_max = component_position[1] + parsed_size[1];
    const double component_z_max = component_position[2] + parsed_size[2];
    for(int x = 0; x < mesh.get_nx(); ++x) {
        for(int y = 0; y < mesh.get_ny(); ++y) {
            for(int z = 0; z < mesh.get_nz(); ++z) {
                const Cell& cell = mesh.at(x, y, z);
                require(std::isfinite(cell.get_pressure()),
                        label + " produced non-finite pressure");
                require(std::isfinite(cell.get_vx()) &&
                            std::isfinite(cell.get_vy()) &&
                            std::isfinite(cell.get_vz()),
                        label + " produced non-finite velocity");
                if(!cell.is_fluid()) continue;
                maximum_speed = std::max(maximum_speed, cell.get_vmag());
                if(inline_shelf &&
                   mesh.cell_center_y(y) >= component_position[1] - 1.0e-12 &&
                   mesh.cell_center_y(y) <= component_y_max + 1.0e-12 &&
                   (mesh.cell_center_x(x) < component_position[0] - 1.0e-12 ||
                    mesh.cell_center_x(x) > component_x_max + 1.0e-12 ||
                    mesh.cell_center_z(z) < component_position[2] - 1.0e-12 ||
                    mesh.cell_center_z(z) > component_z_max + 1.0e-12)) {
                    ++inline_shelf_bypass_cells;
                    maximum_inline_shelf_bypass_speed = std::max(
                        maximum_inline_shelf_bypass_speed, cell.get_vmag());
                }
            }
        }
    }
    require(maximum_speed > 0.0, label + " produced a zero flow field");
    if(inline_shelf) {
        require(inline_shelf_bypass_cells > 0,
                "inline shelf leaves no resolved lateral bypass cells");
        require(maximum_inline_shelf_bypass_speed > 0.0,
                "inline shelf obstruction does not produce resolved bypass flow");
    }
    std::size_t stalled_curved_fans = 0;
    for(const Mesh::InternalFanInterface& fan : mesh.get_internal_fans()) {
        require(std::isfinite(fan.q_ref) && fan.q_ref >= 0.0,
                label + " internal fan solved flow is invalid");
        if(fan.has_curve()) {
            const double zero = fan_curve_first_positive_zero(
                fan.curve_a, fan.curve_b, fan.curve_c);
            if(!(std::isfinite(zero) && zero > 0.0 &&
                 fan.q_ref <= zero * (1.0 + 1.0e-9))) {
                std::ostringstream detail;
                detail << std::setprecision(17) << label
                       << " internal fan solved outside its curve domain: "
                       << "q_ref=" << fan.q_ref
                       << " m^3/s per cell, curve_zero=" << zero
                       << " m^3/s per cell";
                fail(detail.str());
            }

            const Cell& upstream = mesh.at(
                fan.upstream[0], fan.upstream[1], fan.upstream[2]);
            const Cell& downstream = mesh.at(
                fan.downstream[0], fan.downstream[1], fan.downstream[2]);
            const double density_ratio =
                0.5 * (upstream.get_rho() + downstream.get_rho()) /
                fan.rho_rated;
            const double required_head =
                downstream.get_pressure() - upstream.get_pressure();
            const double curve_head = bounded_fan_curve_pressure(
                fan.curve_a, fan.curve_b, fan.curve_c, fan.q_ref) *
                density_ratio;
            require(std::isfinite(required_head) &&
                        std::isfinite(curve_head),
                    label + " internal fan complementarity state is non-finite");
            const double flow_endpoint_tolerance =
                std::max(1.0e-12, 1.0e-9 * zero);
            const double head_tolerance = 1.0e-6 *
                std::max({1.0, std::abs(required_head),
                          std::abs(curve_head)});
            if(fan.q_ref <= flow_endpoint_tolerance) {
                ++stalled_curved_fans;
                require(required_head + head_tolerance >= curve_head,
                        label + " stalled internal fan violates its lower-bound "
                            "complementarity condition");
            } else if(fan.q_ref >= zero - flow_endpoint_tolerance) {
                require(required_head <= curve_head + head_tolerance,
                        label + " free-delivery internal fan violates its "
                            "upper-bound complementarity condition");
            } else {
                require(std::abs(required_head - curve_head) <= head_tolerance,
                        label + " interior internal fan does not satisfy its "
                            "pressure-flow curve");
            }
        }
    }
    check_elapsed(campaign_start, label + " flow solve");

    const bool powered_case = std::abs(spec.watts) > 1.0e-12;
    const double initial_component_temperature = powered_case
        ? average_powered_temperature(mesh)
        : average_solid_temperature(mesh);
    if(inline_shelf)
        require(close(
                    initial_component_temperature,
                    kAmbientTemperatureC + 5.0),
                "inline shelf initial thermal perturbation was not stamped");
    // An independent temp-cleanup job can run while this longer campaign is
    // active. Re-establish our exact owned parent immediately before output
    // so removal of an empty parent cannot invalidate every later case.
    std::filesystem::create_directories(temporary_root);
    const std::filesystem::path csv_path = temporary_root / (label + ".csv");
    double component_temperature_change =
        std::numeric_limits<double>::quiet_NaN();
    {
        const double thermal_dt = inline_shelf ? 1.0e-3 : 1.0e-5;
        const double thermal_duration = inline_shelf ? 2.0e-3 : 2.0e-5;
        Solver thermal(
            std::move(mesh), thermal_dt, thermal_duration, false, 2, -1,
            4.6, 1.0e-8, 1000, 1.1, maximum_outer_iterations, 1.0e-3,
            true, 0.5, 10000, csv_path.string(), "pcg", true);
        thermal.solve();
        const Mesh& result = thermal.get_mesh();
        for(const Cell& cell : result.get_cells()) {
            require(std::isfinite(cell.get_T()) && cell.get_T() > -100.0 &&
                        cell.get_T() < 500.0,
                    label + " thermal step produced a nonphysical temperature");
        }
        require(std::abs(integrated_watts(result) - spec.watts) <= watt_tolerance,
                label + " thermal steps changed integrated heat input");
        const double final_component_temperature = powered_case
            ? average_powered_temperature(result)
            : average_solid_temperature(result);
        component_temperature_change =
            final_component_temperature - initial_component_temperature;
        if(powered_case) {
            require(component_temperature_change > 0.0,
                    label + " powered cells did not warm after two thermal steps");
        } else {
            require(component_temperature_change < 0.0,
                    label + " perturbed unpowered solid did not cool");
            require(final_component_temperature > kAmbientTemperatureC,
                    label + " perturbed unpowered solid overshot ambient");
        }
    }
    std::error_code remove_error;
    std::filesystem::remove(csv_path, remove_error);
    require(!remove_error, label + " could not remove its temporary CSV");
    check_elapsed(campaign_start, label + " thermal solve");

    std::cout << "native template microcase PASS: " << label
              << " cells=" << cell_count
              << " planningEstimate=" << spec.planning_estimate
              << " watts=" << spec.watts
              << " fanCells=" << fan_cells
              << " ventCells=" << vent_cells
              << " maxOuter=" << maximum_outer_iterations
              << " outerIterations=" << flow_outer_iterations
              << " localResidual=" << maximum_local_continuity_residual
              << " bootstrapCurveOverrunFans="
              << bootstrap_curve_overrun_fans
              << " maxBootstrapOverrunPct="
              << maximum_bootstrap_overrun_percent
              << " stalledCurvedFans=" << stalled_curved_fans
              << " shelfBypassCells=" << inline_shelf_bypass_cells
              << " shelfMaxBypassSpeed="
              << maximum_inline_shelf_bypass_speed
              << " solidThermalCapacityJPerK="
              << solid_thermal_capacity_j_per_k
              << " componentTemperatureChangeK="
              << component_temperature_change
              << " maxSpeed=" << maximum_speed << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc <= 3,
                "usage: native_template_microcase_test "
                "[template-path-or-stem-or-inline_storage_shelf "
                "[max-outer-iters]]");
        const std::string effective_filter = argc >= 2 ? argv[1] : "";
        int maximum_outer_iterations = 50;
        if(argc == 3) {
            std::size_t parsed = 0;
            maximum_outer_iterations = std::stoi(argv[2], &parsed);
            require(parsed == std::string(argv[2]).size() &&
                        maximum_outer_iterations >= 2 &&
                        maximum_outer_iterations <= 100,
                    "max-outer-iters must be an integer in [2,100]");
        }
        const auto campaign_start = std::chrono::steady_clock::now();
        verify_inventory();
        const auto unique_suffix = std::to_string(
            campaign_start.time_since_epoch().count());
        TemporaryDirectory temporary{
            std::filesystem::temp_directory_path() /
            ("thermal_native_template_microcases_" + unique_suffix)};
        std::filesystem::create_directories(temporary.path);

        std::size_t selected_cases = 0;
        std::size_t functional_passes = 0;
        std::size_t inline_shelf_passes = 0;
        std::size_t expected_rejections = 0;
        std::vector<std::string> failures;
        auto execute_case = [&](const TemplateSpec& spec, const auto& body) {
            ++selected_cases;
            check_elapsed(campaign_start, "campaign dispatch");
            try {
                body();
                if(!spec.expected_rejection.empty()) {
                    failures.push_back(
                        case_label(spec) +
                        ": unexpectedly passed; review and remove its pinned "
                        "rejection before accepting the geometry");
                } else {
                    ++functional_passes;
                    if(spec.path == kInlineShelfSpec.path)
                        ++inline_shelf_passes;
                }
            } catch(const std::exception& error) {
                if(!spec.expected_rejection.empty() &&
                   std::string(error.what()).find(spec.expected_rejection) !=
                       std::string::npos) {
                    ++expected_rejections;
                    std::cout
                        << "native template microcase EXPECTED REJECTION: "
                        << case_label(spec) << ": " << error.what() << '\n';
                } else {
                    const std::string failure =
                        case_label(spec) + ": " + error.what();
                    failures.push_back(failure);
                    std::cerr << "native template microcase FAIL: "
                              << failure << '\n';
                }
            }
        };

        for(const TemplateSpec& spec : kSpecs) {
            if(!effective_filter.empty() && effective_filter != spec.path &&
               effective_filter != case_label(spec))
                continue;
            execute_case(spec, [&] {
                ComponentLoader loader;
                loader.load_fan_curves("library/fan_curves/fan_curves.toml");
                loader.load_component(spec.path);
                run_microcase(
                    spec, loader.model, loader.fan_curve_library,
                    temporary.path, campaign_start,
                    maximum_outer_iterations);
            });
        }

        if(effective_filter.empty() ||
           effective_filter == kInlineShelfSpec.path ||
           effective_filter == kInlineShelfName) {
            execute_case(kInlineShelfSpec, [&] {
                ModelLoader model_loader;
                model_loader.load_fan_curves(
                    "library/fan_curves/fan_curves.toml");
                model_loader.load_model(
                    "library/models/new_model_updated.toml");
                const ComponentInput* shelf = nullptr;
                for(const ComponentInput& component :
                    model_loader.model.components) {
                    if(component.template_path ||
                       component.name != kInlineShelfName)
                        continue;
                    require(shelf == nullptr,
                            "updated rack contains duplicate inline storage shelves");
                    shelf = &component;
                }
                require(shelf != nullptr,
                        "updated rack is missing the inline storage shelf");
                run_microcase(
                    kInlineShelfSpec, *shelf,
                    model_loader.fan_curve_library, temporary.path,
                    campaign_start, maximum_outer_iterations);
            });
        }
        require(selected_cases > 0,
                "component filter did not match a campaign case: " +
                    effective_filter);
        const double elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - campaign_start).count();
        if(!failures.empty()) {
            std::cerr << "native_template_microcase_test FAILED: "
                      << failures.size() << '/' << selected_cases
                      << " selected component cases failed; elapsed="
                      << elapsed_seconds << " s\n";
            for(const std::string& failure : failures)
                std::cerr << "  - " << failure << '\n';
            return 1;
        }
        std::cout << "native_template_microcase_test PASSED: "
                  << functional_passes << " functional passes, "
                  << expected_rejections << " expected geometry rejections, "
                  << selected_cases << " selected component cases, "
                  << inline_shelf_passes
                  << " inline storage-shelf obstruction/thermal passes, "
                  << "2 Meanwell placements, "
                  << "2315 W rack inventory, elapsed="
                  << elapsed_seconds << " s\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "native_template_microcase_test FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}
