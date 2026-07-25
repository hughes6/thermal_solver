#ifndef MESH_REFINEMENT_PLANNER_HPP
#define MESH_REFINEMENT_PLANNER_HPP

#include <algorithm>
#include <cmath>
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
// openings, and coarse cells elsewhere. Mesh and the solvers only consume
// the resulting widths; refinement policy remains isolated here.
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

        for (const auto& component : components) {
            const auto corner = component.get_coords();
            const double sizes[3] = {
                component.get_width_m(),
                component.get_depth_m(),
                component.get_height_m()
            };
            bands.push_back({
                std::max(0.0, corner[axis] - margin),
                std::min(extent, corner[axis] + sizes[axis] + margin)
            });
        }
        for (const auto& fan : fans) {
            double lo, hi;
            footprint_extent(Footprint::from_fan(fan), axis, lo, hi);
            bands.push_back({
                std::max(0.0, lo - margin),
                std::min(extent, hi + margin)
            });
        }
        for (const auto& vent : vents) {
            double lo, hi;
            footprint_extent(Footprint::from_vent(vent), axis, lo, hi);
            bands.push_back({
                std::max(0.0, lo - margin),
                std::min(extent, hi + margin)
            });
        }

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
        while (position < extent - 1e-12) {
            double width = is_fine(position) ? fine_dx : coarse_dx;

            // Stop at a refinement boundary rather than stepping across it.
            // This preserves the intended fine band and gives stamping exact,
            // deterministic coordinate boundaries.
            for (const auto& band : merged) {
                if (band.first > position + 1e-12)
                    width = std::min(width, band.first - position);
                if (band.second > position + 1e-12)
                    width = std::min(width, band.second - position);
            }
            width = std::min(width, extent - position);
            widths.push_back(width);
            position += width;
        }
        if (widths.empty()) widths.push_back(extent);
        return widths;
    }
};

#endif
