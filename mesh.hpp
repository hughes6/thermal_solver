#ifndef MESH_HPP
#define MESH_HPP

#include <cmath>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <iomanip>

#include "cell.hpp"
#include "component.hpp"
#include "environment.hpp"
#include "fan.hpp"
#include "rack.hpp"
#include "vent.hpp"

class Mesh {
public:
    Mesh() = default;
    
    Mesh(int nx_, int ny_, int nz_,
         double dx_, double dy_, double dz_, Environment env_)
        : nx(nx_),
          ny(ny_),
          nz(nz_),
          dx(dx_),
          dy(dy_),
          dz(dz_),
          env(env_)
    {
        cells.resize(
            static_cast<size_t>(nx) *
            static_cast<size_t>(ny) *
            static_cast<size_t>(nz)
        );
    }

    const std::vector<Cell>& get_cells() const { return cells; }

    size_t idx(int x, int y, int z) const {
        return
            static_cast<size_t>(x) * ny * nz +
            static_cast<size_t>(y) * nz +
            static_cast<size_t>(z);
    }

    bool in_bounds(int x, int y, int z) const {
        return
            x >= 0 && x < nx &&
            y >= 0 && y < ny &&
            z >= 0 && z < nz;
    }

    bool v_in_bounds(int x, int y, int z, std::array<bool,3> wall) const {

        return 
            x >= 0.0 && (wall[0] ? x <= nx : x < nx) &&
            y >= 0.0 && (wall[1] ? y <= ny : y < ny) &&
            z >= 0.0 && (wall[2] ? z <= nz : z < nz );
    }

    Cell& at(int x, int y, int z) {
        return cells[idx(x,y,z)];
    }

    const Cell& at(int x, int y, int z) const {
        return cells[idx(x,y,z)];
    }

    void set_cell(int x, int y, int z, const Cell& cell) {
        cells[idx(x,y,z)] = cell;
    }

    int get_nx() const { return nx; }
    int get_ny() const { return ny; }
    int get_nz() const { return nz; }
    double get_dx() const { return dx; }
    double get_dy() const { return dy; }
    double get_dz() const { return dz; }
    double cell_volume() const { return dx * dy * dz; }
    double area_x() const { return dy * dz; }
    double area_y() const { return dx * dz; }
    double area_z() const { return dx * dy; }

    Mesh build_mesh(const Rack& rack, double dx, double dy, double dz, Environment env){
        int nx = std::ceil(rack.get_width_m()  / dx);
        int ny = std::ceil(rack.get_depth_m()  / dy);
        int nz = std::ceil(rack.get_height_m() / dz);
        Mesh mesh(nx, ny, nz, dx, dy, dz, env);
        for(int i=0;i<nx;i++)
        {
            for(int j=0;j<ny;j++)
            {
                for(int k=0;k<nz;k++)
                {
                    mesh.at(i,j,k) = Cell(
                        env.get_T_ambient(),
                        env.get_rho(),
                        env.get_cp(),
                        env.get_k(),
                        0.0,
                        0.0,
                        Cell::State::Air,
                        dx,
                        dy,
                        dz,
                        env.get_mu(),
                        env.get_pr()
                    );
                }
            }
        }

        return mesh;
    }

    void stamp_component(const Component& c) {
        auto[x, y, z] = c.get_coords();
        // start coord converted to mesh units
        int mx = static_cast<int>(std::floor(x / dx));
        int my = static_cast<int>(std::floor(y / dy));
        int mz = static_cast<int>(std::floor(z / dz));
        // size of component converted to mesh units
        int cnx = std::ceil(c.get_width_m() / dx);
        int cny = std::ceil(c.get_depth_m() / dy); 
        int cnz = std::ceil(c.get_height_m() / dz);
    
        // for all cells in component space, set to component properties
        for(int i = mx; i < mx + cnx; i++) {
            for(int j = my; j < my + cny; j++) {
                for(int k = mz; k < mz + cnz; k++) {
                    Cell& cell = at(i, j, k);
                    // overlap detection below
                    if(cell.get_state() != Cell::State::Air) {
                        throw std::runtime_error("Component overlap detected");
                    }
                    cell.set_qdot(c.watt_density());
                    cell.set_rho(c.get_rho());
                    cell.set_k(c.get_k());
                    cell.set_cp(c.get_cp());
                    cell.set_h(0.0);
                    cell.set_state(Cell::State::Component);
                    cell.set_T(c.get_t());
                }
            }
        }
        // stamp other regions in order of std::vector<RegionType>
        for(InternalRegion r : c.get_regions()) {
            if(r.get_region_type() == RegionType::Air) {
                auto[air_x, air_y, air_z] = r.get_global_position();
                auto[air_size_x, air_size_y, air_size_z] = r.get_size();
                // start coord converted to mesh units
                int air_mx = static_cast<int>(std::floor(air_x / dx));
                int air_my = static_cast<int>(std::floor(air_y / dy));
                int air_mz = static_cast<int>(std::floor(air_z / dz));
                // size of air converted to mesh units
                int air_sx = std::ceil(air_size_x / dx);
                int air_sy = std::ceil(air_size_y / dy);
                int air_sz = std::ceil(air_size_z / dz);
                // stamp all air cells in their size space
                for(int i = air_mx; i < air_mx + air_sx; i++) {
                    for(int j = air_my; j < air_my + air_sy; j++) {
                        for(int k = air_mz; k < air_mz + air_sz; k++) {
                            Cell& cell = at(i, j, k);
                            // collision detection done in component.hpp already
                            cell.set_T(env.get_T_ambient());
                            cell.set_rho(env.get_rho());
                            cell.set_cp(env.get_cp());
                            cell.set_k(env.get_k());
                            cell.set_mu(env.get_mu());
                            cell.set_state(Cell::State::Air);
                        }
                    }
                }

            }
            if(r.get_region_type() == RegionType::HeatSource) {
                auto[hs_x, hs_y, hs_z] = r.get_global_position();
                auto[hs_size_x, hs_size_y, hs_size_z] = r.get_size();
                // start coord converted to mesh units
                int hs_mx = static_cast<int>(std::floor(hs_x / dx));
                int hs_my = static_cast<int>(std::floor(hs_y / dy));
                int hs_mz = static_cast<int>(std::floor(hs_z / dz));
                // size of air converted to mesh units
                int hs_sx = std::ceil(hs_size_x / dx);
                int hs_sy = std::ceil(hs_size_y / dy);
                int hs_sz = std::ceil(hs_size_z / dz);
                // stamp all air cells in their size space
                for(int i = hs_mx; i < hs_mx + hs_sx; i++) {
                    for(int j = hs_my; j < hs_my + hs_sy; j++) {
                        for(int k = hs_mz; k < hs_mz + hs_sz; k++) {
                            Cell& cell = at(i, j, k);
                            // collision detection done in component.hpp already
                            cell.set_T(env.get_T_ambient());
                            cell.set_rho(r.get_rho());
                            cell.set_cp(r.get_cp());
                            cell.set_k(r.get_k());
                            cell.set_mu(0.0);
                            cell.set_qdot(r.watt_density());
                            cell.set_state(Cell::State::Component);
                        }
                    }
                }
            }          
            if(r.get_region_type() == RegionType::Vent) {
                // stamp vent
            }
            if(r.get_region_type() == RegionType::Fan) {
                // stamp fan
            }
        }

    }
 
    void stamp_fan(const Fan& f) {
        auto [cx, cy, cz] = f.get_center();
        auto [nnx, nny, nnz] = f.get_velocity_dir();
        bool is_circular = f.is_circular();

        double r = f.get_diameter() / 2.0;
        std::vector<std::array<int, 3>> covered;

        auto stamp_cell = [&](int i, int j, int k) {
            if(!in_bounds(i, j, k)) return;
            covered.push_back({i, j, k});
        };

        double ax = std::abs(nnx);
        double ay = std::abs(nny);
        double az = std::abs(nnz);

        //====================================================
        // Fan normal in +Z/-Z (disk lies in XY plane)
        //====================================================
        if(az >= ax && az >= ay) {
            double w = f.get_size()[0] / 2.0;
            double h = f.get_size()[1] / 2.0;

            int k = static_cast<int>(std::floor(cz / dz));
            int i0 = static_cast<int>(std::floor((cx-w)/dx));
            int i1 = static_cast<int>(std::ceil ((cx+w)/dx));
            int j0 = static_cast<int>(std::floor((cy-h)/dy));
            int j1 = static_cast<int>(std::ceil ((cy+h)/dy));

            if(is_circular) {
                i0 = static_cast<int>(std::floor((cx-r)/dx));
                i1 = static_cast<int>(std::ceil ((cx+r)/dx));
                j0 = static_cast<int>(std::floor((cy-r)/dy));
                j1 = static_cast<int>(std::ceil ((cy+r)/dy));
            } 

            for(int i=i0;i<i1;i++) {
                for(int j=j0;j<j1;j++) {
                    if(!is_circular) {
                        stamp_cell(i, j, k);
                    } else {
                        double xc = (i+0.5)*dx;
                        double yc = (j+0.5)*dy;

                        double dist2 =
                            (xc-cx)*(xc-cx) +
                            (yc-cy)*(yc-cy);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k);
                    }
                }
            }
        }

        //====================================================
        // Fan normal in +Y/-Y (disk lies in XZ plane)
        //====================================================
        else if(ay >= ax && ay >= az) {
            double w = f.get_size()[0] / 2.0;
            double h = f.get_size()[2] / 2.0;

            int j = static_cast<int>(std::floor(cy / dy));
            int i0 = static_cast<int>(std::floor((cx-w)/dx));
            int i1 = static_cast<int>(std::ceil ((cx+w)/dx));
            int k0 = static_cast<int>(std::floor((cz-h)/dz));
            int k1 = static_cast<int>(std::ceil ((cz+h)/dz));

            if(is_circular) {
                i0 = static_cast<int>(std::floor((cx-r)/dx));
                i1 = static_cast<int>(std::ceil ((cx+r)/dx));
                k0 = static_cast<int>(std::floor((cz-r)/dz));
                k1 = static_cast<int>(std::ceil ((cz+r)/dz));
            }

            for(int i=i0;i<i1;i++) {
                for(int k=k0;k<k1;k++) {
                    if(!is_circular) {
                        stamp_cell(i, j, k);
                    } else {
                        double xc = (i+0.5)*dx;
                        double zc = (k+0.5)*dz;

                        double dist2 =
                            (xc-cx)*(xc-cx) +
                            (zc-cz)*(zc-cz);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k);
                    }
                }
            }
            // stamping init fluid velocites
            std::vector<std::array<int, 3>> covered;

        }

        //====================================================
        // Fan normal in +X/-X (disk lies in YZ plane)
        //====================================================
        else {
            double w = f.get_size()[1] / 2.0;
            double h = f.get_size()[2] / 2.0;

            int i = static_cast<int>(std::floor(cx / dx));
            int j0 = static_cast<int>(std::floor((cy-w)/dy));
            int j1 = static_cast<int>(std::ceil ((cy+w)/dy));
            int k0 = static_cast<int>(std::floor((cz-h)/dz));
            int k1 = static_cast<int>(std::ceil ((cz+h)/dz));

            if(is_circular) {
                j0 = static_cast<int>(std::floor((cy-r)/dy));
                j1 = static_cast<int>(std::ceil ((cy+r)/dy));
                k0 = static_cast<int>(std::floor((cz-r)/dz));
                k1 = static_cast<int>(std::ceil ((cz+r)/dz));
            }


            for(int j=j0;j<j1;j++) {
                for(int k=k0;k<k1;k++) {
                    if(!is_circular) {
                        stamp_cell(i, j, k);
                    } else {
                        double yc = (j+0.5)*dy;
                        double zc = (k+0.5)*dz;

                        double dist2 =
                            (yc-cy)*(yc-cy) +
                            (zc-cz)*(zc-cz);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k);
                    }
                }
            }
        }
        
        // apply init velocites
        double Q_total = f.flow_m3s();
        double Q_per_cell = covered.empty() ? 0.0 : Q_total / covered.size();
        double sign = (f.get_type_t() == FlowType::Intake) ? +1.0 : -1.0;
        for (auto& [i, j,k ] : covered) {
            Cell& cell = at(i, j, k);
            if(cell.get_state() == Cell::State::Component) {
                throw std::runtime_error("Fan overlaps component.");
            } else {
                cell.set_state(
                    f.get_type_t() == FlowType::Intake ?
                    Cell::State::Intake :
                    Cell::State::Exhaust
                );
            }
            cell.set_vx(f.velocity_x());
            cell.set_vy(f.velocity_y());
            cell.set_vz(f.velocity_z());
            cell.set_flow_source(sign * Q_per_cell);
        }
    }



    void stamp_vent(const Vent& v, double Cd = 0.6) {
        auto [cx, cy, cz] = v.get_center();
        auto [nnx, nny, nnz] = v.get_direction();

        std::vector<std::array<int, 3>> covered;

        auto stamp_cell = [&](int i, int j, int k, std::array<bool,3> wall) {
            if(!v_in_bounds(i, j, k, wall)) return;
            if(wall[0] && i == nx) i -= 1;
            if(wall[1] && j == ny) j -= 1;
            if(wall[2] && k == nz) k -= 1;
            covered.push_back({i, j, k});
        };

        double ax = std::abs(nnx);
        double ay = std::abs(nny);
        double az = std::abs(nnz);

        //====================================================
        // Vent normal in +Z/-Z (rect lies in XY plane)
        //====================================================
        if(az >= ax && az >= ay) {

            int k = static_cast<int>(std::floor(cz / dz));

            double w = v.get_size()[0] / 2.0; // get x component of size
            int i0 = static_cast<int>(std::floor((cx-w)/dx));
            int i1 = static_cast<int>(std::ceil ((cx+w)/dx));

            double h = v.get_size()[1] / 2.0; // get y component of size
            int j0 = static_cast<int>(std::floor((cy-h)/dy));
            int j1 = static_cast<int>(std::ceil ((cy+h)/dy));

            for(int i=i0;i<i1;i++) {
                for(int j=j0;j<j1;j++) {
                    stamp_cell(i,j,k,{false,false,true});
                }
            }
        }

        //====================================================
        // Vent normal in +Y/-Y (rect lies in XZ plane)
        //====================================================
        else if(ay >= ax && ay >= az) {

            int j = static_cast<int>(std::floor(cy / dy));

            double w = v.get_size()[0] / 2.0; //get x component of size
            int i0 = static_cast<int>(std::floor((cx-w)/dx));
            int i1 = static_cast<int>(std::ceil ((cx+w)/dx));

            double h = v.get_size()[2] / 2.0; // get z component of size
            int k0 = static_cast<int>(std::floor((cz-h)/dz));
            int k1 = static_cast<int>(std::ceil ((cz+h)/dz));

            for(int i=i0;i<i1;i++) {
                for(int k=k0;k<k1;k++) {
                    stamp_cell(i,j,k,{false,true,false});
                }
            }
        }

        //====================================================
        // Vent normal in +X/-X (rect lies in YZ plane)
        //====================================================
        else {

            int i = static_cast<int>(std::floor(cx / dx));

            double w = v.get_size()[1] / 2.0; // get y component of size
            int j0 = static_cast<int>(std::floor((cy-w)/dy));
            int j1 = static_cast<int>(std::ceil ((cy+w)/dy));

            double h = v.get_size()[2] / 2.0; // get z component of size
            int k0 = static_cast<int>(std::floor((cz-h)/dz));
            int k1 = static_cast<int>(std::ceil ((cz+h)/dz));
            for(int j=j0;j<j1;j++) {
                for(int k=k0;k<k1;k++) {
                    // std::cout << i << " " << j << " " << k << std::endl;
                    // std::cout << nx << " " << ny << " " << nz << std::endl;
                    stamp_cell(i,j,k,{true,false,false});
                }
            }
        }

        // apply vent conductance
        double C_total = Cd * v.free_area();
        double C_per_cell = covered.empty() ? 0.0 : C_total / covered.size();
        for(auto& [i, j, k] : covered) {
            Cell& cell = at(i, j, k);
            if(cell.get_state() == Cell::State::Component) {
                throw std::runtime_error("Vent overlaps component.");
            } else if(cell.get_state() == Cell::State::Fan ||
                      cell.get_state() == Cell::State::Intake ||
                      cell.get_state() == Cell::State::Exhaust) {
                throw std::runtime_error("Vent overlaps fan/intake/exhaust");
            } else {
                cell.set_state(Cell::State::Vent);
            }

            cell.set_vent_conductance(C_per_cell);
        }

    }

    void print_mesh() const {
        for (int z = 0; z < nz; ++z) {
            std::cout << "\n========== Layer z = " << z << " ==========\n";
            // x headers
            std::cout << "    ";
            for (int x = 0; x < nx; ++x) {
                std::cout << std::setw(2) << x << ' ';
            }
            std::cout << '\n';
            // rows are y
            for (int y = 0; y < ny; ++y) { 
                std::cout << std::setw(2) << y << "  ";
                for (int x = 0; x < nx; ++x) {
                    const auto& cell = cells[idx(x,y,z)];
                    std::string print = " . ";
                    if(cell.get_state() == Cell::State::Component) {
                        print = " # ";
                    } else if (cell.get_state() == Cell::State::Exhaust) {
                        print = " E ";
                    } else if (cell.get_state() == Cell::State::Intake) {
                        print = " I ";
                    } else if (cell.get_state() == Cell::State::Vent) {
                        print = "V";
                    }
                    std::cout
                        << print;
                }
                std::cout << '\n';
            }
        }
        std::cout << std::endl;
    }

    void print_mesh_layer_temp(int layer) {
        std::cout << "\n========== Layer z = " << layer << " ==========\n";
        // x headers
        std::cout << "    ";
        for (int x = 0; x < nx; ++x) {
            std::cout << std::setw(2) << x << ' ';
        }
        std::cout << '\n';
        // rows are y
        for (int y = 0; y < ny; ++y) { 
            std::cout << std::setw(2) << y << "  ";
            for (int x = 0; x < nx; ++x) {
                const auto& cell = cells[idx(x,y,layer)];
                double print = cell.get_T();
                std::cout << print << " ";
            }
            std::cout << '\n';
        }
    
        std::cout << std::endl;
    }

private:
    int nx, ny, nz;
    double dx, dy, dz;
    Environment env;

    std::vector<Cell> cells;
};

#endif
