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

    Mesh build_mesh(const Rack& rack, double dx, double dy, double dz){
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
                        dz
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

        double r = f.get_diameter() / 2.0;

        auto stamp_cell = [&](int i, int j, int k) {
            if(!in_bounds(i, j, k)) return;
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
        };

        double ax = std::abs(nx);
        double ay = std::abs(ny);
        double az = std::abs(nz);

        //====================================================
        // Fan normal in +Z/-Z (disk lies in XY plane)
        //====================================================
        if(az >= ax && az >= ay) {

            int k = static_cast<int>(std::floor(cz / dz));

            int i0 = static_cast<int>(std::floor((cx-r)/dx));
            int i1 = static_cast<int>(std::ceil ((cx+r)/dx));

            int j0 = static_cast<int>(std::floor((cy-r)/dy));
            int j1 = static_cast<int>(std::ceil ((cy+r)/dy));

            for(int i=i0;i<i1;i++) {
                for(int j=j0;j<j1;j++) {

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

        //====================================================
        // Fan normal in +Y/-Y (disk lies in XZ plane)
        //====================================================
        else if(ay >= ax && ay >= az) {

            int j = static_cast<int>(std::floor(cy / dy));

            int i0 = static_cast<int>(std::floor((cx-r)/dx));
            int i1 = static_cast<int>(std::ceil ((cx+r)/dx));

            int k0 = static_cast<int>(std::floor((cz-r)/dz));
            int k1 = static_cast<int>(std::ceil ((cz+r)/dz));

            for(int i=i0;i<i1;i++) {
                for(int k=k0;k<k1;k++) {

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

        //====================================================
        // Fan normal in +X/-X (disk lies in YZ plane)
        //====================================================
        else {

            int i = static_cast<int>(std::floor(cx / dx));

            int j0 = static_cast<int>(std::floor((cy-r)/dy));
            int j1 = static_cast<int>(std::ceil ((cy+r)/dy));

            int k0 = static_cast<int>(std::floor((cz-r)/dz));
            int k1 = static_cast<int>(std::ceil ((cz+r)/dz));

            for(int j=j0;j<j1;j++) {
                for(int k=k0;k<k1;k++) {

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
                    } else if (cell.get_state() == Cell::State::Fan) {
                        print = " F ";
                    } else if (cell.get_state() == Cell::State::Exhaust) {
                        print = " E ";
                    } else if (cell.get_state() == Cell::State::Intake) {
                        print = " I ";
                    }
                    std::cout
                        << print;
                }
                std::cout << '\n';
            }
        }
        std::cout << std::endl;
    }

private:
    int nx, ny, nz;
    double dx, dy, dz;

    std::vector<Cell> cells;
};

#endif