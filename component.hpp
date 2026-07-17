#ifndef COMPONENT_HPP
#define COMPONENT_HPP

#include <array>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

#include "fan.hpp"

enum class RegionType {
    Air,
    HeatSource,
    Vent,
    Fan,
    Uninitialized
};

struct InternalRegion {
    InternalRegion()  : region_type(RegionType::Uninitialized), flow_type(FlowType::Uninitialized), shape_type(ShapeType::Uninitialized),
          size{0.0, 0.0, 0.0}, local_position{0.0, 0.0, 0.0}, direction{0.0, 0.0, 0.0}, diameter(0.0), cfm(0.0),
          free_area_ratio(0.0), cp(0.0), rho(0.0), k(0.0), watts(0.0), global_position{0.0, 0.0, 0.0} {}

    InternalRegion(std::array<double,3> size_, std::array<double,3> local_position_, double cp_, double rho_, double k_, double watts_)
        : InternalRegion() 
    {
        region_type = RegionType::HeatSource;
        size = size_;
        local_position = local_position_;
        cp = cp_;
        rho = rho_;
        k = k_;
        watts = watts_;
        validate_heat_source();
    }

    InternalRegion(std::array<double,3> size_, std::array<double,3> local_position_)
        : InternalRegion()
    {
        region_type = RegionType::Air;
        size = size_;
        local_position = local_position_;
        validate_air();
    }

    InternalRegion(std::array<double,3> size_, std::array<double,3> center_, std::array<double,3> direction_,
                   double diameter_, double cfm_, FlowType flow_type_,  ShapeType shape_type_)
        : InternalRegion()
    {
        region_type = RegionType::Fan;
        size = size_;
        local_position = center_;
        direction = direction_;
        diameter = diameter_;
        cfm = cfm_;
        flow_type = flow_type_;
        shape_type = shape_type_;
        validate_fan();
    }

    InternalRegion(std::array<double,3> size_, std::array<double,3> center_, std::array<double,3> direction_, double free_area_ratio_)
        : InternalRegion()
    {
        region_type = RegionType::Vent;
        size = size_;
        local_position = center_;
        direction = direction_;
        free_area_ratio = free_area_ratio_;
        validate_vent();
    }

    void set_region_type(RegionType t) { region_type = t; }
    void set_flow_type(FlowType t) { flow_type = t; }
    void set_shape_type(ShapeType t) { shape_type = t; }
    void set_size(std::array<double, 3> size_) { size = size_; }
    void set_local_position(std::array<double, 3> loc_pos) { local_position = loc_pos; }
    void set_gloabal_position(std::array<double, 3> g_pos) { global_position = g_pos; }
    void set_direction(std::array<double, 3> d) { direction = d; }
    void set_diameter(double d) { diameter = d; }
    void set_cfm(double cfm_) { cfm = cfm_; }
    void set_free_area_ratio(double far) { free_area_ratio = far; }
    void set_cp(double cp_) { cp = cp_; }
    void set_rho(double rho_) { rho = rho_; }
    void set_k(double k_) { k = k_; }
    void set_watts(double watts_) { watts = watts_; }

    RegionType get_region_type() const { return region_type; }
    FlowType get_flow_type() const { return flow_type; }
    ShapeType get_shape_type() const { return shape_type; }
    std::array<double, 3> get_size() const { return size; }
    std::array<double, 3> get_local_position() const { return local_position; }
    std::array<double, 3> get_global_position() const { return global_position; }
    std::array<double, 3> get_direction() const { return direction; }
    double get_diameter() const { return diameter; }
    double get_cfm() const { return cfm; }
    double get_far() const { return free_area_ratio; }
    double get_cp() const { return cp; }
    double get_rho() const { return rho; }
    double get_k() const { return k; }
    double get_watts() const { return watts; }

    double volume() const { return size[0] * size[1] * size[2]; }
    double watt_density() const {  
        return volume() > 0.0 ? watts / volume() : 0.0; // W/m^3
    }

    void validate_size() {
        int zeros = 0;
        for(double s : size) {
            if(s == 0.0) {
                zeros++;
            }
        }
        if(zeros > 1) throw std::invalid_argument("InternalRegion: invalid size input, cannot have more than one dimension = 0.0.");
    }

    void validate_heat_source() {
        validate_size();
        if(cp <= 0.0) throw std::invalid_argument("InternalRegion: invalid cp input, must be > 0.0.");
        if(rho <= 0.0) throw std::invalid_argument("InternalRegion: invalid rho input, must be > 0.0.");
        if(k <= 0.0) throw std::invalid_argument("InternalRegion: invalid k input, must be > 0.0.");
        if(watts < 0.0) throw std::invalid_argument("InternalRegion: invalid watts, must be >= 0.0");
    }

    void validate_air() {
        validate_size();
    }

    void validate_fan() {
        validate_size();
        if(shape_type == ShapeType::Rectangular && diameter != 0.0) throw std::invalid_argument("InternalRegion: rectangular fan has diameter defined.");
        if(shape_type == ShapeType::Circular && (size[0] != 0.0 || size[1] != 0.0 || size[2] != 0.0)) throw std::invalid_argument("InternalRegion: circular fan has size vector");
        if(cfm < 0.0) throw std::invalid_argument("InternalRegion: fan cfm cannot be < 0.0.");
    }

    void validate_vent() {
        validate_size();
        if(free_area_ratio < 0.0 || free_area_ratio > 1.0) throw std::invalid_argument("InternalRegion: vent free area ration needs to be > 0.0 and < 1.0.");
    }

private:
    RegionType region_type;
    FlowType flow_type;
    ShapeType shape_type;
    std::array<double, 3> size;
    std::array<double, 3> local_position;
    std::array<double, 3> global_position; // gets set when put into component
    std::array<double, 3> direction;
    double diameter;
    double cfm;
    double free_area_ratio;
    double cp;
    double rho;
    double k;
    double watts;
};



struct Component {
    static constexpr double U_TO_M  = 1.75 * 0.0254;
    static constexpr double IN_TO_M = 0.0254;
    static constexpr double MM_TO_M = 0.001;
    std::vector<InternalRegion> internal_regions;

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

    std::vector<InternalRegion> get_regions() const { return internal_regions; }

    void order_internal_regions() {
        // air then, heat block, then vent, then fan
        std::vector<InternalRegion> ordered_regions;
        for(InternalRegion r : internal_regions) {
            if(r.get_region_type() == RegionType::Uninitialized) throw std::invalid_argument("Component:InternalRegion - Uninitialized region type.");
            if(r.get_region_type() == RegionType::Air) {
                ordered_regions.push_back(r);
            }
        }
        for(InternalRegion r : internal_regions) {
            if(r.get_region_type() == RegionType::HeatSource) {
                ordered_regions.push_back(r);
            }
        }
        for(InternalRegion r : internal_regions) {
            if(r.get_region_type() == RegionType::Vent) {
                ordered_regions.push_back(r);
            }
        }
        for(InternalRegion r : internal_regions) {
            if(r.get_region_type() == RegionType::Fan) {
                ordered_regions.push_back(r);
            }
        }
        internal_regions.swap(ordered_regions);
    }

    void add_region(InternalRegion r) {
        validate_internal_bounds(r);  // make sure doesn't extend beyond component
        if(r.get_region_type() == RegionType::Fan || r.get_region_type() == RegionType::Vent) {
            validate_fan_vent(r);
        }
        r.set_gloabal_position(local_to_global(r.get_local_position()));
        internal_regions.push_back(r);
    }


    std::array<double, 3> local_to_global(std::array<double, 3> loc) {
        double gx = loc[0] + bot_left_corner_coords[0];
        double gy = loc[1] + bot_left_corner_coords[1];
        double gz = loc[2] + bot_left_corner_coords[2];
        return {gx, gy, gz};
    }

    void validate_internal_bounds(InternalRegion r) {
        std::array<double, 3> loc = local_to_global(r.get_local_position());
        std::array<double, 3> loc_size = r.get_size();
        if(loc[0] < bot_left_corner_coords[0] || loc[0] + loc_size[0] > bot_left_corner_coords[0] + width_m) {
            std::cout << "loc x min: " << loc[0] << " comp x min: " << bot_left_corner_coords[0];
            std::cout << " loc x max: " << loc[0] + loc_size[0] << " comp x max: " << bot_left_corner_coords[0] + width_m << std::endl;
            throw std::invalid_argument("Component:InternalRegion - int region x component out of component x bounds.");
        }
        if(loc[1] < bot_left_corner_coords[1] || loc[1] + loc_size[1] > bot_left_corner_coords[1] + depth_m) {
            std::cout << "loc y min: " << loc[1] << " comp y min: " << bot_left_corner_coords[1];
            std::cout << " loc y max: " << loc[1] + loc_size[1] << " comp y max: " << bot_left_corner_coords[1] + depth_m << std::endl;
            throw std::invalid_argument("Component:InternalRegion - int region y component out of component y bounds.");
        }
        if(loc[2] < bot_left_corner_coords[2] || loc[2] + loc_size[2] > bot_left_corner_coords[2] + height_m) {
            std::cout << "loc z min: " << loc[2] << " comp z min: " << bot_left_corner_coords[2];
            std::cout << " loc z max: " << loc[2] + loc_size[2] << " comp z max: " << bot_left_corner_coords[2] + height_m << std::endl;
            throw std::invalid_argument("Component:InternalRegion - int region z component out of component z bounds.");
        }
    }

    void validate_fan_vent(InternalRegion r) {
        std::array<double, 3> loc = local_to_global(r.get_local_position());
        std::array<double, 3> loc_size = r.get_size();
        int lies_on_face = 0;
        if(loc[0] == 0.0 || loc[0] + loc_size[0] == width_m) lies_on_face++;
        if(loc[1] == 0.0 || loc[1] + loc_size[1] == depth_m) lies_on_face++;
        if(loc[2] == 0.0 || loc[2] + loc_size[2] == height_m) lies_on_face++;
        if(lies_on_face > 1) {
            throw std::invalid_argument("Component:InternalRegion - fan/vent intercepts more than 1 face.");
        }
        if(lies_on_face < 1) {
            throw std::invalid_argument("Component:InternalRegion - fan/vent does not intercept any component face.");
        }
    }
};

#endif
