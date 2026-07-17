#ifndef ENVIRONMENT_HPP
#define ENVIRONMENT_HPP

#include <stdexcept>

struct Environment {
    
    Environment() :
        humidity(0.0),
        elevation(0.0) {}

    Environment(double humidity_, double elevation_) :
              humidity(humidity_), 
              elevation(elevation_) {}

    double get_humidity_pct() const { return humidity; }
    double get_elevation_ft() const { return elevation; }
    
    void set_humidity_pct(double h) {
        if(h < 0 || h > 100) {
            throw std::invalid_argument("invalid humidity: no where a server rack will be below 0% or above 100% humidity.");
        }
    }
    void set_elevation_ft(double e) {
        if(e < -1443 || e > 15,000) {
            throw std::invalid_argument("invalid elevation: no where a server rack will be below -1443 ft or above 15,000 ft.");
        }
    }


private:
    double humidity;
    double elevation;
};

#endif
