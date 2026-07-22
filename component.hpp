#ifndef THERMAL_SOLVER_COMPONENT_HPP
#define THERMAL_SOLVER_COMPONENT_HPP

#include <array>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

#include "fan.hpp"
#include "vent.hpp"

enum class RegionType {
    Air,
    HeatSource,
    Vent,
    Fan,
    Uninitialized
};

struct InternalRegion {
    InternalRegion()  : region_type(RegionType::Uninitialized), flow_type(FlowType::Uninitialized), shape_type(ShapeType::Uninitialized),
          size_m{0.0, 0.0, 0.0}, local_position{0.0, 0.0, 0.0}, direction{0.0, 0.0, 0.0}, diameter(0.0), cfm(0.0),
          free_area_ratio(0.0), cp(0.0), rho(0.0), k(0.0), watts(0.0), global_position{0.0, 0.0, 0.0}, cd(0.0), velocity_direction{0.0, 0.0, 0.0},
          name("Uninitialized") {}

    InternalRegion(std::string name_, std::array<double,3> size_, std::array<double,3> local_position_, double cp_, double rho_, double k_, double watts_)
        : InternalRegion() 
    {
        name = name_;
        region_type = RegionType::HeatSource;
        size_m = size_;
        local_position = local_position_;
        cp = cp_;
        rho = rho_;
        k = k_;
        watts = watts_;
        validate_heat_source();
    }

    InternalRegion(std::string name_, std::array<double,3> size_, std::array<double,3> local_position_)
        : InternalRegion()
    {
        name = name_;
        region_type = RegionType::Air;
        size_m = size_;
        local_position = local_position_;
        validate_air();
    }

    InternalRegion(std::string name_, std::array<double,3> size_, std::array<double,3> center_, std::array<double,3> direction_,
                  std::array<double, 3> velocity_direction_, double diameter_, double cfm_, FlowType flow_type_,  ShapeType shape_type_)
        : InternalRegion()
    {
        name = name_;
        region_type = RegionType::Fan;
        size_m = size_;
        local_position = center_;
        direction = direction_;
        velocity_direction = velocity_direction_;
        diameter = diameter_;
        cfm = cfm_;
        flow_type = flow_type_;
        shape_type = shape_type_;
        validate_fan();
    }

    InternalRegion(std::string name_, std::array<double,3> size_, std::array<double,3> center_, std::array<double,3> direction_, double free_area_ratio_, double cd_)
        : InternalRegion()
    {
        name = name_;
        region_type = RegionType::Vent;
        size_m = size_;
        local_position = center_;
        direction = direction_;
        free_area_ratio = free_area_ratio_;
        cd = cd_;
        validate_vent();
    }

    explicit InternalRegion(const Fan& fan)
        : InternalRegion()
    {
        name = fan.get_name();
        region_type = RegionType::Fan;
        size_m = fan.get_size_m();
        local_position = fan.get_center();
        direction = fan.get_velocity_dir();
        velocity_direction = fan.get_velocity_dir();
        diameter = fan.get_diameter();
        cfm = fan.get_cfm();
        flow_type = fan.get_type_t();
        shape_type = fan.get_shape_t();

        validate_fan();
    }

    explicit InternalRegion(const Vent& vent)
        : InternalRegion()
    {
        name = vent.get_name();
        region_type = RegionType::Vent;
        size_m = vent.get_size_m();
        local_position = vent.get_center();
        direction = vent.get_direction();
        diameter = vent.get_diameter();
        free_area_ratio = vent.get_free_area_ratio();
        cd = vent.get_cd();

        shape_type =
            vent.get_shape() == VentShapeType::Circular
                ? ShapeType::Circular
                : ShapeType::Rectangular;

        validate_vent();
    }

    // factories
    static InternalRegion from_meters(double width_m, double depth_m, double height_m, std::string name = "not set") {
        InternalRegion r;
        r.set_size_meters(width_m, depth_m, height_m);
        r.set_name(name);
        return r;
    }

    static InternalRegion from_rack_units(double width_u, double depth_u, double height_u, std::string name = "not set") {
        InternalRegion r;
        r.set_size_rack_units(width_u, depth_u, height_u);
        r.set_name(name);
        return r;
    }

    static InternalRegion from_inches(double width_in, double depth_in, double height_in, std::string name = "not set") {
        InternalRegion r;
        r.set_size_inches(width_in, depth_in, height_in);
        r.set_name(name);
        return r;
    }

    static InternalRegion from_mm(double width_mm, double depth_mm, double height_mm, std::string name = "not set") {
        InternalRegion r;
        r.set_size_mm(width_mm, depth_mm, height_mm);
        r.set_name(name);
        return r;
    }

    void set_size_meters(double width_m, double depth_m, double height_m) {
        size_m[0]  = width_m;
        size_m[1]  = depth_m;
        size_m[2]  = height_m;
    }

    void set_size_rack_units(double width_u, double depth_u, double height_u) {
        size_m[0] = width_u  * U_TO_M;
        size_m[1] = depth_u  * U_TO_M;
        size_m[2] = height_u * U_TO_M;
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

    void set_local_position_meters(double x, double y, double z) { 
        local_position[0] = x;
        local_position[1] = y;
        local_position[2] = z;
    }

    void set_local_position_rack_units(double x, double y, double z) { 
        local_position[0] = x * U_TO_M;
        local_position[1] = y * U_TO_M;
        local_position[2] = z * U_TO_M;
    }    

    void set_local_position_inches(double x, double y, double z) { 
        local_position[0] = x * IN_TO_M;
        local_position[1] = y * IN_TO_M;
        local_position[2] = z * IN_TO_M;
    }    

    void set_local_position_mm(double x, double y, double z) { 
        local_position[0] = x * MM_TO_M;
        local_position[1] = y * MM_TO_M;
        local_position[2] = z * MM_TO_M;
    }    

    // setters
    void set_name(std::string name_) { name = name_; }
    void set_region_type(RegionType t) { region_type = t; }
    void set_flow_type(FlowType t) { flow_type = t; }
    void set_shape_type(ShapeType t) { shape_type = t; }
    void set_size(std::array<double, 3> size_) { size_m = size_; }
    void set_local_position(std::array<double, 3> loc_pos) { local_position = loc_pos; }
    void set_gloabal_position(std::array<double, 3> g_pos) { global_position = g_pos; }
    void set_direction(std::array<double, 3> d) { direction = d; }
    void set_diameter(double d) { diameter = d; }
    void set_velocity_direction(std::array<double, 3> v) { velocity_direction = v; }
    void set_cfm(double cfm_) { cfm = cfm_; }
    void set_free_area_ratio(double far) { free_area_ratio = far; }
    void set_cp(double cp_) { cp = cp_; }
    void set_rho(double rho_) { rho = rho_; }
    void set_k(double k_) { k = k_; }
    void set_watts(double watts_) { watts = watts_; }
    void set_cd(double cd_) { cd = cd_; }

    // getters
    std::string get_name() const { return name; }
    RegionType get_region_type() const { return region_type; }
    FlowType get_flow_type() const { return flow_type; }
    ShapeType get_shape_type() const { return shape_type; }
    std::array<double, 3> get_size_m() const { return size_m; }
    std::array<double, 3> get_local_position() const { return local_position; }
    std::array<double, 3> get_global_position() const { return global_position; }
    std::array<double, 3> get_direction() const { return direction; }
    std::array<double, 3> get_velocity_direction() const { return velocity_direction; }
    double get_diameter() const { return diameter; }
    double get_cfm() const { return cfm; }
    double get_far() const { return free_area_ratio; }
    double get_cp() const { return cp; }
    double get_rho() const { return rho; }
    double get_k() const { return k; }
    double get_watts() const { return watts; }
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

    // fan fxns
    bool is_circular() const { return shape_type == ShapeType::Circular; }
    double flow_m3s() const { return cfm * CFM_TO_M3S; }
    double area() const { 
        if(shape_type == ShapeType::Circular) {
            return PI * diameter * diameter / 4.0;
        } else {
            return gross_area(); 
        }
    }
    double velocity_mag() const { return area() > 0.0 ? flow_m3s() / area() : 0.0; }
    double velocity_x() const { return velocity_mag() * direction[0]; }
    double velocity_y() const { return velocity_mag() * direction[1]; }
    double velocity_z() const { return velocity_mag() * direction[2]; }

    // vent fxns
    double gross_area() const {
        const double ax = std::abs(direction[0]);
        const double ay = std::abs(direction[1]);
        const double az = std::abs(direction[2]);
        if (az >= ax && az >= ay) return size_m[0] * size_m[1]; // XY plane
        if (ay >= ax && ay >= az) return size_m[0] * size_m[2]; // XZ plane
        return size_m[1] * size_m[2];                           // YZ plane
    }    
    double free_area() const { return gross_area() * free_area_ratio; }   
    double volume() const { return size_m[0] * size_m[1] * size_m[2]; }
    
    // heat source fxn
    double watt_density() const {  
        return volume() > 0.0 ? watts / volume() : 0.0; // W/m^3
    }

    // validation fxns
    void validate_size() {
        int zeros = 0;
        for(double s : size_m) {
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
        if(shape_type == ShapeType::Circular && (size_m[0] != 0.0 || size_m[1] != 0.0 || size_m[2] != 0.0)) throw std::invalid_argument("InternalRegion: circular fan has size vector");
        if(cfm < 0.0) throw std::invalid_argument("InternalRegion: fan cfm cannot be < 0.0.");
    }
    void validate_vent() {
        validate_size();
        if(free_area_ratio < 0.0 || free_area_ratio > 1.0) throw std::invalid_argument("InternalRegion: vent free area ration needs to be > 0.0 and < 1.0.");
    }

private:
    static constexpr double CFM_TO_M3S = 0.00047194745;
    static constexpr double PI = 3.14159265358979323846;   
    static constexpr double U_TO_M  = 1.75 * 0.0254;
    static constexpr double IN_TO_M = 0.0254;
    static constexpr double MM_TO_M = 0.001;
    std::string name;
    RegionType region_type;
    FlowType flow_type;
    ShapeType shape_type;
    std::array<double, 3> size_m;
    std::array<double, 3> local_position;
    std::array<double, 3> global_position; // gets set when put into component
    std::array<double, 3> direction;
    std::array<double, 3> velocity_direction;
    double diameter;
    double cfm;
    double free_area_ratio;
    double cp;
    double rho;
    double k;
    double watts;
    double cd;
};



struct Component {
    static constexpr double U_TO_M  = 1.75 * 0.0254;
    static constexpr double IN_TO_M = 0.0254;
    static constexpr double MM_TO_M = 0.001;
    std::vector<InternalRegion> internal_regions;

    std::string name;
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

    // explicit copy constructor
    Component clone() const {
        return Component(*this);
    }


    // Size directly in meters
    Component(double width_m_, double depth_m_, double height_m_, std::string n = "Uninitialized")
        : name(std::move(n)),
          width_m(width_m_),
          depth_m(depth_m_),
          height_m(height_m_) {}


    static Component from_rack_units(double width_u, double depth_u, double height_u, std::string name = "not set") {
        Component c;
        c.set_size_rack_units(width_u, depth_u, height_u);
        c.set_name(name);
        return c;
    }

    static Component from_meters(double width_m, double depth_m, double height_m, std::string name = "not set") {
        Component c;
        c.set_size_meters(width_m, depth_m, height_m);
        c.set_name(name);
        return c;
    }

    static Component from_inches(double width_in, double depth_in, double height_in, std::string name = "not set") {
        Component c;
        c.set_size_inches(width_in, depth_in, height_in);
        c.set_name(name);
        return c;
    }

    static Component from_mm(double width_mm, double depth_mm, double height_mm, std::string name = "not set") {
        Component c;
        c.set_size_mm(width_mm, depth_mm, height_mm);
        c.set_name(name);
        return c;
    }

    void set_size_meters(double width_m_, double depth_m_, double height_m_) {
        width_m  = width_m_;
        depth_m  = depth_m_;
        height_m = height_m_;
    }

    void set_size_rack_units(double width_u, double depth_u, double height_u) {
        width_m  = width_u  * U_TO_M;
        depth_m  = depth_u  * U_TO_M;
        height_m = height_u * U_TO_M;
    }

    void set_size_inches(double width_in, double depth_in, double height_in) {
        width_m  = width_in  * IN_TO_M;
        depth_m  = depth_in  * IN_TO_M;
        height_m = height_in * IN_TO_M;
    }

    void set_size_mm(double width_mm, double depth_mm, double height_mm) {
        width_m  = width_mm  * MM_TO_M;
        depth_m  = depth_mm  * MM_TO_M;
        height_m = height_mm * MM_TO_M;
    }

    void set_coords_m(double x, double y, double z) {
        bot_left_corner_coords = {x, y, z};
    }

    void set_coords_rack_units(double x_u, double y_u, double z_u) {
        bot_left_corner_coords = {
            x_u * U_TO_M,
            y_u * U_TO_M,
            z_u * U_TO_M
        };
    }

    void set_coords_in(double x, double y, double z) {
        bot_left_corner_coords = {
            x * IN_TO_M,
            y * IN_TO_M,
            z * IN_TO_M
        };
    }

    void set_coords_mm(double x, double y, double z) {
        bot_left_corner_coords = {
            x * MM_TO_M,
            y * MM_TO_M,
            z * MM_TO_M
        };
    }

    void set_watts(double q) { component_watts = q; }
    void set_rho_solid(double r) { rho_solid = r; }
    void set_k_solid(double k) { k_solid = k; }
    void set_t(double t) { T = t; }
    void set_cp(double c) { cp = c; }
    void set_h(double hh) { h = hh; }
    void set_name(std::string s) { name = s; }

    double get_t() const { return T; }
    double get_rho() const { return rho_solid; }
    double get_k() const { return k_solid; }
    double get_h() const { return h; }
    double get_cp() const { return cp; }
    double get_watts() const { return component_watts; }

    double get_width_m()  const { return width_m; }
    double get_depth_m()  const { return depth_m; }
    double get_height_m() const { return height_m; }
    double get_width_u()  const { return width_m  /  U_TO_M; }
    double get_depth_u()  const { return depth_m  / U_TO_M; }
    double get_height_u() const { return height_m / U_TO_M; } 
    double get_width_in()  const { return width_m  / IN_TO_M; }
    double get_depth_in()  const { return depth_m  / IN_TO_M; }
    double get_height_in() const { return height_m / IN_TO_M; }
    double get_width_mm()  const { return width_m  / MM_TO_M; }
    double get_depth_mm()  const { return depth_m  / MM_TO_M; }
    double get_height_mm() const { return height_m / MM_TO_M; }

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
            if(r.get_region_type() == RegionType::Uninitialized) throw std::invalid_argument("Component: InternalRegion - Uninitialized region type.");
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
        constexpr double eps = 1e-9;

        std::array<double, 3> loc = local_to_global(r.get_local_position());
        std::array<double, 3> loc_size = r.get_size_m();
        if(loc[0] < bot_left_corner_coords[0] - eps|| loc[0] + loc_size[0] > bot_left_corner_coords[0] + width_m + eps) {
            std::cout << "loc x min: " << loc[0] << " comp x min: " << bot_left_corner_coords[0];
            std::cout << " loc x max: " << loc[0] + loc_size[0] << " comp x max: " << bot_left_corner_coords[0] + width_m << std::endl;
            throw std::invalid_argument("Component:InternalRegion " + r.get_name() + " - int region x component out of component x bounds.");
        }
        if(loc[1] < bot_left_corner_coords[1] - eps|| loc[1] + loc_size[1] > bot_left_corner_coords[1] + depth_m + eps) {
            std::cout << "loc y min: " << loc[1] << " comp y min: " << bot_left_corner_coords[1];
            std::cout << " loc y max: " << loc[1] + loc_size[1] << " comp y max: " << bot_left_corner_coords[1] + depth_m << std::endl;
            throw std::invalid_argument("Component:InternalRegion  " + r.get_name() + " - int region y component out of component y bounds.");
        }
        if(loc[2] < bot_left_corner_coords[2] - eps || loc[2] + loc_size[2] > bot_left_corner_coords[2] + height_m + eps) {
            std::cout << "loc z min: " << loc[2] << " comp z min: " << bot_left_corner_coords[2];
            std::cout << " loc z max: " << loc[2] + loc_size[2] << " comp z max: " << bot_left_corner_coords[2] + height_m << std::endl;
            throw std::invalid_argument("Component:InternalRegion  " + r.get_name() + " - int region z component out of component z bounds.");
        }
    }

    void validate_fan_vent(InternalRegion r) {
        std::array<double, 3> loc = local_to_global(r.get_local_position());
        std::array<double, 3> loc_size = r.get_size_m();
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
