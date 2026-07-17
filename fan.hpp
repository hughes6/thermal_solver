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
    std::array<double, 3> size{0.0, 0.0, 0.0};

    FlowType type = FlowType::Uninitialized;
    ShapeType shape = ShapeType::Uninitialized;

    Fan() : name("Uninitialized"),
            cfm(0.0),
            diameter_m(0.0),
            center{0.0, 0.0, 0.0},
            direction{0.0, 0.0, 1.0} {}

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
          size(size_),
          center(center_),
          direction(direction_),
          type(type_), 
          shape(shape_) { 
            check_shape_dimensions();
        }


    void check_shape_dimensions() {
        if(shape == ShapeType::Circular && (size[0] != 0.0 || size[1] != 0.0 || size[2] != 0.0)) {
            std::cerr << "Invalid size, cartesian size defined for radial fan \n";
        }
        if(shape == ShapeType::Rectangular && diameter_m != 0.0) {
            std::cerr << "Invalid size, diameter defined for a rectangular fan \n";
        }
    }   

    double velocity_x() const { return velocity_mag() * direction[0]; }
    double velocity_y() const { return velocity_mag() * direction[1]; }
    double velocity_z() const { return velocity_mag() * direction[2]; }
    double area() const { return PI * diameter_m * diameter_m / 4.0; }
    double flow_m3s() const { return cfm * CFM_TO_M3S; }
    double velocity_mag() const { return area() > 0.0 ? flow_m3s() / area() : 0.0; }
    std::array<double, 3> get_center() const { return center; }
    std::array<double, 3> get_velocity_dir() const { return direction;} 
    std::array<double, 3> get_size() const { return size; }
    std::string get_name() const { return name; }
    double get_cfm() const { return cfm; }
    double get_diameter() const { return diameter_m; }
    std::string get_type() const { return (type == FlowType::Exhaust) ? "Exhaust" : "Intake"; }
    std::string get_shape() const { return (shape == ShapeType::Circular) ? "Circular" : "Rectangular"; }
    FlowType get_type_t() const { return type; }
    ShapeType get_shape_t() const { return shape; }
    bool is_circular() const { return shape == ShapeType::Circular; }
};

#endif
