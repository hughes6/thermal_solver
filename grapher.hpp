#ifndef GRAPHER_HPP
#define GRAPHER_HPP

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <iomanip>

#include "rack.hpp"
#include "component.hpp"
#include "fan.hpp"

class Grapher {
public:
    Grapher(const Rack& rack,
            double dx = 0.0254,
            double dy = 0.0254,
            double dz = Rack::U_TO_M)
        : rack(rack),
          dx(dx),
          dy(dy),
          dz(dz),
          nx(static_cast<int>(std::ceil(rack.get_width_m()  / dx))),
          ny(static_cast<int>(std::ceil(rack.get_depth_m()  / dy))),
          nz(static_cast<int>(std::ceil(rack.get_height_m() / dz))),
          component_exist(nx, ny, nz),
          fan_exist(nx, ny, nz)
    {}

    void add_component(const Component& c) {
        validate_bounds(c);
        components.push_back(&c);
    }
     
    void add_fan(const Fan& f) {
        validate_bounds(f);
        fans.push_back(&f);
    }

    void stamp_components() {
        for(const Component* c : components) {
            component_exist.populate_component(*c, dx, dy, dz);
        }
    }

    void stamp_fans() {
        for(const Fan* f : fans) {
            fan_exist.populate_fan(*f, dx, dy, dz);
        }
    }

    void print_bitmap() const {
        for(int z = 0; z < nz; ++z) {
            std::cout << "Layer z = " << z << ":\n";

            for(int y = 0; y < ny; ++y) {
                for(int x = 0; x < nx; ++x) {
                    bool comp = component_exist.at(x, y, z);
                    bool fan  = fan_exist.at(x, y, z);

                    if(comp && fan) {
                        std::cout << "X ";
                    }
                    else if(comp) {
                        std::cout << "# ";
                    }
                    else if(fan) {
                        std::cout << "F ";
                    }
                    else {
                        std::cout << ". ";
                    }
                }
                std::cout << '\n';
            }

            std::cout << '\n';
        }
    }

    double total_watts() const {
        double sum = 0.0;
        for(const Component* c : components) {
            sum += c->get_watts();
        }
        return sum;
    }

    void print_summary() const {
        std::cout << "Rack: "
                  << rack.get_height_u() << "U x "
                  << rack.get_width_in() << "in W x "
                  << rack.get_depth_in() << "in D\n";

        std::cout << "Bitmap cells: "
                  << nx << " x " << ny << " x " << nz << "\n";

        std::cout << "Components: " << components.size() << "\n";
        std::cout << "Total heat load: " << total_watts() << " W\n";
    }

    void export_to_file(const std::string& filename) const {
        std::ofstream fout(filename);

        if(!fout) {
            std::cerr << "Error opening " << filename << "\n";
            return;
        }

        fout << "Rack dimensions:\n";
        fout << "  height: " << rack.get_height_m() << " m ("
             << rack.get_height_u() << " U)\n";
        fout << "  width:  " << rack.get_width_m() << " m ("
             << rack.get_width_in() << " in)\n";
        fout << "  depth:  " << rack.get_depth_m() << " m ("
             << rack.get_depth_in() << " in)\n";

        fout << "\nComponents:\n";

        int ctr = 1;
        for(const Component* c : components) {
            auto coords = c->get_coords();

            fout << "Component " << ctr << ": " << c->get_name() << "\n";
            fout << "  dimensions: "
                 << c->get_height_m() << " x "
                 << c->get_width_m()  << " x "
                 << c->get_depth_m()  << " m\n";

            fout << "  coordinates: "
                 << coords[0] << " "
                 << coords[1] << " "
                 << coords[2] << " m\n";

            fout << "  watts: " << c->get_watts() << " W\n";
            ++ctr;
        }
        fout << "\n" << "Fans: \n";

        ctr = 1;
        for(const Fan* f: fans) {
            auto center = f->get_center();
            fout << "Fan " << ctr << ": " << f->get_name() << "\n";
            fout << "  type: " << f->get_type() << "\n";
            fout << "  cfm: " << f->get_cfm() << "\n";
            fout << "  diameter: " << f->get_diameter() << " m\n";
            fout << "  center: " << f->get_center()[0] << " " <<
                                  f->get_center()[1] << " " <<
                                  f->get_center()[2] << " m\n";
            fout << "  direction: " << f->get_velocity_dir()[0] << " " <<
                                     f->get_velocity_dir()[1] << " " << 
                                     f->get_velocity_dir()[2] << "\n";
            fout << "\n";
            ctr++;
        }
    }

    const std::vector<const Component*>& get_components() const {
        return components;
    }

private:
    const Rack& rack;
    double dx, dy, dz;
    int nx, ny, nz;

    std::vector<const Component*> components;
    std::vector<const Fan*> fans;

    struct Bitmap {
        Bitmap(int nx, int ny, int nz)
            : nx(nx),
              ny(ny),
              nz(nz),
              map(static_cast<size_t>(nx) * ny * nz, false)
        {}

        size_t idx(int x, int y, int z) const {
            return static_cast<size_t>(x) * ny * nz
                 + static_cast<size_t>(y) * nz
                 + static_cast<size_t>(z);
        }

        bool in_bounds(int x, int y, int z) const {
            return x >= 0 && x < nx &&
                   y >= 0 && y < ny &&
                   z >= 0 && z < nz;
        }

        char at(int x, int y, int z) const {
            return map[idx(x, y, z)];
        }

        char& at(int x, int y, int z) {
            return map[idx(x, y, z)];
        }

        void populate_component(const Component& c,
                                double dx,
                                double dy,
                                double dz) {
            auto [x_m, y_m, z_m] = c.get_coords();

            int x0 = static_cast<int>(std::floor(x_m / dx));
            int y0 = static_cast<int>(std::floor(y_m / dy));
            int z0 = static_cast<int>(std::floor(z_m / dz));

            int cx = static_cast<int>(std::ceil(c.get_width_m()  / dx));
            int cy = static_cast<int>(std::ceil(c.get_depth_m()  / dy));
            int cz = static_cast<int>(std::ceil(c.get_height_m() / dz));

            for(int x = x0; x < x0 + cx; ++x) {
                for(int y = y0; y < y0 + cy; ++y) {
                    for(int z = z0; z < z0 + cz; ++z) {
                        if(in_bounds(x, y, z)) {
                            at(x, y, z) = 1;
                        }
                    }
                }
            }
        }

        void populate_fan(const Fan& f,
                        double dx,
                        double dy,
                        double dz) {
            auto [cx_m, cy_m, cz_m] = f.get_center();
            auto [vx, vy, vz] = f.get_velocity_dir();

            double r = f.get_diameter() / 2.0;

            double ax = std::abs(vx);
            double ay = std::abs(vy);
            double az = std::abs(vz);

            // Fan normal mostly x: opening plane is y-z
            if(ax >= ay && ax >= az) {
                int x = static_cast<int>(std::floor(cx_m / dx));

                int y0 = static_cast<int>(std::floor((cy_m - r) / dy));
                int y1 = static_cast<int>(std::ceil ((cy_m + r) / dy));

                int z0 = static_cast<int>(std::floor((cz_m - r) / dz));
                int z1 = static_cast<int>(std::ceil ((cz_m + r) / dz));

                for(int y = y0; y < y1; ++y) {
                    for(int z = z0; z < z1; ++z) {
                        double yc = (y + 0.5) * dy;
                        double zc = (z + 0.5) * dz;

                        double dist2 = (yc - cy_m) * (yc - cy_m)
                                    + (zc - cz_m) * (zc - cz_m);

                        if(dist2 <= r * r && in_bounds(x, y, z)) {
                            at(x, y, z) = 1;
                        }
                    }
                }
            }

            // Fan normal mostly y: opening plane is x-z
            else if(ay >= ax && ay >= az) {
                int y = static_cast<int>(std::floor(cy_m / dy));

                int x0 = static_cast<int>(std::floor((cx_m - r) / dx));
                int x1 = static_cast<int>(std::ceil ((cx_m + r) / dx));

                int z0 = static_cast<int>(std::floor((cz_m - r) / dz));
                int z1 = static_cast<int>(std::ceil ((cz_m + r) / dz));

                for(int x = x0; x < x1; ++x) {
                    for(int z = z0; z < z1; ++z) {
                        double xc = (x + 0.5) * dx;
                        double zc = (z + 0.5) * dz;

                        double dist2 = (xc - cx_m) * (xc - cx_m)
                                    + (zc - cz_m) * (zc - cz_m);

                        if(dist2 <= r * r && in_bounds(x, y, z)) {
                            at(x, y, z) = 1;
                        }
                    }
                }
            }

            // Fan normal mostly z: opening plane is x-y
            else {
                int z = static_cast<int>(std::floor(cz_m / dz));

                int x0 = static_cast<int>(std::floor((cx_m - r) / dx));
                int x1 = static_cast<int>(std::ceil ((cx_m + r) / dx));

                int y0 = static_cast<int>(std::floor((cy_m - r) / dy));
                int y1 = static_cast<int>(std::ceil ((cy_m + r) / dy));

                for(int x = x0; x < x1; ++x) {
                    for(int y = y0; y < y1; ++y) {
                        double xc = (x + 0.5) * dx;
                        double yc = (y + 0.5) * dy;

                        double dist2 = (xc - cx_m) * (xc - cx_m)
                                    + (yc - cy_m) * (yc - cy_m);

                        if(dist2 <= r * r && in_bounds(x, y, z)) {
                            at(x, y, z) = 1;
                        }
                    }
                }
            }
        }

        void print_bitmap() const {
            for(int z = 0; z < nz; ++z) {
                std::cout << "Layer z = " << z << ":\n";

                for(int y = 0; y < ny; ++y) {
                    for(int x = 0; x < nx; ++x) {
                        std::cout << (at(x, y, z) ? '#' : '.') << ' ';
                    }
                    std::cout << '\n';
                }

                std::cout << '\n';
            }
        }

        int nx, ny, nz;
        std::vector<char> map;
    };

    Bitmap component_exist;
    Bitmap fan_exist;

    void validate_bounds(const Component& c) const {
        auto [x, y, z] = c.get_coords();

        if(x < 0.0 || y < 0.0 || z < 0.0 ||
           x + c.get_width_m()  > rack.get_width_m() ||
           y + c.get_depth_m()  > rack.get_depth_m() ||
           z + c.get_height_m() > rack.get_height_m()) {
            throw std::out_of_range( "Component '" + c.get_name() + "' out of rack bounds" );
        }
    }

    void validate_bounds(const Fan& f) const {
        auto[x, y, z] = f.get_center();
        double r = f.get_diameter() / 2.0;
        auto[vx, vy, vz] = f.get_velocity_dir();

        double rack_w = rack.get_width_m();
        double rack_d = rack.get_depth_m();
        double rack_h = rack.get_height_m();

        if(x < 0.0 || x > rack_w || y < 0.0 || y > rack_d || z < 0.0 || z > rack_h) {
            throw std::out_of_range("Fan '" + f.get_name() + "' center out of rack bounds");
        }
        double ax = std::abs(vx);
        double ay = std::abs(vy);
        double az = std::abs(vz);
        // Fan normal mostly x: fan opening plane is y-z
        if(ax >= ay && ax >= az) {
            if(y - r < 0.0 || y + r > rack_d || z - r < 0.0 || z + r > rack_h) {
                throw std::out_of_range("Fan '" + f.get_name() + "' disk out of rack bounds");
            }
        }
        // Fan normal mostly y: fan opening plane is x-z
        else if(ay >= ax && ay >= az) {
            if(x - r < 0.0 || x + r > rack_w || z - r < 0.0 || z + r > rack_h) {
                throw std::out_of_range("Fan '" + f.get_name() + "' disk out of rack bounds");
            }
        }
        // Fan normal mostly z: fan opening plane is x-y
        else {
            if(x - r < 0.0 || x + r > rack_w || y - r < 0.0 || y + r > rack_d) {
                throw std::out_of_range("Fan '" + f.get_name() + "' disk out of rack bounds");
            }
        }
    }
};

#endif