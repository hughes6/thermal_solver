#ifndef SOLVER_HPP
#define SOLVER_HPP

#include <stdexcept>

#include "convection.hpp"
#include "mesh.hpp"
#include "workload.hpp"

class Solver {
public:
    Solver() {}
    
    Solver(Mesh initial_mesh, double dt_, double sim_length_, bool print_convections_, int output_interval_)
        : current(std::move(initial_mesh)),
          next(current),
          dt(dt_),
          sim_length(sim_length_),
          print_convections(print_convections_),
          output_interval(output_interval_),
          logfile("simulation.csv")
    { 
      load = current.get_load();
      validate_computational_workload();
      logfile << "step,time,x,y,z,T,qdot,is_component,k,rho,cp,vx,vy,vz,h\n";
      logfile << "dx," << current.get_dx()
        << ",dy," << current.get_dy()
        << ",dz," << current.get_dz()
        << '\n';
    }

    void validate_computational_workload() {
        std::size_t timesteps = get_timestep_count(); // sim length / dt;   5.0/0.1 = 50
        std::size_t cell_updates = get_total_cell_updates();  // timesteps * n cells;  50 * 100 = 5000
        std::size_t MAX_TIMESTEPS = load.get_max_timesteps();
        std::size_t MAX_CELL_UPATES = load.get_max_cell_updates();
        std::cout << "Number of timesteps: " << timesteps << std::endl;
        std::string timestep_msg = "Solver: timesteps exceed the max of " + std::to_string(MAX_TIMESTEPS);
        if(timesteps > MAX_TIMESTEPS) {
            throw std::invalid_argument(timestep_msg);
        }
        std::string update_msg = "Solver: cell updates exceed the max of " + std::to_string(MAX_CELL_UPATES);
        std::cout << "Number of cell updates: " << cell_updates << std::endl;
        if(cell_updates > MAX_CELL_UPATES) {
            throw std::invalid_argument(update_msg);
        }
        int writes = (static_cast<int>(std::ceil(timesteps/output_interval)) + 1) * static_cast<int>(current.get_cell_count());
        std::cout << "Number of writes to csv file: " << writes << std::endl;
        if(output_interval > timesteps) {
            throw std::invalid_argument("Solver: output interval is larger than timesteps.");
        }
    }   

    std::size_t get_timestep_count() const {
        return static_cast<std::size_t>(std::ceil(sim_length / dt));
    }
    std::size_t get_total_cell_updates() const {
        return current.get_cell_count() * get_timestep_count();
    }

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
        // Checked across ALL cells with valid material properties, not
        // just solids -- air's diffusivity can exceed a dense component's
        // depending on the rho/cp/k values chosen (e.g. low-rho, low-cp
        // air vs. high-k metal), so restricting this to is_solid() can
        // miss the actually-limiting cell type.
        double max_C = 0.0;
        for(const Cell c : current.get_cells()) {
            if(c.get_rho() <= 0.0 || c.get_cp() <= 0.0) continue;
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
            timestep_h_sum = 0.0;
            timestep_h_count = 0;
            for(int x = 0; x < current.get_nx(); x++) {
                for(int y = 0; y < current.get_ny(); y++) {
                    for(int z = 0; z < current.get_nz(); z++) {
                        double T_new = compute_t_next(x,y,z);
                        next.at(x,y,z).set_T(T_new);
                    }
                }
            }
            const double average_h =
            timestep_h_count > 0
                ? timestep_h_sum /
                    static_cast<double>(timestep_h_count)
                : 0.0;

            if(print_convections) {
                std::cout << "Step " << step << ": convection faces = " << timestep_h_count
                << ", average h = " << average_h << " W/(m^2 K)\n";
            }

            std::swap(current, next);
            if(step % output_interval == 0) {
                log_state(step + 1);
            }
        }
    }

private:
    Mesh current;
    Mesh next;
    Workload load;
    bool print_convections = false;
    double dt;
    double sim_length;
    double timestep_h_sum = 0.0;
    int timestep_h_count = 0;
    int output_interval = 0;

    std::ofstream logfile;

    double compute_t_next(int x, int y, int z) {
        const Cell& c = current.at(x, y, z);
        double T = c.get_T();
        double Qcond = 0.0, Qconv = 0.0, Qgen = 0.0;
        /*
        if (cell is solid) {
            compute conduction
            compute heat generation
        } else if (cell is fluid - fluid interface) {
            compute advection
            fluid energe exchange
        } else if (cell is fluid - solid interface ) {
            compute convection 
        }
        */
        Qcond = compute_conduction(x, y, z);
        Qgen  = c.get_qdot() * current.cell_volume();
        Qconv = compute_convection(x, y, z);
        double denom = c.get_rho() * c.get_cp() * current.cell_volume();
        if(denom <= 0.0) {
            return T;
        }

        double dTdt = (Qcond + Qconv + Qgen) / denom;
        double dTdt_advection = compute_advection(x, y, z);
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
        Cell& c = current.at(x, y, z);
        double T = c.get_T();
        double Q = 0.0;
        double h_sum = 0.0;
        int h_count = 0;

        auto add_neighbor = [&](int nx, int ny, int nz, double area, double char_length) {
            if(!current.in_bounds(nx, ny, nz)) {
                return;
            }

            const Cell& n = current.at(nx, ny, nz);

            bool c_solid = c.is_solid();
            bool n_solid = n.is_solid();

            // Only convection across solid-air interfaces
            if(c_solid == n_solid) { return; }

            const Cell& air_cell = c_solid ? n : c;
            const Cell& solid_cell = c_solid ? c : n;

            double vmag = std::sqrt(air_cell.get_vx() * air_cell.get_vx() + 
                                    air_cell.get_vy() * air_cell.get_vy() +
                                    air_cell.get_vz() * air_cell.get_vz());
            double delta_T = std::abs(solid_cell.get_T() - air_cell.get_T());
            double t_film_k = (solid_cell.get_T() + air_cell.get_T()) / 2.0 + 273.15;

            // Note: unlike the conduction harmonic-mean (which genuinely
            // blends two different materials' k), h here describes one
            // fluid film at one solid-air interface, so there's only one
            // physical value to compute -- not two cell-owned values to
            // average. Computing it "twice" from the same air/solid pair
            // would always produce identical numbers, so it's computed once.
            double h_face = Convection::compute_local_h(vmag, char_length,
                                Convection::AIR_RHO, Convection::AIR_MU,
                                Convection::AIR_K, Convection::AIR_PR,
                                delta_T, t_film_k);
            Q += h_face * area * (n.get_T() - T);
            h_sum += h_face;
            h_count++;
            timestep_h_sum += h_face;
            ++timestep_h_count;
        };

        add_neighbor(x + 1, y, z, current.area_x(), current.get_dx());
        add_neighbor(x - 1, y, z, current.area_x(), current.get_dx());

        add_neighbor(x, y + 1, z, current.area_y(), current.get_dy());
        add_neighbor(x, y - 1, z, current.area_y(), current.get_dy());

        add_neighbor(x, y, z + 1, current.area_z(), current.get_dz());
        add_neighbor(x, y, z - 1, current.area_z(), current.get_dz());

        // Cache the average computed h onto the cell purely for
        // logging/plotting -- it isn't read back anywhere else this
        // timestep, so overwriting it mid-solve is safe.
        const double h_average =
            h_count > 0
                ? h_sum / static_cast<double>(h_count)
                : 0.0;

        next.at(x, y, z).set_h(h_average);

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
                        << cell.get_cp() << ','
                        << cell.get_vx() << ','
                        << cell.get_vy() << ','
                        << cell.get_vz() << ','
                        << cell.get_h()
                        << '\n';
                }
            }
        }
    }
};

#endif
