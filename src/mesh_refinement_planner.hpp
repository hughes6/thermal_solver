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
                                   double margin,
                                   bool align_internal_geometry = true) {
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
                            fine_dx, coarse_dx, margin, align_internal_geometry);
        out.dys = plan_axis(1, rack.get_depth_m(), components, fans, vents,
                            fine_dx, coarse_dx, margin, align_internal_geometry);
        out.dzs = plan_axis(2, rack.get_height_m(), components, fans, vents,
                            fine_dx, coarse_dx, margin, align_internal_geometry);
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
        double fine_dx, double coarse_dx, double margin,
                                   bool align_internal_geometry = true) {
        // Face-wall coarse meshes intentionally do not honor exact geometry
        // cuts. A globally regular grid guarantees that nearby component
        // boundaries cannot create microscopic remainder/sliver cells.
        // Component walls and openings are snapped to the nearest resulting
        // face by the face-wall stamper.
        if(!align_internal_geometry) {
            const int count = std::max(
                1, static_cast<int>(std::ceil(extent / fine_dx)));
            return std::vector<double>(
                static_cast<size_t>(count),
                extent / static_cast<double>(count));
        }

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

            if (align_internal_geometry) {
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
        // Exact feature cuts that land almost on top of another cut create
        // rack-wide rectilinear sliver planes. Those planes produced 1.1 mm
        // cells in a nominal 20 mm screening mesh, aspect ratios near 178,
        // and low OpenFOAM interpolation weights. Snap cuts closer than one
        // quarter of the requested fine spacing; the geometric error remains
        // bounded by 0.25*fine_dx while avoiding pathological cells.
        const double minimum_interval = 0.25*fine_dx;
        std::vector<double> unique_cuts;
        for (double cut : cuts) {
            if (unique_cuts.empty() ||
                std::abs(cut - unique_cuts.back()) >= minimum_interval) {
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

        // Partition every interval evenly instead of greedily laying down
        // target-sized cells and leaving a microscopic remainder before an
        // exact feature plane.
        std::vector<double> breakpoints=cuts;
        for(const auto& band : merged) {
            breakpoints.push_back(band.first);
            breakpoints.push_back(band.second);
        }
        std::sort(breakpoints.begin(),breakpoints.end());
        std::vector<double> clean_breakpoints;
        for(double point : breakpoints) {
            point=std::clamp(point,0.0,extent);
            if(clean_breakpoints.empty() ||
               point-clean_breakpoints.back()>=minimum_interval)
                clean_breakpoints.push_back(point);
        }
        if(clean_breakpoints.empty() ||
           clean_breakpoints.front()>cut_eps)
            clean_breakpoints.insert(clean_breakpoints.begin(),0.0);
        if(extent-clean_breakpoints.back()<minimum_interval)
            clean_breakpoints.back()=extent;
        else if(extent-clean_breakpoints.back()>cut_eps)
            clean_breakpoints.push_back(extent);

        std::vector<double> widths;
        for(std::size_t i=1;i<clean_breakpoints.size();++i) {
            const double begin=clean_breakpoints[i-1];
            const double end=clean_breakpoints[i];
            const double length=end-begin;
            if(length<=cut_eps) continue;
            const double target=
                is_fine(0.5*(begin+end)) ? fine_dx : coarse_dx;
            const int count=std::max(
                1,static_cast<int>(std::ceil(length/target)));
            const double width=length/static_cast<double>(count);
            widths.insert(
                widths.end(),static_cast<std::size_t>(count),width);
        }

        // Smooth abrupt transitions without refining the entire coarse
        // domain. A tiny feature-aligned interval directly beside a coarse
        // interval otherwise creates low interpolation weights in OpenFOAM.
        // Split only the larger neighbor until every adjacent ratio is at
        // most four; repeat because a split can expose the next transition.
        constexpr double maximum_adjacent_ratio=4.0;
        bool changed=true;
        while(changed) {
            changed=false;
            for(std::size_t i=1;i<widths.size();++i) {
                const double smaller=std::min(widths[i-1],widths[i]);
                const double larger=std::max(widths[i-1],widths[i]);
                if(larger<=maximum_adjacent_ratio*smaller+cut_eps) continue;
                const std::size_t large_index=
                    widths[i-1]>widths[i] ? i-1 : i;
                const int pieces=std::max(
                    2,static_cast<int>(std::ceil(
                        widths[large_index]/
                        (maximum_adjacent_ratio*smaller))));
                const double piece=widths[large_index]/pieces;
                widths.erase(widths.begin()+large_index);
                widths.insert(
                    widths.begin()+large_index,
                    static_cast<std::size_t>(pieces),piece);
                changed=true;
                break;
            }
        }

        if (widths.empty()) widths.push_back(extent);
        return widths;
    }
};

#endif
