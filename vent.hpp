#ifndef VENT_HPP
#define VENT_HPP

#include <array>
#include <cmath>
#include <string>
#include <stdexcept>

struct Vent {
    std::string name;
    double width_m;
    double height_m;
    double free_area_ratio; // 0.0 to 1.0

    std::array<double, 3> size;      // m
    std::array<double, 3> center;    // m
    std::array<double, 3> direction; // outward/inward normal direction

    Vent() : name("Uninitialized"), 
             size{0.0, 0.0, 0.0},
             free_area_ratio(0.0),
             center{0.0, 0.0, 0.0},
             direction{0.0, 0.0, 0.0} {}

    Vent(std::string name_,
         std::array<double, 3> size_,
         double free_area_ratio_,
         std::array<double, 3> center_,
         std::array<double, 3> direction_) :
         name(name_),
         size(size_),
         center(center_),
         direction(direction_) {set_free_area_ratio(free_area_ratio_);}

    // setters
    void set_name(std::string n) { name = n; }
    void set_size(std::array<double, 3> s) {
        size = s;
    }
    void set_free_area_ratio(double a) {
        if(a >= 0.0 && a <= 1.0) { free_area_ratio = a; }
        else {
            throw std::invalid_argument("Invalid free area ratio. Needs to be between 0.0 and 1.0.");
        }
    }

    //getters
    std::string get_name() const { return name; }
    std::array<double, 3> get_size() const { return size; }
    double get_free_area_ratio() const { return free_area_ratio; }
    std::array<double, 3> get_center() const { return center; }
    std::array<double, 3> get_direction() const { return direction; }

    // helpers
    double gross_area() const { return width_m * height_m; }
    double free_area() const { return gross_area() * free_area_ratio; }   
};

#endif
