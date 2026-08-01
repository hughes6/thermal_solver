#ifndef THERMAL_SOLVER_HPP
#define THERMAL_SOLVER_HPP

#include <stdexcept>
#include <iomanip>

#include "air_properties.hpp"
#include "convection.hpp"
#include "flow_solver.hpp"
#include "mesh.hpp"
#include "logger.hpp"
#include "workload.hpp"

class Solver {
public:
    
    Solver(Mesh initial_mesh, double dt_, double sim_length_, bool print_convections_, int output_interval_,
            int update_flow_interval_ = -1,
            double resistivity_ = 4.5, double tolerance_ = 1e-3, int max_iterations_ = 10,
            double sor_omega_ = 1.3, int max_outer_iterations_ = 2, double flow_tolerance_ = 1e-2,
            bool advection_subcycling_ = false,
            double advection_cfl_target_ = 0.8,
            int max_advection_substeps_ = 10000,
            std::string logfile_path_ = "simulation.csv",
            std::string pressure_method_ = "sor")
            : current(std::move(initial_mesh)),
            next(current),
            dt(dt_),
            sim_length(sim_length_),
            print_convections(print_convections_),
            output_interval(output_interval_),
            update_flow_interval(update_flow_interval_),
            advection_subcycling(advection_subcycling_),
            advection_cfl_target(advection_cfl_target_),
            max_advection_substeps(max_advection_substeps_),
            logfile(std::move(logfile_path_)),
            flow_solver(current, resistivity_, tolerance_, max_iterations_,
                        sor_omega_, max_outer_iterations_, flow_tolerance_,
                        std::move(pressure_method_))
    { 
      logfile << std::setprecision(17);
      load = current.get_load();
      adaptive = !current.is_uniform();
      if(advection_cfl_target <= 0.0 || advection_cfl_target > 1.0)
          throw std::invalid_argument(
              "Solver: advection_cfl_target must be in (0,1].");
      if(max_advection_substeps < 1)
          throw std::invalid_argument(
              "Solver: max_advection_substeps must be >= 1.");
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

    // Same structure as check_advection_stability(), reading each cell's
    // own spacing instead of one mesh-wide value.
    void check_advection_stability_adaptive() const {
        double max_C = 0.0;
        for(const Cell& c : current.get_cells()) {
            if(!c.is_fluid()) continue;
            double Cx = std::abs(c.get_vx()) * dt / c.get_dx();
            double Cy = std::abs(c.get_vy()) * dt / c.get_dy();
            double Cz = std::abs(c.get_vz()) * dt / c.get_dz();
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

    // Same structure as check_conduction_stability(), per-cell spacing.
    void check_conduction_stability_adaptive() const {
        double max_C = 0.0;
        for(const Cell& c : current.get_cells()) {
            if(c.get_rho() <= 0.0 || c.get_cp() <= 0.0) continue;
            double a = c.get_k() / (c.get_rho() * c.get_cp());
            double Cx = (a * dt) / (c.get_dx()*c.get_dx());
            double Cy = (a * dt) / (c.get_dy()*c.get_dy());
            double Cz = (a * dt) / (c.get_dz()*c.get_dz());
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

    void check_convection_stability() const {
        double max_h_estimate = 0.0;

        for(const Cell& c : current.get_cells()) {
            if(!c.is_fluid()) continue;

            double vmag = std::sqrt(c.get_vx()*c.get_vx() +
                                    c.get_vy()*c.get_vy() +
                                    c.get_vz()*c.get_vz());

            // Conservative delta_T: worst case is hottest possible solid vs
            // this cell's current air temp. Since we don't know solid temps
            // in advance, use ambient-to-max-expected-hotspot as a bound.
            // A simple proxy: use a generous fixed margin, e.g. 80 C, or
            // wire in the max component watt-density -> rough T estimate
            // if you want this tighter.
            double delta_T_bound = 80.0;
            double t_film_bound = c.get_T() + 273.15 + delta_T_bound / 2.0;

            double h_est = Convection::compute_local_h(
                vmag, /*char_length=*/std::min({current.get_dx(), current.get_dy(), current.get_dz()}),
                Convection::AIR_RHO, Convection::AIR_MU, Convection::AIR_K,
                Convection::AIR_PR, delta_T_bound, t_film_bound);

            max_h_estimate = std::max(max_h_estimate, h_est);
        }

        // Worst-case face area / cell volume ratio for a boundary cell:
        // use the smallest face area over the largest single-cell volume the
        // stencil could apply h across (any of the 6 faces).
        double A_over_V = std::max({
            current.area_x(), current.area_y(), current.area_z()
        }) / current.cell_volume();

        // Use air's rho*cp as the limiting (smaller) thermal mass — a thin
        // air cell heats/cools faster than a solid cell of the same size.
        double rho_cp_air = Convection::AIR_RHO * 1005.0; // or pull from Environment if available

        double C = max_h_estimate * A_over_V * dt / rho_cp_air;

        if(C > 1.0) {
            std::cerr << "WARNING: convection term may be unstable. "
                        "Estimated C = " << C << " > 1.0 (h_est = "
                    << max_h_estimate << " W/m^2K)\n";
        } else {
            std::cout << "Convection stability estimate: C = " << C
                    << " (h_est = " << max_h_estimate << " W/m^2K)\n";
        }
    }

    // Same idea as check_convection_stability(), but since area/volume can
    // now vary cell-to-cell, C is tracked as a running max per-cell inside
    // the same loop rather than combined afterward from two separate
    // mesh-wide maxima.
    void check_convection_stability_adaptive() const {
        double max_C = 0.0;
        double max_h_estimate = 0.0;
        double rho_cp_air = Convection::AIR_RHO * 1005.0;

        for(const Cell& c : current.get_cells()) {
            if(!c.is_fluid()) continue;

            double vmag = std::sqrt(c.get_vx()*c.get_vx() +
                                    c.get_vy()*c.get_vy() +
                                    c.get_vz()*c.get_vz());

            double delta_T_bound = 80.0;
            double t_film_bound = c.get_T() + 273.15 + delta_T_bound / 2.0;

            double h_est = Convection::compute_local_h(
                vmag, /*char_length=*/std::min({c.get_dx(), c.get_dy(), c.get_dz()}),
                Convection::AIR_RHO, Convection::AIR_MU, Convection::AIR_K,
                Convection::AIR_PR, delta_T_bound, t_film_bound);

            max_h_estimate = std::max(max_h_estimate, h_est);

            double A_over_V = std::max({c.area_x(), c.area_y(), c.area_z()}) / c.volume();
            double C = h_est * A_over_V * dt / rho_cp_air;
            max_C = std::max(max_C, C);
        }

        if(max_C > 1.0) {
            std::cerr << "WARNING: convection term may be unstable. "
                        "Estimated C = " << max_C << " > 1.0 (h_est = "
                    << max_h_estimate << " W/m^2K)\n";
        } else {
            std::cout << "Convection stability estimate: C = " << max_C
                    << " (h_est = " << max_h_estimate << " W/m^2K)\n";
        }
    }

    const Mesh& get_mesh() const {
        return current;
    }

    
    void solve() {
        double pct = 0.0;
        if (adaptive) {
            if(!advection_subcycling) check_advection_stability_adaptive();
            check_conduction_stability_adaptive();
            check_convection_stability_adaptive();
        } else {
            if(!advection_subcycling) check_advection_stability();
            check_conduction_stability();
            check_convection_stability();
        }
        if(advection_subcycling)
            std::cout << "Advection subcycling enabled: global dt = "
                      << dt << " s, CFL target = "
                      << advection_cfl_target << "\n";
        int steps = static_cast<int>(sim_length / dt);
        log_state(0);
        if(logger != nullptr) {
            logger->log(current, 0, 0.0);
        }
        for(int step = 0; step < steps; step++) {
            timestep_h_sum = 0.0;
            timestep_h_count = 0;
            if(update_flow_interval != -1 && step % update_flow_interval == 0) {
                flow_solver.solve(current);
                check_convection_stability();
            }
            pct = std::round(100.0 * step / steps);
            if(step % 5 == 0) {
                std::cout<< "Working......" << pct << "%" << std::endl;
            }

            if(!advection_subcycling) {
                for(int x = 0; x < current.get_nx(); x++) {
                    for(int y = 0; y < current.get_ny(); y++) {
                        for(int z = 0; z < current.get_nz(); z++) {
                            double T_new = adaptive ? compute_t_next_adaptive(x,y,z) : compute_t_next(x,y,z);
                            validate_temperature(T_new,x,y,z,"legacy update");
                            Cell& next_cell = next.at(x,y,z);
                            next_cell.set_T(T_new);
                            if(next_cell.is_fluid()) {
                                next_cell.set_rho(AirProperties::density(T_new, current.get_env().get_ambient_pressure()));
                                next_cell.set_mu(AirProperties::viscosity(T_new));
                            }
                        }
                    }
                }
                update_face_wall_temperatures();
                std::swap(current, next);
            } else {
                advance_with_advection_subcycling(step);
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

            if(step % output_interval == 0) {
                log_state(step + 1);
            }

            if(logger != nullptr) {
                logger->log(current, step, static_cast<double>(step) * dt);
            }
        }
        // Always preserve the completed state even when the requested output
        // cadence does not land exactly on the final step.
        if(steps > 0 && (steps-1) % output_interval != 0)
            log_state(steps);
    }

    void set_logger(SimulationLogger& simulation_logger) {
        logger = &simulation_logger;
    }



private:
    Mesh current;
    Mesh next;
    Workload load;
    FlowSolver flow_solver;
    bool print_convections = false;
    // Cached once at construction from current.is_uniform(). Every kernel
    // below has a plain version (unchanged from before Stage 2) and an
    // "_adaptive" sibling with the identical structure but reading each
    // cell's own dx/dy/dz/area/volume instead of one mesh-wide scalar.
    // solve() and compute_t_next() are the only two places that pick
    // between them - nothing else needs to know this distinction exists.
    bool adaptive = false;
    double dt;
    double sim_length;
    double timestep_h_sum = 0.0;
    int timestep_h_count = 0;
    int output_interval = 0;
    int update_flow_interval = 0;
    bool advection_subcycling = false;
    double advection_cfl_target = 0.8;
    int max_advection_substeps = 10000;
    int last_reported_advection_substeps = -1;

    SimulationLogger* logger = nullptr;

    std::ofstream logfile;

    static void validate_temperature(double temperature,
                                     int x,int y,int z,
                                     const char* stage) {
        if(!std::isfinite(temperature) || temperature <= -273.15 ||
           temperature > 1.0e5) {
            throw std::runtime_error(
                std::string("Solver: nonphysical temperature during ") +
                stage + " at cell (" + std::to_string(x) + "," +
                std::to_string(y) + "," + std::to_string(z) +
                "): " + std::to_string(temperature) + " C.");
        }
    }

    double max_advection_cfl_for_dt(double candidate_dt) const {
        double max_cfl = 0.0;
        for(const Cell& cell : current.get_cells()) {
            if(!cell.is_fluid()) continue;
            const double dx = adaptive ? cell.get_dx() : current.get_dx();
            const double dy = adaptive ? cell.get_dy() : current.get_dy();
            const double dz = adaptive ? cell.get_dz() : current.get_dz();
            const double cfl =
                std::abs(cell.get_vx())*candidate_dt/dx +
                std::abs(cell.get_vy())*candidate_dt/dy +
                std::abs(cell.get_vz())*candidate_dt/dz;
            max_cfl = std::max(max_cfl,cfl);
        }
        return max_cfl;
    }

    double compute_t_next_without_advection(int x, int y, int z) {
        const Cell& cell = current.at(x,y,z);
        if(cell.is_intake()) return current.get_env().get_T_ambient();
        const double volume = adaptive ? cell.volume() : current.cell_volume();
        const double denominator = cell.get_rho()*cell.get_cp()*volume;
        if(denominator <= 0.0) return cell.get_T();
        const double conduction = adaptive
            ? compute_conduction_adaptive(x,y,z)
            : compute_conduction(x,y,z);
        const double convection = adaptive
            ? compute_convection_adaptive(x,y,z)
            : compute_convection(x,y,z);
        const double generation = cell.get_qdot()*volume;
        return cell.get_T() +
            dt*(conduction+convection+generation)/denominator;
    }

    void advance_with_advection_subcycling(int step) {
        // Lie split: advance all non-advection physics once at the global
        // timestep, then advance only fluid advection using stable substeps.
        for(int x=0; x<current.get_nx(); ++x)
            for(int y=0; y<current.get_ny(); ++y)
                for(int z=0; z<current.get_nz(); ++z) {
                    const double temperature =
                        compute_t_next_without_advection(x,y,z);
                    validate_temperature(
                        temperature,x,y,z,"non-advection update");
                    Cell& target=next.at(x,y,z);
                    target.set_T(temperature);
                    if(target.is_fluid()) {
                        target.set_rho(AirProperties::density(
                            temperature,current.get_env().get_ambient_pressure()));
                        target.set_mu(AirProperties::viscosity(temperature));
                    }
                }
        update_face_wall_temperatures();
        std::swap(current,next);

        const double global_cfl=max_advection_cfl_for_dt(dt);
        const int substeps=std::max(
            1,static_cast<int>(std::ceil(global_cfl/advection_cfl_target)));
        if(substeps > max_advection_substeps) {
            throw std::runtime_error(
                "Solver: required advection substeps (" +
                std::to_string(substeps) +
                ") exceed max_advection_substeps (" +
                std::to_string(max_advection_substeps) + ").");
        }
        if(substeps != last_reported_advection_substeps) {
            std::cout << "Advection substeps per global step: "
                      << substeps << " (global CFL = "
                      << global_cfl << ", substep dt = "
                      << dt/static_cast<double>(substeps) << " s)\n";
            last_reported_advection_substeps=substeps;
        }

        const double sub_dt=dt/static_cast<double>(substeps);
        for(int sub=0; sub<substeps; ++sub) {
            for(int x=0; x<current.get_nx(); ++x)
                for(int y=0; y<current.get_ny(); ++y)
                    for(int z=0; z<current.get_nz(); ++z) {
                        const Cell& source=current.at(x,y,z);
                        double temperature=source.get_T();
                        if(source.is_intake()) {
                            temperature=current.get_env().get_T_ambient();
                        } else if(source.is_fluid()) {
                            const double derivative=adaptive
                                ? compute_advection_adaptive(x,y,z)
                                : compute_advection(x,y,z);
                            temperature += sub_dt*derivative;
                        }
                        validate_temperature(
                            temperature,x,y,z,"advection substep");
                        Cell& target=next.at(x,y,z);
                        target.set_T(temperature);
                        if(target.is_fluid()) {
                            target.set_rho(AirProperties::density(
                                temperature,current.get_env().get_ambient_pressure()));
                            target.set_mu(AirProperties::viscosity(temperature));
                        }
                    }
            auto& next_walls=next.get_wall_faces();
            const auto& current_walls=current.get_wall_faces();
            for(size_t wi=0; wi<current_walls.size(); ++wi)
                next_walls[wi].temperature=current_walls[wi].temperature;
            std::swap(current,next);
        }
        (void)step;
    }

    void update_face_wall_temperatures() {
        if(!current.has_face_walls()) return;
        auto& next_walls = next.get_wall_faces();
        const auto& walls = current.get_wall_faces();
        for(size_t wi=0; wi<walls.size(); ++wi) {
            const Mesh::WallFace& wall = walls[wi];
            if(!wall.active) continue;
            const int hx = wall.x + (wall.axis == 0);
            const int hy = wall.y + (wall.axis == 1);
            const int hz = wall.z + (wall.axis == 2);
            const Cell& low = current.at(wall.x, wall.y, wall.z);
            const Cell& high = current.at(hx, hy, hz);
            const double area = current.wall_face_area(wall);
            const double low_width =
                wall.axis == 0 ? low.get_dx() :
                wall.axis == 1 ? low.get_dy() : low.get_dz();
            const double high_width =
                wall.axis == 0 ? high.get_dx() :
                wall.axis == 1 ? high.get_dy() : high.get_dz();
            const double wall_half =
                0.5 * wall.thickness / std::max(wall.conductivity, 1e-12);
            const double glow = area /
                (0.5*low_width/std::max(low.get_k(),1e-12) + wall_half);
            const double ghigh = area /
                (0.5*high_width/std::max(high.get_k(),1e-12) + wall_half);
            const double capacitance =
                wall.rho * wall.cp * area * wall.thickness;
            if(capacitance <= 0.0) continue;
            const double q =
                glow * (low.get_T() - wall.temperature) +
                ghigh * (high.get_T() - wall.temperature);
            next_walls[wi].temperature =
                wall.temperature + dt * q / capacitance;
        }
    }

    double compute_t_next(int x, int y, int z) {
        const Cell& c = current.at(x, y, z);
        double T = c.get_T();
            
        // Intake cells are fed by an effectively infinite ambient reservoir —
        // pin them at ambient rather than letting the stencil evolve them.
        if(c.is_intake()) {
            return current.get_env().get_T_ambient();
        }

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

    // Same structure as compute_t_next(), using each cell's own volume and
    // the _adaptive sibling kernels below instead of mesh-wide values.
    double compute_t_next_adaptive(int x, int y, int z) {
        const Cell& c = current.at(x, y, z);
        double T = c.get_T();

        if(c.is_intake()) {
            return current.get_env().get_T_ambient();
        }

        double Qcond = compute_conduction_adaptive(x, y, z);
        double Qgen  = c.get_qdot() * c.volume();
        double Qconv = compute_convection_adaptive(x, y, z);
        double denom = c.get_rho() * c.get_cp() * c.volume();
        if(denom <= 0.0) {
            return T;
        }

        double dTdt = (Qcond + Qconv + Qgen) / denom;
        double dTdt_advection = compute_advection_adaptive(x, y, z);
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
        if(vx > 0 && current.in_bounds(x-1, y, z) &&
           current.wall_between(x,y,z,x-1,y,z) == nullptr) {
            double Tup = current.at(x-1, y, z).get_T();
            dTdt += -vx * (T- Tup) / current.get_dx();
        }
        else if(vx < 0 && current.in_bounds(x + 1, y, z) &&
                current.wall_between(x,y,z,x+1,y,z) == nullptr) {
            double Tup = current.at(x + 1, y, z).get_T();
            dTdt += -vx * (Tup - T) / current.get_dx();
        }

        // y direction
        if(vy > 0 && current.in_bounds(x, y - 1, z) &&
           current.wall_between(x,y,z,x,y-1,z) == nullptr) {
            double Tup = current.at(x, y - 1, z).get_T();
            dTdt += -vy * (T - Tup) / current.get_dy();
        }
        else if(vy < 0 && current.in_bounds(x, y + 1, z) &&
                current.wall_between(x,y,z,x,y+1,z) == nullptr) {
            double Tup = current.at(x, y + 1, z).get_T();
            dTdt += -vy * (Tup - T) / current.get_dy();
        }

        // z direction
        if(vz > 0 && current.in_bounds(x, y, z - 1) &&
           current.wall_between(x,y,z,x,y,z-1) == nullptr) {
            double Tup = current.at(x, y, z - 1).get_T();
            dTdt += -vz * (T - Tup) / current.get_dz();
        }
        else if(vz < 0 && current.in_bounds(x, y, z + 1) &&
                current.wall_between(x,y,z,x,y,z+1) == nullptr) {
            double Tup = current.at(x, y, z + 1).get_T();
            dTdt += -vz * (Tup - T) / current.get_dz();
        }
        return dTdt;
    }

    // Same structure as compute_advection(), using the distance between
    // the two actual cell centers (average of their widths along the
    // stepped axis) instead of one mesh-wide dx/dy/dz.
    double compute_advection_adaptive(int x, int y, int z) {
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
        if(vx > 0 && current.in_bounds(x-1, y, z) &&
           current.wall_between(x,y,z,x-1,y,z) == nullptr) {
            const Cell& up = current.at(x-1, y, z);
            double dist = (c.get_dx() + up.get_dx()) / 2.0;
            dTdt += -vx * (T - up.get_T()) / dist;
        }
        else if(vx < 0 && current.in_bounds(x + 1, y, z) &&
                current.wall_between(x,y,z,x+1,y,z) == nullptr) {
            const Cell& up = current.at(x + 1, y, z);
            double dist = (c.get_dx() + up.get_dx()) / 2.0;
            dTdt += -vx * (up.get_T() - T) / dist;
        }

        // y direction
        if(vy > 0 && current.in_bounds(x, y - 1, z) &&
           current.wall_between(x,y,z,x,y-1,z) == nullptr) {
            const Cell& up = current.at(x, y - 1, z);
            double dist = (c.get_dy() + up.get_dy()) / 2.0;
            dTdt += -vy * (T - up.get_T()) / dist;
        }
        else if(vy < 0 && current.in_bounds(x, y + 1, z) &&
                current.wall_between(x,y,z,x,y+1,z) == nullptr) {
            const Cell& up = current.at(x, y + 1, z);
            double dist = (c.get_dy() + up.get_dy()) / 2.0;
            dTdt += -vy * (up.get_T() - T) / dist;
        }

        // z direction
        if(vz > 0 && current.in_bounds(x, y, z - 1) &&
           current.wall_between(x,y,z,x,y,z-1) == nullptr) {
            const Cell& up = current.at(x, y, z - 1);
            double dist = (c.get_dz() + up.get_dz()) / 2.0;
            dTdt += -vz * (T - up.get_T()) / dist;
        }
        else if(vz < 0 && current.in_bounds(x, y, z + 1) &&
                current.wall_between(x,y,z,x,y,z+1) == nullptr) {
            const Cell& up = current.at(x, y, z + 1);
            double dist = (c.get_dz() + up.get_dz()) / 2.0;
            dTdt += -vz * (up.get_T() - T) / dist;
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
            if(const Mesh::WallFace* wall =
                   current.wall_between(x,y,z,nx,ny,nz)) {
                const double resistance =
                    0.5 * dist / std::max(k, 1e-12) +
                    0.5 * wall->thickness /
                        std::max(wall->conductivity, 1e-12);
                Q += area * (wall->temperature - T) / resistance;
                return;
            }
            double Tn = n.get_T();
            double kn = n.get_k();

            if(c.is_solid() != n.is_solid()) return;

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

    // Same structure as compute_conduction(): identical 6-face stencil,
    // identical harmonic-mean k_face, but area comes from this cell's own
    // area_x/y/z() (shared with its neighbor along the un-stepped axes on
    // a tensor-product grid, so no averaging needed there) and the face
    // distance is the average of the two cells' widths along the stepped
    // axis, since that's the one dimension that can actually differ.
    double compute_conduction_adaptive(int x, int y, int z) {
        const Cell& c = current.at(x, y, z);
        double T = c.get_T();
        double k = c.get_k();

        double Q = 0.0;

        auto add_neighbor = [&](int nx, int ny, int nz, int axis) {
            if(!current.in_bounds(nx, ny, nz)) {
                return;
            }
            const Cell& n = current.at(nx, ny, nz);
            double area, dist;
            switch(axis) {
                case 0: area = c.area_x(); dist = (c.get_dx() + n.get_dx()) / 2.0; break;
                case 1: area = c.area_y(); dist = (c.get_dy() + n.get_dy()) / 2.0; break;
                default: area = c.area_z(); dist = (c.get_dz() + n.get_dz()) / 2.0; break;
            }
            if(const Mesh::WallFace* wall =
                   current.wall_between(x,y,z,nx,ny,nz)) {
                const double cell_half =
                    axis == 0 ? 0.5*c.get_dx() :
                    axis == 1 ? 0.5*c.get_dy() : 0.5*c.get_dz();
                const double resistance =
                    cell_half / std::max(k, 1e-12) +
                    0.5 * wall->thickness /
                        std::max(wall->conductivity, 1e-12);
                Q += area * (wall->temperature - T) / resistance;
                return;
            }
            if(c.is_solid() != n.is_solid()) return;

            double Tn = n.get_T();
            double kn = n.get_k();
            double k_face = 2.0 * k * kn / (k + kn);
            Q += k_face * area * (Tn - T) / dist;
        };

        add_neighbor(x + 1, y, z, 0);
        add_neighbor(x - 1, y, z, 0);

        add_neighbor(x, y + 1, z, 1);
        add_neighbor(x, y - 1, z, 1);

        add_neighbor(x, y, z + 1, 2);
        add_neighbor(x, y, z - 1, 2);

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

    // Same structure as compute_convection(): identical 6-face stencil and
    // film-coefficient physics, area/dist generalized the same way as in
    // compute_conduction_adaptive().
    double compute_convection_adaptive(int x, int y, int z) {
        Cell& c = current.at(x, y, z);
        double T = c.get_T();
        double Q = 0.0;
        double h_sum = 0.0;
        int h_count = 0;

        auto add_neighbor = [&](int nx, int ny, int nz, int axis) {
            if(!current.in_bounds(nx, ny, nz)) {
                return;
            }

            const Cell& n = current.at(nx, ny, nz);

            bool c_solid = c.is_solid();
            bool n_solid = n.is_solid();

            if(c_solid == n_solid) { return; }

            const Cell& air_cell = c_solid ? n : c;
            const Cell& solid_cell = c_solid ? c : n;

            double vmag = std::sqrt(air_cell.get_vx() * air_cell.get_vx() + 
                                    air_cell.get_vy() * air_cell.get_vy() +
                                    air_cell.get_vz() * air_cell.get_vz());
            double delta_T = std::abs(solid_cell.get_T() - air_cell.get_T());
            double t_film_k = (solid_cell.get_T() + air_cell.get_T()) / 2.0 + 273.15;

            double area, char_length;
            switch(axis) {
                case 0: area = c.area_x(); char_length = c.get_dx(); break;
                case 1: area = c.area_y(); char_length = c.get_dy(); break;
                default: area = c.area_z(); char_length = c.get_dz(); break;
            }

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

        add_neighbor(x + 1, y, z, 0);
        add_neighbor(x - 1, y, z, 0);

        add_neighbor(x, y + 1, z, 1);
        add_neighbor(x, y - 1, z, 1);

        add_neighbor(x, y, z + 1, 2);
        add_neighbor(x, y, z - 1, 2);

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
