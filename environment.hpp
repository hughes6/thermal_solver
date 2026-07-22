#ifndef ENVIRONMENT_HPP
#define ENVIRONMENT_HPP

#include <stdexcept>

struct Environment {
    
    Environment() :
        humidity(0.0),
        elevation(0.0),
        T_ambient(0.0),
        cp(0.0),
        k(0.0),
        mu(0.0),
        pr(0.0),
        rho(0.0) {}

    Environment(double humidity_, double elevation_, double T_ambient_, double cp_, double k_, double mu_, double pr_, double rho_) 
    {
        set_humidity_pct(humidity_);
        set_elevation_ft(elevation_);
        set_T_ambient_C(T_ambient_);
        set_cp_J_kgK(cp_);
        set_k_W_mK(k_);
        set_mu_Pa_s(mu_);
        set_pr(pr_);
        set_rho_kg_m3(rho_);
        }

    double get_humidity_pct() const { return humidity; }
    double get_elevation_ft() const { return elevation; }
    double get_T_ambient() const { return T_ambient; }
    double get_cp() const { return cp; }
    double get_k() const { return k; }
    double get_mu() const { return mu; }
    double get_pr() const { return pr; }
    double get_rho() const { return rho; }
    double get_ambient_pressure() const {
        constexpr double p0 = 101325.0;      // Pa
        constexpr double T0 = 288.15;        // K
        constexpr double lapse_rate = 0.0065; // K/m
        constexpr double gravity = 9.80665;  // m/s^2
        constexpr double molar_mass = 0.0289644; // kg/mol
        constexpr double gas_constant = 8.3144598; // J/(mol K)
        constexpr double feet_to_meters = 0.3048;

        const double altitude_m = elevation * feet_to_meters;

        return p0 * std::pow(
            1.0 - lapse_rate * altitude_m / T0,
            gravity * molar_mass / (gas_constant * lapse_rate)
        );
    }

    void set_humidity_pct(double h) {
        if(h < 0 || h > 100) {
            throw std::invalid_argument("invalid humidity: no where a server rack will be below 0% or above 100% humidity.");
        } else {
            humidity = h;
        }
    }
    void set_elevation_ft(double e) {
        if(e < -1443 || e > 15000) {
            throw std::invalid_argument("invalid elevation: no where a server rack will be below -1443 ft or above 15,000 ft.");
        } else {
            elevation = e;
        }
    }
    void set_T_ambient_C(double T) {
        if(T < 0 || T > 50) {
            throw std::invalid_argument("invalid ambient temp: no where a server rack will be below 0 C or above 50 C.");
        } else {
            T_ambient = T;
        }
    }
    void set_cp_J_kgK(double c) {
        if(c <= 0.0 || c > 2000) {
            throw std::invalid_argument("invalid specific heat capacity: no where a server rack will be below 0 J/(kg*K) or above 2,000 J/(kg*K).");
        } else {
            cp = c;
        }
    }
    void set_k_W_mK(double k_) {
        if(k_ < 0 || k_ > 0.5) {
            throw std::invalid_argument("invalid thermal conductivity: no where a server rack will be below 0 W/(m*K) or above 0.5 W/(m*K).");
        } else {
            k = k_;
        }
    }
    void set_mu_Pa_s(double m) {
        if(m < 0 || m > 0.01) {
            throw std::invalid_argument("invalid viscosity: no where a server rack will be below 0 Pa/s or above 0.01 Pa/s.");
        } else {
            mu = m;
        }
    }
    void set_pr(double p) {
        if(p < 0.5 || p > 0.9) {
            throw std::invalid_argument("invalid prandlt number: no where a server rack will be below 0.5 or above 0.9 Pr.");
        } else {
            pr = p;
        }
    }
    void set_rho_kg_m3(double r) {
        if(r < 0.3 || r > 1.5) {
            throw std::invalid_argument("invalid density: no where a server rack will be below 0.3 kg/m^3 or above 1.5 kg/m^3.");
        } else {
            rho = r;
        }
    }
    
private:
    double humidity;
    double elevation;
    double T_ambient;
    double cp;
    double k;
    double mu;
    double pr;
    double rho;
};

#endif
