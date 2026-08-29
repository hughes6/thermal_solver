#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "../src/grapher.hpp"
#include "../src/input/model_loader.hpp"
#include "../src/mesh_refinement_planner.hpp"

namespace {

constexpr char kComponentPath[] =
    "library/components/updated_NI_PXIe_Chassis_minimum_separator_sensitivity.toml";
constexpr char kCanonicalComponentPath[] =
    "library/components/updated_NI_PXIe_Chassis.toml";
constexpr char kWall1Name[] = "Card Slot wall 1";
constexpr char kWall3Name[] =
    "PROVISIONAL minimum Card Slot wall 3 separator sensitivity";
constexpr char kConflictingVentName[] = "Top right side vent";
constexpr char kInteriorAirName[] = "Interior air";
constexpr double kVolumeTolerance = 2.0e-12;
constexpr std::size_t kCellGuard = 175000;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if(!condition) fail(message);
}

bool close(double actual, double expected,
           double tolerance = kVolumeTolerance) {
    return std::abs(actual - expected) <= tolerance;
}

Component build_component(
    const ComponentInput& input,
    const std::unordered_map<std::string, FanCurveInput>& curves,
    const std::array<double, 3>& position,
    const std::string& omitted_region = "") {
    const auto size = size_to_meters(input.size, "NI separator component.size");
    Component component = Component::from_meters(
        size[0], size[1], size[2], input.name);
    component.set_coords_m(position[0], position[1], position[2]);
    component.set_cp(input.material.cp);
    component.set_rho_solid(input.material.density);
    component.set_k_solid(input.material.k);
    component.set_watts(input.watts);
    for(const InternalRegionInput& region : input.internal_regions) {
        if(region.name == omitted_region) continue;
        component.add_region(build_internal_region(region, &curves));
    }
    component.order_internal_regions();
    return component;
}

InternalRegion region_copy(const Component& component,
                           const std::string& name) {
    for(const InternalRegion& region : component.get_regions())
        if(region.get_name() == name) return region;
    fail("missing NI region: " + name);
}

struct BoxAudit {
    double selected_volume = 0.0;
    double solid_volume = 0.0;
    double air_volume = 0.0;
    double vent_volume = 0.0;
    double fan_volume = 0.0;
};

BoxAudit audit_box(const Mesh& mesh, const InternalRegion& region) {
    const auto position = region.get_global_position();
    const auto size = region.get_size_m();
    BoxAudit audit;
    for(int i = 0; i < mesh.get_nx(); ++i) {
        const double x = mesh.cell_center_x(i);
        if(x < position[0] - 1.0e-12 ||
           x > position[0] + size[0] + 1.0e-12)
            continue;
        for(int j = 0; j < mesh.get_ny(); ++j) {
            const double y = mesh.cell_center_y(j);
            if(y < position[1] - 1.0e-12 ||
               y > position[1] + size[1] + 1.0e-12)
                continue;
            for(int k = 0; k < mesh.get_nz(); ++k) {
                const double z = mesh.cell_center_z(k);
                if(z < position[2] - 1.0e-12 ||
                   z > position[2] + size[2] + 1.0e-12)
                    continue;
                const Cell& cell = mesh.at(i, j, k);
                const double volume = cell.volume();
                audit.selected_volume += volume;
                if(cell.is_solid()) audit.solid_volume += volume;
                if(cell.get_state() == Cell::State::Air)
                    audit.air_volume += volume;
                if(cell.get_state() == Cell::State::Vent)
                    audit.vent_volume += volume;
                if(cell.get_state() == Cell::State::Fan)
                    audit.fan_volume += volume;
            }
        }
    }
    return audit;
}

bool has_boundary(const MeshRefinementPlan& plan, int axis,
                  double coordinate) {
    const std::vector<double>& widths =
        axis == 0 ? plan.dxs : (axis == 1 ? plan.dys : plan.dzs);
    double boundary = 0.0;
    if(close(boundary, coordinate, 5.0e-12)) return true;
    for(double width : widths) {
        boundary += width;
        if(close(boundary, coordinate, 5.0e-12)) return true;
    }
    return false;
}

std::array<std::vector<double>, 3> component_feature_cuts(
    const Component& component) {
    std::array<std::vector<double>, 3> cuts;
    const auto component_position = component.get_coords();
    const std::array<double, 3> component_size{
        component.get_width_m(), component.get_depth_m(),
        component.get_height_m()};
    for(int axis = 0; axis < 3; ++axis) {
        cuts[axis].push_back(component_position[axis]);
        cuts[axis].push_back(
            component_position[axis] + component_size[axis]);
    }
    for(const InternalRegion& region : component.get_regions()) {
        const auto position = region.get_global_position();
        const auto size = region.get_size_m();
        if(region.get_region_type() == RegionType::Fan ||
           region.get_region_type() == RegionType::Vent) {
            const auto direction = region.get_direction();
            const double ax = std::abs(direction[0]);
            const double ay = std::abs(direction[1]);
            const double az = std::abs(direction[2]);
            const int normal_axis =
                ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
            for(int axis = 0; axis < 3; ++axis) {
                if(axis == normal_axis) {
                    cuts[axis].push_back(position[axis]);
                } else {
                    const double half_extent = region.is_circular()
                        ? 0.5 * region.get_diameter()
                        : 0.5 * size[axis];
                    cuts[axis].push_back(position[axis] - half_extent);
                    cuts[axis].push_back(position[axis] + half_extent);
                }
            }
        } else {
            for(int axis = 0; axis < 3; ++axis) {
                cuts[axis].push_back(position[axis]);
                cuts[axis].push_back(position[axis] + size[axis]);
            }
        }
    }
    for(auto& axis_cuts : cuts) {
        std::sort(axis_cuts.begin(), axis_cuts.end());
        axis_cuts.erase(
            std::unique(
                axis_cuts.begin(), axis_cuts.end(),
                [](double left, double right) {
                    return close(left, right, 5.0e-12);
                }),
            axis_cuts.end());
    }
    return cuts;
}

bool retains_all_feature_cuts(
    const MeshRefinementPlan& plan,
    const std::array<std::vector<double>, 3>& cuts) {
    for(int axis = 0; axis < 3; ++axis)
        for(double coordinate : cuts[axis])
            if(!has_boundary(plan, axis, coordinate)) return false;
    return true;
}

struct StampResult {
    BoxAudit wall1;
    BoxAudit wall3;
};

StampResult stamp_and_audit(
    const Rack& rack, const MeshRefinementPlan& plan,
    const Environment& environment, const Component& component,
    const InternalRegion& wall1, const InternalRegion& wall3) {
    const std::size_t cell_count =
        plan.dxs.size() * plan.dys.size() * plan.dzs.size();
    require(cell_count <= kCellGuard,
            "NI mesh ladder exceeded its 175,000-cell resource guard");
    Workload workload(
        1, 2 * cell_count, static_cast<int>(cell_count), 128);
    Mesh mesh = Mesh().build_adaptive_mesh(
        rack, plan.dxs, plan.dys, plan.dzs, environment, workload);
    mesh.stamp_component_adaptive(component);
    return {audit_box(mesh, wall1), audit_box(mesh, wall3)};
}

struct LadderResult {
    double fine_dx = 0.0;
    std::size_t variant_cell_count = 0;
    std::size_t canonical_cell_count = 0;
    bool all_feature_cuts_retained = false;
    bool wall_thickness_cuts_retained = false;
    bool upper_2_5_mm_cut_retained = false;
    StampResult variant_full;
    StampResult variant_no_conflicting_vent;
    StampResult canonical_full;
    StampResult canonical_no_conflicting_vent;
    StampResult canonical_on_variant_plan;
    StampResult variant_on_canonical_plan;
};

} // namespace

int main() {
    try {
        ComponentLoader loader;
        loader.load_fan_curves("library/fan_curves/fan_curves.toml");
        loader.load_component(kComponentPath);
        ComponentLoader canonical_loader;
        canonical_loader.load_fan_curves(
            "library/fan_curves/fan_curves.toml");
        canonical_loader.load_component(kCanonicalComponentPath);

        const std::array<double, 3> component_position{0.07, 0.12, 0.06};
        const Component full_component = build_component(
            loader.model, loader.fan_curve_library, component_position);
        const Component no_conflicting_vent = build_component(
            loader.model, loader.fan_curve_library, component_position,
            kConflictingVentName);
        const Component canonical_component = build_component(
            canonical_loader.model, canonical_loader.fan_curve_library,
            component_position);
        const Component canonical_no_conflicting_vent = build_component(
            canonical_loader.model, canonical_loader.fan_curve_library,
            component_position, kConflictingVentName);
        const InternalRegion wall1 = region_copy(full_component, kWall1Name);
        const InternalRegion wall3 = region_copy(full_component, kWall3Name);
        const InternalRegion canonical_wall1 =
            region_copy(canonical_component, kWall1Name);
        const InternalRegion conflicting_vent =
            region_copy(full_component, kConflictingVentName);
        const InternalRegion interior_air =
            region_copy(full_component, kInteriorAirName);
        const auto source_feature_cuts =
            component_feature_cuts(full_component);

        Rack rack = Rack::from_meters(
            0.50, 0.50, 0.30, "NI separator mesh-ladder fixture");
        rack.set_t(20.0);
        rack.set_cp(1005.5);
        rack.set_k(0.02587);
        rack.set_rho(0.9833);
        const Environment environment(
            30.0, 5500.0, 20.0, 1005.5,
            0.02587, 1.81e-5, 0.71, 0.9833);

        const double wall1_requested = wall1.volume();
        const double wall3_requested = wall3.volume();
        const auto wall1_position = wall1.get_global_position();
        const auto wall1_size = wall1.get_size_m();
        const auto wall3_position = wall3.get_global_position();
        const auto wall3_size = wall3.get_size_m();
        const auto vent_position = conflicting_vent.get_global_position();
        const auto vent_size = conflicting_vent.get_size_m();
        const auto interior_position = interior_air.get_global_position();
        const double projected_y_overlap = std::max(
            0.0,
            std::min(
                wall1_position[1] + wall1_size[1],
                vent_position[1] + 0.5 * vent_size[1]) -
            std::max(
                wall1_position[1],
                vent_position[1] - 0.5 * vent_size[1]));
        const double projected_z_overlap = std::max(
            0.0,
            std::min(
                wall1_position[2] + wall1_size[2],
                vent_position[2] + 0.5 * vent_size[2]) -
            std::max(
                wall1_position[2],
                vent_position[2] - 0.5 * vent_size[2]));
        const double projected_overlap_area =
            projected_y_overlap * projected_z_overlap;
        const double topology_overlap =
            wall1_size[0] * projected_overlap_area;
        const double wall3_projected_y_overlap = std::max(
            0.0,
            std::min(
                wall3_position[1] + wall3_size[1],
                vent_position[1] + 0.5 * vent_size[1]) -
            std::max(
                wall3_position[1],
                vent_position[1] - 0.5 * vent_size[1]));
        const double wall3_projected_z_overlap = std::max(
            0.0,
            std::min(
                wall3_position[2] + wall3_size[2],
                vent_position[2] + 0.5 * vent_size[2]) -
            std::max(
                wall3_position[2],
                vent_position[2] - 0.5 * vent_size[2]));
        const double wall3_tunnel_overlap =
            wall3_size[0] * wall3_projected_y_overlap *
            wall3_projected_z_overlap;
        require(close(wall1_requested, 0.000192),
                "Card Slot wall 1 source volume changed");
        require(close(canonical_wall1.volume(), wall1_requested),
                "canonical and provisional wall-1 source volumes differ");
        require(close(wall3_requested, 0.000124),
                "provisional Card Slot wall 3 source volume changed");
        // The vent is a zero-thickness x-normal plane, so it has no direct
        // source-volume intersection with wall 1. Canonical Interior air begins
        // at x=5 mm while wall 1 begins at x=10 mm. Wall 3 exactly fills that
        // 5 mm fluid corridor and makes the tunnel continue into wall 1.
        require(close(vent_position[0], component_position[0]) &&
                    close(interior_position[0],
                          component_position[0] + 0.005) &&
                    close(wall3_position[0], interior_position[0]) &&
                    close(wall3_position[0] + wall3_size[0],
                          wall1_position[0]),
                "the source-space vent/fluid/wall corridor changed");
        require(close(projected_overlap_area, 0.005 * 0.015),
                "vent/wall-1 projected overlap area changed");
        require(close(topology_overlap, 0.000018),
                "analytical extruded vent-tunnel/wall-1 overlap changed");
        require(close(wall3_tunnel_overlap, 0.000010185),
                "analytical vent-tunnel/wall-3 overlap changed");

        const std::vector<double> fine_spacings{
            0.0075, 0.0088, 0.0090, 0.0100,
            0.0150, 0.0190, 0.0200};
        std::vector<LadderResult> results;
        for(double fine_dx : fine_spacings) {
            const MeshRefinementPlan plan = MeshRefinementPlanner::plan(
                rack, {full_component}, {}, {}, fine_dx, 0.05, 0.02);
            const MeshRefinementPlan canonical_plan =
                MeshRefinementPlanner::plan(
                    rack, {canonical_component}, {}, {}, fine_dx,
                    0.05, 0.02);
            const std::size_t cell_count =
                plan.dxs.size() * plan.dys.size() * plan.dzs.size();
            const std::size_t canonical_cell_count =
                canonical_plan.dxs.size() * canonical_plan.dys.size() *
                canonical_plan.dzs.size();
            std::cout << "NI ladder plan: fine_dx_mm="
                      << 1000.0 * fine_dx << ", variant_cells="
                      << cell_count << ", canonical_cells="
                      << canonical_cell_count << '\n';
            require(cell_count <= kCellGuard,
                    "NI mesh ladder plan exceeded its resource guard");
            require(canonical_cell_count <= kCellGuard,
                    "canonical NI mesh ladder plan exceeded its resource guard");

            const bool wall_thickness_cuts_retained =
                has_boundary(plan, 1, wall1_position[1]) &&
                has_boundary(plan, 1,
                    wall1_position[1] + wall1.get_size_m()[1]) &&
                has_boundary(plan, 0, wall3_position[0]) &&
                has_boundary(plan, 0,
                    wall3_position[0] + wall3.get_size_m()[0]);
            // The upper edge of the 15 mm side vent is only 2.5 mm below
            // the modeled wall top (167.5 versus 170.0 mm local). This cut is
            // the first planner threshold reached as fine_dx is coarsened.
            const bool upper_2_5_mm_cut_retained = has_boundary(
                plan, 2, component_position[2] + 0.1700);

            results.push_back({
                fine_dx, cell_count, canonical_cell_count,
                retains_all_feature_cuts(plan, source_feature_cuts),
                wall_thickness_cuts_retained, upper_2_5_mm_cut_retained,
                stamp_and_audit(
                    rack, plan, environment, full_component, wall1, wall3),
                stamp_and_audit(
                    rack, plan, environment, no_conflicting_vent,
                    wall1, wall3),
                stamp_and_audit(
                    rack, canonical_plan, environment, canonical_component,
                    canonical_wall1, wall3),
                stamp_and_audit(
                    rack, canonical_plan, environment,
                    canonical_no_conflicting_vent, canonical_wall1, wall3),
                stamp_and_audit(
                    rack, plan, environment, canonical_component,
                    canonical_wall1, wall3),
                stamp_and_audit(
                    rack, canonical_plan, environment, full_component,
                    wall1, wall3)});
        }

        std::cout << std::setprecision(12);
        std::cout
            << "fine_dx_mm\tvariant_cells\tcanonical_cells\tall_source_cuts\t"
               "5mm_cuts\twall_top_cut\tvariant_wall1_m3\t"
               "variant_wall1_no_vent_m3\tvariant_vent_loss_m3\t"
               "canonical_wall1_m3\tcanonical_wall1_no_vent_m3\t"
               "canonical_on_variant_plan_m3\tvariant_on_canonical_plan_m3\t"
               "variant_wall3_m3\tvariant_wall3_no_vent_m3\n";
        for(const LadderResult& result : results) {
            std::cout
                << 1000.0 * result.fine_dx << '\t'
                << result.variant_cell_count << '\t'
                << result.canonical_cell_count << '\t'
                << (result.all_feature_cuts_retained ? "yes" : "no") << '\t'
                << (result.wall_thickness_cuts_retained ? "yes" : "no") << '\t'
                << (result.upper_2_5_mm_cut_retained ? "yes" : "no") << '\t'
                << result.variant_full.wall1.solid_volume << '\t'
                << result.variant_no_conflicting_vent.wall1.solid_volume << '\t'
                << (result.variant_no_conflicting_vent.wall1.solid_volume -
                    result.variant_full.wall1.solid_volume) << '\t'
                << result.canonical_full.wall1.solid_volume << '\t'
                << result.canonical_no_conflicting_vent.wall1.solid_volume << '\t'
                << result.canonical_on_variant_plan.wall1.solid_volume << '\t'
                << result.variant_on_canonical_plan.wall1.solid_volume << '\t'
                << result.variant_full.wall3.solid_volume << '\t'
                << result.variant_no_conflicting_vent.wall3.solid_volume
                << '\n';
        }

        for(const LadderResult& result : results) {
            require(result.wall_thickness_cuts_retained,
                    "a tested fine spacing failed to retain a 5 mm wall cut");
            require(close(
                        result.variant_no_conflicting_vent.wall1.solid_volume -
                            result.variant_full.wall1.solid_volume,
                        topology_overlap),
                    "top-right side vent did not remove the analytical "
                    "240 x 5 x 15 mm Card Slot wall 1 overlap");
            require(close(
                        result.variant_no_conflicting_vent.wall3.solid_volume -
                            result.variant_full.wall3.solid_volume,
                        wall3_tunnel_overlap),
                    "top-right side vent did not remove the analytical "
                    "5 x 135.8 x 15 mm provisional wall-3 overlap");
            require(result.variant_full.wall1.air_volume >=
                        topology_overlap - kVolumeTolerance,
                    "wall 1 overlap was not converted to a fluid tunnel");
            require(result.variant_no_conflicting_vent.wall1.solid_volume >
                        result.variant_full.wall1.solid_volume,
                    "vent-omission control did not restore wall 1 material");
            require(result.variant_no_conflicting_vent.wall3.solid_volume >
                        result.variant_full.wall3.solid_volume,
                    "vent-omission control did not restore wall 3 material");
            require(close(
                        result.canonical_full.wall1.solid_volume,
                        result.canonical_no_conflicting_vent.wall1.solid_volume),
                    "canonical vent unexpectedly removes wall-1 material");
            require(close(
                        result.canonical_on_variant_plan.wall1.solid_volume,
                        result.variant_no_conflicting_vent.wall1.solid_volume),
                    "variant planner alone changed canonical wall-1 retention");
            require(close(
                        result.canonical_full.wall1.solid_volume -
                            result.variant_on_canonical_plan.wall1.solid_volume,
                        topology_overlap),
                    "adding wall 3 on the canonical plan did not create the "
                    "analytical wall-1 tunnel loss");
        }

        require(results[0].all_feature_cuts_retained &&
                    results[1].all_feature_cuts_retained,
                "fine_dx <= 8.8 mm no longer retains every NI source cut");
        for(std::size_t index = 2; index < results.size(); ++index)
            require(!results[index].all_feature_cuts_retained,
                    "fine_dx > 8.8 mm unexpectedly retained every NI source cut");
        require(results[0].upper_2_5_mm_cut_retained &&
                    results[1].upper_2_5_mm_cut_retained &&
                    results[2].upper_2_5_mm_cut_retained &&
                    results[3].upper_2_5_mm_cut_retained,
                "fine_dx <= 10 mm no longer retains the 2.5 mm wall/vent cut gap");
        require(!results[4].upper_2_5_mm_cut_retained &&
                    !results[5].upper_2_5_mm_cut_retained &&
                    !results[6].upper_2_5_mm_cut_retained,
                "fine_dx > 10 mm unexpectedly retained the 2.5 mm cut gap");
        require(close(results[0].variant_no_conflicting_vent.wall1.solid_volume,
                      wall1_requested) &&
                    close(results[1].variant_no_conflicting_vent.wall1.solid_volume,
                          wall1_requested) &&
                    close(results[2].variant_no_conflicting_vent.wall1.solid_volume,
                          wall1_requested) &&
                    close(results[3].variant_no_conflicting_vent.wall1.solid_volume,
                          wall1_requested),
                "fine_dx <= 10 mm did not preserve wall 1 in the vent-omission control");
        require(close(results[0].variant_full.wall1.solid_volume,
                      wall1_requested - topology_overlap) &&
                    close(results[1].variant_full.wall1.solid_volume,
                          wall1_requested - topology_overlap) &&
                    close(results[2].variant_full.wall1.solid_volume,
                          wall1_requested - topology_overlap) &&
                    close(results[3].variant_full.wall1.solid_volume,
                          wall1_requested - topology_overlap),
                "fine_dx <= 10 mm did not isolate the topology-overlap loss");

        std::cout
            << "ni_separator_mesh_ladder_test PASSED: all NI source cuts are "
               "retained through 8.8 mm fine_dx and the 2.5 mm vent/wall-top "
               "pair through 10 mm. Canonical wall 1 loses 0 m^3 with or "
               "without the vent on either plan. Adding wall 3 seals the "
               "canonical 5 mm fluid corridor, so Top right side vent carves "
               "the invariant 0.000018 m^3 wall-1 tunnel on either plan; no "
               "tested mesh preserves the stamped provisional geometry.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "ni_separator_mesh_ladder_test FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}
