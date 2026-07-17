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
#include "fan.hpp"
#include "rack.hpp"
#include "vent.hpp"

class Mesh {
public:
    Mesh() = default;
    
    Mesh(int nx_, int ny_, int nz_,
         double dx_, double dy_, double dz_)
        : nx(nx_),
          ny(ny_),
          nz(nz_),
          dx(dx_),
          dy(dy_),
          dz(dz_)
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

    Mesh build_mesh(const Rack& rack, double dx, double dy, double dz, double mu, double pr){
        int nx = std::ceil(rack.get_width_m()  / dx);
        int ny = std::ceil(rack.get_depth_m()  / dy);
        int nz = std::ceil(rack.get_height_m() / dz);
        Mesh mesh(nx, ny, nz, dx, dy, dz);
        for(int i=0;i<nx;i++)
        {
            for(int j=0;j<ny;j++)
            {
                for(int k=0;k<nz;k++)
                {
                    mesh.at(i,j,k) = Cell(
                        rack.get_t(),
                        rack.get_rho(),
                        rack.get_cp(),
                        rack.get_k(),
                        rack.get_h(),
                        0.0,
                        Cell::State::Air,
                        dx,
                        dy,
                        dz,
                        mu,
                        pr
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
                    cell.set_h(c.get_h());
                    cell.set_state(Cell::State::Component);
                    cell.set_T(c.get_t());
                }
            }
        }
    }
 
    void stamp_fan(const Fan& f) {
        auto [cx, cy, cz] = f.get_center();
        auto [nx, ny, nz] = f.get_velocity_dir();
        bool is_circular = f.is_circular();

        double r = f.get_diameter() / 2.0;
        std::vector<std::array<int, 3>> covered;

        auto stamp_cell = [&](int i, int j, int k) {
            if(!in_bounds(i, j, k)) return;
            covered.push_back({i, j, k});
        };

        double ax = std::abs(nx);
        double ay = std::abs(ny);
        double az = std::abs(nz);

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
        auto [nx, ny, nz] = v.get_direction();

        std::vector<std::array<int, 3>> covered;

        auto stamp_cell = [&](int i, int j, int k) {
            if(!in_bounds(i, j, k)) return;
            covered.push_back({i, j, k});
        };

        double ax = std::abs(nx);
        double ay = std::abs(ny);
        double az = std::abs(nz);

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
                    stamp_cell(i,j,k);
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
                    stamp_cell(i,j,k);
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
                    stamp_cell(i,j,k);
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

    std::vector<Cell> cells;
};

#endif
