#ifndef THERMAL_SOLVER_HPP
#define THERMAL_SOLVER_HPP

#include <stdexcept>
#include <iomanip>
#include <limits>
#include <sstream>

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
            std::string pressure_method_ = "sor",
            bool initialize_flow_before_thermal_ = false)
            : current(std::move(initial_mesh)),
            next(),
            dt(dt_),
            sim_length(sim_length_),
            print_convections(print_convections_),
            output_interval(output_interval_),
            update_flow_interval(update_flow_interval_),
            advection_subcycling(advection_subcycling_),
            advection_cfl_target(advection_cfl_target_),
            max_advection_substeps(max_advection_substeps_),
            initialize_flow_before_thermal(initialize_flow_before_thermal_),
            logfile_path(std::move(logfile_path_)),
            flow_solver(current, resistivity_, tolerance_, max_iterations_,
                        sor_omega_, max_outer_iterations_, flow_tolerance_,
                        std::move(pressure_method_))
    { 
      load = current.get_load();
      adaptive = !current.is_uniform();
      if(!std::isfinite(dt) || dt <= 0.0)
          throw std::invalid_argument(
              "Solver: dt must be a positive finite number.");
      if(!std::isfinite(sim_length) || sim_length <= 0.0)
          throw std::invalid_argument(
              "Solver: simulation length must be a positive finite number.");
      if(output_interval < 1)
          throw std::invalid_argument(
              "Solver: output interval must be >= 1.");
      if(update_flow_interval == 0 || update_flow_interval < -1)
          throw std::invalid_argument(
              "Solver: update_flow_interval must be -1 or >= 1.");
      if(!std::isfinite(advection_cfl_target) ||
         advection_cfl_target <= 0.0 || advection_cfl_target > 1.0)
          throw std::invalid_argument(
              "Solver: advection_cfl_target must be in (0,1].");
      if(max_advection_substeps < 1)
          throw std::invalid_argument(
              "Solver: max_advection_substeps must be >= 1.");
      validate_computational_workload();
      // Delay the second full Cell payload until every scalar and base
      // workload guard has accepted the run.
      next=current;
    }

    Solver(const Solver&) = delete;
    Solver& operator=(const Solver&) = delete;
    Solver(Solver&&) = delete;
    Solver& operator=(Solver&&) = delete;


    void validate_computational_workload() {
        require_mesh_available("validate computational workload");
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
        std::cout << "Number of base thermal cell updates: "
                  << cell_updates << std::endl;
        if(cell_updates > MAX_CELL_UPATES) {
            throw std::invalid_argument(update_msg);
        }
        const std::size_t output_stride =
            static_cast<std::size_t>(output_interval);
        const std::size_t write_events = checked_work_add(
            (timesteps + output_stride - 1u)/output_stride,1u,
            "CSV write-event estimate");
        const std::size_t writes = checked_work_multiply(
            write_events,current.get_cell_count(),
            "CSV cell-write estimate");
        std::cout << "Number of writes to csv file: " << writes << std::endl;
        if(output_stride > timesteps) {
            throw std::invalid_argument("Solver: output interval is larger than timesteps.");
        }
    }   

    std::size_t get_timestep_count() const {
        const double raw_steps = sim_length / dt;
        if(!std::isfinite(raw_steps) || raw_steps < 1.0)
            throw std::invalid_argument(
                "Solver: duration/dt must define at least one finite "
                "timestep.");
        const double rounded_steps = std::round(raw_steps);
        const double ratio_tolerance =
            64.0*std::numeric_limits<double>::epsilon()*
            std::max(1.0,std::abs(raw_steps));
        if(std::abs(raw_steps-rounded_steps) > ratio_tolerance)
            throw std::invalid_argument(
                "Solver: duration must be an integer multiple of dt; "
                "partial final timesteps are not implemented.");
        if(rounded_steps >
           static_cast<double>(std::numeric_limits<int>::max()))
            throw std::invalid_argument(
                "Solver: timestep count exceeds the implementation limit "
                "of INT_MAX.");
        return static_cast<std::size_t>(rounded_steps);
    }
    std::size_t get_total_cell_updates() const {
        require_mesh_available("inspect total cell updates");
        return checked_work_multiply(
            current.get_cell_count(), get_timestep_count(),
            "base thermal cell-update estimate");
    }

    void apply_bulk_velocity(double vx, double vy, double vz) {
        require_mesh_available("apply bulk velocity");
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
        require_mesh_available("check advection stability");
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
        require_mesh_available("check adaptive advection stability");
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
        require_mesh_available("check conduction stability");
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
        require_mesh_available("check adaptive conduction stability");
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

    // The explicit convection row for one cell is
    //
    //     C = dt * sum_faces(h_face * A_face) / (rho * cp * V).
    //
    // Monotonicity requires C <= 1.  Evaluate the actual six-face row rather
    // than combining unrelated mesh-wide maxima: adaptive cells, corner air
    // pockets, and solids touching several air cells can otherwise be missed.
    // The same shared face-h function is used by the thermal update, with an
    // 80 K lower bound on |delta T| here so natural-convection stability is
    // guarded before a hotspot has already developed.
    void check_convection_stability() const {
        require_mesh_available("check convection stability");
        double max_C = 0.0;
        double max_conductance = 0.0;
        double max_capacity = 0.0;
        int max_x = 0, max_y = 0, max_z = 0;

        for(int x=0; x<current.get_nx(); ++x)
            for(int y=0; y<current.get_ny(); ++y)
                for(int z=0; z<current.get_nz(); ++z) {
                    const Cell& cell=current.at(x,y,z);
                    double conductance=0.0;
                    auto add_face=[&](int nx,int ny,int nz,int axis) {
                        if(!current.in_bounds(nx,ny,nz)) return;
                        const Cell& neighbor=current.at(nx,ny,nz);
                        if(cell.is_solid() == neighbor.is_solid()) return;
                        const double area=axis == 0 ? cell.area_x() :
                            axis == 1 ? cell.area_y() : cell.area_z();
                        const double h=convection_face_h(
                            cell,neighbor,axis,80.0);
                        if(!std::isfinite(h) || h < 0.0 ||
                           !std::isfinite(area) || area <= 0.0)
                            throw std::runtime_error(
                                "Solver: invalid solid/air face conductance "
                                "during convection stability check at cell ("+
                                std::to_string(x)+","+std::to_string(y)+","+
                                std::to_string(z)+").");
                        conductance += h*area;
                    };
                    add_face(x+1,y,z,0);
                    add_face(x-1,y,z,0);
                    add_face(x,y+1,z,1);
                    add_face(x,y-1,z,1);
                    add_face(x,y,z+1,2);
                    add_face(x,y,z-1,2);
                    if(conductance == 0.0) continue;

                    const double capacity=
                        cell.get_rho()*cell.get_cp()*cell.volume();
                    if(!std::isfinite(conductance) || conductance < 0.0 ||
                       !std::isfinite(capacity) || capacity <= 0.0)
                        throw std::runtime_error(
                            "Solver: invalid cell capacity or convection row "
                            "sum during stability check at cell ("+
                            std::to_string(x)+","+std::to_string(y)+","+
                            std::to_string(z)+").");
                    const double C=dt*conductance/capacity;
                    if(!std::isfinite(C) || C < 0.0)
                        throw std::runtime_error(
                            "Solver: non-finite explicit convection stability "
                            "coefficient at cell ("+std::to_string(x)+","+
                            std::to_string(y)+","+std::to_string(z)+").");
                    if(C > max_C) {
                        max_C=C;
                        max_conductance=conductance;
                        max_capacity=capacity;
                        max_x=x; max_y=y; max_z=z;
                    }
                }

        if(max_C > 1.0) {
            const double maximum_stable_dt=max_capacity/max_conductance;
            std::ostringstream message;
            message << std::setprecision(17)
                << "Solver: explicit convection stability limit exceeded at "
                   "cell (" << max_x << ',' << max_y << ',' << max_z
                << "): max C=" << max_C << " > 1 for dt=" << dt
                << " s (sum(hA)=" << max_conductance
                << " W/K, rho*cp*V=" << max_capacity
                << " J/K, maximum stable dt=" << maximum_stable_dt
                << " s). Reduce simulation.dt; thermal advancement was "
                   "refused.";
            throw std::runtime_error(message.str());
        }
        std::cout << "Convection explicit row-sum max C = " << max_C
                  << " at dt = " << dt << " s (limit <= 1)\n";
    }

    const Mesh& get_mesh() const {
        if(mesh_released)
            throw std::logic_error(
                "Solver: completed mesh ownership has already been released.");
        return current;
    }

    // Transfer the completed field without making a third full Cell copy.
    // This is intentionally terminal: FlowSolver references current, so the
    // Solver must not be inspected or advanced after ownership is released.
    Mesh release_completed_mesh() {
        if(mesh_released)
            throw std::logic_error(
                "Solver: completed mesh ownership was already released.");
        if(!solve_completed)
            throw std::logic_error(
                "Solver: completed mesh cannot be released before solve() "
                "finishes successfully.");
        Mesh released=std::move(current);
        current=Mesh{};
        next=Mesh{};
        mesh_released=true;
        return released;
    }

    // Populate the face-flux field owned by this Solver exactly once. This is
    // intentionally idempotent so a model loader can initialize flow before
    // producing its thermal/CFL estimate and solve() can reuse that same
    // field at timestep zero. A nonconverged flow field is never published to
    // the thermal update.
    void initialize_flow() {
        require_mesh_available("initialize flow");
        if(!flow_solver.has_face_flux_solution())
            refresh_flow("initial");
        else
            require_usable_flow_solution("initial");
    }

    bool has_face_flux_solution() const {
        require_mesh_available("inspect face-flux solution status");
        return flow_solver.has_face_flux_solution();
    }

    double relative_flow_mass_imbalance() const {
        require_mesh_available("inspect relative flow mass imbalance");
        require_usable_flow_solution("mass-balance inspection");
        const double scale=std::max({
            std::abs(flow_solver.total_source_m3s()),
            std::abs(flow_solver.total_vent_flow_m3s()),
            1.0e-9});
        return std::abs(flow_solver.mass_imbalance_m3s())/scale;
    }

    // Read-only flow diagnostics used by conservation audits.  Advection is
    // advanced from FlowSolver's published face field rather than the cell
    // velocity cache, so exposing that exact field is necessary to reconstruct
    // the discrete thermal-energy ledger without duplicating or guessing the
    // pressure-network solution.
    double x_face_flux_m3s(int x_face,int y,int z) const {
        require_mesh_available("inspect x-face flux");
        require_usable_flow_solution("x-face flux inspection");
        return flow_solver.x_face_flux_m3s(x_face,y,z);
    }

    double y_face_flux_m3s(int x,int y_face,int z) const {
        require_mesh_available("inspect y-face flux");
        require_usable_flow_solution("y-face flux inspection");
        return flow_solver.y_face_flux_m3s(x,y_face,z);
    }

    double z_face_flux_m3s(int x,int y,int z_face) const {
        require_mesh_available("inspect z-face flux");
        require_usable_flow_solution("z-face flux inspection");
        return flow_solver.z_face_flux_m3s(x,y,z_face);
    }

    double ambient_flow_into_cell_m3s(int x,int y,int z) const {
        require_mesh_available("inspect ambient flow");
        require_usable_flow_solution("ambient-flow inspection");
        return flow_solver.ambient_flow_into_cell_m3s(x,y,z);
    }

    double maximum_flow_continuity_residual_m3s() const {
        require_mesh_available("inspect flow continuity residual");
        require_usable_flow_solution("continuity inspection");
        return flow_solver.maximum_realized_continuity_residual_m3s();
    }

    int planned_advection_substeps_for_current_flow() const {
        require_mesh_available("plan advection substeps");
        return advection_subcycling ? plan_advection_substeps().substeps : 0;
    }

    std::size_t completed_native_cell_visits() const {
        return completed_cell_visits;
    }

    
    void solve() {
        if(solve_started)
            throw std::logic_error(
                "Solver: solve() may only be called once.");
        if(mesh_released)
            throw std::logic_error(
                "Solver: cannot solve after mesh ownership is released.");
        solve_started=true;
        double pct = 0.0;
        if(initialize_flow_before_thermal) initialize_flow();
        // A periodic flow run would refresh at step zero anyway. Do it before
        // opening/truncating output so convergence and exact face-CFL workload
        // failures preserve any prior successful CSV.
        if(update_flow_interval != -1 &&
           !flow_solver.has_face_flux_solution())
            refresh_flow("initial");
        if (adaptive) {
            if(!advection_subcycling) check_advection_stability_adaptive();
            check_conduction_stability_adaptive();
            check_convection_stability();
        } else {
            if(!advection_subcycling) check_advection_stability();
            check_conduction_stability();
            check_convection_stability();
        }
        if(advection_subcycling)
            std::cout << "Advection subcycling enabled: global dt = "
                      << dt << " s, CFL target = "
                      << advection_cfl_target << "\n";
        const std::size_t steps = get_timestep_count();
        completed_cell_visits = 0;
        int last_reported_workload_substeps = -1;
        if(advection_subcycling && steps > 0) {
            const AdvectionSubstepPlan initial_plan =
                plan_advection_substeps();
            validate_advection_workload(
                completed_cell_visits,
                steps,
                initial_plan.substeps,
                true);
            last_reported_workload_substeps = initial_plan.substeps;
        }
        if(logger != nullptr && !logger->is_initialized())
            throw std::logic_error(
                "Solver: attached SimulationLogger is no longer "
                "initialized; output files were not opened.");
        open_logfile();
        log_state(0);
        if(logger != nullptr) {
            logger->log(current, 0, 0.0);
        }
        for(std::size_t step = 0; step < steps; step++) {
            timestep_h_sum = 0.0;
            timestep_h_count = 0;
            if(update_flow_interval != -1 && step % update_flow_interval == 0) {
                if(!(step == 0 && flow_solver.has_face_flux_solution())) {
                    refresh_flow("periodic");
                } else {
                    require_usable_flow_solution("initial");
                }
                check_convection_stability();
            }
            pct = std::round(
                100.0*static_cast<double>(step)/
                static_cast<double>(steps));
            if(step % 5 == 0) {
                std::cout<< "Working......" << pct << "%" << std::endl;
            }

            AdvectionSubstepPlan advection_plan;
            if(advection_subcycling) {
                advection_plan = plan_advection_substeps();
                const bool report_workload =
                    advection_plan.substeps !=
                    last_reported_workload_substeps;
                validate_advection_workload(
                    completed_cell_visits,
                    steps-step,
                    advection_plan.substeps,
                    report_workload);
                last_reported_workload_substeps =
                    advection_plan.substeps;
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
                completed_cell_visits = checked_work_add(
                    completed_cell_visits,current.get_cell_count(),
                    "completed native cell visits");
            } else {
                advance_with_advection_subcycling(advection_plan);
                completed_cell_visits = checked_work_add(
                    completed_cell_visits,
                    cell_visits_per_subcycled_step(
                        advection_plan.substeps),
                    "completed advection-inclusive cell visits");
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

            if((step + 1) % output_interval == 0) {
                log_state(static_cast<int>(step + 1));
            }

            if(logger != nullptr) {
                logger->log(
                    current,static_cast<int>(step + 1),
                    static_cast<double>(step + 1) * dt);
            }
        }
        // Always preserve the completed state even when the requested output
        // cadence does not land exactly on the final step.
        if(steps > 0 && steps % output_interval != 0)
            log_state(static_cast<int>(steps));
        if(logger != nullptr)
            logger->flush_and_validate();
        logfile.flush();
        if(!logfile)
            throw std::runtime_error(
                "Solver: failed while writing CSV output file '"+
                logfile_path+"'.");
        logfile.close();
        if(!logfile)
            throw std::runtime_error(
                "Solver: failed to finalize CSV output file '"+
                logfile_path+"'.");
        solve_completed=true;
    }

    void set_logger(SimulationLogger& simulation_logger) {
        require_mesh_available("attach simulation logger");
        if(solve_started)
            throw std::logic_error(
                "Solver: logger must be attached before solve() starts.");
        if(!simulation_logger.is_initialized())
            throw std::logic_error(
                "Solver: SimulationLogger must be initialized before it is "
                "attached.");
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
    bool initialize_flow_before_thermal = false;
    int last_reported_advection_substeps = -1;
    std::size_t completed_cell_visits = 0;
    bool solve_started = false;
    bool solve_completed = false;
    bool mesh_released = false;

    SimulationLogger* logger = nullptr;

    std::string logfile_path;
    std::ofstream logfile;

    void require_mesh_available(const char* operation) const {
        if(mesh_released)
            throw std::logic_error(
                std::string("Solver: cannot ")+operation+
                " after completed mesh ownership was released.");
    }

    void open_logfile() {
        logfile.open(logfile_path,std::ios::trunc);
        if(!logfile)
            throw std::runtime_error(
                "Solver: unable to open CSV output file '"+
                logfile_path+"'.");
        logfile << std::setprecision(17);
        logfile
            << "step,time,x,y,z,T,qdot,is_component,k,rho,cp,vx,vy,vz,h\n";
        logfile << "dx," << current.get_dx()
                << ",dy," << current.get_dy()
                << ",dz," << current.get_dz() << '\n';
        if(!logfile)
            throw std::runtime_error(
                "Solver: unable to initialize CSV output file '"+
                logfile_path+"'.");
    }

    struct AdvectionSubstepPlan {
        int substeps = 1;
        double global_cfl = 0.0;
    };

    static std::size_t checked_work_multiply(
        std::size_t first,
        std::size_t second,
        const char* context) {
        if(first != 0 &&
           second > std::numeric_limits<std::size_t>::max() / first) {
            throw std::overflow_error(
                std::string("Solver: workload overflow while computing ") +
                context + ".");
        }
        return first * second;
    }

    static std::size_t checked_work_add(
        std::size_t first,
        std::size_t second,
        const char* context) {
        if(second > std::numeric_limits<std::size_t>::max() - first) {
            throw std::overflow_error(
                std::string("Solver: workload overflow while computing ") +
                context + ".");
        }
        return first + second;
    }

    std::size_t cell_visits_per_subcycled_step(int substeps) const {
        const std::size_t passes = checked_work_add(
            1u, static_cast<std::size_t>(substeps),
            "advection-inclusive passes per timestep");
        return checked_work_multiply(
            current.get_cell_count(), passes,
            "advection-inclusive cell visits per timestep");
    }

    void validate_advection_workload(
        std::size_t completed_visits,
        std::size_t remaining_steps,
        int substeps,
        bool report) const {
        const std::size_t visits_per_step =
            cell_visits_per_subcycled_step(substeps);
        const std::size_t remaining_visits = checked_work_multiply(
            visits_per_step, remaining_steps,
            "remaining advection-inclusive cell visits");
        const std::size_t projected_visits = checked_work_add(
            completed_visits, remaining_visits,
            "projected advection-inclusive cell visits");
        const std::size_t maximum_visits = load.get_max_cell_updates();
        if(report) {
            std::cout
                << "Projected advection-inclusive cell visits at current "
                   "flow: "
                << projected_visits << " (" << substeps
                << " advection substep(s) plus one non-advection pass per "
                   "global step)"
                << std::endl;
        }
        if(projected_visits > maximum_visits) {
            throw std::runtime_error(
                "Solver: projected advection-inclusive cell visits (" +
                std::to_string(projected_visits) +
                ") exceed simulation.max_updates (" +
                std::to_string(maximum_visits) +
                "); thermal advancement was refused. Reduce dt/duration, "
                "coarsen the mesh, or deliberately raise max_updates after "
                "reviewing the reported substep count.");
        }
    }

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

    static double cell_width_on_axis(const Cell& cell,int axis) {
        if(axis == 0) return cell.get_dx();
        if(axis == 1) return cell.get_dy();
        return cell.get_dz();
    }

    // A solid/air face has one physical fluid film, so both finite-volume
    // rows must use the same h.  In particular, an adaptive transition must
    // not use the solid width while updating the solid and the air width
    // while updating the air: that creates unequal-and-opposite heat rates.
    // The air cell also owns the fluid properties used by the correlation;
    // using its configured/current values preserves altitude and temperature
    // dependence instead of silently reverting to sea-level constants.
    static double convection_face_h(const Cell& first,
                                    const Cell& second,
                                    int axis,
                                    double minimum_delta_temperature=0.0) {
        const bool first_solid = first.is_solid();
        const Cell& air_cell = first_solid ? second : first;
        const Cell& solid_cell = first_solid ? first : second;
        const double velocity = std::sqrt(
            air_cell.get_vx()*air_cell.get_vx() +
            air_cell.get_vy()*air_cell.get_vy() +
            air_cell.get_vz()*air_cell.get_vz());
        const double delta_temperature = std::max(
            std::abs(solid_cell.get_T()-air_cell.get_T()),
            minimum_delta_temperature);
        const double film_temperature_kelvin =
            0.5*(solid_cell.get_T()+air_cell.get_T())+273.15;
        return Convection::compute_local_h(
            velocity,cell_width_on_axis(air_cell,axis),
            air_cell.get_rho(),air_cell.get_mu(),air_cell.get_k(),
            air_cell.get_pr(),delta_temperature,
            film_temperature_kelvin);
    }

    void require_usable_flow_solution(const char* stage) const {
        if(!flow_solver.converged() &&
           flow_solver.outer_iterations() > 0)
            throw std::runtime_error(
                std::string("Solver: ") + stage +
                " flow solve did not converge after " +
                std::to_string(flow_solver.outer_iterations()) +
                " outer iterations (relative face change=" +
                std::to_string(
                    flow_solver.last_relative_face_flow_change()) +
                ", relative fan change=" +
                std::to_string(
                    flow_solver.last_relative_fan_flow_change()) +
                "); thermal advancement was refused.");
        if(!flow_solver.has_face_flux_solution())
            throw std::runtime_error(
                std::string("Solver: ") + stage +
                " flow solve produced no face-flux field; thermal "
                "advancement was refused.");
    }

    void refresh_flow(const char* stage) {
        flow_solver.solve();
        require_usable_flow_solution(stage);
        // Both ping-pong meshes must carry the refreshed pressure, velocity,
        // and fan operating-point state. Otherwise the next thermal swap
        // restores the stale pre-refresh flow field.
        next = current;
    }

    double max_advection_cfl_for_dt(double candidate_dt) const {
        if(flow_solver.has_face_flux_solution()) {
            double maximum = 0.0;
            for(int x = 0; x < current.get_nx(); ++x)
                for(int y = 0; y < current.get_ny(); ++y)
                    for(int z = 0; z < current.get_nz(); ++z) {
                        const Cell& cell = current.at(x, y, z);
                        if(!cell.is_fluid()) continue;
                        double outflow = 0.0;
                        outflow += std::max(
                            -flow_solver.x_face_flux_m3s(x, y, z), 0.0);
                        outflow += std::max(
                            flow_solver.x_face_flux_m3s(x + 1, y, z), 0.0);
                        outflow += std::max(
                            -flow_solver.y_face_flux_m3s(x, y, z), 0.0);
                        outflow += std::max(
                            flow_solver.y_face_flux_m3s(x, y + 1, z), 0.0);
                        outflow += std::max(
                            -flow_solver.z_face_flux_m3s(x, y, z), 0.0);
                        outflow += std::max(
                            flow_solver.z_face_flux_m3s(x, y, z + 1), 0.0);
                        outflow += std::max(
                            -flow_solver.ambient_flow_into_cell_m3s(x, y, z),
                            0.0);
                        maximum = std::max(
                            maximum, candidate_dt * outflow / cell.volume());
                    }
            return maximum;
        }
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

    AdvectionSubstepPlan plan_advection_substeps() const {
        AdvectionSubstepPlan plan;
        plan.global_cfl = max_advection_cfl_for_dt(dt);
        if(!std::isfinite(plan.global_cfl) || plan.global_cfl < 0.0) {
            throw std::runtime_error(
                "Solver: invalid global CFL while planning advection "
                "substeps.");
        }
        const double requested_substeps =
            std::ceil(plan.global_cfl / advection_cfl_target);
        if(!std::isfinite(requested_substeps) ||
           requested_substeps >
               static_cast<double>(max_advection_substeps)) {
            throw std::runtime_error(
                "Solver: required advection substeps (" +
                std::to_string(requested_substeps) +
                ") exceed max_advection_substeps (" +
                std::to_string(max_advection_substeps) + ").");
        }
        plan.substeps = std::max(
            1, static_cast<int>(requested_substeps));
        return plan;
    }

    double compute_face_flux_advection(int x, int y, int z) {
        const Cell& cell = current.at(x, y, z);
        if(!cell.is_fluid()) return 0.0;
        const double capacity =
            cell.get_rho() * cell.get_cp() * cell.volume();
        if(!(capacity > 0.0) || !std::isfinite(capacity))
            throw std::runtime_error(
                "Solver: invalid fluid heat capacity for face-flux advection");

        double enthalpy_rate = 0.0;
        auto add_face = [&](double outward_flow, int nx, int ny, int nz) {
            if(!std::isfinite(outward_flow))
                throw std::runtime_error(
                    "Solver: non-finite face flux during advection");
            if(outward_flow == 0.0) return;
            if(!current.in_bounds(nx, ny, nz) ||
               !current.at(nx, ny, nz).is_fluid() ||
               current.wall_between(x, y, z, nx, ny, nz) != nullptr)
                throw std::runtime_error(
                    "Solver: nonzero face flux crosses a boundary, solid, or "
                    "wall without an ambient-flow model");
            const Cell* upstream = outward_flow < 0.0
                ? &current.at(nx, ny, nz) : &cell;
            const double upstream_enthalpy =
                upstream->get_rho() * upstream->get_cp() * upstream->get_T();
            if(!std::isfinite(upstream_enthalpy))
                throw std::runtime_error(
                    "Solver: non-finite upstream enthalpy during advection");
            enthalpy_rate -= outward_flow * upstream_enthalpy;
        };

        add_face(-flow_solver.x_face_flux_m3s(x, y, z), x - 1, y, z);
        add_face(flow_solver.x_face_flux_m3s(x + 1, y, z), x + 1, y, z);
        add_face(-flow_solver.y_face_flux_m3s(x, y, z), x, y - 1, z);
        add_face(flow_solver.y_face_flux_m3s(x, y + 1, z), x, y + 1, z);
        add_face(-flow_solver.z_face_flux_m3s(x, y, z), x, y, z - 1);
        add_face(flow_solver.z_face_flux_m3s(x, y, z + 1), x, y, z + 1);

        const double ambient_inflow =
            flow_solver.ambient_flow_into_cell_m3s(x, y, z);
        if(ambient_inflow != 0.0) {
            const double transported_enthalpy = ambient_inflow > 0.0
                ? current.get_env().get_rho() * current.get_env().get_cp() *
                      current.get_env().get_T_ambient()
                : cell.get_rho() * cell.get_cp() * cell.get_T();
            enthalpy_rate += ambient_inflow * transported_enthalpy;
        }
        if(!std::isfinite(enthalpy_rate))
            throw std::runtime_error(
                "Solver: non-finite advective enthalpy rate");
        return enthalpy_rate / capacity;
    }

    double compute_t_next_without_advection(int x, int y, int z) {
        const Cell& cell = current.at(x,y,z);
        if(cell.is_intake() && !flow_solver.has_face_flux_solution())
            return current.get_env().get_T_ambient();
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

    void advance_with_advection_subcycling(
        const AdvectionSubstepPlan& plan) {
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

        const double global_cfl = plan.global_cfl;
        const int substeps = plan.substeps;
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
                        if(source.is_intake() &&
                           !flow_solver.has_face_flux_solution()) {
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
        if(c.is_intake() && !flow_solver.has_face_flux_solution()) {
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

        if(c.is_intake() && !flow_solver.has_face_flux_solution()) {
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
        if(flow_solver.has_face_flux_solution())
            return compute_face_flux_advection(x, y, z);
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
        if(flow_solver.has_face_flux_solution())
            return compute_face_flux_advection(x, y, z);
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

        auto add_neighbor = [&](int nx, int ny, int nz, double area, int axis) {
            if(!current.in_bounds(nx, ny, nz)) {
                return;
            }

            const Cell& n = current.at(nx, ny, nz);

            bool c_solid = c.is_solid();
            bool n_solid = n.is_solid();

            // Only convection across solid-air interfaces
            if(c_solid == n_solid) { return; }

            // Note: unlike the conduction harmonic-mean (which genuinely
            // blends two different materials' k), h here describes one
            // fluid film at one solid-air interface, so there's only one
            // physical value to compute -- not two cell-owned values to
            // average. Computing it "twice" from the same air/solid pair
            // would always produce identical numbers, so it's computed once.
            const double h_face = convection_face_h(c,n,axis);
            Q += h_face * area * (n.get_T() - T);
            h_sum += h_face;
            h_count++;
            timestep_h_sum += h_face;
            ++timestep_h_count;
        };

        add_neighbor(x + 1, y, z, current.area_x(), 0);
        add_neighbor(x - 1, y, z, current.area_x(), 0);

        add_neighbor(x, y + 1, z, current.area_y(), 1);
        add_neighbor(x, y - 1, z, current.area_y(), 1);

        add_neighbor(x, y, z + 1, current.area_z(), 2);
        add_neighbor(x, y, z - 1, current.area_z(), 2);

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

            double area;
            switch(axis) {
                case 0: area = c.area_x(); break;
                case 1: area = c.area_y(); break;
                default: area = c.area_z(); break;
            }

            const double h_face = convection_face_h(c,n,axis);
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
        if(!logfile)
            throw std::runtime_error(
                "Solver: failed while writing step "+
                std::to_string(step)+" to CSV output file '"+
                logfile_path+"'.");
    }
};

#endif
