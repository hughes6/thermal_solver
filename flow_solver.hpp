#ifndef FLOW_SOLVER_HPP
#define FLOW_SOLVER_HPP

#include <vector>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <algorithm>

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
    
    FlowSolver(Mesh& mesh_, 
               double linear_resistivity_ = 5.0,
               double tolerance_ = 1e-6,
               int max_iters_ = 20000,
               double sor_omega_ = 1.3,
               int max_outer_iters_ = 30,
               double flow_tolerance_ = 1e-4) :
               mesh(mesh_),
               linear_resistivity(linear_resistivity_),
               pressure_tolerance(tolerance_),
               max_pressure_iters(max_iters_),
               omega(sor_omega_),
               max_outer_iters(max_outer_iters_),
               flow_tolerance(flow_tolerance_) 
               {
                    if(linear_resistivity < 0.0) {
                        throw std::invalid_argument("FlowSolver: resistivity must be >= 0");
                    }
                    if(omega <= 0.0 || omega >= 2.0) {
                        throw std::invalid_argument("FlowSolver: SOR omega must be in (0,2)");
                    }
               }
    
    // runs the full pre-pass
    // derive face flows and write vx,vy,vz into every cell
    // of mesh, call once before solve           
    void solve() {
        validate_grounding();
        initialize_storage();
        initialize_pressures();

        bool outer_converged = false;
        for (int outer = 0; outer < max_outer_iters; ++outer) {
            build_linearized_network();
            solve_pressures();

            const double max_relative_change = update_face_flows();
            update_cell_velocities();

            if (max_relative_change < flow_tolerance && outer > 0) {
                std::cout << "FlowSolver: nonlinear flow converged after "
                          << outer + 1 << " outer iterations (max relative face-flow change = "
                          << max_relative_change << ")\n";
                outer_converged = true;
                break;
            }
        }

        if (!outer_converged) {
            std::cerr << "FlowSolver: WARNING -- nonlinear face flow did not "
                         "converge within " << max_outer_iters
                      << " outer iterations.\n";
        }

        report_mass_balance();
    }           

    void set_resistivity(double r) { linear_resistivity = r; }
    double get_resistivity() const { return linear_resistivity; }
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

    Mesh& mesh;
    double linear_resistivity;
    double pressure_tolerance;   // absolute continuity residual, m^3/s
    int max_pressure_iters;
    double omega;
    int max_outer_iters;
    double flow_tolerance;

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
    std::vector<double> source_S;

    // One value per global positive-oriented mesh face.
    std::vector<double> qx;
    std::vector<double> qy;
    std::vector<double> qz;

    size_t cell_idx(int x, int y, int z) const { return mesh.idx(x, y, z); }

    size_t xface_idx(int x_face, int y, int z) const {
        return (static_cast<size_t>(x_face) * mesh.get_ny() + y) * mesh.get_nz() + z;
    }
    size_t yface_idx(int x, int y_face, int z) const {
        return (static_cast<size_t>(x) * (mesh.get_ny() + 1) + y_face) * mesh.get_nz() + z;
    }
    size_t zface_idx(int x, int y, int z_face) const {
        return (static_cast<size_t>(x) * mesh.get_ny() + y) * (mesh.get_nz() + 1) + z_face;
    }

    void initialize_storage() {
        const size_t n = static_cast<size_t>(mesh.get_nx()) * mesh.get_ny() * mesh.get_nz();
        neighbors.assign(n, {});
        vent_C.assign(n, 0.0);
        source_S.assign(n, 0.0);

        qx.assign(static_cast<size_t>(mesh.get_nx() + 1) * mesh.get_ny() * mesh.get_nz(), 0.0);
        qy.assign(static_cast<size_t>(mesh.get_nx()) * (mesh.get_ny() + 1) * mesh.get_nz(), 0.0);
        qz.assign(static_cast<size_t>(mesh.get_nx()) * mesh.get_ny() * (mesh.get_nz() + 1), 0.0);
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

    void validate_grounding() const {
        bool has_source = false;
        bool has_vent = false;
        for (const Cell& c : mesh.get_cells()) {
            has_source = has_source || std::abs(c.get_flow_source()) > 0.0;
            has_vent = has_vent || c.get_state() == Cell::State::Vent;
        }
        if (has_source && !has_vent) {
            throw std::runtime_error(
                "FlowSolver: fan source exists but no vent pressure reference exists.");
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

    double old_face_flow(Axis axis, size_t face_index) const {
        if (axis == Axis::X) return qx[face_index];
        if (axis == Axis::Y) return qy[face_index];
        return qz[face_index];
    }

    double friction_factor(double Re) const {
        Re = std::max(Re, minimum_reynolds);
        if (Re < 2300.0) return 64.0 / Re;
        // Haaland for a smooth duct (relative roughness = 0).
        const double inv_sqrt_f = -1.8 * std::log10(6.9 / Re);
        return 1.0 / (inv_sqrt_f * inv_sqrt_f);
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

        // DeltaP = R_linear*Q + R_quad*Q|Q|.
        const double R_linear = linear_resistivity * length / area;
        const double R_quad = (K + f * length / Dh) * rho / (2.0 * area * area);
        const double R_effective = R_linear + R_quad * q_ref;
        return 1.0 / std::max(R_effective, 1e-30);
    }

    void build_linearized_network() {
        for (auto& links : neighbors) links.clear();
        std::fill(vent_C.begin(), vent_C.end(), 0.0);
        std::fill(source_S.begin(), source_S.end(), 0.0);

        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    const Cell& c = mesh.at(x, y, z);
                    if (!c.is_fluid()) continue;
                    const size_t i = cell_idx(x, y, z);
                    source_S[i] = c.get_flow_source();

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
    }

    void add_link(size_t i,
                  int x, int y, int z,
                  int nx, int ny, int nz,
                  Axis axis,
                  size_t face_index,
                  double area,
                  double length,
                  double global_face_sign) {
        if (!mesh.in_bounds(nx, ny, nz) || !mesh.at(nx, ny, nz).is_fluid()) return;

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

    void solve_pressures() {
        for (int iter = 0; iter < max_pressure_iters; ++iter) {
            for (int x = 0; x < mesh.get_nx(); ++x) {
                for (int y = 0; y < mesh.get_ny(); ++y) {
                    for (int z = 0; z < mesh.get_nz(); ++z) {
                        Cell& c = mesh.at(x, y, z);
                        if (!c.is_fluid()) continue;

                        const size_t i = cell_idx(x, y, z);
                        double diagonal = vent_C[i];
                        double rhs = source_S[i];
                        for (const FaceLink& link : neighbors[i]) {
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
            if (residual < pressure_tolerance) return;
        }

        std::cerr << "FlowSolver: WARNING -- pressure solve reached "
                  << max_pressure_iters << " iterations; residual = "
                  << max_mass_residual() << " m^3/s.\n";
    }

    double max_mass_residual() const {
        double max_r = 0.0;
        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    const Cell& c = mesh.at(x, y, z);
                    if (!c.is_fluid()) continue;
                    const size_t i = cell_idx(x, y, z);
                    double r = source_S[i] - vent_C[i] * c.get_pressure();
                    for (const FaceLink& link : neighbors[i]) {
                        r -= link.conductance *
                             (c.get_pressure() -
                              mesh.at(link.nx, link.ny, link.nz).get_pressure());
                    }
                    max_r = std::max(max_r, std::abs(r));
                }
            }
        }
        return max_r;
    }

    double update_face_flows() {
        double max_relative_change = 0.0;

        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    if (!mesh.at(x, y, z).is_fluid()) continue;
                    const size_t i = cell_idx(x, y, z);
                    const double Pi = mesh.at(x, y, z).get_pressure();

                    for (const FaceLink& link : neighbors[i]) {
                        // Update each global face only from its low-coordinate cell.
                        if (link.direction_sign < 0.0) continue;
                        const double Pj = mesh.at(link.nx, link.ny, link.nz).get_pressure();
                        const double q_raw = link.conductance * (Pi - Pj);
                        double& q = face_flow_reference(link.axis, link.face_index);
                        const double q_old = q;
                        q = q_old + flow_relaxation * (q_raw - q_old);
                        const double scale = std::max({std::abs(q), std::abs(q_old), minimum_flow});
                        max_relative_change = std::max(
                            max_relative_change, std::abs(q - q_old) / scale);
                    }
                }
            }
        }
        return max_relative_change;
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

    void report_mass_balance() const {
        double total_source = 0.0;
        double total_vent = 0.0;
        for (int x = 0; x < mesh.get_nx(); ++x) {
            for (int y = 0; y < mesh.get_ny(); ++y) {
                for (int z = 0; z < mesh.get_nz(); ++z) {
                    const Cell& c = mesh.at(x, y, z);
                    if (!c.is_fluid()) continue;
                    const size_t i = cell_idx(x, y, z);
                    total_source += source_S[i];
                    total_vent += vent_C[i] * c.get_pressure();
                }
            }
        }
        std::cout << "FlowSolver: source = " << total_source
                  << " m^3/s, vent flow = " << total_vent
                  << " m^3/s, imbalance = " << total_source - total_vent
                  << " m^3/s\n";
    }
};

#endif