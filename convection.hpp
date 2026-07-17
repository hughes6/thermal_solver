#ifndef CONVECTION_HPP
#define CONVECTION_HPP

#include <cmath>

namespace Convection {
constexpr double RE_NATURAL_THRESHOLD = 10.0;
constexpr double RE_TURBULENT         = 4000;
constexpr double G_ACCEL              = 9.81;
constexpr double AIR_RHO              = 1.2;
constexpr double AIR_MU               = 1.8e-5;
constexpr double AIR_K                = 0.025;
constexpr double AIR_PR               = 0.71;

inline double local_reynolds(double velocity_mag, double char_length, double rho, double mu) {
    if (mu <= 0.0 || char_length <= 0.0) return 0.0;
    return (rho * velocity_mag * char_length) / mu;
}

inline double local_grashoff(double delta_T, double char_length, double t_film_K, double nu_kin) {
    if(t_film_K <= 0.0 || nu_kin <= 0.0) return 0.0;
    double beta = 1.0 / t_film_K;
    double L3 = char_length * char_length * char_length;
    return (G_ACCEL * beta * delta_T * L3) / (nu_kin * nu_kin);
}

inline double local_nusselt(double Re, double Pr, double Gr, bool heating = true) {
    // natural convection
    if (Re < RE_NATURAL_THRESHOLD) {
        double Ra = Gr * Pr;
        // no velocity or temp diff -> no driving force for convection at all
        if (Ra <= 0.0) return 1.0;

        if (Ra < 1.0e9) {
            return 0.59 * std::pow(Ra, 0.25);
        } else {
            return 0.10 * std::pow(Ra, 1.0 / 3.0);
        }
    }
    // turbulent
    if (Re >= RE_TURBULENT) {
        double n = heating ? 0.4 : 0.3;
        return 0.023 * std::pow(Re, 0.8) * std::pow(Pr, n);
    }

    // laminar
    return 4.36;
}

inline double local_h(double Nu, double k_air, double char_length) {
    if (char_length <= 0.0) return 0.0;
    return Nu * k_air / char_length;
}

inline double compute_local_h(double velocity_mag, double char_length, double rho, double mu, double k_air,
                              double Pr, double delta_T, double t_film_K, bool heating = true) {
        double Re = local_reynolds(velocity_mag, char_length, rho, mu);
        double Gr = 0.0;
        if (Re < RE_NATURAL_THRESHOLD) {
            double nu_kin = (rho > 0.0) ? (mu / rho) : 0.0;
            Gr = local_grashoff(delta_T, char_length, t_film_K, nu_kin);
        }
        double Nu = local_nusselt(Re, Pr, Gr, heating);
        return local_h(Nu, k_air, char_length);
    }
} 

#endif