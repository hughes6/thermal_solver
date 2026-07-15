#ifndef RACK_HPP
#define RACK_HPP

class Rack {
public:
    static constexpr double U_TO_M  = 1.75 * 0.0254;
    static constexpr double IN_TO_M = 0.0254;
    static constexpr double M_TO_IN = 1.0 / 0.0254;

    Rack() = default;

    static Rack from_rack_units(double height_u,
                                double width_in,
                                double depth_in) {
        Rack r;
        r.set_size_rack_units(height_u, width_in, depth_in);
        return r;
    }

    static Rack from_meters(double height_m,
                            double width_m,
                            double depth_m) {
        Rack r;
        r.set_size_meters(height_m, width_m, depth_m);
        return r;
    }

    void set_size_rack_units(double height_u,
                             double width_in,
                             double depth_in) {
        height_m = height_u * U_TO_M;
        width_m  = width_in * IN_TO_M;
        depth_m  = depth_in * IN_TO_M;
    }

    void set_size_meters(double h, double w, double d) {
        height_m = h;
        width_m  = w;
        depth_m  = d;
    }

    double get_height_m() const { return height_m; }
    double get_width_m()  const { return width_m; }
    double get_depth_m()  const { return depth_m; }

    double get_height_u() const { return height_m / U_TO_M; }
    double get_width_in() const { return width_m / IN_TO_M; }
    double get_depth_in() const { return depth_m / IN_TO_M; }

    void set_t(double t)   { ambient_temp = t; }
    void set_rho(double r) { rho = r; }
    void set_cp(double c)  { cp = c; }
    void set_k(double kk)  { k = kk; }
    void set_h(double hh)  { h = hh; }

    double get_t()   const { return ambient_temp; }
    double get_k()   const { return k; }
    double get_h()   const { return h; }
    double get_rho() const { return rho; }
    double get_cp()  const { return cp; }

    void set_top_fans(int n, double cfm_per_fan) {
        top_fans.has_fans = true;
        top_fans.n_fans = n;
        top_fans.cfm_per_fan = cfm_per_fan;
    }

    bool has_top_fans() const { return top_fans.has_fans; }
    int get_n_top_fans() const { return top_fans.n_fans; }
    double get_cfm_per_top_fan() const { return top_fans.cfm_per_fan; }
    double total_top_fan_cfm() const { return top_fans.n_fans * top_fans.cfm_per_fan; }
    double volume() const { return height_m * width_m * depth_m; }

private:
    double height_m = 0.0;
    double width_m  = 0.0;
    double depth_m  = 0.0;

    double ambient_temp = 20.0; // °C
    double rho = 0.0;           // kg/m^3
    double cp  = 0.0;           // J/(kg·K)
    double k   = 0.0;           // W/(m·K)
    double h   = 0.0;           // W/(m^2·K)

    struct Fans {
        bool has_fans = false;
        int n_fans = 0;
        double cfm_per_fan = 0.0;
    };

    Fans top_fans;
};

#endif