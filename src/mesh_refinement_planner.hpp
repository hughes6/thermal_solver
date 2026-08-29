#ifndef MESH_REFINEMENT_PLANNER_HPP
#define MESH_REFINEMENT_PLANNER_HPP

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
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
                                   bool align_internal_geometry = true,
                                   std::size_t maximum_axis_cells =
                                       static_cast<std::size_t>(
                                           std::numeric_limits<int>::max())-
                                       1u) {
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
        if(maximum_axis_cells < 1u)
            throw std::invalid_argument(
                "Adaptive mesh maximum_axis_cells must be >= 1.");
        maximum_axis_cells=std::min(
            maximum_axis_cells,
            static_cast<std::size_t>(
                std::numeric_limits<int>::max())-1u);
        const double extents[3]={
            rack.get_width_m(),rack.get_depth_m(),rack.get_height_m()};
        for(int axis=0;axis<3;++axis)
            if(!std::isfinite(extents[axis]) || extents[axis] <= 0.0)
                throw std::invalid_argument(
                    "Adaptive mesh rack extents must be finite and positive.");

        MeshRefinementPlan out;
        out.dxs = plan_axis(
            0,rack.get_width_m(),components,fans,vents,
            fine_dx,coarse_dx,margin,align_internal_geometry,
            maximum_axis_cells);
        out.dys = plan_axis(
            1,rack.get_depth_m(),components,fans,vents,
            fine_dx,coarse_dx,margin,align_internal_geometry,
            maximum_axis_cells);
        out.dzs = plan_axis(
            2,rack.get_height_m(),components,fans,vents,
            fine_dx,coarse_dx,margin,align_internal_geometry,
            maximum_axis_cells);
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
        bool align_internal_geometry,
        std::size_t maximum_axis_cells) {
        // Face-wall coarse meshes intentionally do not honor exact geometry
        // cuts. A globally regular grid guarantees that nearby component
        // boundaries cannot create microscopic remainder/sliver cells.
        // Component walls and openings are snapped to the nearest resulting
        // face by the face-wall stamper.
        if(!align_internal_geometry) {
            const std::size_t count=checked_partition_count(
                extent,fine_dx,maximum_axis_cells,
                "unaligned adaptive axis");
            return std::vector<double>(
                count,extent/static_cast<double>(count));
        }

        std::vector<std::pair<double, double>> bands;
        // OpenFOAM's cell-determinant check becomes singular for a material
        // layer that is only one cell thick between region boundaries. Keep
        // reduced-order chassis walls, heat blocks, and their surrounding air
        // gaps at a minimum of two cells without refining the whole rack.
        std::vector<std::pair<double,double>> minimum_two_cell_spans;
        struct PrioritizedCut {
            double coordinate;
            int priority;
        };
        // Rack bounds are immutable, component envelopes define the material
        // regions, and internal features/openings are fitted inside them.
        // Keeping this priority explicit prevents a nearby lower-priority
        // feature from erasing a component face during sliver suppression.
        constexpr int rack_cut_priority=3;
        constexpr int component_cut_priority=2;
        constexpr int feature_cut_priority=1;
        std::vector<PrioritizedCut> prioritized_cuts{
            {0.0,rack_cut_priority},{extent,rack_cut_priority}};

        auto add_cut = [&](double coordinate,int priority) {
            if (!std::isfinite(coordinate)) return;
            prioritized_cuts.push_back(
                {std::clamp(coordinate,0.0,extent),priority});
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
            add_cut(component_min,component_cut_priority);
            add_cut(component_max,component_cut_priority);

            if (align_internal_geometry) {
            std::vector<std::pair<double,double>> air_spans;
            std::vector<std::pair<double,double>> heat_source_spans;
            for (const InternalRegion& region : component.get_regions()) {
                const auto position = region.get_global_position();
                const auto region_size = region.get_size_m();

                if (region.get_region_type() == RegionType::Air ||
                    region.get_region_type() == RegionType::HeatSource) {
                    add_cut(position[axis],feature_cut_priority);
                    add_cut(position[axis]+region_size[axis],feature_cut_priority);
                    auto& spans=region.get_region_type()==RegionType::Air
                        ? air_spans : heat_source_spans;
                    spans.push_back(
                        {position[axis],position[axis]+region_size[axis]});
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
                    add_cut(position[axis],feature_cut_priority);
                } else if (region.is_circular()) {
                    const double radius = region.get_diameter() / 2.0;
                    add_cut(position[axis]-radius,feature_cut_priority);
                    add_cut(position[axis]+radius,feature_cut_priority);
                } else {
                    // Rectangular fan/vent positions are centers.
                    const double half_extent = region_size[axis] / 2.0;
                    add_cut(position[axis]-half_extent,feature_cut_priority);
                    add_cut(position[axis]+half_extent,feature_cut_priority);
                }
            }
            constexpr double containment_tolerance=1e-12;
            for(const auto& air : air_spans) {
                if(air.first>component_min+containment_tolerance)
                    minimum_two_cell_spans.push_back(
                        {component_min,air.first});
                if(air.second<component_max-containment_tolerance)
                    minimum_two_cell_spans.push_back(
                        {air.second,component_max});
                for(const auto& heat_source : heat_source_spans) {
                    if(heat_source.first<air.first-containment_tolerance ||
                       heat_source.second>air.second+containment_tolerance)
                        continue;
                    if(heat_source.first>air.first+containment_tolerance)
                        minimum_two_cell_spans.push_back(
                            {air.first,heat_source.first});
                    if(heat_source.second<air.second-containment_tolerance)
                        minimum_two_cell_spans.push_back(
                            {heat_source.second,air.second});
                }
            }
            minimum_two_cell_spans.insert(
                minimum_two_cell_spans.end(),heat_source_spans.begin(),
                heat_source_spans.end());
            }
        }

        for (const auto& fan : fans) {
            double lo, hi;
            const Footprint footprint=Footprint::from_fan(fan);
            footprint_extent(footprint, axis, lo, hi);
            // A full-span opening has no interior tangential edge to resolve:
            // its two edges already coincide with immutable rack boundaries.
            // Refining that entire axis only because a full-face inlet/outlet
            // covers it turns otherwise local refinement into a global mesh.
            const double span_tolerance=
                1e-9*std::max(1.0,std::abs(extent));
            const bool redundant_full_span=
                axis!=footprint.normal_axis &&
                lo<=span_tolerance && hi>=extent-span_tolerance;
            if(!redundant_full_span) {
                bands.push_back({
                    std::max(0.0, lo - margin),
                    std::min(extent, hi + margin)
                });
            }
            add_cut(lo,feature_cut_priority);
            add_cut(hi,feature_cut_priority);
        }

        for (const auto& vent : vents) {
            double lo, hi;
            const Footprint footprint=Footprint::from_vent(vent);
            footprint_extent(footprint, axis, lo, hi);
            const double span_tolerance=
                1e-9*std::max(1.0,std::abs(extent));
            const bool redundant_full_span=
                axis!=footprint.normal_axis &&
                lo<=span_tolerance && hi>=extent-span_tolerance;
            if(!redundant_full_span) {
                bands.push_back({
                    std::max(0.0, lo - margin),
                    std::min(extent, hi + margin)
                });
            }
            add_cut(lo,feature_cut_priority);
            add_cut(hi,feature_cut_priority);
        }

        constexpr double cut_eps = 1e-12;
        // Exact feature cuts that land almost on top of another cut create
        // rack-wide rectilinear sliver planes. Those planes produced 1.1 mm
        // cells in a nominal 20 mm screening mesh, aspect ratios near 178,
        // and low OpenFOAM interpolation weights. Snap cuts closer than one
        // quarter of the requested fine spacing; the geometric error remains
        // bounded by 0.25*fine_dx while avoiding pathological cells.
        const double minimum_interval = 0.25*fine_dx;
        std::sort(prioritized_cuts.begin(),prioritized_cuts.end(),
            [](const PrioritizedCut& a,const PrioritizedCut& b) {
                if(a.priority!=b.priority) return a.priority>b.priority;
                return a.coordinate<b.coordinate;
            });
        std::vector<double> cuts;
        for(const PrioritizedCut& candidate : prioritized_cuts) {
            bool separated=true;
            for(double accepted : cuts) {
                if(std::abs(candidate.coordinate-accepted)+cut_eps<
                   minimum_interval) {
                    separated=false;
                    break;
                }
            }
            if(separated) cuts.push_back(candidate.coordinate);
        }
        std::sort(cuts.begin(),cuts.end());

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
        // Required geometry cuts take priority over refinement-band edges.
        // The previous sorted first-come deduplication allowed a band edge to
        // suppress a nearby component or internal-region boundary. Changing
        // only refinement_margin could therefore change component volume.
        std::vector<double> clean_breakpoints=cuts;
        for(const auto& band : merged) {
            for(double point : {band.first,band.second}) {
                point=std::clamp(point,0.0,extent);
                bool separated=true;
                for(double required : clean_breakpoints) {
                    if(std::abs(point-required)+cut_eps<minimum_interval) {
                        separated=false;
                        break;
                    }
                }
                if(separated) clean_breakpoints.push_back(point);
            }
        }
        std::sort(clean_breakpoints.begin(),clean_breakpoints.end());

        // Material boundaries may be snapped to a nearby higher-priority
        // global cut (for example, one component's air boundary can lie near
        // another component's outer face). Apply the two-cell guarantee to
        // those realized breakpoints, not the pre-snap coordinates, or the
        // stamper can create a one-cell material strip even though the
        // original span was marked as protected.
        auto nearest_breakpoint = [&](double coordinate) {
            auto upper=std::lower_bound(
                clean_breakpoints.begin(),clean_breakpoints.end(),coordinate);
            if(upper==clean_breakpoints.begin()) return *upper;
            if(upper==clean_breakpoints.end()) return clean_breakpoints.back();
            const double lower=*std::prev(upper);
            return coordinate-lower<=*upper-coordinate+cut_eps
                ? lower : *upper;
        };
        std::vector<std::pair<double,double>> snapped_two_cell_spans;
        for(const auto& span : minimum_two_cell_spans) {
            const double begin=nearest_breakpoint(span.first);
            const double end=nearest_breakpoint(span.second);
            if(end-begin>cut_eps)
                snapped_two_cell_spans.push_back({begin,end});
        }

        std::vector<double> widths;
        for(std::size_t i=1;i<clean_breakpoints.size();++i) {
            const double begin=clean_breakpoints[i-1];
            const double end=clean_breakpoints[i];
            const double length=end-begin;
            if(length<=cut_eps) continue;
            const double target=
                is_fine(0.5*(begin+end)) ? fine_dx : coarse_dx;
            std::size_t count=checked_partition_count(
                length,target,maximum_axis_cells,
                "adaptive interval");
            const bool requires_two_cells=std::any_of(
                snapped_two_cell_spans.begin(),snapped_two_cell_spans.end(),
                [&](const auto& span) {
                    return std::abs(begin-span.first)<cut_eps &&
                           std::abs(end-span.second)<cut_eps;
                });
            if(requires_two_cells)
                count=std::max(count,std::size_t{2});
            require_axis_capacity(
                widths.size(),count,maximum_axis_cells,
                "adaptive interval insertion");
            const double width=length/static_cast<double>(count);
            widths.insert(widths.end(),count,width);
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
                const std::size_t pieces=std::max(
                    checked_partition_count(
                        widths[large_index],
                        maximum_adjacent_ratio*smaller,
                        maximum_axis_cells,
                        "adaptive transition smoothing"),
                    std::size_t{2});
                require_axis_capacity(
                    widths.size()-1u,pieces,maximum_axis_cells,
                    "adaptive transition smoothing insertion");
                const double piece=widths[large_index]/pieces;
                widths.erase(widths.begin()+large_index);
                widths.insert(
                    widths.begin()+large_index,
                    pieces,piece);
                changed=true;
                break;
            }
        }

        if (widths.empty()) widths.push_back(extent);
        return widths;
    }

    static std::size_t checked_partition_count(
        double extent,
        double target,
        std::size_t maximum_axis_cells,
        const char* context) {
        if(!std::isfinite(extent) || extent <= 0.0 ||
           !std::isfinite(target) || target <= 0.0)
            throw std::invalid_argument(
                std::string("Adaptive mesh ")+context+
                " requires finite positive extent and spacing.");
        const double raw=std::ceil(extent/target);
        if(!std::isfinite(raw) || raw < 1.0 ||
           raw > static_cast<double>(maximum_axis_cells))
            throw std::overflow_error(
                std::string("Adaptive mesh ")+context+
                " exceeds the configured axis-cell limit before allocation.");
        return static_cast<std::size_t>(raw);
    }

    static void require_axis_capacity(
        std::size_t existing,
        std::size_t added,
        std::size_t maximum_axis_cells,
        const char* context) {
        if(existing > maximum_axis_cells ||
           added > maximum_axis_cells-existing)
            throw std::overflow_error(
                std::string("Adaptive mesh ")+context+
                " exceeds the configured axis-cell limit before allocation.");
    }
};

#endif
