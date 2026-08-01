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
    double recommended_dt_s = std::numeric_limits<double>::infinity();

    // Realized mesh diagnostics. Geometry-aligned cuts can create cells
    // smaller than the configured fine_dx (for example, 5 mm walls).
    double min_cell_dx_m = std::numeric_limits<double>::infinity();
    double min_cell_dy_m = std::numeric_limits<double>::infinity();
    double min_cell_dz_m = std::numeric_limits<double>::infinity();
    std::size_t mesh_cell_count = 0;
    std::size_t mesh_memory_bytes = 0;

    void print() const {
        std::cout << "----- Thermal Time Estimate -----\n";
        std::cout << "Solid thermal mass:         " << thermal_mass_J_per_K << " J/K\n";
        std::cout << "Solid/air interface area:   " << interface_area_m2 << " m^2\n";
        std::cout << "Representative h:           " << h_estimate_W_m2K << " W/m^2K\n";
        std::cout << "Estimated tau:              " << tau_seconds << " s\n";
        std::cout << "Recommended sim_length:     " << recommended_sim_length_s << " s (~5*tau)\n";
        std::cout << "Minimum realized dx:       " << min_cell_dx_m << " m\n";
        std::cout << "Minimum realized dy:       " << min_cell_dy_m << " m\n";
        std::cout << "Minimum realized dz:       " << min_cell_dz_m << " m\n";
        std::cout << "Mesh cell count:           " << mesh_cell_count << "\n";
        std::cout << "Approx. cell memory:       " << mesh_memory_bytes
                  << " bytes (" << mesh_memory_bytes / (1024.0 * 1024.0) << " MiB)\n";
        std::cout << "Max stable dt (conduction): " << max_stable_dt_conduction_s << " s\n";
        std::cout << "Max stable dt (advection):  " << max_stable_dt_advection_s << " s\n";
        std::cout << "Recommended dt (80% limit): " << recommended_dt_s << " s\n";
        std::cout << "----------------------------------\n";
        std::cout << "This is a single-tau, lumped-mass estimate - a starting point,\n"
                     "not a substitute for watching the logger's summary output actually\n"
                     "flatten out. Real geometry has more than one time constant.\n";
    }
};

struct ThermalTimeEstimator {
    static ThermalTimeEstimate estimate(const Mesh& mesh) {
        ThermalTimeEstimate result;
        result.mesh_cell_count =
            static_cast<std::size_t>(mesh.get_nx()) *
            static_cast<std::size_t>(mesh.get_ny()) *
            static_cast<std::size_t>(mesh.get_nz());
        result.mesh_memory_bytes =
            result.mesh_cell_count * sizeof(Cell) +
            mesh.get_face_wall_memory_byte();

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
                    result.min_cell_dx_m = std::min(result.min_cell_dx_m, c.get_dx());
                    result.min_cell_dy_m = std::min(result.min_cell_dy_m, c.get_dy());
                    result.min_cell_dz_m = std::min(result.min_cell_dz_m, c.get_dz());

                    if (c.get_rho() > 0.0 && c.get_cp() > 0.0) {
                        const double alpha =
                            c.get_k() / (c.get_rho() * c.get_cp());
                        const double sum_inv_d2 =
                            1.0 / (c.get_dx()*c.get_dx()) +
                            1.0 / (c.get_dy()*c.get_dy()) +
                            1.0 / (c.get_dz()*c.get_dz());
                        max_conduction_rate = std::max(
                            max_conduction_rate,alpha*sum_inv_d2);
                    }

                    if (c.is_solid()) {
                        result.thermal_mass_J_per_K += c.get_rho() * c.get_cp() * c.volume();

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

        // Coarse face walls carry thermal mass without occupying cells.
        // Include both exposed sides in the convection-area estimate and
        // include the wall-node explicit update in the conduction dt limit.
        for(const Mesh::WallFace& wall : mesh.get_wall_faces()) {
            if(!wall.active) continue;
            const int hi = wall.x + (wall.axis == 0);
            const int hj = wall.y + (wall.axis == 1);
            const int hk = wall.z + (wall.axis == 2);
            const Cell& low = mesh.at(wall.x, wall.y, wall.z);
            const Cell& high = mesh.at(hi, hj, hk);
            const double area = mesh.wall_face_area(wall);
            const double capacitance =
                wall.rho * wall.cp * area * wall.thickness;
            result.thermal_mass_J_per_K += capacitance;

            const Cell* sides[2]{&low, &high};
            for(const Cell* side : sides) {
                if(!side->is_fluid()) continue;
                result.interface_area_m2 += area;
                const double width =
                    wall.axis == 0 ? side->get_dx() :
                    wall.axis == 1 ? side->get_dy() : side->get_dz();
                const double vmag = std::sqrt(
                    side->get_vx()*side->get_vx() +
                    side->get_vy()*side->get_vy() +
                    side->get_vz()*side->get_vz());
                const double delta_T =
                    std::max(std::abs(wall.temperature-side->get_T()),1.0);
                const double film =
                    0.5*(wall.temperature+side->get_T())+273.15;
                h_sum += Convection::compute_local_h(
                    vmag,width,Convection::AIR_RHO,Convection::AIR_MU,
                    Convection::AIR_K,Convection::AIR_PR,delta_T,film);
                ++h_count;
            }

            if(capacitance > 0.0) {
                const double low_width =
                    wall.axis == 0 ? low.get_dx() :
                    wall.axis == 1 ? low.get_dy() : low.get_dz();
                const double high_width =
                    wall.axis == 0 ? high.get_dx() :
                    wall.axis == 1 ? high.get_dy() : high.get_dz();
                const double wall_half =
                    0.5*wall.thickness /
                    std::max(wall.conductivity,1e-12);
                const double glow = area /
                    (0.5*low_width/std::max(low.get_k(),1e-12)+wall_half);
                const double ghigh = area /
                    (0.5*high_width/std::max(high.get_k(),1e-12)+wall_half);
                max_conduction_rate = std::max(
                    max_conduction_rate,(glow+ghigh)/capacitance);
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

        const double limiting_dt = std::min(
            result.max_stable_dt_conduction_s,
            result.max_stable_dt_advection_s);
        result.recommended_dt_s = std::isfinite(limiting_dt)
            ? 0.8 * limiting_dt
            : std::numeric_limits<double>::infinity();

        return result;
    }
};

#endif
