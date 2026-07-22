#ifndef THERMAL_SOLVER_COLLISION_HPP
#define THERMAL_SOLVER_COLLISION_HPP

#include <array>
#include <cmath>
#include <string>
#include <stdexcept>
#include <vector>

#include "component.hpp"
#include "fan.hpp"
#include "vent.hpp"

// -------------------------------------------------------------
// AABB
// -------------------------------------------------------------
// Pure geometry, no mesh knowledge at all. min/max are in meters,
// in the same global coordinate frame as Component/Fan/Vent.
struct AABB {
    std::array<double, 3> min{0.0, 0.0, 0.0};
    std::array<double, 3> max{0.0, 0.0, 0.0};
    std::string owner_name;   // for error messages
    std::string owner_kind;   // "Component" / "Fan" / "Vent"

    bool overlaps(const AABB& other, double eps = 1e-6) const {
        return (min[0] < other.max[0] - eps) && (max[0] > other.min[0] + eps) &&
               (min[1] < other.max[1] - eps) && (max[1] > other.min[1] + eps) &&
               (min[2] < other.max[2] - eps) && (max[2] > other.min[2] + eps);
    }

    // -----------------------------------------------------------
    // Factories - one per stampable geometry type
    // -----------------------------------------------------------
    static AABB from_component(const Component& c) {
        auto corner = c.get_coords(); // bottom-left corner {x,y,z}
        AABB box;
        box.min = corner;
        box.max = {
            corner[0] + c.get_width_m(),
            corner[1] + c.get_depth_m(),
            corner[2] + c.get_height_m()
        };
        box.owner_name = c.get_name();
        box.owner_kind = "Component";
        return box;
    }

    // Fans/vents are openings on a surface, not volumes. Give them a thin
    // slab in the plane they actually occupy (perpendicular to direction),
    // with a small fixed physical thickness - NOT tied to mesh spacing.
    static AABB from_footprint(const std::array<double,3>& center,
                               const std::array<double,3>& direction,
                               const std::array<double,3>& rect_size, // 0 for circular
                               double diameter,                       // 0 for rectangular
                               bool is_circular,
                               const std::string& name,
                               const std::string& kind,
                               double slab_thickness = 1e-4) {
        double ax = std::abs(direction[0]);
        double ay = std::abs(direction[1]);
        double az = std::abs(direction[2]);

        double hx, hy, hz; // half-extents

        if (is_circular) {
            double r = diameter / 2.0;
            hx = hy = hz = r;
        } else {
            hx = rect_size[0] / 2.0;
            hy = rect_size[1] / 2.0;
            hz = rect_size[2] / 2.0;
        }

        double half_thick = slab_thickness / 2.0;

        // collapse whichever axis the normal points along down to slab thickness
        if (az >= ax && az >= ay) {
            hz = half_thick;
        } else if (ay >= ax && ay >= az) {
            hy = half_thick;
        } else {
            hx = half_thick;
        }

        AABB box;
        box.min = { center[0]-hx, center[1]-hy, center[2]-hz };
        box.max = { center[0]+hx, center[1]+hy, center[2]+hz };
        box.owner_name = name;
        box.owner_kind = kind;
        return box;
    }

    static AABB from_fan(const Fan& f) {
        return from_footprint(
            f.get_center(), f.get_velocity_dir(),
            f.get_size_m(), f.get_diameter(), f.is_circular(),
            f.get_name(), "Fan"
        );
    }

    static AABB from_vent(const Vent& v) {
        return from_footprint(
            v.get_center(), v.get_direction(),
            v.get_size_m(), v.get_diameter(), v.is_circular(),
            v.get_name(), "Vent"
        );
    }
};

// -------------------------------------------------------------
// CollisionChecker
// -------------------------------------------------------------
// Runs once, at model-assembly time, before the mesh is built.
// Mesh stamping should be able to assume everything passed here
// is already geometrically valid.
struct CollisionChecker {

    static void check_all(const std::vector<Component>& components,
                           const std::vector<Fan>& fans,
                           const std::vector<Vent>& vents) {

        std::vector<AABB> boxes;
        boxes.reserve(components.size() + fans.size() + vents.size());

        for (const auto& c : components) boxes.push_back(AABB::from_component(c));
        for (const auto& f : fans)       boxes.push_back(AABB::from_fan(f));
        for (const auto& v : vents)      boxes.push_back(AABB::from_vent(v));

        for (size_t i = 0; i < boxes.size(); ++i) {
            for (size_t j = i + 1; j < boxes.size(); ++j) {
                if (boxes[i].overlaps(boxes[j])) {
                    throw std::runtime_error(
                        boxes[i].owner_kind + " '" + boxes[i].owner_name +
                        "' overlaps " +
                        boxes[j].owner_kind + " '" + boxes[j].owner_name + "'."
                    );
                }
            }
        }
    }
};

#endif