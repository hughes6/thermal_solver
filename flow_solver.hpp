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
               double resistivity_ = 5.0,
               double vent_discharge_coeff_ = 0.6,
               double tolerance_ = 1e-6,
               int max_iters_ = 20000,
               double sor_omega_ = 1.5) :
               mesh(mesh_),
               resistivity(resistivity_),
               vent_dischange_coeff(vent_discharge_coeff_),
               tolerance(tolerance_),
               max_iters(max_iters_),
               omega(sor_omega_) {}
    
    // runs the full pre-pass
    // derive face flows and write vx,vy,vz into every cell
    // of mesh, call once before solve           
    void solve() {
        validate_grounding();
        build_network();
        solve_pressures();
        compute_velocities();
    }           

    // -------------------------------------------------------------
    // Optional refinement hook 
    // -------------------------------------------------------------
    // after solve() converges once, you have a real velocity field
    // from that you can compute a local reynolds number per face
    // then you can derive friction factor based resistance instead 
    // of flat resistivity constant, then call solve again
    // 2-3 passes should be good to stabalize
    void set_resistivity(double r) { resistivity = r; }
    double get_resistivity() const { return resistivity; }

private:
    Mesh& mesh;
    double resistivity;
    double vent_dischange_coeff;
    double tolerance;
    int max_iters;
    double omega;

    void validate_grounding() {
        int n_fan_cells = 0;
        int n_vent_cells = 0;

        for(int x = 0; x < mesh.get_nx(); x++) {
            for(int y = 0; y < mesh.get_ny(); y++) {
                for(int z = 0; z < mesh.get_nz(); z++ ) {
                    const Cell& c = mesh.at(x, y, z);
                    if(c.is_intake() || c.is_exhaust()) n_fan_cells++;
                    if(c.get_state() == Cell::State::Vent) n_vent_cells++;
                }
            }
        }
        if(n_fan_cells > 0 && n_vent_cells == 0) {
            throw std::runtime_error(
                                    "FlowSolver: network has fan(s) but no vent(s) -- "
                                    "there is no pressure reference to ground the system against.");
        }
    }

    struct FaceLink {
        int nx, ny, nz; // neighbor cell coords
        double C;       // face conductance
    };

    std::vector<std::vector<FaceLink>> neighbors;
    std::vector<double> vent_C;
    std::vector<double> source_S;

    size_t idx(int x, int y, int z) const { return mesh.idx(x, y, z); }

    void build_network() {
        size_t n = static_cast<size_t>(mesh.get_nx()) *
                   static_cast<size_t>(mesh.get_ny()) *
                   static_cast<size_t>(mesh.get_nz());

        neighbors.assign(n, {});
        vent_C.assign(n, 0.0);
        source_S.assign(n, 0.0);

        for(int x = 0; x < mesh.get_nx(); x++) {
            for(int y = 0; y < mesh.get_ny(); y++) {
                for(int z = 0; z < mesh.get_nz(); z++) {
                    const Cell& c = mesh.at(x, y, z);
                    if(!c.is_fluid()) continue; // skip if solid

                    size_t i = idx(x, y, z);

                    add_face_link(i, x, y, z, x + 1, y, z, mesh.area_x(), mesh.get_dx());
                    add_face_link(i, x, y, z, x - 1, y, z, mesh.area_x(), mesh.get_dx());
                    add_face_link(i, x, y, z, x, y + 1, z, mesh.area_x(), mesh.get_dx());
                    add_face_link(i, x, y, z, x, y - 1, z, mesh.area_x(), mesh.get_dx());
                    add_face_link(i, x, y, z, x, y, z + 1, mesh.area_x(), mesh.get_dx());
                    add_face_link(i, x, y, z, x, y, z - 1, mesh.area_x(), mesh.get_dx());

                    if(c.get_state() == Cell::State::Vent) {
                        vent_C[i] == c.get_vent_conductance();
                    }

                    if (c.is_intake() || c.is_exhaust()) {
                        source_S[i] = c.get_flow_source();
                    }

                }
            }
        }
    }

    void add_face_link(size_t i, int x, int y, int z, int nx_, int ny_, int nz_, double area, double dist) {
        if(!mesh.in_bounds(nx_, ny_, nz_)) return;
        const Cell& n = mesh.at(nx_, ny_, nz_);
        if (!n.is_fluid()) return;

        double C = area / (resistivity * dist);
        neighbors[i].push_back({nx_, ny_, nz_, C});
    }

    void solve_pressures() {
        for(int iter = 0; iter < max_iters; iter ++ ) {
            double max_residual = 0.0;

            for(int x = 0; x < mesh.get_nx(); x++) {
                for(int y = 0; y < mesh.get_ny(); y++) {
                    for(int z = 0; z < mesh.get_nz(); z++) {
                        Cell& c = mesh.at(x, y, z);
                        if(c.is_fluid()) return;

                        size_t i = idx(x, y, z);
                        double sum_C = vent_C[i];
                        double sum_CP = 0.0;

                        for(const FaceLink& link : neighbors[i]) {
                            sum_C += link.C;
                            sum_CP += link.C * mesh.at(link.nx, link.ny, link.nz).get_pressure();
                        }

                        if(sum_C <= 0.0) {
                            continue;
                        }

                        double P_gs = (source_S[i] + sum_CP) / sum_C;
                        double P_old = c.get_pressure();
                        double P_new = P_old + omega * (P_gs - P_old);
                        c.set_pressure(P_new);

                        double residual = source_S[i] + sum_CP - P_new * sum_C;
                        max_residual = std::max(max_residual, std::abs(residual));
                    }
                }
            }
            if (max_residual < tolerance) {
                std::cout << "FlowSolver: pressure converged after " << iter       
                          << " iterations (max residual = " << max_residual << ")\n";
                return;
            }
        }
        std::cerr << "FlowSolver: Warning -- pressure did not converge within " << max_iters 
                  << " iterations. should resize iterations, loosen tolerance, or reduce sor_omega toward 1.0 \n";
    }

    void compute_velocities() {
        double Ax = mesh.area_x();
        double Ay = mesh.area_y();
        double Az = mesh.area_z();

        for(int x = 0; x < mesh.get_nx(); x++) {
            for(int y = 0; y < mesh.get_ny(); y++) {
                for(int z = 0; z < mesh.get_nz(); z++) {
                    Cell& c = mesh.at(x, y, z);
                    if(!c.is_fluid()) {
                        c.set_vx(0.0);
                        c.set_vy(0.0);
                        c.set_vz(0.0);
                        continue;
                    }

                    double Q_xplus =  face_flow(x, y, z, x + 1, y, z);
                    double Q_xminus = face_flow(x, y, z, x - 1, y, z);
                    double Q_yplus =  face_flow(x, y, z, x, y + 1, z);
                    double Q_yminus = face_flow(x, y, z, x, y - 1, z);
                    double Q_zplus =  face_flow(x, y, z, x, y, z + 1);
                    double Q_zminus = face_flow(x, y, z, x, y, z - 1);

                    c.set_vx((Q_xplus - Q_xminus) / (2.0 * Ax));
                    c.set_vy((Q_yplus - Q_yminus) / (2.0 * Ay));
                    c.set_vz((Q_zplus - Q_zminus) / (2.0 * Az));

                }
            }
        }
    }

    double face_flow(int x, int y, int z, int nx_, int ny_, int nz_) const {
        if (!mesh.in_bounds(nx_, ny_, nz_)) return 0.0;
        const Cell& n = mesh.at(nx_, ny_, nz_);
        if(!n.is_fluid()) return 0.0;

        size_t i = idx(x, y, z);
        for(const FaceLink& link : neighbors[i]) {
            if(link.nx == nx_ && link.ny == ny_ && link.nz == nz_) {
                double Pi = mesh.at(x, y, z).get_pressure();
                double Pj = n.get_pressure();
                return link.C * (Pi = Pj);
            }
        }
        return 0.0;
    }

    void recompute_resistance_from_velocity(double rho, double mu) {
        for(int x = 0; x < mesh.get_nx(); x++) {
            for(int y = 0; y < mesh.get_ny(); y++) {
                for(int z = 0; z < mesh.get_nz(); z++) {
                    Cell& c = mesh.at(x, y, z);
                    if (!c.is_fluid()) continue;
                    double vmag = std::sqrt(c.get_vx() * c.get_vx() + c.get_vy() * c.get_vy() + c.get_vz() * c.get_vz());
                    double Re = Convection::local_reynolds(vmag, mesh.get_dx(), rho, mu);
                    // darcy friction factor
                    double f = (Re > 1.0)
                        ? (Re < 2300.0 ? 64.0 / Re : 0.316 * std::pow(Re, -0.25))
                        : 64.0;
                    c.set_vent_conductance(c.get_vent_conductance()); // unchanged
                    loca
                }
            }
        }
    }
};

#endif
