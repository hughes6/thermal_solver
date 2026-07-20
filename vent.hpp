#ifndef VENT_HPP
#define VENT_HPP

#include <array>
#include <cmath>
#include <string>
#include <stdexcept>

struct Vent {
    static constexpr double U_TO_M  = 1.75 * 0.0254;
    static constexpr double IN_TO_M = 0.0254;
    static constexpr double MM_TO_M = 0.001;
    std::string name;
    double free_area_ratio; // 0.0 to 1.0
    double cd; // 0.0 to 1.0

    std::array<double, 3> size_m;      // m
    std::array<double, 3> center;    // m
    std::array<double, 3> direction; // outward/inward normal direction

    Vent() : name("Uninitialized"), 
             size_m{0.0, 0.0, 0.0},
             free_area_ratio(0.0),
             cd(0.0),
             center{0.0, 0.0, 0.0},
             direction{0.0, 0.0, 0.0} {}

    Vent(std::string name_,
         std::array<double, 3> size_,
         double free_area_ratio_,
         double cd_,
         std::array<double, 3> center_,
         std::array<double, 3> direction_) :
         name(name_),
         size_m(size_),
         center(center_),
         direction(direction_) {set_free_area_ratio(free_area_ratio_);
                                set_cd(cd_);}

    // setters
    void set_name(std::string n) { name = n; }
    void set_center(std::array<double, 3> c) { center = c; }
    void set_size_m(std::array<double, 3> s) {
        size_m = s;
    }
    void set_free_area_ratio(double a) {
        if(a >= 0.0 && a <= 1.0) { free_area_ratio = a; }
        else {
            throw std::invalid_argument("Invalid free area ratio. Needs to be between 0.0 and 1.0.");
        }
    }
    void set_cd(double cd_) {
        if(cd < 1.0 || cd > 0.0) { cd = cd_; }
        else {
            throw std::invalid_argument("Invalid vent discharge coefficient. Needs to be between 0.0 and 1.0.");
        }
    }

    //getters
    std::string get_name() const { return name; }
    std::array<double, 3> get_size_m() const { return size_m; }
    double get_free_area_ratio() const { return free_area_ratio; }
    std::array<double, 3> get_center() const { return center; }
    std::array<double, 3> get_direction() const { return direction; }
    double get_cd() const { return cd; }

    // helpers
    double get_width_m()  const { return size_m[0]; }
    double get_depth_m()  const { return size_m[1]; }
    double get_height_m() const { return size_m[2]; }
    double get_width_u()  const { return size_m[0] / U_TO_M; }
    double get_depth_u()  const { return size_m[1] / U_TO_M; }
    double get_height_u() const { return size_m[2] / U_TO_M; }
    double get_width_in()  const { return size_m[0] / IN_TO_M; }
    double get_depth_in()  const { return size_m[1] / IN_TO_M; }
    double get_height_in() const { return size_m[2] / IN_TO_M; }
    double get_width_mm()  const { return size_m[0] / MM_TO_M; }
    double get_depth_mm()  const { return size_m[1] / MM_TO_M; }
    double get_height_mm() const { return size_m[2] / MM_TO_M; }

    static Vent from_rack_units(double width_u, double depth_u, double height_u, std::string name = "not set") {
        Vent v;
        v.set_size_rack_units(width_u, depth_u, height_u);
        v.set_name(name);
        return v;
    }

    static Vent from_meters(double width_m, double depth_m, double height_m, std::string name = "not set") {
        Vent v;
        v.set_size_meters(width_m, depth_m, height_m);
        v.set_name(name);
        return v;
    }

    static Vent from_inches(double width_in, double depth_in, double height_in, std::string name = "not set") {
        Vent v;
        v.set_size_inches(width_in, depth_in, height_in);
        v.set_name(name);
        return v;
    }

    static Vent from_mm(double width_mm, double depth_mm, double height_mm, std::string name = "not set") {
        Vent v;
        v.set_size_mm(width_mm, depth_mm, height_mm);
        v.set_name(name);
        return v;
    }

    void set_size_rack_units(double width_u, double depth_u, double height_u) {
        size_m[0]  = width_u  * U_TO_M;
        size_m[1]  = depth_u  * U_TO_M;
        size_m[2]  = height_u * U_TO_M;
    }

    void set_size_meters(double width_m, double depth_m, double height_m) {
        size_m[0] = width_m;
        size_m[1] = depth_m;
        size_m[2] = height_m;
    }

    void set_size_inches(double width_in, double depth_in, double height_in) {
        size_m[0] = width_in  * IN_TO_M;
        size_m[1] = depth_in  * IN_TO_M;
        size_m[2] = height_in * IN_TO_M;
    }

    void set_size_mm(double width_mm, double depth_mm, double height_mm) {
        size_m[0] = width_mm  * MM_TO_M;
        size_m[1] = depth_mm  * MM_TO_M;
        size_m[2] = height_mm * MM_TO_M;
    }

    double gross_area() const {
        const double ax = std::abs(direction[0]);
        const double ay = std::abs(direction[1]);
        const double az = std::abs(direction[2]);

        if (az >= ax && az >= ay) return size_m[0] * size_m[1]; // XY plane
        if (ay >= ax && ay >= az) return size_m[0] * size_m[2]; // XZ plane
        return size_m[1] * size_m[2];                           // YZ plane
    }
    double free_area() const { return gross_area() * free_area_ratio; }   
};

#endif
