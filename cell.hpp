#ifndef CELL_HPP
#define CELL_HPP

class Cell {
public:
    enum class State {
        Uninitialized,
        Air,
        Component,
        Fan,
        Intake,
        Exhaust,
        Wall, 
        Vent
    };

    Cell()
        : T(0.0),
          rho(0.0),
          cp(0.0),
          k(0.0),
          h(0.0),
          qdot(0.0),
          cell_state(State::Uninitialized),
          dx(0.0),
          dy(0.0),
          dz(0.0),
          vx(0.0),
          vy(0.0),
          vz(0.0)
    {}

    Cell(double T,
         double rho,
         double cp,
         double k,
         double h,
         double qdot,
         State s,
         double dx,
         double dy,
         double dz)
        : T(T),
          rho(rho),
          cp(cp),
          k(k),
          h(h),
          qdot(qdot),
          cell_state(s),
          dx(dx),
          dy(dy),
          dz(dz),
          vx(0.0),
          vy(0.0),
          vz(0.0)
    {}

    //========================
    // Getters
    //========================

    double get_T() const { return T; }
    double get_rho() const { return rho; }
    double get_cp() const { return cp; }
    double get_k() const { return k; }
    double get_h() const { return h; }
    double get_qdot() const { return qdot; }

    double get_dx() const { return dx; }
    double get_dy() const { return dy; }
    double get_dz() const { return dz; }

    double get_vx() const { return vx; }
    double get_vy() const { return vy; }
    double get_vz() const { return vz; }
    
    bool is_fluid() const { return cell_state == State::Air || cell_state == State::Fan ||
                                   cell_state == State::Exhaust || cell_state == State::Intake ||
                                   cell_state == State::Vent || cell_state == State::Wall; }
    bool is_air() const { return cell_state == State::Air; }
    bool is_solid() const { return cell_state == State::Component || cell_state == State::Wall; }
    bool is_fan() const { return cell_state == State::Fan; }
    bool is_intake() const { return cell_state == State::Intake; }
    bool is_exhaust() const { return cell_state == State::Exhaust; }

    State get_state() const { return cell_state; }

    //========================
    // Setters
    //========================

    void set_T(double t) { T = t; }
    void set_rho(double r) { rho = r; }
    void set_cp(double c) { cp = c; }
    void set_k(double kk) { k = kk; }
    void set_h(double hh) { h = hh; }
    void set_qdot(double q) { qdot = q; }

    void set_dx(double x) { dx = x; }
    void set_dy(double y) { dy = y; }
    void set_dz(double z) { dz = z; }

    void set_vx(double x) { vx = x; }
    void set_vy(double y) { vy = y; }
    void set_vz(double z) { vz = z; }

    void set_state(State s) { cell_state = s; }

    //========================
    // Geometry
    //========================
    double volume() const { return dx * dy * dz; }
    double area_x() const { return dy * dz; }
    double area_y() const { return dx * dz; }
    double area_z() const { return dx * dy; }

private:

    // Thermal state
    double T;          // Temperature (°C)
    double rho;        // Density (kg/m^3)
    double cp;         // Specific heat (J/kg-K)
    double k;          // Thermal conductivity (W/m-K)
    double h;          // Convection coefficient (W/m^2-K)
    double qdot;       // Heat generation (W/m^3)

    bool is_component;

    // Component state
    State cell_state;

    // Cell dimensions (m)
    double dx;
    double dy;
    double dz;

    // Air velocity (m/s)
    // Currently unused but ready for advection.
    double vx;
    double vy;
    double vz;
};

#endif