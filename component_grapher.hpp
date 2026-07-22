#ifndef COMPONENT_GRAPHER_HPP
#define COMPONENT_GRAPHER_HPP

#include <string>
#include <iostream>
#include <fstream>

#include "component.hpp"

struct ComponentGrapher
{
    Component c; 

    void add_component(const Component& c_){
        c = c_;
    }

    void export_to_file(const std::string& filename) const {
        std::ofstream fout(filename);

        if(!fout) {
            std::cerr << "Error opening " << filename << "\n";
            return;
        }

        fout << "\nComponents:\n";

        auto coords = c.get_coords();

        fout << "Component " << ": " << c.get_name() << "\n";
        // Canonical project order: width, depth, height.
        fout << "  dimensions: "
                << c.get_width_m()  << " x "
                << c.get_depth_m()  << " x "
                << c.get_height_m() << " m\n";

        fout << "  coordinates: "
                << coords[0] << " "
                << coords[1] << " "
                << coords[2] << " m\n";

        fout << "  watts: " << c.get_watts() << " W\n";

        int region_ctr = 1;
        for(const InternalRegion& region : c.get_regions()) {
            const auto size = region.get_size_m();
            const auto local = region.get_local_position();
            const auto global = region.get_global_position();

            const char* type = "Uninitialized";
            switch(region.get_region_type()) {
                case RegionType::Air:        type = "Air"; break;
                case RegionType::HeatSource: type = "HeatSource"; break;
                case RegionType::Vent:       type = "Vent"; break;
                case RegionType::Fan:        type = "Fan"; break;
                case RegionType::Uninitialized: break;
            }

            fout << "  Internal Region " << region_ctr << ":\n";
            fout << "    type: " << type << "\n";
            fout << "    size: " << size[0] << " " << size[1] << " " << size[2] << " m\n";
            fout << "    local_position: " << local[0] << " " << local[1] << " " << local[2] << " m\n";
            fout << "    global_position: " << global[0] << " " << global[1] << " " << global[2] << " m\n";
            if(region.get_region_type() == RegionType::HeatSource) {
                fout << "    watts: " << region.get_watts() << " W\n";
                fout << "    k: " << region.get_k() << " W/m-K\n";
                fout << "    rho: " << region.get_rho() << " kg/m^3\n";
                fout << "    cp: " << region.get_cp() << " J/kg-K\n";
            }
            ++region_ctr;
        }
    }
};



#endif
