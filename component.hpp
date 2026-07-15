#ifndef COMPONENT_HPP
#define COMPONENT_HPP

#include <array>
#include <string>
#include <utility>

struct Component {
    static constexpr double U_TO_M  = 1.75 * 0.0254;
    static constexpr double IN_TO_M = 0.0254;
    static constexpr double MM_TO_M = 0.001;

    std::string name;

    // Physical size, always stored in meters
    double height_m = 0.0;
    double width_m  = 0.0;
    double depth_m  = 0.0;

    // Bottom-left-front corner in meters: x, y, z
    std::array<double, 3> bot_left_corner_coords{0.0, 0.0, 0.0};

    // Material / thermal properties
    double k_solid = 0.0;     // W/m-K
    double rho_solid = 0.0;   // kg/m^3
    double cp = 0.0;          // J/kg-K
    double h = 0.0;           // W/m^2-K
    double T = 20.0;          // deg C

    double component_watts = 0.0; // total watts

    Component() = default;

    // Size directly in meters
    Component(double height_m_, double width_m_, double depth_m_, std::string n = "")
        : name(std::move(n)),
          height_m(height_m_),
          width_m(width_m_),
          depth_m(depth_m_) {}

    // Factory: rack units for height, inches for width/depth
    static Component from_rack_units(double height_u,
                                     double width_in,
                                     double depth_in,
                                     std::string name = "") {
        return Component(
            height_u * U_TO_M,
            width_in * IN_TO_M,
            depth_in * IN_TO_M,
            std::move(name)
        );
    }

    // Factory: all dimensions in inches
    static Component from_inches(double height_in,
                                 double width_in,
                                 double depth_in,
                                 std::string name = "") {
        return Component(
            height_in * IN_TO_M,
            width_in * IN_TO_M,
            depth_in * IN_TO_M,
            std::move(name)
        );
    }

    // Factory: all dimensions in millimeters
    static Component from_mm(double height_mm,
                             double width_mm,
                             double depth_mm,
                             std::string name = "") {
        return Component(
            height_mm * MM_TO_M,
            width_mm * MM_TO_M,
            depth_mm * MM_TO_M,
            std::move(name)
        );
    }

    void set_size_m(double h, double w, double d) {
        height_m = h;
        width_m = w;
        depth_m = d;
    }

    void set_size_rack_units(double height_u, double width_in, double depth_in) {
        height_m = height_u * U_TO_M;
        width_m = width_in * IN_TO_M;
        depth_m = depth_in * IN_TO_M;
    }

    void set_coords_m(double x, double y, double z) {
        bot_left_corner_coords = {x, y, z};
    }

    void set_coords_rack_units(double x_in, double y_in, double z_u) {
        bot_left_corner_coords = {
            x_in * IN_TO_M,
            y_in * IN_TO_M,
            z_u * U_TO_M
        };
    }

    void set_watts(double q) { component_watts = q; }
    void set_rho_solid(double r) { rho_solid = r; }
    void set_k_solid(double k) { k_solid = k; }
    void set_t(double t) { T = t; }
    void set_cp(double c) { cp = c; }
    void set_h(double hh) { h = hh; }

    double get_t() const { return T; }
    double get_rho() const { return rho_solid; }
    double get_k() const { return k_solid; }
    double get_h() const { return h; }
    double get_cp() const { return cp; }
    double get_watts() const { return component_watts; }

    double get_height_m() const { return height_m; }
    double get_width_m() const { return width_m; }
    double get_depth_m() const { return depth_m; }

    std::string get_name() const { return name; }
    std::array<double, 3> get_coords() const { return bot_left_corner_coords; }

    double volume() const {
        return height_m * width_m * depth_m;
    }

    double watt_density() const {
        return volume() > 0.0 ? component_watts / volume() : 0.0; // W/m^3
    }
};

#endif