
[logger]
output_directory = "simulation_output"

enable_field_logging = true
enable_summary_logging = true
enable_probe_logging = true

field_interval = 100
summary_interval = 10
probe_interval = 1

field_variables = [
    "temperature",
    "pressure",
    "velocity_x",
    "velocity_y",
    "velocity_z",
    "velocity_magnitude"
]

[[logger.summary]]
name = "solid_temperature"
variable = "temperature"
selection = "solid"

log_min = true
log_max = true
log_average = true
log_rms = false
log_standard_deviation = true

[[logger.summary]]
name = "fluid_velocity"
variable = "velocity_magnitude"
selection = "fluid"

log_min = true
log_max = true
log_average = true
log_rms = true
log_standard_deviation = true

[[logger.probe]]
name = "rack_inlet"
position = [0.20, 0.05, 0.75]

variables = [
    "temperature",
    "pressure",
    "velocity_magnitude"
]

[[logger.probe]]
name = "rack_exhaust"
position = [0.20, 0.80, 0.75]

variables = [
    "temperature",
    "pressure",
    "velocity_magnitude"
]
    
