#ifndef THERMAL_TIME_ESTIMATOR_HPP
#define THERMAL_TIME_ESTIMATOR_HPP

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include "convection.hpp"
#include "mesh.hpp"

// Lumped-capacitance estimate: treats all solid mass in the mesh as one
// thermal capacitance cooled through its total solid/air interface area
// at one representative convection coefficient. That's a genuine
// simplification (real geometry has multiple time constants, not one),
// but it's the right order-of-magnitude tool for picking a starting
// sim_length rather than guessing - a lumped RC network reaches ~99% of
// its way to steady state at 5*tau, ~98% at 4*tau.
//
// Also reports the max stable dt for conduction (CFL <= 0.5) and
// advection (CFL <= 1.0) from the mesh's own current state, using the
// same stability formulas Solver's check_*_stability() already use - so
// "can I bump dt up" gets an actual computed answer instead of a guess.
struct ThermalTimeEstimate {
    double thermal_mass_J_per_K = 0.0;   // sum(rho*cp*volume) over solid cells
    double interface_area_m2 = 0.0;      // total solid/fluid face area
    double h_estimate_W_m2K = 0.0;       // average convection coeff across that interface
    double tau_seconds = 0.0;
    double recommended_sim_length_s = 0.0; // 5 * tau

    double max_stable_dt_conduction_s = std::numeric_limits<double>::infinity();
    double max_stable_dt_advection_s  = std::numeric_limits<double>::infinity();

    void print() const {
        std::cout << "----- Thermal Time Estimate -----\n";
        std::cout << "Solid thermal mass:         " << thermal_mass_J_per_K << " J/K\n";
        std::cout << "Solid/air interface area:   " << interface_area_m2 << " m^2\n";
        std::cout << "Representative h:           " << h_estimate_W_m2K << " W/m^2K\n";
        std::cout << "Estimated tau:              " << tau_seconds << " s\n";
        std::cout << "Recommended sim_length:     " << recommended_sim_length_s << " s (~5*tau)\n";
        std::cout << "Max stable dt (conduction): " << max_stable_dt_conduction_s << " s\n";
        std::cout << "Max stable dt (advection):  " << max_stable_dt_advection_s << " s\n";
        std::cout << "----------------------------------\n";
        std::cout << "This is a single-tau, lumped-mass estimate - a starting point,\n"
                     "not a substitute for watching the logger's summary output actually\n"
                     "flatten out. Real geometry has more than one time constant.\n";
    }
};

struct ThermalTimeEstimator {
    static ThermalTimeEstimate estimate(const Mesh& mesh) {
        ThermalTimeEstimate result;

        double h_sum = 0.0;
        int h_count = 0;
        double max_conduction_rate = 0.0; // alpha * sum(1/d^2), per cell, worst case
        double max_advection_rate = 0.0;  // |v| / min(dx,dy,dz), per cell, worst case

        static const int di[6] = {1,-1,0,0,0,0};
        static const int dj[6] = {0,0,1,-1,0,0};
        static const int dk[6] = {0,0,0,0,1,-1};

        for (int i = 0; i < mesh.get_nx(); ++i) {
            for (int j = 0; j < mesh.get_ny(); ++j) {
                for (int k = 0; k < mesh.get_nz(); ++k) {
                    const Cell& c = mesh.at(i, j, k);

                    if (c.is_solid()) {
                        result.thermal_mass_J_per_K += c.get_rho() * c.get_cp() * c.volume();

                        if (c.get_rho() > 0.0 && c.get_cp() > 0.0) {
                            double alpha = c.get_k() / (c.get_rho() * c.get_cp());
                            double sum_inv_d2 =
                                1.0 / (c.get_dx()*c.get_dx()) +
                                1.0 / (c.get_dy()*c.get_dy()) +
                                1.0 / (c.get_dz()*c.get_dz());
                            max_conduction_rate = std::max(max_conduction_rate, alpha * sum_inv_d2);
                        }

                        for (int f = 0; f < 6; ++f) {
                            int ni = i + di[f], nj = j + dj[f], nk = k + dk[f];
                            if (!mesh.in_bounds(ni, nj, nk)) continue;
                            const Cell& n = mesh.at(ni, nj, nk);
                            if (!n.is_fluid()) continue;

                            double area = (di[f] != 0) ? c.area_x()
                                        : (dj[f] != 0) ? c.area_y()
                                                       : c.area_z();
                            result.interface_area_m2 += area;

                            double char_length = (di[f] != 0) ? n.get_dx()
                                                : (dj[f] != 0) ? n.get_dy()
                                                               : n.get_dz();
                            double vmag = std::sqrt(n.get_vx()*n.get_vx() +
                                                     n.get_vy()*n.get_vy() +
                                                     n.get_vz()*n.get_vz());
                            double delta_T = std::max(std::abs(c.get_T() - n.get_T()), 1.0);
                            double t_film_k = (c.get_T() + n.get_T()) / 2.0 + 273.15;

                            double h = Convection::compute_local_h(
                                vmag, char_length,
                                Convection::AIR_RHO, Convection::AIR_MU, Convection::AIR_K,
                                Convection::AIR_PR, delta_T, t_film_k);

                            h_sum += h;
                            ++h_count;
                        }
                    }

                    if (c.is_fluid()) {
                        double vmag = std::sqrt(c.get_vx()*c.get_vx() +
                                                 c.get_vy()*c.get_vy() +
                                                 c.get_vz()*c.get_vz());
                        double min_d = std::min({c.get_dx(), c.get_dy(), c.get_dz()});
                        if (min_d > 0.0) {
                            max_advection_rate = std::max(max_advection_rate, vmag / min_d);
                        }
                    }
                }
            }
        }

        result.h_estimate_W_m2K = (h_count > 0) ? (h_sum / h_count) : 5.0; // weak natural-convection fallback

        double denom = result.h_estimate_W_m2K * result.interface_area_m2;
        result.tau_seconds = (denom > 0.0) ? (result.thermal_mass_J_per_K / denom) : 0.0;
        result.recommended_sim_length_s = 5.0 * result.tau_seconds;

        result.max_stable_dt_conduction_s =
            (max_conduction_rate > 0.0) ? (0.5 / max_conduction_rate)
                                         : std::numeric_limits<double>::infinity();
        result.max_stable_dt_advection_s =
            (max_advection_rate > 0.0) ? (1.0 / max_advection_rate)
                                        : std::numeric_limits<double>::infinity();

        return result;
    }
};

#endif