#ifndef THERMAL_FLOW_SOLVER_HPP
#define THERMAL_FLOW_SOLVER_HPP

#include <vector>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <limits>
#include <string>

#include "mesh.hpp"
#include "cell.hpp"
#include "convection.hpp"

/*
==========================================================================================================================================================
FlowSolver
==========================================================================================================================================================
Purpose:
-------
Computes a quasi-steady pressure field over all fluid cells in the mesh, then derives face flow rates and cell-centered celocities from it
This is a PRE-PASS: it runs once (or whenever geometry / fan state changes), NOT once per thermal timestep. Air pressure in a rack equilibrates on the order
of MILISECONDS; thermal diffusion happens over seconds to minutes. Treating flow as quasi-steady relative to the thermal solver is a simplification for 
hotspot rediction targeting 10-20% accuracy- not meant to be a transieft SFD solver.

Physical model:
------------------
    Node     <-> fluid cell pressure unknown P_i
    Resistor <-> face between two fluid cells (conductance C_i)
    Ground   <-> vent cell (fixed connection t ambient, P=0)
    Current  <-> volumentric flow rate Q
    Source   <-> fan cell (fixed Q injected/removed, independent of P)

This is literally Kirchoff's Current Law with pressure <-> voltage, flow <-> current
COM at every fluid cell reduces to one linera equation per cell, solved iteratively with Gauss-Siedel/SOR 

Nodal eequation for fluid cell i:
    P_i = ( sum J C_ij + C_vent, i ) - sum_j ( C_ij + P_j ) = S_i

    sum_j C_ij = total conductance to fluid neighbors j
    C_vent,i   = conductance to ambient (0 unless i is a vent cell)
    S_i        = net flow source at i (0 unless i is a fan cell)

Solid cells (components, walls) are NOT part of the FLOW network 
This is what makes flow 'go around" obstacles for free: gauss-seidel finds path of least resistance
*/

class FlowSolver {
public: 
    //-------------------------------------------------------------------------------------
    // Tunables
    //-------------------------------------------------------------------------------------
    // Resistivity: linearized friction resistance per unit length for fluid-fluid faces
    // This is the global knob for a first pass 
    // bigger = "Stickier", smaller = "freer flow"
    //
    // vend_discharge_coeff: Cd for vent openings, dimensionless. ~0.6 is a standard value
    // for sharp-edged orifice; 
    // rounded/louvered ~(0.7-0.9)
    //
    // tolerance / max_iters / sor_omega: standard iterative-solver knobs 
    // sor_omega in (1.0, 2.0) accelerates convergence over plain
    // too high will destabalize but back off toward 1.0 - oscillations ~ convergence

    FlowSolver() = default;

    FlowSolver(const FlowSolver&) = delete;
    FlowSolver& operator=(const FlowSolver&) = delete;
    FlowSolver(FlowSolver&&) = delete;
    FlowSolver& operator=(FlowSolver&&) = delete;
    
    FlowSolver(Mesh& mesh_, 
               double linear_resistivity_ = 5.0,
               double tolerance_ = 1e-6,
               int max_iters_ = 20000,
               double sor_omega_ = 1.3,
               int max_outer_iters_ = 30,
               double flow_tolerance_ = 1e-4,
               std::string pressure_method_ = "sor") :
               mesh(mesh_),
               linear_resistivity(linear_resistivity_),
               pressure_tolerance(tolerance_),
               max_pressure_iters(max_iters_),
               omega(sor_omega_),
               max_outer_iters(max_outer_iters_),
               flow_tolerance(flow_tolerance_),
               pressure_method(std::move(pressure_method_))
               {
                    if(!std::isfinite(linear_resistivity) ||
                       linear_resistivity < 0.0) {
                        throw std::invalid_argument("FlowSolver: resistivity must be >= 0");
                    }
                    if(!std::isfinite(pressure_tolerance) ||
                       pressure_tolerance <= 0.0) {
                        throw std::invalid_argument(
                            "FlowSolver: pressure tolerance must be finite and > 0");
                    }
                    if(max_pressure_iters <= 0 || max_outer_iters <= 0) {
                        throw std::invalid_argument(
                            "FlowSolver: iteration limits must be > 0");
                    }
                    if(!std::isfinite(omega) || omega <= 0.0 || omega >= 2.0) {
                        throw std::invalid_argument("FlowSolver: SOR omega must be in (0,2)");
                    }
                    if(!std::isfinite(flow_tolerance) ||
                       flow_tolerance <= 0.0) {
                        throw std::invalid_argument(
                            "FlowSolver: flow tolerance must be finite and > 0");
                    }
                    if(!std::isfinite(
                           std::max(pressure_tolerance, minimum_flow) /
                           flow_tolerance)) {
                        throw std::invalid_argument(
                            "FlowSolver: mixed flow convergence scale overflow");
                    }
                    if(pressure_method != "sor" && pressure_method != "pcg") {
                        throw std::invalid_argument(
                            "FlowSolver: pressure_method must be 'sor' or 'pcg'");
                    }
               }
    
    // runs the full pre-pass
    // derive face flows and write vx,vy,vz into every cell
    // of mesh, call once before solve           
    void solve() {
        face_flux_solution_available = false;
        last_outer_converged = false;
        last_pressure_converged = false;
        last_realized_continuity_residual =
            std::numeric_limits<double>::infinity();
        last_outer_iterations = 0;
        last_face_flow_change = 0.0;
        last_fan_flow_change = 0.0;
        initialize_storage();
        initialize_pressures();

        const bool adaptive = !mesh.is_uniform();

        bool outer_converged = false;
        for (int outer = 0; outer < max_outer_iters; ++outer) {
            if (adaptive) build_linearized_network_adaptive();
            else          build_linearized_network();
            solve_pressures();

            last_outer_iterations = outer + 1;
            last_face_flow_change = update_face_flows(false);
            last_fan_flow_change = update_fan_operating_points(false);
            write_internal_fan_face_fluxes();
            if (adaptive) update_cell_velocities_adaptive();
            else          update_cell_velocities();
            apply_boundary_fan_velocities();
            const double max_relative_change = std::max(
                last_face_flow_change, last_fan_flow_change);

            if (max_relative_change < flow_tolerance && outer > 0 &&
                last_pressure_converged) {
                // Exact publication may expose a bound transition that was
                // hidden by relaxed nonlinear iterates.  In that case the
                // new active set must receive its own pressure solve before
                // any face field can be published.
                if(!publish_consistent_face_fluxes(adaptive)) continue;
                std::cout << "FlowSolver: nonlinear flow converged after "
                          << outer + 1 << " outer iterations (relative flow change = "
                          << max_relative_change << ", relative face change = "
                          << last_face_flow_change << ", relative fan change = "
                          << last_fan_flow_change << ", absolute face change = "
                          << last_absolute_face_flow_change
                          << " m^3/s, max |face flow| = "
                          << last_maximum_face_flow << " m^3/s, max |fan flow| = "
                          << maximum_fan_operating_flow_m3s() << " m^3/s)\n";
                outer_converged = true;
                last_outer_converged = true;
                break;
            }
        }

        if (!outer_converged) {
            std::cerr << "FlowSolver: WARNING -- nonlinear face flow did not "
                         "converge within " << max_outer_iters
                      << " outer iterations (relative face change = "
                      << last_face_flow_change << ", relative fan change = "
                      << last_fan_flow_change << ", absolute face change = "
                      << last_absolute_face_flow_change
                      << " m^3/s, max |face flow| = "
                      << last_maximum_face_flow << " m^3/s, max |fan flow| = "
                      << maximum_fan_operating_flow_m3s() << " m^3/s).\n";
        }

        report_mass_balance();
        face_flux_solution_available =
            last_outer_converged && last_pressure_converged;
    }           

    void solve(Mesh& mesh_) {
        if(&mesh_ != &mesh)
            throw std::invalid_argument(
                "FlowSolver::solve(Mesh&) cannot rebind the solver's mesh; "
                "construct a new FlowSolver for a different mesh.");
        solve();
    }    

    void set_resistivity(double r) { linear_resistivity = r; }
    double get_resistivity() const { return linear_resistivity; }
    bool converged() const { return last_outer_converged; }
    int outer_iterations() const { return last_outer_iterations; }
    double last_relative_face_flow_change() const {
        return last_face_flow_change;
    }
    double last_relative_fan_flow_change() const {
        return last_fan_flow_change;
    }
    double last_absolute_face_flow_change_m3s() const {
        return last_absolute_face_flow_change;
    }
    double last_maximum_face_flow_m3s() const {
        return last_maximum_face_flow;
    }
    double maximum_fan_operating_flow_m3s() const {
        double maximum = 0.0;
        for(const Cell& cell : mesh.get_cells()) {
            const double flow = cell.has_fan_curve() ?
                cell.get_fan_Q_ref() : cell.get_flow_source();
            if(!std::isfinite(flow))
                return std::numeric_limits<double>::infinity();
            maximum = std::max(maximum, std::abs(flow));
        }
        for(const auto& fan : mesh.get_internal_fans()) {
            const double flow = fan.has_curve() ? fan.q_ref : fan.flow_m3s;
            if(!std::isfinite(flow))
                return std::numeric_limits<double>::infinity();
            maximum = std::max(
                maximum, std::abs(flow));
        }
        return maximum;
    }
    double x_face_flux_m3s(int x_face, int y, int z) const {
        if(x_face < 0 || x_face > mesh.get_nx() ||
           y < 0 || y >= mesh.get_ny() || z < 0 || z >= mesh.get_nz())
            throw std::out_of_range("FlowSolver: x-face flux index out of range");
        return qx[xface_idx(x_face, y, z)];
    }
    double y_face_flux_m3s(int x, int y_face, int z) const {
        if(x < 0 || x >= mesh.get_nx() ||
           y_face < 0 || y_face > mesh.get_ny() ||
           z < 0 || z >= mesh.get_nz())
            throw std::out_of_range("FlowSolver: y-face flux index out of range");
        return qy[yface_idx(x, y_face, z)];
    }
    double z_face_flux_m3s(int x, int y, int z_face) const {
        if(x < 0 || x >= mesh.get_nx() || y < 0 || y >= mesh.get_ny() ||
           z_face < 0 || z_face > mesh.get_nz())
            throw std::out_of_range("FlowSolver: z-face flux index out of range");
        return qz[zface_idx(x, y, z_face)];
    }
    bool has_face_flux_solution() const {
        return face_flux_solution_available;
    }
    double maximum_realized_continuity_residual_m3s() const {
        return last_realized_continuity_residual;
    }
    // Positive means ambient air enters this cell. Negative means the cell
    // discharges to ambient. Internal fans are excluded because their paired
    // transfer is represented on qx/qy/qz.
    double ambient_flow_into_cell_m3s(int x, int y, int z) const {
        if(!mesh.in_bounds(x, y, z))
            throw std::out_of_range(
                "FlowSolver: ambient-flow cell index out of range");
        if(!face_flux_solution_available) return 0.0;
        return ambient_flow_into_cell_unchecked(x, y, z);
    }
    bool ordinary_face_is_suppressed_for_internal_fan(
        const std::array<int, 3>& upstream,
        const std::array<int, 3>& downstream) const {
        const InternalFanFace face = canonical_internal_fan_face(
            upstream, downstream);
        return is_internal_fan_face(face.axis, face.face_index);
    }
    // Positive means flow from the supplied upstream cell to downstream,
    // independent of whether that direction is a positive or negative mesh
    // coordinate direction.
    double internal_fan_face_flux_m3s(
        const std::array<int, 3>& upstream,
        const std::array<int, 3>& downstream) const {
        const InternalFanFace face = canonical_internal_fan_face(
            upstream, downstream);
        if(!is_internal_fan_face(face.axis, face.face_index))
            throw std::invalid_argument(
                "FlowSolver: requested cell pair is not an internal-fan face");
        return face.global_sign * global_face_flow(face.axis, face.face_index);
    }
    static constexpr double transitional_reynolds_start() { return 2000.0; }
    static constexpr double transitional_reynolds_end() { return 4000.0; }
    static double smooth_pipe_friction_factor(double reynolds) {
        const double bounded_reynolds = std::max(reynolds, 1.0);
        const double laminar = 64.0 / bounded_reynolds;
        if(bounded_reynolds <= transitional_reynolds_start()) return laminar;
        const double inv_sqrt_f =
            -1.8 * std::log10(6.9 / bounded_reynolds);
        const double turbulent = 1.0 / (inv_sqrt_f * inv_sqrt_f);
        if(bounded_reynolds >= transitional_reynolds_end()) return turbulent;
        const double x = (bounded_reynolds - transitional_reynolds_start()) /
            (transitional_reynolds_end() - transitional_reynolds_start());
        const double blend = x * x * (3.0 - 2.0 * x);
        return laminar + blend * (turbulent - laminar);
    }
    static double mixed_relative_update_norm(
        double maximum_absolute_update,
        double maximum_absolute_value,
        double relative_tolerance,
        double absolute_update_tolerance) {
        if(!std::isfinite(maximum_absolute_update) ||
           !std::isfinite(maximum_absolute_value) ||
           !std::isfinite(relative_tolerance) ||
           !std::isfinite(absolute_update_tolerance) ||
           maximum_absolute_update < 0.0 ||
           maximum_absolute_value < 0.0 ||
           relative_tolerance <= 0.0 || absolute_update_tolerance <= 0.0)
            return std::numeric_limits<double>::infinity();
        const double update = maximum_absolute_update;
        const double scale_floor =
            absolute_update_tolerance / relative_tolerance;
        if(!std::isfinite(scale_floor))
            return std::numeric_limits<double>::infinity();
        const double scale = std::max(maximum_absolute_value, scale_floor);
        const double norm = update / scale;
        return std::isfinite(norm) ? norm :
            std::numeric_limits<double>::infinity();
    }
    double total_source_m3s() const { return last_total_source; }
    double total_vent_flow_m3s() const { return last_total_vent; }
    double mass_imbalance_m3s() const {
        return last_total_source - last_total_vent;
    }
private:
    enum class Axis { X, Y, Z };

    struct FaceLink {
        int nx = 0;
        int ny = 0;
        int nz = 0;
        Axis axis = Axis::X;
        size_t face_index = 0;
        double area = 0.0;
        double length = 0.0;
        double hydraulic_diameter = 0.0;
        double conductance = 0.0; // linearized m^3/(s Pa)
        double direction_sign = 1.0; // cell -> neighbor vs global +axis face
    };

    struct InternalCurveLink {
        int nx = 0;
        int ny = 0;
        int nz = 0;
        double conductance = 0.0;
    };

    struct InternalFanFace {
        Axis axis = Axis::X;
        size_t face_index = 0;
        double global_sign = 1.0;
    };

    enum class FanActiveSet : unsigned char {
        Interior,
        LowerBound,
        UpperBound
    };

    struct FanOperatingPoint {
        FanActiveSet active_set = FanActiveSet::Interior;
        double target_flow = 0.0;
    };

    Mesh& mesh;
    double linear_resistivity;
    double pressure_tolerance;   // absolute continuity residual, m^3/s
    int max_pressure_iters;
    double omega;
    int max_outer_iters;
    double flow_tolerance;
    std::string pressure_method = "sor";
    bool last_outer_converged = false;
    bool last_pressure_converged = false;
    bool face_flux_solution_available = false;
    int last_outer_iterations = 0;
    double last_face_flow_change = 0.0;
    double last_fan_flow_change = 0.0;
    double last_absolute_face_flow_change = 0.0;
    double last_maximum_face_flow = 0.0;
    double last_total_source = 0.0;
    double last_total_vent = 0.0;
    double last_realized_continuity_residual =
        std::numeric_limits<double>::infinity();
    bool fan_active_set_changed_last_update = false;

    // Model safeguards/tunables.
    double minimum_reynolds = 1.0;
    double minimum_flow = 1e-9;       // m^3/s, nonlinear bootstrap
    double minimum_pressure = 1e-3;   // Pa, vent linearization bootstrap
    double pressure_relaxation = 1.0; // damp nonlinear outer updates
    double flow_relaxation = 0.5;
    double straight_loss_K = 0.15;
    double ninety_turn_loss_K = 1.0;
    double reverse_loss_K = 2.0;

    std::vector<std::vector<FaceLink>> neighbors;
    std::vector<double> vent_C;
    std::vector<double> fan_ground_C;  // conductance to ambient from fan's internal resistance
    std::vector<double> source_S;
    // Boundary-only Norton source before paired internal-fan sources are
    // assembled. This is the source seen by thermal ambient exchange.
    std::vector<double> ambient_source_S;
    std::vector<std::vector<InternalCurveLink>> internal_curve_neighbors;
    std::vector<FanActiveSet> boundary_fan_active_sets;
    std::vector<FanActiveSet> internal_fan_active_sets;
    std::vector<unsigned char> pressure_references;

    // One value per global positive-oriented mesh face.
    std::vector<double> qx;
    std::vector<double> qy;
    std::vector<double> qz;
    std::vector<unsigned char> internal_fan_x_faces;
    std::vector<unsigned char> internal_fan_y_faces;
    std::vector<unsigned char> internal_fan_z_faces;

    size_t cell_idx(int x, int y, int z) const { return mesh.idx(x, y, z); }

    bool is_pressure_reference(size_t index) const {
        return index < pressure_references.size() &&
               pressure_references[index] != 0;
    }

    size_t xface_idx(int x_face, int y, int z) const {
        return (static_cast<size_t>(x_face) * mesh.get_ny() + y) * mesh.get_nz() + z;
    }
    size_t yface_idx(int x, int y_face, int z) const {
        return (static_cast<size_t>(x) * (mesh.get_ny() + 1) + y_face) * mesh.get_nz() + z;
    }
    size_t zface_idx(int x, int y, int z_face) const {
        return (static_cast<size_t>(x) * mesh.get_ny() + y) * (mesh.get_nz() + 1) + z_face;
    }

    double ambient_flow_into_cell_unchecked(int x, int y, int z) const {
        const Cell& cell = mesh.at(x, y, z);
        if(!cell.is_fluid()) return 0.0;
        const size_t index = cell_idx(x, y, z);
        double inflow = ambient_source_S[index];
        if(cell.get_state() == Cell::State::Vent)
            inflow -= vent_C[index] * cell.get_pressure();
        inflow -= fan_ground_C[index] * cell.get_pressure();
        if(!std::isfinite(inflow))
            throw std::runtime_error(
                "FlowSolver: non-finite ambient boundary flow");
        return inflow;
    }

    double maximum_realized_continuity_residual() const {
        double maximum = 0.0;
        for(int x = 0; x < mesh.get_nx(); ++x)
            for(int y = 0; y < mesh.get_ny(); ++y)
                for(int z = 0; z < mesh.get_nz(); ++z) {
                    if(!mesh.at(x, y, z).is_fluid()) continue;
                    const double outward =
                        -qx[xface_idx(x, y, z)] +
                         qx[xface_idx(x + 1, y, z)] -
                         qy[yface_idx(x, y, z)] +
                         qy[yface_idx(x, y + 1, z)] -
                         qz[zface_idx(x, y, z)] +
                         qz[zface_idx(x, y, z + 1)];
                    const double residual =
                        ambient_flow_into_cell_unchecked(x, y, z) - outward;
                    if(!std::isfinite(residual))
                        throw std::runtime_error(
                            "FlowSolver: non-finite realized continuity residual");
                    maximum = std::max(maximum, std::abs(residual));
                }
        return maximum;
    }

    // The nonlinear iterations deliberately relax face and fan-flow updates.
    // Once their linearization has converged, rebuild and solve that one final
    // network, then publish every ordinary face and fan flow from the same
    // pressure solution.  Thermal advection must never receive a mixture of
    // relaxed ordinary flows and newly evaluated fan flows because that field
    // does not, in general, satisfy local continuity.
    bool publish_consistent_face_fluxes(bool adaptive) {
        if(adaptive) build_linearized_network_adaptive();
        else         build_linearized_network();
        solve_pressures();
        if(!last_pressure_converged)
            throw std::runtime_error(
                "FlowSolver: final pressure solve failed; refusing to publish "
                "a non-conservative face-flux field");

        const double converged_absolute_face_change =
            last_absolute_face_flow_change;
        const double converged_maximum_face_flow =
            last_maximum_face_flow;

        (void)update_face_flows(true);
        (void)update_fan_operating_points(true);
        if(fan_active_set_changed_last_update)
            return false;
        write_internal_fan_face_fluxes();
        if(adaptive) update_cell_velocities_adaptive();
        else         update_cell_velocities();
        apply_boundary_fan_velocities();

        last_realized_continuity_residual =
            maximum_realized_continuity_residual();
        const double continuity_limit =
            std::max(1e-12, 1.01 * pressure_tolerance);
        if(last_realized_continuity_residual > continuity_limit)
            throw std::runtime_error(
                "FlowSolver: final published face-flux field violates local "
                "continuity (maximum residual " +
                std::to_string(last_realized_continuity_residual) +
                " m^3/s exceeds " + std::to_string(continuity_limit) +
                " m^3/s)");

        // Preserve the nonlinear convergence diagnostics rather than replacing
        // them with the intentionally unrelaxed final publication delta.
        last_absolute_face_flow_change = converged_absolute_face_change;
        last_maximum_face_flow = converged_maximum_face_flow;
        return true;
    }

    InternalFanFace canonical_internal_fan_face(
        const std::array<int, 3>& upstream,
        const std::array<int, 3>& downstream) const {
        if(!mesh.in_bounds(upstream[0], upstream[1], upstream[2]) ||
           !mesh.in_bounds(downstream[0], downstream[1], downstream[2]))
            throw std::invalid_argument(
                "FlowSolver: internal-fan endpoint is outside the mesh");
        const int dx = downstream[0] - upstream[0];
        const int dy = downstream[1] - upstream[1];
        const int dz = downstream[2] - upstream[2];
        if(std::abs(dx) + std::abs(dy) + std::abs(dz) != 1)
            throw std::invalid_argument(
                "FlowSolver: internal-fan endpoints must share one mesh face");
        if(dx != 0) {
            const int face_x = std::max(upstream[0], downstream[0]);
            return {Axis::X,
                    xface_idx(face_x, upstream[1], upstream[2]),
                    dx > 0 ? 1.0 : -1.0};
        }
        if(dy != 0) {
            const int face_y = std::max(upstream[1], downstream[1]);
            return {Axis::Y,
                    yface_idx(upstream[0], face_y, upstream[2]),
                    dy > 0 ? 1.0 : -1.0};
        }
        const int face_z = std::max(upstream[2], downstream[2]);
        return {Axis::Z,
                zface_idx(upstream[0], upstream[1], face_z),
                dz > 0 ? 1.0 : -1.0};
    }

    std::vector<unsigned char>& internal_fan_face_mask(Axis axis) {
        if(axis == Axis::X) return internal_fan_x_faces;
        if(axis == Axis::Y) return internal_fan_y_faces;
        return internal_fan_z_faces;
    }

    const std::vector<unsigned char>& internal_fan_face_mask(Axis axis) const {
        if(axis == Axis::X) return internal_fan_x_faces;
        if(axis == Axis::Y) return internal_fan_y_faces;
        return internal_fan_z_faces;
    }

    bool is_internal_fan_face(Axis axis, size_t face_index) const {
        const std::vector<unsigned char>& mask = internal_fan_face_mask(axis);
        return face_index < mask.size() && mask[face_index] != 0;
    }

    void write_internal_fan_face_fluxes() {
        for(const auto& fan : mesh.get_internal_fans()) {
            const InternalFanFace face = canonical_internal_fan_face(
                fan.upstream, fan.downstream);
            const double flow = fan.has_curve() ? fan.q_ref : fan.flow_m3s;
            if(!std::isfinite(flow) || flow < 0.0)
                throw std::runtime_error(
                    "FlowSolver: internal-fan flow must be finite and nonnegative");
            face_flow_reference(face.axis, face.face_index) =
                face.global_sign * flow;
        }
    }

    void initialize_storage() {
        const size_t n = static_cast<size_t>(mesh.get_nx()) * mesh.get_ny() * mesh.get_nz();
        neighbors.assign(n, {});
        vent_C.assign(n, 0.0);
        fan_ground_C.assign(n, 0.0);
        source_S.assign(n, 0.0);
        ambient_source_S.assign(n, 0.0);
        internal_curve_neighbors.assign(n, {});
        boundary_fan_active_sets.assign(n, FanActiveSet::Interior);
        internal_fan_active_sets.assign(
            mesh.get_internal_fans().size(), FanActiveSet::Interior);
        pressure_references.assign(n, 0);

        qx.assign(static_cast<size_t>(mesh.get_nx() + 1) * mesh.get_ny() * mesh.get_nz(), 0.0);
        qy.assign(static_cast<size_t>(mesh.get_nx()) * (mesh.get_ny() + 1) * mesh.get_nz(), 0.0);
        qz.assign(static_cast<size_t>(mesh.get_nx()) * mesh.get_ny() * (mesh.get_nz() + 1), 0.0);
        internal_fan_x_faces.assign(qx.size(), 0);
        internal_fan_y_faces.assign(qy.size(), 0);
        internal_fan_z_faces.assign(qz.size(), 0);

        for(const auto& fan : mesh.get_internal_fans()) {
            const InternalFanFace face = canonical_internal_fan_face(
                fan.upstream, fan.downstream);
            if(!mesh.at(fan.upstream[0], fan.upstream[1], fan.upstream[2])
                    .is_fluid() ||
               !mesh.at(fan.downstream[0], fan.downstream[1], fan.downstream[2])
                    .is_fluid())
                throw std::runtime_error(
                    "FlowSolver: internal-fan endpoints must both be fluid "
                    "cells");
            std::vector<unsigned char>& mask = internal_fan_face_mask(face.axis);
            if(mask[face.face_index] != 0)
                throw std::runtime_error(
                    "FlowSolver: multiple internal fans occupy one mesh face");
            mask[face.face_index] = 1;
        }
        write_internal_fan_face_fluxes();
    }

    void initialize_pressures() {
        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    if (mesh.at(x, y, z).is_fluid()) {
                        mesh.at(x, y, z).set_pressure(0.0);
                    }
                }
            }
        }
    }

    static double hydraulic_diameter(Axis axis, const Mesh& m) {
        double a = 0.0;
        double b = 0.0;
        if (axis == Axis::X) { a = m.get_dy(); b = m.get_dz(); }
        if (axis == Axis::Y) { a = m.get_dx(); b = m.get_dz(); }
        if (axis == Axis::Z) { a = m.get_dx(); b = m.get_dy(); }
        return (a > 0.0 && b > 0.0) ? 2.0 * a * b / (a + b) : 0.0;
    }

    // Same formula, reading one cell's own width instead of the mesh-wide
    // scalar. On a tensor-product grid the two un-stepped axes are shared
    // by every cell at that face regardless of which side you read from.
    static double hydraulic_diameter_adaptive(Axis axis, const Cell& c) {
        double a = 0.0;
        double b = 0.0;
        if (axis == Axis::X) { a = c.get_dy(); b = c.get_dz(); }
        if (axis == Axis::Y) { a = c.get_dx(); b = c.get_dz(); }
        if (axis == Axis::Z) { a = c.get_dx(); b = c.get_dy(); }
        return (a > 0.0 && b > 0.0) ? 2.0 * a * b / (a + b) : 0.0;
    }

    double old_face_flow(Axis axis, size_t face_index) const {
        if (axis == Axis::X) return qx[face_index];
        if (axis == Axis::Y) return qy[face_index];
        return qz[face_index];
    }

    double friction_factor(double Re) const {
        // Haaland for a smooth duct (relative roughness = 0).  Real pipe
        // flow transitions over a band rather than jumping at one Reynolds
        // number.  A C1 smoothstep blend over Re=[2000,4000] avoids a
        // nonphysical 74% coefficient jump at Re=2300 while preserving the
        // exact laminar and turbulent correlations outside that band.
        return smooth_pipe_friction_factor(std::max(Re, minimum_reynolds));
    }

    double turn_loss(const Cell& c, Axis axis, double global_face_sign) const {
        const double vx = c.get_vx();
        const double vy = c.get_vy();
        const double vz = c.get_vz();
        const double mag = std::sqrt(vx * vx + vy * vy + vz * vz);
        if (mag < 1e-12) return straight_loss_K;

        double directional_velocity = 0.0;
        if (axis == Axis::X) directional_velocity = vx;
        if (axis == Axis::Y) directional_velocity = vy;
        if (axis == Axis::Z) directional_velocity = vz;

        const double cos_theta = global_face_sign * directional_velocity / mag;
        if (cos_theta > 0.707) return straight_loss_K;
        if (cos_theta > -0.25) return ninety_turn_loss_K;
        return reverse_loss_K;
    }

    double linearized_face_conductance(const Cell& low_cell,
                                       const Cell& high_cell,
                                       Axis axis,
                                       size_t face_index,
                                       double area,
                                       double length,
                                       double Dh) const {
        const double q_old = old_face_flow(axis, face_index); // global +axis
        const double q_ref = std::max(std::abs(q_old), minimum_flow);
        const double velocity = q_ref / area;
        const double rho = std::max(0.5 * (low_cell.get_rho() + high_cell.get_rho()), 1e-9);
        const double mu = std::max(0.5 * (low_cell.get_mu() + high_cell.get_mu()), 1e-12);
        const double Re = rho * velocity * Dh / mu;
        const double f = friction_factor(Re);

        // Use the cell upstream of this global face flow to estimate the turn.
        // This produces one shared conductance for both cells touching the face.
        const Cell& upstream = (q_old >= 0.0) ? low_cell : high_cell;
        const double flow_sign = (q_old >= 0.0) ? +1.0 : -1.0;
        const double K = turn_loss(upstream, axis, flow_sign);

        const int axis_index = axis == Axis::X ? 0 : (axis == Axis::Y ? 1 : 2);
        const double porous_d = 0.5*(mesh.get_porous_darcy(low_cell,axis_index)+
                                     mesh.get_porous_darcy(high_cell,axis_index));
        const double porous_f = 0.5*(mesh.get_porous_forchheimer(low_cell,axis_index)+
                                     mesh.get_porous_forchheimer(high_cell,axis_index));

        // DeltaP = R_linear*Q + R_quad*Q|Q|.
        const double R_linear = linear_resistivity * length / area +
                                mu*porous_d*length/area;
        const double R_quad = (K + f * length / Dh) * rho / (2.0 * area * area) +
                              0.5*rho*porous_f*length/(area*area);
        const double R_effective = R_linear + R_quad * q_ref;
        return 1.0 / std::max(R_effective, 1e-30);
    }

    double fan_upper_flow_bound(
        double curve_a, double curve_b, double curve_c) const {
        const double upper = fan_curve_first_positive_zero(
            curve_a, curve_b, curve_c);
        if(!std::isfinite(upper) || upper <= minimum_flow)
            throw std::runtime_error(
                "FlowSolver: curved fan must have a finite positive "
                "zero-pressure flow above the nonlinear bootstrap floor");
        return upper;
    }

    double active_set_fixed_flow(
        FanActiveSet active_set,
        double curve_a, double curve_b, double curve_c) const {
        // minimum_flow is only a bootstrap for evaluating an interior
        // tangent. A physically active one-way lower bound is exactly zero;
        // retaining the bootstrap as a fixed source would manufacture mass
        // and incorrectly make a shut-off passive pocket source-bearing.
        if(active_set == FanActiveSet::LowerBound) return 0.0;
        if(active_set == FanActiveSet::UpperBound)
            return fan_upper_flow_bound(curve_a, curve_b, curve_c);
        throw std::logic_error(
            "FlowSolver: interior fan active set has no fixed flow");
    }

    FanOperatingPoint project_fan_operating_point(
        double stored_flow,
        double curve_a,
        double curve_b,
        double curve_c,
        double rho_ratio,
        double required_pressure_head) const {
        if(!std::isfinite(stored_flow) || stored_flow < 0.0 ||
           !std::isfinite(curve_a) ||
           !std::isfinite(curve_b) || !std::isfinite(curve_c) ||
           !std::isfinite(rho_ratio) || rho_ratio <= 0.0 ||
           !std::isfinite(required_pressure_head))
            throw std::runtime_error(
                "FlowSolver: non-finite bounded fan operating-point state");

        const double upper = fan_upper_flow_bound(
            curve_a, curve_b, curve_c);
        const double reference = std::clamp(
            stored_flow, minimum_flow, upper);
        const double reference_pressure = bounded_fan_curve_pressure(
            curve_a, curve_b, curve_c, reference) * rho_ratio;
        const double slope =
            -(curve_b + 2.0 * curve_c * reference) * rho_ratio;
        const double safe_slope = std::min(slope, -1e-9);
        const double unconstrained = reference +
            (required_pressure_head - reference_pressure) / safe_slope;
        if(!std::isfinite(unconstrained))
            throw std::runtime_error(
                "FlowSolver: bounded fan projection became non-finite");
        // Equality at either endpoint remains on the tangent. That limiting
        // relation still supplies the pressure coupling needed to determine
        // shutoff/free-delivery pressure in an otherwise passive pocket.
        // Activate a fixed branch only when the unconstrained Newton flow is
        // genuinely outside the physical fan domain.
        if(unconstrained < 0.0)
            return {FanActiveSet::LowerBound, 0.0};
        if(unconstrained > upper)
            return {FanActiveSet::UpperBound, upper};
        return {FanActiveSet::Interior, unconstrained};
    }

    void assemble_boundary_curve_fan(
        size_t index, const Cell& cell) {
        const double sign = cell.is_intake() ? +1.0 : -1.0;
        const FanActiveSet active_set = boundary_fan_active_sets[index];
        if(active_set != FanActiveSet::Interior) {
            source_S[index] += sign * active_set_fixed_flow(
                active_set,
                cell.get_fan_curve_a(),
                cell.get_fan_curve_b(),
                cell.get_fan_curve_c());
            return;
        }

        const double upper = fan_upper_flow_bound(
            cell.get_fan_curve_a(),
            cell.get_fan_curve_b(),
            cell.get_fan_curve_c());
        const double reference = std::clamp(
            cell.get_fan_Q_ref(), minimum_flow, upper);
        const double rho_local = std::max(cell.get_rho(), 1e-9);
        const double rho_ratio = rho_local / cell.get_fan_rho_rated();
        const double reference_pressure = bounded_fan_curve_pressure(
            cell.get_fan_curve_a(), cell.get_fan_curve_b(),
            cell.get_fan_curve_c(), reference) * rho_ratio;
        const double slope =
            -(cell.get_fan_curve_b() +
              2.0 * cell.get_fan_curve_c() * reference) * rho_ratio;
        const double safe_slope = std::min(slope, -1e-9);
        const double conductance = -1.0 / safe_slope;
        fan_ground_C[index] = conductance;
        source_S[index] += sign *
            (reference + reference_pressure * conductance);
    }

    void build_linearized_network() {
        for (auto& links : neighbors) links.clear();
        std::fill(vent_C.begin(), vent_C.end(), 0.0);
        std::fill(source_S.begin(), source_S.end(), 0.0);
        std::fill(ambient_source_S.begin(), ambient_source_S.end(), 0.0);
        for(auto& links : internal_curve_neighbors) links.clear();

        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    const Cell& c = mesh.at(x, y, z);
                    if (!c.is_fluid()) continue;
                    const size_t i = cell_idx(x, y, z);
                    source_S[i] = c.get_flow_source();

                    fan_ground_C[i] = 0.0;

                    if (c.has_fan_curve())
                        assemble_boundary_curve_fan(i, c);
                    ambient_source_S[i] = source_S[i];
                    add_link(i, x, y, z, x + 1, y, z, Axis::X,
                             xface_idx(x + 1, y, z), mesh.area_x(), mesh.get_dx(), +1.0);
                    add_link(i, x, y, z, x - 1, y, z, Axis::X,
                             xface_idx(x, y, z), mesh.area_x(), mesh.get_dx(), -1.0);
                    add_link(i, x, y, z, x, y + 1, z, Axis::Y,
                             yface_idx(x, y + 1, z), mesh.area_y(), mesh.get_dy(), +1.0);
                    add_link(i, x, y, z, x, y - 1, z, Axis::Y,
                             yface_idx(x, y, z), mesh.area_y(), mesh.get_dy(), -1.0);
                    add_link(i, x, y, z, x, y, z + 1, Axis::Z,
                             zface_idx(x, y, z + 1), mesh.area_z(), mesh.get_dz(), +1.0);
                    add_link(i, x, y, z, x, y, z - 1, Axis::Z,
                             zface_idx(x, y, z), mesh.area_z(), mesh.get_dz(), -1.0);

                    if (c.get_state() == Cell::State::Vent) {
                        // Mesh stamps Cd*A_free into this field.
                        const double CdA = c.get_vent_conductance();
                        const double rho = std::max(c.get_rho(), 1e-9);
                        const double p_ref = std::max(std::abs(c.get_pressure()), minimum_pressure);
                        vent_C[i] = CdA * std::sqrt(2.0 / (rho * p_ref));
                    }
                }
            }
        }

        apply_internal_fans_and_pressure_reference();
    }

    void apply_internal_fans_and_pressure_reference() {
        // Internal fans are geometry-independent two-node elements. This
        // same assembly is shared by uniform and adaptive face networks.
        const auto& fans = mesh.get_internal_fans();
        for(size_t fan_index = 0; fan_index < fans.size(); ++fan_index) {
            const auto& fan = fans[fan_index];
            const size_t upstream =
                cell_idx(fan.upstream[0], fan.upstream[1], fan.upstream[2]);
            const size_t downstream =
                cell_idx(fan.downstream[0], fan.downstream[1], fan.downstream[2]);
            if(!fan.has_curve()) {
                source_S[upstream] -= fan.flow_m3s;
                source_S[downstream] += fan.flow_m3s;
                continue;
            }

            const FanActiveSet active_set =
                internal_fan_active_sets[fan_index];
            if(active_set != FanActiveSet::Interior) {
                const double fixed_flow = active_set_fixed_flow(
                    active_set, fan.curve_a, fan.curve_b, fan.curve_c);
                source_S[upstream] -= fixed_flow;
                source_S[downstream] += fixed_flow;
                continue;
            }

            const Cell& up_cell =
                mesh.at(fan.upstream[0], fan.upstream[1], fan.upstream[2]);
            const Cell& down_cell =
                mesh.at(fan.downstream[0], fan.downstream[1], fan.downstream[2]);
            const double rho_local =
                std::max(0.5 * (up_cell.get_rho() + down_cell.get_rho()), 1e-9);
            const double rho_ratio = rho_local / fan.rho_rated;
            const double upper = fan_upper_flow_bound(
                fan.curve_a, fan.curve_b, fan.curve_c);
            const double Q_ref = std::clamp(
                fan.q_ref, minimum_flow, upper);
            const double dP_ref=bounded_fan_curve_pressure(
                fan.curve_a,fan.curve_b,fan.curve_c,Q_ref)*rho_ratio;
            const double slope =
                -(fan.curve_b + 2.0 * fan.curve_c * Q_ref) * rho_ratio;
            const double safe_slope = std::min(slope, -1e-9);
            const double C_fan = -1.0 / safe_slope;
            const double Q0 = Q_ref + dP_ref * C_fan;

            source_S[upstream] -= Q0;
            source_S[downstream] += Q0;
            internal_curve_neighbors[upstream].push_back(
                {fan.downstream[0], fan.downstream[1], fan.downstream[2], C_fan});
            internal_curve_neighbors[downstream].push_back(
                {fan.upstream[0], fan.upstream[1], fan.upstream[2], C_fan});
        }

        configure_pressure_references_and_validate_grounding();
    }

    // Pressure is identifiable independently in each connected fluid
    // component. Ordinary faces and curved internal fans are conductance
    // edges; fixed internal fans are paired sources and therefore do not join
    // pressure components. Ambient vents and curved boundary fans are ground
    // shunts. Passive sealed pockets receive one deterministic gauge, while
    // any source-bearing ungrounded component is rejected before SOR/PCG.
    void configure_pressure_references_and_validate_grounding() {
        const size_t n = mesh.get_cells().size();
        pressure_references.assign(n, 0);
        std::vector<size_t> parent(n);
        std::vector<unsigned char> rank(n, 0);
        for(size_t i = 0; i < n; ++i) parent[i] = i;

        auto find_root = [&](size_t node) {
            size_t root = node;
            while(parent[root] != root) root = parent[root];
            while(parent[node] != node) {
                const size_t next = parent[node];
                parent[node] = root;
                node = next;
            }
            return root;
        };
        auto unite = [&](size_t a, size_t b) {
            size_t root_a = find_root(a);
            size_t root_b = find_root(b);
            if(root_a == root_b) return;
            if(rank[root_a] < rank[root_b]) std::swap(root_a, root_b);
            parent[root_b] = root_a;
            if(rank[root_a] == rank[root_b]) ++rank[root_a];
        };

        for(int x = 0; x < mesh.get_nx(); ++x)
            for(int y = 0; y < mesh.get_ny(); ++y)
                for(int z = 0; z < mesh.get_nz(); ++z) {
                    const size_t i = cell_idx(x, y, z);
                    if(!mesh.at(x, y, z).is_fluid()) continue;
                    for(const FaceLink& link : neighbors[i]) {
                        if(!std::isfinite(link.conductance) ||
                           link.conductance <= 0.0)
                            throw std::runtime_error(
                                "FlowSolver: fluid-component face conductance "
                                "must be finite and positive");
                        unite(i, cell_idx(link.nx, link.ny, link.nz));
                    }
                    for(const InternalCurveLink& link :
                        internal_curve_neighbors[i]) {
                        if(!std::isfinite(link.conductance) ||
                           link.conductance <= 0.0)
                            throw std::runtime_error(
                                "FlowSolver: internal-fan conductance must be "
                                "finite and positive");
                        unite(i, cell_idx(link.nx, link.ny, link.nz));
                    }
                }

        std::vector<unsigned char> grounded(n, 0);
        std::vector<double> absolute_source(n, 0.0);
        std::vector<size_t> minimum_index(
            n, std::numeric_limits<size_t>::max());
        std::vector<size_t> fluid_count(n, 0);
        for(int x = 0; x < mesh.get_nx(); ++x)
            for(int y = 0; y < mesh.get_ny(); ++y)
                for(int z = 0; z < mesh.get_nz(); ++z) {
                    const size_t i = cell_idx(x, y, z);
                    if(!mesh.at(x, y, z).is_fluid()) continue;
                    if(!std::isfinite(vent_C[i]) || vent_C[i] < 0.0 ||
                       !std::isfinite(fan_ground_C[i]) ||
                       fan_ground_C[i] < 0.0 ||
                       !std::isfinite(source_S[i]) ||
                       !std::isfinite(ambient_source_S[i]))
                        throw std::runtime_error(
                            "FlowSolver: non-finite or negative pressure-"
                            "network coefficient");
                    const size_t root = find_root(i);
                    grounded[root] = grounded[root] ||
                        vent_C[i] > 0.0 || fan_ground_C[i] > 0.0;
                    absolute_source[root] += std::abs(source_S[i]);
                    minimum_index[root] = std::min(minimum_index[root], i);
                    ++fluid_count[root];
                }

        for(const auto& fan : mesh.get_internal_fans()) {
            if(fan.has_curve()) continue;
            const size_t upstream = cell_idx(
                fan.upstream[0], fan.upstream[1], fan.upstream[2]);
            const size_t downstream = cell_idx(
                fan.downstream[0], fan.downstream[1], fan.downstream[2]);
            const size_t upstream_root = find_root(upstream);
            const size_t downstream_root = find_root(downstream);
            if(upstream_root != downstream_root &&
               (!grounded[upstream_root] || !grounded[downstream_root]))
                throw std::runtime_error(
                    "FlowSolver: ungrounded fluid component at a fixed "
                    "internal fan; both separated pressure components must "
                    "have an ambient pressure ground");
        }

        for(size_t root = 0; root < n; ++root) {
            if(fluid_count[root] == 0 || grounded[root]) continue;
            if(absolute_source[root] > 0.0)
                throw std::runtime_error(
                    "FlowSolver: ungrounded source-bearing fluid component "
                    "starting at cell index " +
                    std::to_string(minimum_index[root]) +
                    "; add an ambient vent/curved boundary-fan ground or "
                    "remove the source");
            pressure_references[minimum_index[root]] = 1;
        }
    }

    // Same structure as build_linearized_network() - identical fan/vent
    // logic (that part never touched geometry) - just routes each face
    // through add_link_adaptive() instead of add_link() so area/length/Dh
    // come from the two cells actually touching that face.
    void build_linearized_network_adaptive() {
        for (auto& links : neighbors) links.clear();
        std::fill(vent_C.begin(), vent_C.end(), 0.0);
        std::fill(source_S.begin(), source_S.end(), 0.0);
        std::fill(ambient_source_S.begin(), ambient_source_S.end(), 0.0);
        for(auto& links : internal_curve_neighbors) links.clear();

        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    const Cell& c = mesh.at(x, y, z);
                    if (!c.is_fluid()) continue;
                    const size_t i = cell_idx(x, y, z);
                    source_S[i] = c.get_flow_source();

                    fan_ground_C[i] = 0.0;

                    if (c.has_fan_curve())
                        assemble_boundary_curve_fan(i, c);
                    ambient_source_S[i] = source_S[i];
                    add_link_adaptive(i, x, y, z, x + 1, y, z, Axis::X, xface_idx(x + 1, y, z), +1.0);
                    add_link_adaptive(i, x, y, z, x - 1, y, z, Axis::X, xface_idx(x, y, z), -1.0);
                    add_link_adaptive(i, x, y, z, x, y + 1, z, Axis::Y, yface_idx(x, y + 1, z), +1.0);
                    add_link_adaptive(i, x, y, z, x, y - 1, z, Axis::Y, yface_idx(x, y, z), -1.0);
                    add_link_adaptive(i, x, y, z, x, y, z + 1, Axis::Z, zface_idx(x, y, z + 1), +1.0);
                    add_link_adaptive(i, x, y, z, x, y, z - 1, Axis::Z, zface_idx(x, y, z), -1.0);

                    if (c.get_state() == Cell::State::Vent) {
                        const double CdA = c.get_vent_conductance();
                        const double rho = std::max(c.get_rho(), 1e-9);
                        const double p_ref = std::max(std::abs(c.get_pressure()), minimum_pressure);
                        vent_C[i] = CdA * std::sqrt(2.0 / (rho * p_ref));
                    }
                }
            }
        }
        apply_internal_fans_and_pressure_reference();
    }

    void add_link(size_t i,
                  int x, int y, int z,
                  int nx, int ny, int nz,
                  Axis axis,
                  size_t face_index,
                  double area,
                  double length,
                  double global_face_sign) {
        if (!mesh.in_bounds(nx, ny, nz) || !mesh.at(nx, ny, nz).is_fluid() ||
            mesh.wall_between(x, y, z, nx, ny, nz) != nullptr) return;
        if(is_internal_fan_face(axis, face_index)) return;

        const double Dh = hydraulic_diameter(axis, mesh);

        // Identify the low/high-coordinate cells so both directions use the
        // exact same physical face conductance.
        const Cell* low_cell = &mesh.at(x, y, z);
        const Cell* high_cell = &mesh.at(nx, ny, nz);
        if (global_face_sign < 0.0) std::swap(low_cell, high_cell);

        const double C = linearized_face_conductance(
            *low_cell, *high_cell, axis, face_index, area, length, Dh);

        neighbors[i].push_back(
            {nx, ny, nz, axis, face_index, area, length, Dh, C, global_face_sign});
    }

    // Same structure as add_link(), but area/length/Dh are computed here
    // (after the bounds+fluid check, once both cells are in hand) instead
    // of being passed in as precomputed mesh-wide scalars - length in
    // particular now needs the neighbor's own width too.
    void add_link_adaptive(size_t i,
                            int x, int y, int z,
                            int nx, int ny, int nz,
                            Axis axis,
                            size_t face_index,
                            double global_face_sign) {
        if (!mesh.in_bounds(nx, ny, nz) || !mesh.at(nx, ny, nz).is_fluid() ||
            mesh.wall_between(x, y, z, nx, ny, nz) != nullptr) return;
        if(is_internal_fan_face(axis, face_index)) return;

        const Cell& c = mesh.at(x, y, z);
        const Cell& n = mesh.at(nx, ny, nz);

        // Area is shared across this face on a tensor-product grid (the
        // two un-stepped axes are identical for both cells); length is the
        // average of the two cells' widths along the stepped axis, since
        // that's the one dimension that can actually differ.
        double area, length;
        switch (axis) {
            case Axis::X: area = c.area_x(); length = (c.get_dx() + n.get_dx()) / 2.0; break;
            case Axis::Y: area = c.area_y(); length = (c.get_dy() + n.get_dy()) / 2.0; break;
            default:      area = c.area_z(); length = (c.get_dz() + n.get_dz()) / 2.0; break;
        }
        const double Dh = hydraulic_diameter_adaptive(axis, c);

        const Cell* low_cell = &c;
        const Cell* high_cell = &n;
        if (global_face_sign < 0.0) std::swap(low_cell, high_cell);

        const double C = linearized_face_conductance(
            *low_cell, *high_cell, axis, face_index, area, length, Dh);

        neighbors[i].push_back(
            {nx, ny, nz, axis, face_index, area, length, Dh, C, global_face_sign});
    }

    void solve_pressures() {
        last_pressure_converged = false;
        if(pressure_method == "pcg") solve_pressures_pcg();
        else                         solve_pressures_sor();
    }

    void solve_pressures_sor() {
        for (int iter = 0; iter < max_pressure_iters; ++iter) {
            for (int x = 0; x < mesh.get_nx(); ++x) {
                for (int y = 0; y < mesh.get_ny(); ++y) {
                    for (int z = 0; z < mesh.get_nz(); ++z) {
                        Cell& c = mesh.at(x, y, z);
                        if (!c.is_fluid()) continue;

                        const size_t i = cell_idx(x, y, z);
                        if(is_pressure_reference(i)) {
                            c.set_pressure(0.0);
                            continue;
                        }
                        double diagonal = vent_C[i] + fan_ground_C[i];
                        double rhs = source_S[i];
                        for (const FaceLink& link : neighbors[i]) {
                            diagonal += link.conductance;
                            rhs += link.conductance *
                                   mesh.at(link.nx, link.ny, link.nz).get_pressure();
                        }
                        for(const InternalCurveLink& link :
                            internal_curve_neighbors[i]) {
                            diagonal += link.conductance;
                            rhs += link.conductance *
                                   mesh.at(link.nx, link.ny, link.nz).get_pressure();
                        }
                        if (diagonal <= 0.0) continue;

                        const double p_gs = rhs / diagonal;
                        const double p_old = c.get_pressure();
                        const double p_sor = p_old + omega * (p_gs - p_old);
                        c.set_pressure(p_old + pressure_relaxation * (p_sor - p_old));
                    }
                }
            }

            const double residual = max_mass_residual();
            if (residual < pressure_tolerance) {
                last_pressure_converged = true;
                return;
            }
        }

        std::cerr << "FlowSolver: WARNING -- pressure solve reached "
                  << max_pressure_iters << " iterations; residual = "
                  << max_mass_residual() << " m^3/s.\n";
    }

    void solve_pressures_pcg() {
        const size_t n = mesh.get_cell_count();
        std::vector<double> x(n, 0.0), rhs(n, 0.0), residual(n, 0.0);
        std::vector<double> z(n, 0.0), direction(n, 0.0);
        std::vector<double> product(n, 0.0), diagonal(n, 0.0);
        std::vector<unsigned char> active(n, 0);

        for(int ix = 0; ix < mesh.get_nx(); ++ix) {
            for(int iy = 0; iy < mesh.get_ny(); ++iy) {
                for(int iz = 0; iz < mesh.get_nz(); ++iz) {
                    const size_t i = cell_idx(ix, iy, iz);
                    const Cell& cell = mesh.at(ix, iy, iz);
                    if(!cell.is_fluid() || is_pressure_reference(i)) continue;

                    double d = vent_C[i] + fan_ground_C[i];
                    for(const FaceLink& link : neighbors[i])
                        d += link.conductance;
                    for(const InternalCurveLink& link :
                        internal_curve_neighbors[i])
                        d += link.conductance;
                    if(d <= 0.0) continue;

                    active[i] = 1;
                    diagonal[i] = d;
                    rhs[i] = source_S[i];
                    x[i] = cell.get_pressure();
                }
            }
        }
        for(size_t i = 0; i < n; ++i)
            if(is_pressure_reference(i)) x[i] = 0.0;

        auto apply_matrix = [&](const std::vector<double>& input,
                                std::vector<double>& output) {
            std::fill(output.begin(), output.end(), 0.0);
            for(size_t i = 0; i < n; ++i) {
                if(!active[i]) continue;
                double value = diagonal[i] * input[i];
                for(const FaceLink& link : neighbors[i])
                    value -= link.conductance *
                        input[cell_idx(link.nx, link.ny, link.nz)];
                for(const InternalCurveLink& link :
                    internal_curve_neighbors[i])
                    value -= link.conductance *
                        input[cell_idx(link.nx, link.ny, link.nz)];
                output[i] = value;
            }
        };
        auto dot_active = [&](const std::vector<double>& a,
                              const std::vector<double>& b) {
            double sum = 0.0;
            for(size_t i = 0; i < n; ++i) {
                if(!active[i]) continue;
                if(!std::isfinite(a[i]) || !std::isfinite(b[i]))
                    throw std::runtime_error(
                        "FlowSolver: non-finite PCG vector entry");
                sum += a[i] * b[i];
                if(!std::isfinite(sum))
                    throw std::runtime_error(
                        "FlowSolver: non-finite PCG dot product");
            }
            return sum;
        };
        auto max_abs_active = [&](const std::vector<double>& values) {
            double maximum = 0.0;
            for(size_t i = 0; i < n; ++i) {
                if(!active[i]) continue;
                if(!std::isfinite(values[i]))
                    throw std::runtime_error(
                        "FlowSolver: non-finite PCG residual entry");
                maximum = std::max(maximum, std::abs(values[i]));
            }
            return maximum;
        };
        auto write_pressures = [&]() {
            for(int ix = 0; ix < mesh.get_nx(); ++ix)
                for(int iy = 0; iy < mesh.get_ny(); ++iy)
                    for(int iz = 0; iz < mesh.get_nz(); ++iz) {
                        const size_t i = cell_idx(ix, iy, iz);
                        if(mesh.at(ix, iy, iz).is_fluid()) {
                            if(!std::isfinite(x[i]))
                                throw std::runtime_error(
                                    "FlowSolver: non-finite solved pressure");
                            mesh.at(ix, iy, iz).set_pressure(
                                is_pressure_reference(i) ? 0.0 : x[i]);
                        }
                    }
        };

        apply_matrix(x, product);
        for(size_t i = 0; i < n; ++i) {
            if(!active[i]) continue;
            residual[i] = rhs[i] - product[i];
            z[i] = residual[i] / diagonal[i];
            direction[i] = z[i];
        }

        double residual_max = max_abs_active(residual);
        if(residual_max < pressure_tolerance) {
            write_pressures();
            last_pressure_converged = true;
            return;
        }

        double rz = dot_active(residual, z);
        if(!(rz > 0.0) || !std::isfinite(rz))
            throw std::runtime_error(
                "FlowSolver: PCG pressure matrix is singular or invalid. "
                "Check that every fluid region is connected to a vent, fan "
                "boundary, or the pressure reference.");

        for(int iter = 0; iter < max_pressure_iters; ++iter) {
            apply_matrix(direction, product);
            const double denominator = dot_active(direction, product);
            if(!(denominator > 0.0) || !std::isfinite(denominator))
                throw std::runtime_error(
                    "FlowSolver: PCG lost positive definiteness. Check "
                    "fluid connectivity and face conductances.");

            const double alpha = rz / denominator;
            for(size_t i = 0; i < n; ++i) {
                if(!active[i]) continue;
                x[i] += alpha * direction[i];
                residual[i] -= alpha * product[i];
            }

            residual_max = max_abs_active(residual);
            if(residual_max < pressure_tolerance) {
                write_pressures();
                last_pressure_converged = true;
                std::cout << "FlowSolver: PCG pressure converged after "
                          << iter + 1 << " iterations; residual = "
                          << residual_max << " m^3/s.\n";
                return;
            }

            for(size_t i = 0; i < n; ++i)
                if(active[i]) z[i] = residual[i] / diagonal[i];
            const double rz_new = dot_active(residual, z);
            if(!std::isfinite(rz_new))
                throw std::runtime_error(
                    "FlowSolver: PCG pressure residual became non-finite.");
            const double beta = rz_new / rz;
            for(size_t i = 0; i < n; ++i)
                if(active[i])
                    direction[i] = z[i] + beta * direction[i];
            rz = rz_new;
        }

        write_pressures();
        std::cerr << "FlowSolver: WARNING -- PCG pressure solve reached "
                  << max_pressure_iters << " iterations; residual = "
                  << residual_max << " m^3/s.\n";
    }

    double max_mass_residual() const {
        double max_r = 0.0;
        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    const Cell& c = mesh.at(x, y, z);
                    if (!c.is_fluid()) continue;
                    const size_t i = cell_idx(x, y, z);
                    if(is_pressure_reference(i)) continue;
                    double r = source_S[i] - vent_C[i] * c.get_pressure() - fan_ground_C[i] * c.get_pressure();
                    for (const FaceLink& link : neighbors[i]) {
                        r -= link.conductance *
                             (c.get_pressure() -
                              mesh.at(link.nx, link.ny, link.nz).get_pressure());
                    }
                    for(const InternalCurveLink& link :
                        internal_curve_neighbors[i]) {
                        r -= link.conductance *
                             (c.get_pressure() -
                              mesh.at(link.nx, link.ny, link.nz).get_pressure());
                    }
                    if(!std::isfinite(r))
                        throw std::runtime_error(
                            "FlowSolver: non-finite mass residual");
                    max_r = std::max(max_r, std::abs(r));
                }
            }
        }
        return max_r;
    }

    double update_face_flows(bool publish_exact) {
        last_absolute_face_flow_change = 0.0;
        last_maximum_face_flow = 0.0;
        double maximum_mixed_relative_change = 0.0;
        const double absolute_update_tolerance =
            std::max(pressure_tolerance, minimum_flow);

        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    if (!mesh.at(x, y, z).is_fluid()) continue;
                    const size_t i = cell_idx(x, y, z);
                    const double Pi = mesh.at(x, y, z).get_pressure();
                    if(!std::isfinite(Pi))
                        throw std::runtime_error(
                            "FlowSolver: non-finite cell pressure before face-flow update");

                    for (const FaceLink& link : neighbors[i]) {
                        // Update each global face only from its low-coordinate cell.
                        if (link.direction_sign < 0.0) continue;
                        const double Pj = mesh.at(link.nx, link.ny, link.nz).get_pressure();
                        if(!std::isfinite(Pj) ||
                           !std::isfinite(link.conductance))
                            throw std::runtime_error(
                                "FlowSolver: non-finite neighbor pressure or conductance");
                        const double q_raw = link.conductance * (Pi - Pj);
                        double& q = face_flow_reference(link.axis, link.face_index);
                        const double q_old = q;
                        if(!std::isfinite(q_raw) || !std::isfinite(q_old))
                            throw std::runtime_error(
                                "FlowSolver: non-finite raw or prior face flow");
                        const double q_new = publish_exact
                            ? q_raw
                            : q_old + flow_relaxation * (q_raw - q_old);
                        if(!std::isfinite(q_new))
                            throw std::runtime_error(
                                "FlowSolver: non-finite relaxed face flow");
                        q = q_new;
                        const double absolute_update = std::abs(q - q_old);
                        const double local_absolute_value =
                            std::max(std::abs(q), std::abs(q_old));
                        const double mixed_change = mixed_relative_update_norm(
                            absolute_update, local_absolute_value,
                            flow_tolerance, absolute_update_tolerance);
                        if(!std::isfinite(mixed_change))
                            throw std::runtime_error(
                                "FlowSolver: non-finite face convergence metric");
                        last_absolute_face_flow_change = std::max(
                            last_absolute_face_flow_change, absolute_update);
                        last_maximum_face_flow = std::max(
                            last_maximum_face_flow,
                            local_absolute_value);
                        maximum_mixed_relative_change = std::max(
                            maximum_mixed_relative_change, mixed_change);
                    }
                }
            }
        }
        // Apply a per-face mixed relative/absolute test. Material changes in
        // a low-flow branch cannot hide behind the network's largest flow,
        // while jitter at or below the pressure solver's absolute continuity
        // resolution cannot hold the nonlinear solve open indefinitely.
        return maximum_mixed_relative_change;
    }

    double& face_flow_reference(Axis axis, size_t face_index) {
        if (axis == Axis::X) return qx[face_index];
        if (axis == Axis::Y) return qy[face_index];
        return qz[face_index];
    }

    double global_face_flow(Axis axis, size_t face_index) const {
        return old_face_flow(axis, face_index);
    }

    void update_cell_velocities() {
        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    Cell& c = mesh.at(x, y, z);
                    if (!c.is_fluid()) {
                        c.set_vx(0.0); c.set_vy(0.0); c.set_vz(0.0);
                        continue;
                    }

                    const double ux_minus = qx[xface_idx(x, y, z)] / mesh.area_x();
                    const double ux_plus  = qx[xface_idx(x + 1, y, z)] / mesh.area_x();
                    const double uy_minus = qy[yface_idx(x, y, z)] / mesh.area_y();
                    const double uy_plus  = qy[yface_idx(x, y + 1, z)] / mesh.area_y();
                    const double uz_minus = qz[zface_idx(x, y, z)] / mesh.area_z();
                    const double uz_plus  = qz[zface_idx(x, y, z + 1)] / mesh.area_z();

                    c.set_vx(0.5 * (ux_minus + ux_plus));
                    c.set_vy(0.5 * (uy_minus + uy_plus));
                    c.set_vz(0.5 * (uz_minus + uz_plus));
                }
            }
        }
    }

    // Same structure as update_cell_velocities(), reading each cell's own
    // face areas instead of one mesh-wide value. On a tensor-product grid
    // the area at a given face is shared by whichever cell you read it
    // from, so this is a pure per-cell substitution.
    void update_cell_velocities_adaptive() {
        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    Cell& c = mesh.at(x, y, z);
                    if (!c.is_fluid()) {
                        c.set_vx(0.0); c.set_vy(0.0); c.set_vz(0.0);
                        continue;
                    }

                    const double ux_minus = qx[xface_idx(x, y, z)] / c.area_x();
                    const double ux_plus  = qx[xface_idx(x + 1, y, z)] / c.area_x();
                    const double uy_minus = qy[yface_idx(x, y, z)] / c.area_y();
                    const double uy_plus  = qy[yface_idx(x, y + 1, z)] / c.area_y();
                    const double uz_minus = qz[zface_idx(x, y, z)] / c.area_z();
                    const double uz_plus  = qz[zface_idx(x, y, z + 1)] / c.area_z();

                    c.set_vx(0.5 * (ux_minus + ux_plus));
                    c.set_vy(0.5 * (uy_minus + uy_plus));
                    c.set_vz(0.5 * (uz_minus + uz_plus));
                }
            }
        }
    }

    void apply_boundary_fan_velocities() {
        for(int x = 0; x < mesh.get_nx(); ++x)
            for(int y = 0; y < mesh.get_ny(); ++y)
                for(int z = 0; z < mesh.get_nz(); ++z) {
                    Cell& cell = mesh.at(x, y, z);
                    if(!cell.has_fan_curve() ||
                       cell.get_fan_area() <= 0.0) continue;
                    const double flow = cell.get_fan_Q_ref();
                    if(!std::isfinite(flow))
                        throw std::runtime_error(
                            "FlowSolver: non-finite boundary-fan velocity flow");
                    const auto direction = cell.get_fan_dir();
                    const double velocity = flow / cell.get_fan_area();
                    cell.set_vx(velocity * direction[0]);
                    cell.set_vy(velocity * direction[1]);
                    cell.set_vz(velocity * direction[2]);
                }
    }

    double update_fan_operating_points(bool publish_exact) {
        fan_active_set_changed_last_update = false;
        double max_relative_change = 0.0;
        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    Cell& c = mesh.at(x, y, z);
                    if (!c.has_fan_curve()) continue;

                    const double stored_Q_ref = c.get_fan_Q_ref();
                    if(!std::isfinite(stored_Q_ref) ||
                       !std::isfinite(c.get_rho()))
                        throw std::runtime_error(
                            "FlowSolver: non-finite boundary-fan state");
                    const double rho_local = std::max(c.get_rho(), 1e-9);
                    const double rho_ratio = rho_local / c.get_fan_rho_rated();
                    const double P_i = c.get_pressure();
                    const double sign = c.is_intake() ? +1.0 : -1.0;
                    const FanOperatingPoint operating_point =
                        project_fan_operating_point(
                            stored_Q_ref,
                            c.get_fan_curve_a(),
                            c.get_fan_curve_b(),
                            c.get_fan_curve_c(),
                            rho_ratio,
                            sign * P_i);
                    const size_t index = cell_idx(x, y, z);
                    if(boundary_fan_active_sets[index] !=
                       operating_point.active_set) {
                        boundary_fan_active_sets[index] =
                            operating_point.active_set;
                        fan_active_set_changed_last_update = true;
                    }
                    const double Q_new = operating_point.target_flow;
                    const double scale =
                        std::max({std::abs(Q_new),
                                  std::abs(stored_Q_ref), minimum_flow});
                    const double relative_change =
                        std::abs(Q_new - stored_Q_ref) / scale;
                    if(!std::isfinite(relative_change))
                        throw std::runtime_error(
                            "FlowSolver: non-finite boundary-fan convergence metric");
                    max_relative_change = std::max(
                        max_relative_change, relative_change);
                    c.set_fan_Q_ref(Q_new);
                }
            }
        }

        auto& fans = mesh.get_internal_fans();
        for(size_t fan_index = 0; fan_index < fans.size(); ++fan_index) {
            auto& fan = fans[fan_index];
            if(!fan.has_curve()) continue;
            const Cell& upstream =
                mesh.at(fan.upstream[0], fan.upstream[1], fan.upstream[2]);
            const Cell& downstream =
                mesh.at(fan.downstream[0], fan.downstream[1], fan.downstream[2]);
            if(!std::isfinite(fan.q_ref) ||
               !std::isfinite(upstream.get_rho()) ||
               !std::isfinite(downstream.get_rho()))
                throw std::runtime_error(
                    "FlowSolver: non-finite internal-fan state");
            const double rho_local =
                std::max(0.5 * (upstream.get_rho() + downstream.get_rho()), 1e-9);
            const double rho_ratio = rho_local / fan.rho_rated;
            const double stored_Q_ref = fan.q_ref;
            const FanOperatingPoint operating_point =
                project_fan_operating_point(
                    stored_Q_ref,
                    fan.curve_a,
                    fan.curve_b,
                    fan.curve_c,
                    rho_ratio,
                    downstream.get_pressure() - upstream.get_pressure());
            if(internal_fan_active_sets[fan_index] !=
               operating_point.active_set) {
                internal_fan_active_sets[fan_index] =
                    operating_point.active_set;
                fan_active_set_changed_last_update = true;
            }
            const double Q_new = operating_point.target_flow;
            const double scale =
                std::max({std::abs(Q_new),
                          std::abs(stored_Q_ref), minimum_flow});
            const double relative_change =
                std::abs(Q_new - stored_Q_ref) / scale;
            if(!std::isfinite(relative_change))
                throw std::runtime_error(
                    "FlowSolver: non-finite internal-fan convergence metric");
            max_relative_change = std::max(
                max_relative_change, relative_change);
            // A projected bound is an exact branch equation, so publish the
            // bound immediately. Interior Newton updates retain the existing
            // relaxation for nonlinear stability.
            fan.q_ref = publish_exact ||
                    operating_point.active_set != FanActiveSet::Interior
                ? Q_new
                : stored_Q_ref +
                    flow_relaxation * (Q_new - stored_Q_ref);
        }
        if(fan_active_set_changed_last_update)
            max_relative_change = std::max(max_relative_change, 1.0);
        return max_relative_change;
    }

    void report_mass_balance() {
        double total_norton_source = 0.0;
        double total_effective_source = 0.0;
        double total_vent = 0.0;
        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    const Cell& c = mesh.at(x, y, z);
                    if (!c.is_fluid()) continue;
                    const size_t i = cell_idx(x, y, z);
                    total_norton_source += source_S[i];
                    // Curved ambient fans are represented as a Norton source
                    // in parallel with fan_ground_C. Their actual boundary
                    // flow is source - C*pressure, not source alone.
                    total_effective_source +=
                        source_S[i] - fan_ground_C[i] * c.get_pressure();
                    total_vent += vent_C[i] * c.get_pressure();
                }
            }
        }
        last_total_source = total_effective_source;
        last_total_vent = total_vent;
        if(!std::isfinite(total_norton_source) ||
           !std::isfinite(total_effective_source) ||
           !std::isfinite(total_vent))
            throw std::runtime_error(
                "FlowSolver: non-finite mass-balance total");
        std::cout << "FlowSolver: effective fan/source flow = "
                  << total_effective_source
                  << " m^3/s, vent flow = " << total_vent
                  << " m^3/s, imbalance = "
                  << total_effective_source - total_vent
                  << " m^3/s (raw Norton source = "
                  << total_norton_source << " m^3/s)\n";
    }
};

#endif
