#ifndef SOLVER_HPP
#define SOLVER_HPP

#include "convection.hpp"
#include "mesh.hpp"

class Solver {
public:
    Solver() {}
    
    Solver(Mesh initial_mesh, double dt, double sim_length)
        : current(std::move(initial_mesh)),
          next(current),
          dt(dt),
          sim_length(sim_length),
          logfile("simulation.csv")
    { logfile << "step,time,x,y,z,T,qdot,is_component,k,rho,cp\n";
      logfile << "dx," << current.get_dx()
        << ",dy," << current.get_dy()
        << ",dz," << current.get_dz()
        << '\n'; }

    void apply_bulk_velocity(double vx, double vy, double vz) {
        for(int x = 0; x < current.get_nx(); ++x) {
            for(int y = 0; y < current.get_ny(); ++y) {
                for(int z = 0; z < current.get_nz(); ++z) {
                    Cell& c = current.at(x,y,z);

                    if(c.is_fluid()) {
                        c.set_vx(vx);
                        c.set_vy(vy);
                        c.set_vz(vz);
                    }
                }
            }
        }
        next = current;
    }


    void check_advection_stability() const {
        // C = vz * dt / dz
        double max_C = 0.0;
        for(const Cell c : current.get_cells()) {
            if(!c.is_fluid()) continue;
            double Cx = std::abs(c.get_vx()) * dt / current.get_dx();
            double Cy = std::abs(c.get_vy()) * dt / current.get_dy();
            double Cz = std::abs(c.get_vz()) * dt / current.get_dz();
            double C = Cx + Cy + Cz;
            if(C > max_C) {
                max_C = C;
            }
            if(C > 1.0) {
                std::cerr << "WARNING: advection unstable. CFL = "
                          << C << " > 1.0\n";
                return;
            }
        }
        std::cout << "Advection CFL max = " << max_C << '\n';
    }

    void check_conduction_stability() const {
        // C = a * dt / dx     a = k / (rho * cp)
        double max_C = 0.0;
        for(const Cell c : current.get_cells()) {
            if(!c.is_solid()) continue;
            double a = c.get_k() / (c.get_rho() * c.get_cp());
            double Cx = (a * dt) / (current.get_dx()*current.get_dx());
            double Cy = (a * dt) / (current.get_dy()*current.get_dy());
            double Cz = (a * dt) / (current.get_dz()*current.get_dz());
            double C = Cx + Cy + Cz;
            if(C > max_C) {
                max_C = C;
            }
            if(C > 0.5) {
                std::cerr << "WARNING: condection unstable. CFL = "
                          << C << " > 0.5\n";
                return;
            }
        }
        std::cout << "Conduction CFL max = " << max_C << '\n';
    }

    const Mesh& get_mesh() const {
        return current;
    }

    
    void solve() {
        check_advection_stability();
        check_conduction_stability();

        int steps = static_cast<int>(sim_length / dt);
        log_state(0);
        
        for(int step = 0; step < steps; step++) {
            for(int x = 0; x < current.get_nx(); x++) {
                for(int y = 0; y < current.get_ny(); y++) {
                    for(int z = 0; z < current.get_nz(); z++) {
                        double T_new = compute_t_next(x,y,z);
                        next.at(x,y,z).set_T(T_new);
                    }
                }
            }
            std::swap(current, next);
            log_state(step);
        }
    }
private:
    Mesh current;
    Mesh next;

    double dt;
    double sim_length;
    
    std::ofstream logfile;

    double compute_t_next(int x, int y, int z) {
        const Cell& c = current.at(x, y, z);
        double T = c.get_T();
        double Qcond = 0.0, Qconv = 0.0, Qgen = 0.0;
        Qcond = compute_conduction(x, y, z);
        Qgen  = c.get_qdot() * current.cell_volume();
        Qconv = compute_convection(x, y, z);
        double denom = c.get_rho() * c.get_cp() * current.cell_volume();
        if(denom <= 0.0) {
            return T;
        }

        double dTdt = (Qcond + Qconv + Qgen) / denom;
        double dTdt_advection = compute_advection(x, y, z);
        dTdt_advection = 0.0;
        return T + dt * (dTdt + dTdt_advection);
    }

    double compute_advection(int x, int y, int z) {
        const Cell& c = current.at(x, y, z);
        if(!c.is_fluid()) {
            return 0.0;
        }
        double T = c.get_T();
        double vx = c.get_vx();
        double vy = c.get_vy();
        double vz = c.get_vz();
        double dTdt = 0.0;

        // x dir
        if(vx > 0 && current.in_bounds(x-1, y, z)) {
            double Tup = current.at(x-1, y, z).get_T();
            dTdt += -vx * (T- Tup) / current.get_dx();
        }
        else if(vx < 0 && current.in_bounds(x + 1, y, z)) {
            double Tup = current.at(x + 1, y, z).get_T();
            dTdt += -vx * (Tup - T) / current.get_dx();
        }

        // y direction
        if(vy > 0 && current.in_bounds(x, y - 1, z)) {
            double Tup = current.at(x, y - 1, z).get_T();
            dTdt += -vy * (T - Tup) / current.get_dy();
        }
        else if(vy < 0 && current.in_bounds(x, y + 1, z)) {
            double Tup = current.at(x, y + 1, z).get_T();
            dTdt += -vy * (Tup - T) / current.get_dy();
        }

        // z direction
        if(vz > 0 && current.in_bounds(x, y, z - 1)) {
            double Tup = current.at(x, y, z - 1).get_T();
            dTdt += -vz * (T - Tup) / current.get_dz();
        }
        else if(vz < 0 && current.in_bounds(x, y, z + 1)) {
            double Tup = current.at(x, y, z + 1).get_T();
            dTdt += -vz * (Tup - T) / current.get_dz();
        }
        return dTdt;
    }

    double compute_conduction(int x, int y, int z) {
        const Cell& c = current.at(x, y, z);
        double T = c.get_T();
        double k = c.get_k();

        double Q = 0.0;

        auto add_neighbor = [&](int nx, int ny, int nz, double area, double dist) {
            if(!current.in_bounds(nx, ny, nz)) {
                return;
            }
            const Cell& n = current.at(nx, ny, nz);
            double Tn = n.get_T();
            double kn = n.get_k();
            if(k <= 0.0 || kn <= 0.0) {
                return;
            }
            double k_face = 2.0 * k * kn / (k + kn);
            Q += k_face * area * (Tn - T) / dist;
        };

        add_neighbor(x + 1, y, z, current.area_x(), current.get_dx());
        add_neighbor(x - 1, y, z, current.area_x(), current.get_dx());

        add_neighbor(x, y + 1, z, current.area_y(), current.get_dy());
        add_neighbor(x, y - 1, z, current.area_y(), current.get_dy());

        add_neighbor(x, y, z + 1, current.area_z(), current.get_dz());
        add_neighbor(x, y, z - 1, current.area_z(), current.get_dz());

        return Q;
    }

    double compute_convection(int x, int y, int z) {
        const Cell& c = current.at(x, y, z);
        double T = c.get_T();
        double Q = 0.0;

        auto add_neighbor = [&](int nx, int ny, int nz, double area, double char_length) {
            if(!current.in_bounds(nx, ny, nz)) {
                return;
            }

            const Cell& n = current.at(nx, ny, nz);

            bool c_solid = c.is_solid();
            bool n_solid = n.is_solid();

            // Only convection across solid-air interfaces or air-air
            if(c_solid == n_solid) { return; }

            const Cell& air_cell = c_solid ? n : c;
            const Cell& solid_cell = c_solid ? c : n;

            double vmag = std::sqrt(air_cell.get_vx() * air_cell.get_vx() + 
                                    air_cell.get_vy() * air_cell.get_vy() +
                                    air_cell.get_vz() + air_cell.get_vz());
            double delta_T = std::abs(solid_cell.get_T() - air_cell.get_T());
            double t_film_k = (solid_cell.get_T() + air_cell.get_T()) / 2.0 + 273.15;

            double h_c = Convection::compute_local_h(vmag, char_length, Convection::AIR_RHO, Convection::AIR_MU, Convection::AIR_K, Convection::AIR_PR, delta_T, t_film_k);
            double h_n = Convection::compute_local_h(vmag, char_length, Convection::AIR_RHO, Convection::AIR_MU, Convection::AIR_K, Convection::AIR_PR, delta_T, t_film_k);

            double h_face = (h_c + h_n > 0.0) ? (2.0 * h_c * h_n / (h_c + h_n)) : 0.0;
            Q += h_face * area * (n.get_T() - T);
        };

        add_neighbor(x + 1, y, z, current.area_x(), current.get_dx());
        add_neighbor(x - 1, y, z, current.area_x(), current.get_dx());

        add_neighbor(x, y + 1, z, current.area_y(), current.get_dy());
        add_neighbor(x, y - 1, z, current.area_y(), current.get_dy());

        add_neighbor(x, y, z + 1, current.area_z(), current.get_dz());
        add_neighbor(x, y, z - 1, current.area_z(), current.get_dz());

        return Q;
    }

    void log_state(int step) {
        double time = step * dt;
        for(int x = 0; x < current.get_nx(); x++) {
            for(int y = 0; y < current.get_ny(); y++) {
                for(int z = 0; z < current.get_nz(); z++) {
                    const Cell& cell = current.at(x,y,z);
                    logfile
                        << step << ','
                        << time << ','
                        << x << ','
                        << y << ','
                        << z << ','
                        << cell.get_T() << ','
                        << cell.get_qdot() << ','
                        << cell.is_solid() << ','
                        << cell.get_k() << ','
                        << cell.get_rho() << ','
                        << cell.get_cp()
                        << '\n';
                }
            }
        }
    }
};

#endif
