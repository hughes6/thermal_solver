#ifndef AIR_PROPERTIES_HPP
#define AIR_PROPERTIES_HPP

namespace AirProperties {
    constexpr double R_SPECIFIC_AIR = 287.05;  // J/(kg*K), dry air
    constexpr double MU_REF = 1.716e-5;        // Pa*s, at T_REF
    constexpr double T_REF  = 273.15;          // K
    constexpr double SUTHERLAND_S = 110.4;     // K, Sutherland's constant for air

    // Ideal gas law at constant ambient pressure.
    inline double density(double T_celsius, double P_ambient) {
        double T_kelvin = T_celsius + 273.15;
        if (T_kelvin <= 0.0) return 0.0; // guard against garbage input
        return P_ambient / (R_SPECIFIC_AIR * T_kelvin);
    }

    // Sutherland's law -- captures air viscosity's temperature dependence
    // much better than a linear/ideal-gas assumption would.
    inline double viscosity(double T_celsius) {
        double T_kelvin = T_celsius + 273.15;
        if (T_kelvin <= 0.0) return MU_REF;
        return MU_REF * (T_REF + SUTHERLAND_S) / (T_kelvin + SUTHERLAND_S)
                       * std::pow(T_kelvin / T_REF, 1.5);
    }
}

#endif