#ifndef MESH_REFINEMENT_PLANNER_HPP
#define MESH_REFINEMENT_PLANNER_HPP

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "collision.hpp"
#include "component.hpp"
#include "fan.hpp"
#include "rack.hpp"
#include "vent.hpp"

struct MeshRefinementPlan {
    std::vector<double> dxs, dys, dzs;
};

// Builds a rectilinear mesh with fine cells around components and rack
// openings. Exact geometry cuts prevent cells from crossing component,
// internal-region, fan, and vent boundaries.
struct MeshRefinementPlanner {
    static MeshRefinementPlan plan(const Rack& rack,
                                   const std::vector<Component>& components,
                                   const std::vector<Fan>& fans,
                                   const std::vector<Vent>& vents,
                                   double fine_dx,
                                   double coarse_dx,
                                   double margin) {
        if (!std::isfinite(fine_dx) || !std::isfinite(coarse_dx) ||
            !std::isfinite(margin) || fine_dx <= 0.0 ||
            coarse_dx <= 0.0 || margin < 0.0) {
            throw std::invalid_argument(
                "Adaptive mesh spacing must be positive and its refinement margin non-negative.");
        }
        if (fine_dx > coarse_dx) {
            throw std::invalid_argument(
                "Adaptive mesh fine spacing cannot exceed coarse spacing.");
        }

        MeshRefinementPlan out;
        out.dxs = plan_axis(0, rack.get_width_m(), components, fans, vents,
                            fine_dx, coarse_dx, margin);
        out.dys = plan_axis(1, rack.get_depth_m(), components, fans, vents,
                            fine_dx, coarse_dx, margin);
        out.dzs = plan_axis(2, rack.get_height_m(), components, fans, vents,
                            fine_dx, coarse_dx, margin);
        return out;
    }

private:
    static void footprint_extent(const Footprint& fp, int axis,
                                 double& lo, double& hi) {
        if (axis == fp.normal_axis) {
            lo = hi = fp.mount_coord;
            return;
        }
        const auto axes = fp.plane_axes();
        const int k = (axes[0] == axis) ? 0 : 1;
        lo = fp.center2[k] - fp.half2[k];
        hi = fp.center2[k] + fp.half2[k];
    }

    static std::vector<double> plan_axis(
        int axis, double extent,
        const std::vector<Component>& components,
        const std::vector<Fan>& fans,
        const std::vector<Vent>& vents,
        double fine_dx, double coarse_dx, double margin) {
        std::vector<std::pair<double, double>> bands;
        std::vector<double> cuts{0.0, extent};

        auto add_cut = [&](double coordinate) {
            if (!std::isfinite(coordinate)) return;
            cuts.push_back(std::clamp(coordinate, 0.0, extent));
        };

        for (const auto& component : components) {
            const auto corner = component.get_coords();
            const double sizes[3] = {
                component.get_width_m(),
                component.get_depth_m(),
                component.get_height_m()
            };
            const double component_min = corner[axis];
            const double component_max = corner[axis] + sizes[axis];

            bands.push_back({
                std::max(0.0, component_min - margin),
                std::min(extent, component_max + margin)
            });
            add_cut(component_min);
            add_cut(component_max);

            for (const InternalRegion& region : component.get_regions()) {
                const auto position = region.get_global_position();
                const auto region_size = region.get_size_m();

                if (region.get_region_type() == RegionType::Air ||
                    region.get_region_type() == RegionType::HeatSource) {
                    add_cut(position[axis]);
                    add_cut(position[axis] + region_size[axis]);
                    continue;
                }

                if (region.get_region_type() != RegionType::Fan &&
                    region.get_region_type() != RegionType::Vent) {
                    continue;
                }

                const auto direction = region.get_direction();
                const double ax = std::abs(direction[0]);
                const double ay = std::abs(direction[1]);
                const double az = std::abs(direction[2]);
                const int normal_axis =
                    ax >= ay && ax >= az ? 0 :
                    ay >= az ? 1 : 2;

                if (axis == normal_axis) {
                    add_cut(position[axis]);
                } else if (region.is_circular()) {
                    const double radius = region.get_diameter() / 2.0;
                    add_cut(position[axis] - radius);
                    add_cut(position[axis] + radius);
                } else {
                    // Rectangular fan/vent positions are centers.
                    const double half_extent = region_size[axis] / 2.0;
                    add_cut(position[axis] - half_extent);
                    add_cut(position[axis] + half_extent);
                }
            }
        }

        for (const auto& fan : fans) {
            double lo, hi;
            footprint_extent(Footprint::from_fan(fan), axis, lo, hi);
            bands.push_back({
                std::max(0.0, lo - margin),
                std::min(extent, hi + margin)
            });
            add_cut(lo);
            add_cut(hi);
        }

        for (const auto& vent : vents) {
            double lo, hi;
            footprint_extent(Footprint::from_vent(vent), axis, lo, hi);
            bands.push_back({
                std::max(0.0, lo - margin),
                std::min(extent, hi + margin)
            });
            add_cut(lo);
            add_cut(hi);
        }

        std::sort(cuts.begin(), cuts.end());
        constexpr double cut_eps = 1e-12;
        std::vector<double> unique_cuts;
        for (double cut : cuts) {
            if (unique_cuts.empty() ||
                std::abs(cut - unique_cuts.back()) > cut_eps) {
                unique_cuts.push_back(cut);
            }
        }
        cuts.swap(unique_cuts);

        std::sort(bands.begin(), bands.end());
        std::vector<std::pair<double, double>> merged;
        for (const auto& band : bands) {
            if (!merged.empty() && band.first <= merged.back().second) {
                merged.back().second = std::max(merged.back().second, band.second);
            } else {
                merged.push_back(band);
            }
        }

        auto is_fine = [&merged](double coordinate) {
            for (const auto& band : merged) {
                if (coordinate >= band.first && coordinate < band.second) return true;
            }
            return false;
        };

        std::vector<double> widths;
        double position = 0.0;
        while (position < extent - cut_eps) {
            double width = is_fine(position) ? fine_dx : coarse_dx;

            // Stop at refinement-band boundaries.
            for (const auto& band : merged) {
                if (band.first > position + cut_eps)
                    width = std::min(width, band.first - position);
                if (band.second > position + cut_eps)
                    width = std::min(width, band.second - position);
            }

            // Stop at exact component/internal-region boundaries.
            for (double cut : cuts) {
                if (cut > position + cut_eps) {
                    width = std::min(width, cut - position);
                    break;
                }
            }

            width = std::min(width, extent - position);
            if (width <= cut_eps) {
                throw std::runtime_error(
                    "MeshRefinementPlanner generated a zero-width cell.");
            }
            widths.push_back(width);
            position += width;
        }

        if (widths.empty()) widths.push_back(extent);
        return widths;
    }
};

#endif