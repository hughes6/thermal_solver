#ifndef RACK_HPP
#define RACK_HPP

class Rack {
public:
    static constexpr double U_TO_M  = 1.75 * 0.0254;
    static constexpr double IN_TO_M = 0.0254;
    static constexpr double MM_TO_M = 0.001;

    Rack() = default;

    static Rack from_rack_units(double width_u, double depth_u, double height_u, std::string name = "rack") {
        Rack r;
        r.set_size_rack_units(width_u, depth_u, height_u);
        r.set_name(name);
        return r;
    }
    
    static Rack from_meters(double width_m, double depth_m, double height_m, std::string name = "rack") {
        Rack r;
        r.set_size_meters(width_m, depth_m, height_m);
        r.set_name(name);
        return r;
    }

    static Rack from_inches(double width_in, double depth_in, double height_in, std::string name = "rack") {
        Rack r;
        r.set_size_inches(width_in, depth_in, height_in);
        r.set_name(name);
        return r;
    }

    static Rack from_mm(double width_mm, double depth_mm, double height_mm, std::string name = "rack") {
        Rack r;
        r.set_size_mm(width_mm, depth_mm, height_mm);
        r.set_name(name);
        return r;
    }

    void set_size_rack_units(double width_u, double depth_u, double height_u) {
        width_m  = width_u * U_TO_M;
        depth_m  = depth_u * U_TO_M;
        height_m = height_u * U_TO_M;
    }

    void set_size_meters(double width_m_, double depth_m_, double height_m_) {
        width_m  = width_m_;
        depth_m  = depth_m_;
        height_m = height_m_;
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


    void set_name(std::string name_) { name = name_; }
    void set_t(double t)   { ambient_temp = t; }
    void set_rho(double r) { rho = r; }
    void set_cp(double c)  { cp = c; }
    void set_k(double kk)  { k = kk; }
    void set_h(double hh)  { h = hh; }

    std::string get_name() const { return name; }
    double get_t()   const { return ambient_temp; }
    double get_k()   const { return k; }
    double get_h()   const { return h; }
    double get_rho() const { return rho; }
    double get_cp()  const { return cp; }
    double volume() const { return height_m * width_m * depth_m; }

private:
    std::string name;
    double height_m = 0.0;
    double width_m  = 0.0;
    double depth_m  = 0.0;

    double ambient_temp = 20.0; // °C
    double rho = 0.0;           // kg/m^3
    double cp  = 0.0;           // J/(kg·K)
    double k   = 0.0;           // W/(m·K)
    double h   = 0.0;           // W/(m^2·K)
};

#endif