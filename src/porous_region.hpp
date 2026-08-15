#ifndef POROUS_REGION_HPP
#define POROUS_REGION_HPP

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

struct PorousRegion {
    std::string name;
    std::array<double,3> position{0.0,0.0,0.0};
    std::array<double,3> size{0.0,0.0,0.0};
    std::array<double,3> direction{0.0,1.0,0.0};
    double darcy = 0.0;                 // 1/m2, normal direction
    double forchheimer = 0.0;           // 1/m, normal direction
    double transverse_darcy = 0.0;      // 1/m2
    double transverse_forchheimer = 0.0;// 1/m

    void validate() const {
        if(name.empty()) throw std::invalid_argument("Porous region name cannot be empty.");
        for(double value:size)
            if(!(value>0.0) || !std::isfinite(value))
                throw std::invalid_argument("Porous region '"+name+"' size must be positive and finite.");
        const double mag=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
        if(!(mag>0.0) || !std::isfinite(mag))
            throw std::invalid_argument("Porous region '"+name+"' direction must be nonzero and finite.");
        for(double value:{darcy,forchheimer,transverse_darcy,transverse_forchheimer})
            if(value<0.0 || !std::isfinite(value))
                throw std::invalid_argument("Porous region '"+name+"' resistance coefficients must be finite and non-negative.");
        if(darcy==0.0 && forchheimer==0.0)
            throw std::invalid_argument("Porous region '"+name+"' must have nonzero normal resistance.");
    }

    std::array<double,3> unit_direction() const {
        const double mag=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
        return {direction[0]/mag,direction[1]/mag,direction[2]/mag};
    }
};

#endif
