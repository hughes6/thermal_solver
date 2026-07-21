#ifndef FAN_HPP
#define FAN_HPP

#include <array>
#include <cmath>
#include <string>

enum class FlowType {
    Intake,
    Exhaust,
    Uninitialized
};

enum class ShapeType {
    Circular,
    Rectangular,
    Uninitialized
}; 

struct Fan {
    static constexpr double U_TO_M  = 1.75 * 0.0254;
    static constexpr double IN_TO_M = 0.0254;
    static constexpr double MM_TO_M = 0.001;
    static constexpr double CFM_TO_M3S = 0.00047194745;
    static constexpr double PI = 3.14159265358979323846;    
    std::string name;
    double cfm = 0.0;
    double diameter_m = 0.0;

    // Fan center location in meters
    std::array<double, 3> center{0.0, 0.0, 0.0};

    // Unit direction vector of airflow
    // Example top exhaust: {0,0,1}
    // Front intake: {0,1,0} or {1,0,0} depending on your axes
    std::array<double, 3> direction{0.0, 0.0, 1.0};
    std::array<double, 3> size_m{0.0, 0.0, 0.0};

    FlowType type = FlowType::Uninitialized;
    ShapeType shape = ShapeType::Uninitialized;

    Fan() : name("Uninitialized"),
            cfm(0.0),
            diameter_m(0.0),
            center{0.0, 0.0, 0.0},
            direction{0.0, 0.0, 1.0},
            size_m{0.0, 0.0, 0.0},
            curve_a(0.0),
            curve_b(0.0),
            curve_c(0.0) {}

    // explicit copy constructor
    Fan clone() const {
        return Fan(*this);
    }

    Fan(std::string name_,
        double cfm_,
        double diameter_m_,
        std::array<double, 3> size_,
        std::array<double, 3> center_,
        std::array<double, 3> direction_,
        FlowType type_ = FlowType::Uninitialized,
        ShapeType shape_ = ShapeType::Uninitialized)
        : name(std::move(name_)),
          cfm(cfm_),
          diameter_m(diameter_m_),
          size_m(size_),
          center(center_),
          direction(direction_),
          type(type_), 
          shape(shape_) { 
            check_shape_dimensions();
        }


    void check_shape_dimensions() {
        if(shape == ShapeType::Circular && (size_m[0] != 0.0 || size_m[1] != 0.0 || size_m[2] != 0.0)) {
            std::cerr << "Invalid size, cartesian size defined for radial fan \n";
        }
        if(shape == ShapeType::Rectangular && diameter_m != 0.0) {
            std::cerr << "Invalid size, diameter defined for a rectangular fan \n";
        }
    }   

    double velocity_x() const { return velocity_mag() * direction[0]; }
    double velocity_y() const { return velocity_mag() * direction[1]; }
    double velocity_z() const { return velocity_mag() * direction[2]; }
    double area() const { 
        if(shape == ShapeType::Circular) {
            return PI * diameter_m* diameter_m / 4.0;
        } else {
            return gross_area(); 
        }
    }
    double gross_area() const {
        const double ax = std::abs(direction[0]);
        const double ay = std::abs(direction[1]);
        const double az = std::abs(direction[2]);
        if (az >= ax && az >= ay) return size_m[0] * size_m[1]; // XY plane
        if (ay >= ax && ay >= az) return size_m[0] * size_m[2]; // XZ plane
        return size_m[1] * size_m[2];                           // YZ plane
    }  

    static Fan from_rack_units(double width_u, double depth_u, double height_u, std::string name = "not set") {
        Fan f;
        f.set_size_rack_units(width_u, depth_u, height_u);
        f.set_name(name);
        return f;
    }

    static Fan from_meters(double width_m, double depth_m, double height_m, std::string name = "not set") {
        Fan f;
        f.set_size_meters(width_m, depth_m, height_m);
        f.set_name(name);
        return f;
    }

    static Fan from_inches(double width_in, double depth_in, double height_in, std::string name = "not set") {
        Fan f;
        f.set_size_inches(width_in, depth_in, height_in);
        f.set_name(name);
        return f;
    }

    static Fan from_mm(double width_mm, double depth_mm, double height_mm, std::string name = "not set") {
        Fan f;
        f.set_size_mm(width_mm, depth_mm, height_mm);
        f.set_name(name);
        return f;
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

    void set_center_rack_units(double width_u, double depth_u, double height_u) {
        center[0]  = width_u  * U_TO_M;
        center[1]  = depth_u  * U_TO_M;
        center[2]  = height_u * U_TO_M;
    }

    void set_center_meters(double width_m, double depth_m, double height_m) {
        center[0] = width_m;
        center[1] = depth_m;
        center[2] = height_m;
    }

    void set_center_inches(double width_in, double depth_in, double height_in) {
        center[0] = width_in  * IN_TO_M;
        center[1] = depth_in  * IN_TO_M;
        center[2] = height_in * IN_TO_M;
    }

    void set_center_mm(double width_mm, double depth_mm, double height_mm) {
        center[0] = width_mm  * MM_TO_M;
        center[1] = depth_mm  * MM_TO_M;
        center[2] = height_mm * MM_TO_M;
    }
    
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

    void set_name(std::string s) { name = s; }
    void set_cfm(double cfm_) { cfm = cfm_; }
    void set_diameter(double diameter_) { diameter_m = diameter_; }
    void set_diameter_meters(double diameter_) { diameter_m = diameter_; }
    void set_diameter_rack_units(double diameter_) { diameter_m = diameter_ * U_TO_M; }
    void set_diameter_inches(double diameter_) { diameter_m = diameter_ * IN_TO_M; }
    void set_diameter_mm(double diameter_) { diameter_m = diameter_ * MM_TO_M; }
    void set_size(std::array<double, 3> size_) { size_m = size_; }
    void set_center(std::array<double, 3> center_) { center = center_; }
    void set_velocity_dir(std::array<double, 3> direction_) { direction = direction_; }
    void set_type(FlowType type_) { type = type_; }
    void set_shape(ShapeType shape_) { shape = shape_; }
    void set_rho_rated(double rho) { rho_rated = rho; }
    void set_curve_a(double a) { curve_a = a; }
    void set_curve_b(double b) { curve_b = b; }
    void set_curve_c(double c) { curve_c = c; }
    void set_curve(double a, double b, double c, double rho_rated_ = 1.2) {
        curve_a = a; curve_b = b; curve_c = c; rho_rated = rho_rated_;
    }

    bool has_curve() const { return curve_a > 0.0; }
    double get_rho_rated() const { return rho_rated; }
    double get_curve_a() const { return curve_a; }
    double get_curve_b() const { return curve_b; }
    double get_curve_c() const { return curve_c; }
    double flow_m3s() const { return cfm * CFM_TO_M3S; }
    double velocity_mag() const { return area() > 0.0 ? flow_m3s() / area() : 0.0; }
    std::array<double, 3> get_center() const { return center; }
    std::array<double, 3> get_velocity_dir() const { return direction;} 
    std::array<double, 3> get_size_m() const { return size_m; }
    std::string get_name() const { return name; }
    double get_cfm() const { return cfm; }
    double get_diameter() const { return diameter_m; }
    std::string get_type() const { return (type == FlowType::Exhaust) ? "Exhaust" : "Intake"; }
    std::string get_shape() const { return (shape == ShapeType::Circular) ? "Circular" : "Rectangular"; }
    FlowType get_type_t() const { return type; }
    ShapeType get_shape_t() const { return shape; }
    bool is_circular() const { return shape == ShapeType::Circular; }

    double curve_a = 0.0;      // Pa, shutoff pressure at rho_rated
    double curve_b = 0.0;      // Pa per (m^3/s)
    double curve_c = 0.0;      // Pa per (m^3/s)^2
    double rho_rated = 1.2;    // kg/m^3, density the curve was measured at

    // Density-corrected available pressure at a candidate flow rate.
    double curve_pressure(double Q, double rho_local) const {
        double dP = curve_a - curve_b * Q - curve_c * Q * Q;
        dP = std::max(dP, 0.0); // curve shouldn't go negative
        return dP * (rho_local / rho_rated);
    }
};

#endif
