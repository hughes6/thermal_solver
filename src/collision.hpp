#ifndef THERMAL_SOLVER_COLLISION_HPP
#define THERMAL_SOLVER_COLLISION_HPP

#include <array>
#include <cmath>
#include <string>
#include <stdexcept>
#include <vector>

#include "component.hpp"
#include "fan.hpp"
#include "rack.hpp"
#include "vent.hpp"

namespace geom {

    constexpr double TOUCH_EPS  = 1e-6; // "just touching" tolerance for 2D/3D overlap
    constexpr double FACE_TOL   = 1e-6; // how close to a component face still counts as "on" it
    constexpr double PLANE_TOL  = 1e-4; // how close two openings' mount coords must be to call them "on the same plane" (0.1mm)

    inline bool overlaps_1d(double minA, double maxA, double minB, double maxB, double eps = TOUCH_EPS) {
        return (minA < maxB - eps) && (maxA > minB + eps);
    }

    // Index of the axis a direction/normal vector mostly points along.
    // Ties break z > y > x, matching the convention already used in
    // mesh.hpp/grapher.hpp's stamp_fan/stamp_vent plane selection.
    inline int normal_axis(const std::array<double,3>& dir) {
        double ax = std::abs(dir[0]);
        double ay = std::abs(dir[1]);
        double az = std::abs(dir[2]);
        if (az >= ax && az >= ay) return 2;
        if (ay >= ax && ay >= az) return 1;
        return 0;
    }

} // namespace geom

// -------------------------------------------------------------
// AABB - full 3D volumetric box. Used for Component vs Component only:
// components are genuine solids, so ordinary box-overlap is correct.
// -------------------------------------------------------------
struct AABB {
    std::array<double, 3> min{0.0, 0.0, 0.0};
    std::array<double, 3> max{0.0, 0.0, 0.0};
    std::string owner_name;
    std::string owner_kind;

    bool overlaps(const AABB& other, double eps = geom::TOUCH_EPS) const {
        return geom::overlaps_1d(min[0], max[0], other.min[0], other.max[0], eps) &&
               geom::overlaps_1d(min[1], max[1], other.min[1], other.max[1], eps) &&
               geom::overlaps_1d(min[2], max[2], other.min[2], other.max[2], eps);
    }

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
};

// -------------------------------------------------------------
// Footprint - a Fan or Vent opening, represented the way it actually is:
// a flat 2D shape (rect or circle) living on a plane perpendicular to
// its own normal/direction vector, at one coordinate along that axis.
//
// This is what fixes the "fan flush on a component face reads as an
// overlap" problem: a flush-mounted opening's mount coordinate sits
// exactly AT the component's face, not inside its volume, and that
// distinction is checked explicitly rather than via a slab that
// straddles the boundary.
// -------------------------------------------------------------
struct Footprint {
    int normal_axis;         // 0=x, 1=y, 2=z - the axis the opening faces
    double mount_coord;      // position along normal_axis
    double center2[2];       // center along the other two axes (in original x/y/z order, normal_axis entry unused)
    double half2[2];         // half-extents along those same two axes (bounding square for circular)
    std::string owner_name;
    std::string owner_kind;  // "Fan" / "Vent"

    // in-plane axis indices, in a fixed order excluding normal_axis
    std::array<int,2> plane_axes() const {
        switch (normal_axis) {
            case 0: return {1, 2};
            case 1: return {0, 2};
            default: return {0, 1};
        }
    }

    static Footprint from_footprint(const std::array<double,3>& center,
                                     const std::array<double,3>& direction,
                                     const std::array<double,3>& rect_size, // 0 for circular
                                     double diameter,                       // 0 for rectangular
                                     bool is_circular,
                                     const std::string& name,
                                     const std::string& kind) {
        Footprint fp;
        fp.normal_axis = geom::normal_axis(direction);
        fp.mount_coord = center[fp.normal_axis];
        fp.owner_name = name;
        fp.owner_kind = kind;

        auto axes = fp.plane_axes();
        if (is_circular) {
            double r = diameter / 2.0;
            fp.half2[0] = r;
            fp.half2[1] = r;
        } else {
            fp.half2[0] = rect_size[axes[0]] / 2.0;
            fp.half2[1] = rect_size[axes[1]] / 2.0;
        }
        fp.center2[0] = center[axes[0]];
        fp.center2[1] = center[axes[1]];
        return fp;
    }

    static Footprint from_fan(const Fan& f) {
        return from_footprint(f.get_center(), f.get_velocity_dir(),
                               f.get_size_m(), f.get_diameter(), f.is_circular(),
                               f.get_name(), "Fan");
    }

    static Footprint from_vent(const Vent& v) {
        return from_footprint(v.get_center(), v.get_direction(),
                               v.get_size_m(), v.get_diameter(), v.is_circular(),
                               v.get_name(), "Vent");
    }

    // Two openings only conflict if they face the same way (same normal
    // axis), sit on essentially the same plane, and their in-plane
    // shapes overlap. Openings on different walls/planes never collide,
    // even if their raw coordinate ranges happen to intersect.
    bool overlaps_footprint(const Footprint& other) const {
        if (normal_axis != other.normal_axis) return false;
        if (std::abs(mount_coord - other.mount_coord) > geom::PLANE_TOL) return false;

        return geom::overlaps_1d(center2[0]-half2[0], center2[0]+half2[0],
                                  other.center2[0]-other.half2[0], other.center2[0]+other.half2[0]) &&
               geom::overlaps_1d(center2[1]-half2[1], center2[1]+half2[1],
                                  other.center2[1]-other.half2[1], other.center2[1]+other.half2[1]);
    }

    // A fan/vent conflicts with a component only if it is genuinely
    // embedded in the component's solid volume along the normal axis
    // (strictly between the two faces, past FACE_TOL) - sitting AT a
    // face (flush-mounted, the normal and intended case) is not a
    // conflict, regardless of in-plane overlap.
    bool overlaps_component(const AABB& box) const {
        const double lo = box.min[normal_axis];
        const double hi = box.max[normal_axis];
        const bool embedded_along_normal =
            (mount_coord > lo + geom::FACE_TOL) && (mount_coord < hi - geom::FACE_TOL);
        if (!embedded_along_normal) return false;

        auto axes = plane_axes();
        return geom::overlaps_1d(center2[0]-half2[0], center2[0]+half2[0],
                                  box.min[axes[0]], box.max[axes[0]]) &&
               geom::overlaps_1d(center2[1]-half2[1], center2[1]+half2[1],
                                  box.min[axes[1]], box.max[axes[1]]);
    }
};

// -------------------------------------------------------------
// CollisionChecker
// -------------------------------------------------------------
// Runs once, at model-assembly time, before the mesh is built.
// Mesh stamping trusts that everything passed here is already valid.
//
// Known simplification: circular fans/vents are treated as their
// bounding square (side = diameter) rather than an exact circle, which
// is slightly conservative (may flag two near-miss circles that don't
// actually touch) but keeps this checker simple and cheap.
struct CollisionChecker {

    static void check_all(const std::vector<Component>& components,
                           const std::vector<Fan>& fans,
                           const std::vector<Vent>& vents) {

        std::vector<AABB> boxes;
        boxes.reserve(components.size());
        for (const auto& c : components) boxes.push_back(AABB::from_component(c));

        std::vector<Footprint> openings;
        openings.reserve(fans.size() + vents.size());
        for (const auto& f : fans)  openings.push_back(Footprint::from_fan(f));
        for (const auto& v : vents) openings.push_back(Footprint::from_vent(v));

        // Component vs Component
        for (size_t i = 0; i < boxes.size(); ++i) {
            for (size_t j = i + 1; j < boxes.size(); ++j) {
                if (boxes[i].overlaps(boxes[j])) {
                    throw std::runtime_error(
                        boxes[i].owner_kind + " '" + boxes[i].owner_name + "' overlaps " +
                        boxes[j].owner_kind + " '" + boxes[j].owner_name + "'.");
                }
            }
        }

        // Opening (Fan/Vent) vs Opening
        for (size_t i = 0; i < openings.size(); ++i) {
            for (size_t j = i + 1; j < openings.size(); ++j) {
                if (openings[i].overlaps_footprint(openings[j])) {
                    throw std::runtime_error(
                        openings[i].owner_kind + " '" + openings[i].owner_name + "' overlaps " +
                        openings[j].owner_kind + " '" + openings[j].owner_name + "'.");
                }
            }
        }

        // Opening (Fan/Vent) vs Component
        for (const auto& fp : openings) {
            for (const auto& box : boxes) {
                if (fp.overlaps_component(box)) {
                    throw std::runtime_error(
                        fp.owner_kind + " '" + fp.owner_name + "' is embedded inside " +
                        box.owner_kind + " '" + box.owner_name + "' (not just flush-mounted on its face).");
                }
            }
        }
    }
};

// -------------------------------------------------------------
// RackBoundsChecker
// -------------------------------------------------------------
// Everything must stay within the rack envelope. This is the same
// check Grapher used to run internally at add_component/add_fan/add_vent
// time; pulling it out here lets it run once, up front, alongside
// CollisionChecker - so by the time anything reaches Mesh or Grapher,
// both "does it fit in the rack" and "does it collide with anything
// else" are already known-good.
struct RackBoundsChecker {

    static void check_component(const Rack& rack, const Component& c) {
        auto [x, y, z] = c.get_coords();
        double tol = 1e-8;
        if (x + tol < 0.0 || y + tol < 0.0 || z + tol < 0.0 ||
            x + c.get_width_m()  > rack.get_width_m()  + tol ||
            y + c.get_depth_m()  > rack.get_depth_m()  + tol ||
            z + c.get_height_m() > rack.get_height_m() + tol) {
            std::cout << "x min: " << x << " y min: " << y << " z min: " << z << std::endl;
            std::cout << "x max: " << x + c.get_width_m() << " y max: " << y + c.get_depth_m() << " z max: " << z + c.get_height_m() << std::endl;
            std::cout << "rack x: " << rack.get_width_m() << " rack y: " << rack.get_depth_m() << " rack z: " << rack.get_height_m() << std::endl;
            throw std::out_of_range("Component '" + c.get_name() + "' out of rack bounds");
        }
    }

    static void check_opening(const Rack& rack, const Footprint& fp) {
        double rack_extent[3] = { rack.get_width_m(), rack.get_depth_m(), rack.get_height_m() };

        if (fp.mount_coord < 0.0 || fp.mount_coord > rack_extent[fp.normal_axis]) {
            throw std::out_of_range(fp.owner_kind + " '" + fp.owner_name + "' mount position out of rack bounds");
        }

        auto axes = fp.plane_axes();
        for (int k = 0; k < 2; ++k) {
            int axis = axes[k];
            double lo = fp.center2[k] - fp.half2[k];
            double hi = fp.center2[k] + fp.half2[k];
            if (lo < 0.0 || hi > rack_extent[axis]) {
                throw std::out_of_range(fp.owner_kind + " '" + fp.owner_name + "' extends out of rack bounds");
            }
        }
    }

    static void check_all(const Rack& rack,
                           const std::vector<Component>& components,
                           const std::vector<Fan>& fans,
                           const std::vector<Vent>& vents) {
        for (const auto& c : components) check_component(rack, c);
        for (const auto& f : fans)  check_opening(rack, Footprint::from_fan(f));
        for (const auto& v : vents) check_opening(rack, Footprint::from_vent(v));
    }
};

#endif
