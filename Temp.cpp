#include "logger.hpp"
#include "solver.hpp"

int main() {
    Mesh mesh = /* build mesh */;

    LoggingConfig config;

    config.output_directory =
        "simulation_output";

    config.field_interval = 100;
    config.summary_interval = 10;
    config.probe_interval = 1;

    config.field_variables = {
        LogVariable::Temperature,
        LogVariable::Pressure,
        LogVariable::VelocityX,
        LogVariable::VelocityY,
        LogVariable::VelocityZ
    };

    config.summary_requests = {
        {
            "temperature_all",
            LogVariable::Temperature,
            CellSelection::All,
            true,   // minimum
            true,   // maximum
            true,   // average
            false,  // RMS
            true    // standard deviation
        },

        {
            "fluid_velocity",
            LogVariable::VelocityMagnitude,
            CellSelection::FluidOnly,
            true,
            true,
            true,
            true,
            true
        },

        {
            "fluid_pressure",
            LogVariable::Pressure,
            CellSelection::FluidOnly,
            true,
            true,
            true,
            false,
            true
        }
    };

    config.probes = {
        {
            "rack_inlet",
            0.20,
            0.05,
            0.75,
            {
                LogVariable::Temperature,
                LogVariable::Pressure,
                LogVariable::VelocityMagnitude
            }
        },

        {
            "rack_exhaust",
            0.20,
            0.80,
            0.75,
            {
                LogVariable::Temperature,
                LogVariable::Pressure,
                LogVariable::VelocityMagnitude
            }
        }
    };

    SimulationLogger logger(config);

    logger.initialize(mesh);

    Solver solver(
        mesh,
        /* dt */,
        /* simulation length */
    );

    solver.set_logger(logger);
    solver.run();

    return 0;
}
