#ifndef THERMAL_MODEL_LOADER_HPP
#define THERMAL_MODEL_LOADER_HPP

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <string>

#include "../logger.hpp"
#include "../convection.hpp"
#include "../solver.hpp"
#include "input_types.hpp"
#include "../component_grapher.hpp"
#include "../collision.hpp"
#include "../mesh_refinement_planner.hpp"
#include "../openfoam_exporter.hpp"
#include "../thermal_estimator.hpp"
#include "../toml.hpp"


// -----------------------------
// ANON NAMESPACE FOR HELPERS
// -----------------------------
namespace {

    const toml::table& require_table (const toml::node_view<const toml::node>& node, const std::string& context) {
        const toml::table* table = node.as_table();
        if(table == nullptr) {
            throw std::runtime_error(context + " must be a TOML table");
        }

        return *table;
    }


    template<typename T>
    T require_value(const toml::node_view<const toml::node>& node, const std::string& context) {
        const auto value = node.value<T>();

        if(!value) {
            throw std::runtime_error("Missing or invalid value: " + context);
        }

        return *value;
    }

    PositionInput parse_position(const toml::table& table, const std::string& context) {
        PositionInput position;
        position.x = require_value<double>(table["x"], context + ".x");
        position.y = require_value<double>(table["y"], context + ".y");
        position.z = require_value<double>(table["z"], context + ".z");
        position.units = table["units"].value<std::string>();
        return position;
    }

    DirectionInput parse_direction(const toml::table& table, const std::string& context) {
        return {
            require_value<double>(table["x"], context + ".x"),
            require_value<double>(table["y"], context + ".y"),
            require_value<double>(table["z"], context + ".z")
        };
    }

    SizeInput parse_size(const toml::table& table, const std::string& context) {
        SizeInput size;
        size.width = require_value<double>(table["width"], context + ".width");
        size.depth = require_value<double>(table["depth"], context + ".depth");
        size.height = require_value<double>(table["height"], context + ".height");
        size.units = table["units"].value<std::string>();
        return size;
    }

    MaterialInput parse_material(const toml::table& table, const std::string& context) {
        return {
            require_value<double>(table["rho"], context + ".rho"),
            require_value<double>(table["cp"],  context + ".cp"),
            require_value<double>(table["k"],   context + ".k")
        };
    }

    FanShape parse_fan_shape(const std::string& value) {
        if(value == "circular") {
            return FanShape::Circular;
        }
        if(value == "rectangular") {
            return FanShape::Rectangular;
        }
        throw std::runtime_error("Invalid Fan Shape" + value);
    }

    FanFlowType parse_fan_flow_type(const std::string& value) {
        if(value == "intake") {
            return FanFlowType::Intake;
        }
        if(value == "exhaust") {
            return FanFlowType::Exhaust;
        }
        throw std::runtime_error("Invalid fan flow type: " + value);
    }

    VentShape parse_vent_shape(const std::string& value) {
        if(value == "circular") {
            return VentShape::Circular;
        }
        if(value == "rectangular") {
            return VentShape::Rectangular;
        }
        throw std::runtime_error("Invalid vent flow type: " + value);
    }

    FanInput parse_fan(const toml::table& table, const std::string& context);
    VentInput parse_vent(const toml::table& table, const std::string& context);

    RegionState parse_region_state(const std::string& value) {
        if(value == "solid") {
            return RegionState::Solid;
        }
        if(value == "air") {
            return RegionState::Air;
        }
        if(value == "vent") {
            return RegionState::Vent;
        }
        if(value == "fan") {
            return RegionState::Fan;
        }
        throw std::runtime_error("Invalid region state: " + value);
    }

    InternalRegionInput parse_internal_region(const toml::table& table, const std::string& context) {
        InternalRegionInput internal_region;
        internal_region.state = parse_region_state(require_value<std::string>(table["state"], context + ".state"));

        if(internal_region.state == RegionState::Fan) {
            internal_region.fan = parse_fan(table, context);
            internal_region.name = internal_region.fan->name;
            internal_region.local_position = internal_region.fan->position;
            if(internal_region.fan->size.has_value()) internal_region.size = *internal_region.fan->size;
            return internal_region;
        }

        if(internal_region.state == RegionState::Vent) {
            internal_region.vent = parse_vent(table, context);
            internal_region.name = internal_region.vent->name;
            internal_region.local_position = internal_region.vent->position;
            if(internal_region.vent->size.has_value()) internal_region.size = *internal_region.vent->size;
            return internal_region;
        }

        internal_region.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        internal_region.local_position = parse_position(require_table(table["position"], context + ".position"), context + ".position");
        internal_region.name = require_value<std::string>(table["name"], context + ".name");

        if(internal_region.state == RegionState::Air) return internal_region;

        if(internal_region.state == RegionState::Solid) {
            internal_region.material = parse_material(require_table(table["material"], context + ".material"), context + ".material");
            internal_region.watts = require_value<double>(table["watts"], context + ".watts");
            return internal_region;
        }

        throw std::runtime_error(context + ": unsupported internal region state");
    }

    ComponentInput parse_component(const toml::table& table, const std::string& context, bool is_loading_from_template = false, PositionInput pos = PositionInput()) {
        ComponentInput component;

        component.template_path = table["template"].value<std::string>();
        if(!is_loading_from_template) {
        component.position = parse_position(require_table(table["position"], context + ".position"), context + ".position");
        } else {
            component.position = pos;
        }
        if (component.template_path.has_value()) return component;

        component.name = require_value<std::string>(table["name"], context + ".name");
        component.watts = require_value<double>(table["watts"], context + ".watts");
        component.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        component.material = parse_material(require_table(table["material"], context + ".material"), context + ".material");

        const toml::array* internal_regions = table["internal_regions"].as_array();

        if(internal_regions == nullptr) {
            throw std::runtime_error("internal_regions[...] must be a table");
        }

        std::size_t index = 0;

        for(const toml::node& node : *internal_regions) {

            const toml::table* internal_region_table = node.as_table();
            if(internal_region_table == nullptr) {
                throw std::runtime_error("internal_regions[" + std::to_string(index) + "] must be a table");
            }
            component.internal_regions.push_back(parse_internal_region(*internal_region_table, "internal_regions[" + std::to_string(index) + "]"));
            index++;
        }
        return component;
    }
        
    ComponentInput load_component_template(const std::filesystem::path& path, PositionInput pos) {
        try {
            const toml::table root = toml::parse_file(path.string());
            return parse_component(root, path.stem().string(), true, pos);
        } catch (const toml::parse_error& error) {
            throw std::runtime_error("Failed to parse component template '" + path.string() + "': "
                                    + std::string(error.description()));
        }
    }

    FanInput parse_fan(const toml::table& table, const std::string& context) {
        FanInput fan;

        fan.name = require_value<std::string>(table["name"], context + ".name");
        fan.shape = parse_fan_shape(require_value<std::string>(table["shape"], context + ".shape"));
        fan.flow_type = parse_fan_flow_type(require_value<std::string>(table["flow_type"], context + ".flow_type"));
        fan.cfm = require_value<double>(table["cfm"], context + ".cfm");
        fan.position = parse_position(require_table(table["position"], context + ".position"), context + ".position");
        fan.direction = parse_direction(require_table(table["direction"], context + ".direction"), context + ".direction");
       
        fan.curve_name = table["curve"].value<std::string>();

        if(fan.shape == FanShape::Rectangular) {
            fan.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        } else {
            fan.diameter = table["diameter"].value<double>().value_or(0.0);       
            fan.diameter_units = table["diameter_units"].value<std::string>().value_or("u");
        }

        return fan;
    }

    VentInput parse_vent(const toml::table& table, const std::string& context) {
        VentInput vent;

        vent.name = require_value<std::string>(table["name"], context + ".name");
        vent.shape = parse_vent_shape(require_value<std::string>(table["shape"], context + ".shape"));
        vent.position = parse_position(require_table(table["position"], context + ".position"), context + ".position");
        vent.direction = parse_direction(require_table(table["normal"], context + ".direction"), context + ".direction");
        vent.free_area_ratio = require_value<double>(table["free_area_ratio"], context + ".free_area_ratio");
        vent.cd = require_value<double>(table["vent_discharge_coeff"],context + ".vent_discharge_coeff");

        if(vent.shape == VentShape::Rectangular) {
            vent.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        } else {
            vent.diameter = require_value<double>(table["diameter"], context + ".diameter");
            vent.diameter_units = table["diameter_units"].value<std::string>().value_or("u");
        }
        
        return vent;
    }

    void parse_global_components(const toml::table& root, ModelInput& model) {
        const toml::array* components = root["components"].as_array();
        if(components == nullptr) return;

        std::size_t index = 0;

        for(const toml::node& node : *components) {
            const toml::table* component_table = node.as_table();
            if(component_table == nullptr) {
                throw std::runtime_error("components[" + std::to_string(index) + "] must be a table");
            }

            model.components.push_back(parse_component(*component_table, "compnents[" + std::to_string(index) + "]"));
            index++;
        }
    }

    void parse_global_fans(const toml::table& root, ModelInput& model) {
        const toml::array* fans = root["fans"].as_array();
        if(fans == nullptr) return;

        std::size_t index = 0;

        for(const toml::node& node : *fans) {
            const toml::table* fan_table = node.as_table();
            if(fan_table == nullptr) {
                throw std::runtime_error("fans[" + std::to_string(index) + "] must be a table");
            }
            model.fans.push_back(parse_fan(*fan_table, "fans[" + std::to_string(index) + "]"));
            index++;
        }
    }

    void parse_global_vents(const toml::table& root, ModelInput& model) {
        const toml::array* vents = root["vents"].as_array();
        if(vents == nullptr) return;

        std::size_t index = 0;

        for(const toml::node& node : *vents) {
            const toml::table* vent_table = node.as_table();
            if(vent_table == nullptr) {
                throw std::runtime_error("vents[" + std::to_string(index) + "] must be a table");
            }
            model.vents.push_back(parse_vent(*vent_table, "vents[" + std::to_string(index) + "]"));
            index++;
        }
    }

    FanCurveInput parse_fan_curve(const toml::table& table, const std::string& context) {
        FanCurveInput curve;
        curve.name = require_value<std::string>(table["name"], context + ".name");
        curve.a = require_value<double>(table["a"], context + ".a");
        curve.b = require_value<double>(table["b"], context + ".b");
        curve.c = table["c"].value<double>().value_or(0.0);
        curve.rho_rated = table["rho_rated"].value<double>().value_or(1.2);

        if (curve.a <= 0.0) {
            throw std::runtime_error(context + ": shutoff pressure 'a' must be > 0.0");
        }
        return curve;
    }

    std::unordered_map<std::string, FanCurveInput>
        load_fan_curve_library(const std::filesystem::path& path) {
            std::unordered_map<std::string, FanCurveInput> library;
            try {
                const toml::table root = toml::parse_file(path.string());
                const toml::array* curves = root["fan_curve"].as_array();
                if (curves == nullptr) return library;

                std::size_t index = 0;
                for (const toml::node& node : *curves) {
                    const toml::table* curve_table = node.as_table();
                    if (curve_table == nullptr) {
                        throw std::runtime_error("fan_curve[" + std::to_string(index) + "] must be a table");
                    }
                    FanCurveInput curve = parse_fan_curve(*curve_table, "fan_curve[" + std::to_string(index) + "]");
                    if (library.count(curve.name)) {
                        throw std::runtime_error("Duplicate fan curve name: " + curve.name);
                    }
                    library[curve.name] = curve;
                    index++;
                }
            } catch (const toml::parse_error& error) {
                throw std::runtime_error("Failed to parse fan curve library '" + path.string() + "': "
                                        + std::string(error.description()));
            }
            return library;
        }

    double to_meters(double value, const std::string& units, const std::string& context) {
        if(units == "m") return value;
        if(units == "u") return value * Fan::U_TO_M;
        if(units == "in") return value * Fan::IN_TO_M;
        if(units == "mm") return value * Fan::MM_TO_M;
        throw std::runtime_error("Invalid " + context + " units: '" + units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
    }

    std::array<double, 3> position_to_meters(const PositionInput& position, const std::string& context) {
        const std::string units = position.units.value_or("u");
        return {
            to_meters(position.x, units, context),
            to_meters(position.y, units, context),
            to_meters(position.z, units, context)
        };
    }

    std::array<double, 3> size_to_meters(const SizeInput& size, const std::string& context) {
        const std::string units = size.units.value_or("u");
        return {
            to_meters(size.width, units, context),
            to_meters(size.depth, units, context),
            to_meters(size.height, units, context)
        };
    }

    InternalRegion build_internal_region(
        const InternalRegionInput& input,
        const std::unordered_map<std::string, FanCurveInput>* curve_library = nullptr) {
        if(input.state == RegionState::Fan) {
            if(!input.fan.has_value()) throw std::runtime_error("Internal fan region is missing parsed fan data.");
            const FanInput& f = *input.fan;
            const std::array<double, 3> size = f.shape == FanShape::Rectangular
                ? size_to_meters(*f.size, "internal fan.size")
                : std::array<double, 3>{0.0, 0.0, 0.0};
            const double diameter = f.shape == FanShape::Circular
                ? to_meters(*f.diameter, f.diameter_units.value_or("u"), "internal fan.diameter")
                : 0.0;
            Fan fan(f.name, f.cfm, diameter, size,
                position_to_meters(f.position, "internal fan.position"),
                {f.direction.x, f.direction.y, f.direction.z},
                f.flow_type == FanFlowType::Intake ? FlowType::Intake : FlowType::Exhaust,
                f.shape == FanShape::Circular ? ShapeType::Circular : ShapeType::Rectangular
            );
            if(f.curve_name.has_value()) {
                if(curve_library == nullptr) {
                    throw std::runtime_error(
                        "Internal fan '" + f.name + "' references curve '" +
                        *f.curve_name + "' but no fan curve library was loaded.");
                }
                const auto it = curve_library->find(*f.curve_name);
                if(it == curve_library->end()) {
                    throw std::runtime_error(
                        "Internal fan '" + f.name + "' references unknown curve '" +
                        *f.curve_name + "'");
                }
                const FanCurveInput& curve = it->second;
                fan.set_curve(curve.a, curve.b, curve.c, curve.rho_rated);
            }
            return InternalRegion(fan);
        }

        if(input.state == RegionState::Vent) {
            if(!input.vent.has_value()) throw std::runtime_error("Internal vent region is missing parsed vent data.");
            const VentInput& v = *input.vent;
            const std::array<double, 3> size = v.shape == VentShape::Rectangular
                ? size_to_meters(*v.size, "internal vent.size")
                : std::array<double, 3>{0.0, 0.0, 0.0};
            const double diameter = v.shape == VentShape::Circular
                ? to_meters(*v.diameter, v.diameter_units.value_or("u"), "internal vent.diameter")
                : 0.0;
            Vent vent(v.name, size, v.free_area_ratio, diameter, v.cd,
                position_to_meters(v.position, "internal vent.position"),
                {v.direction.x, v.direction.y, v.direction.z},
                v.shape == VentShape::Circular ? VentShapeType::Circular : VentShapeType::Rectangular
            );
            return InternalRegion(vent);
        }

        InternalRegion region;
        const auto position = position_to_meters(input.local_position, "internal_region.position");
        const auto size = size_to_meters(input.size, "internal_region.size");
        region.set_local_position(position);
        region.set_size(size);
        region.set_name(input.name);

        if(input.state == RegionState::Solid) {
            region.set_region_type(RegionType::HeatSource);
            region.set_cp(input.material.cp);
            region.set_rho(input.material.density);
            region.set_k(input.material.k);
            region.set_watts(input.watts);
        } else if(input.state == RegionState::Air) {
            region.set_region_type(RegionType::Air);
        } else {
            throw std::runtime_error("Unsupported internal region state.");
        }
        return region;
    }



    // --------------logger----------------------------

    LogVariable parse_log_variable(const std::string& value) {
        if(value =="temperature" || value == "T") {
            return LogVariable::Temperature;
        }
        if(value =="pressure" || value == "[vz]") {
            return LogVariable::Pressure;
        }
        if(value =="velocity_x" || value == "vx") {
            return LogVariable::VelocityX;
        }
        if(value =="velocity_y" || value == "vy") {
            return LogVariable::VelocityY;
        }
        if(value =="velocity_z" || value == "vz") {
            return LogVariable::VelocityZ;
        }
        if(value =="velocity_mag" || value == "vmag" || value == "velocity" || value == "v" || value == "velocity_magnitude") {
            return LogVariable::VelocityMagnitude;
        }
        if(value =="density" || value == "rho") {
            return LogVariable::Density;
        }
        if(value =="specific_heat" || value == "cp") {
            return LogVariable::SpecificHeat;
        }
        if(value =="conductivity" || value == "k") {
            return LogVariable::Conductivity;
        }
        if(value =="heat_generation" || value == "qdot") {
            return LogVariable::HeatGeneration;
        }
        if(value =="reynolds_number" || value == "re") {
            return LogVariable::ReynoldsNumber;
        }
        if(value =="convection_coefficient" || value == "h") {
            return LogVariable::ConvectionCoefficient;
        }
        throw std::runtime_error("Uknown logger variable: " + value);
    }

    CellSelection parse_cell_selection(const std::string& value) {
        if(value == "all") {
            return CellSelection::All;
        }
        if(value == "air") {
            return CellSelection::AirOnly;
        }
        if(value == "solid") {
            return CellSelection::SolidOnly;
        }
        if(value == "fluid") {
            return CellSelection::FluidOnly;
        }
        if(value == "heat_generating" || value == "heat-generating") {
            return CellSelection::HeatGeneratingOnly;
        }
        throw std::runtime_error("Unknown cell selection: " + value);
    }

    void parse_logger_summary_requests(const toml::array& summaries, LoggerInput& cfg) {
        std::vector<LoggerSummaryInput> parsed_summaries;
        for(const toml::node& node : summaries) {
            const toml::table* summary_table = node.as_table();

            if(summary_table == nullptr) {
                throw std::runtime_error("Each [[logger.summary]] entry must be a table");
            }

            LoggerSummaryInput input;
            if(auto value = (*summary_table)["name"].value<std::string>()) {
                input.name = *value;
            }
            if(auto value = (*summary_table)["variable"].value<std::string>()) {
                input.variable = parse_log_variable(*value);
            }
            if(auto value = (*summary_table)["selection"].value<std::string>()) {
                input.selection = parse_cell_selection(*value);
            }
            if(auto value = (*summary_table)["log_min"].value<bool>()) {
                input.log_min = *value;
            }
            if(auto value = (*summary_table)["log_max"].value<bool>()) {
                input.log_max = *value;
            }
            if(auto value = (*summary_table)["log_average"].value<bool>()) {
                input.log_average = *value;
            }
            if(auto value = (*summary_table)["log_rms"].value<bool>()) {
                input.log_rms = *value;
            }
            if(auto value = (*summary_table)["log_standard_deviation"].value<bool>()) {
                input.log_standard_deviation = *value;
            }
            parsed_summaries.push_back(std::move(input));
        }
        cfg.summary_requests = std::move(parsed_summaries);
    }

    void parse_logger_probes(const toml::array& probes, LoggerInput& cfg) {
        std::vector<LoggerProbeInput> parsed_probes;
        for(const toml::node& node : probes) {
            const toml::table* probe_table = node.as_table();

            if(probe_table == nullptr)  {
                throw std::runtime_error("Each [[logger.probe]] entry must be a table.");
            }

            LoggerProbeInput input;
            if(auto value = (*probe_table)["name"].value<std::string>()) {
                input.name = *value;
            }
            if(const toml::array* position = (*probe_table)["position"].as_array()) {
                if(position->size() != 3) {
                    throw std::runtime_error("logger.probe position must contain [x, y, z].");
                }
                const auto x = (*position)[0].value<double>();
                const auto y = (*position)[1].value<double>();
                const auto z = (*position)[2].value<double>();
                if(!x || !y || !z) {
                    throw std::runtime_error("logger.probe position values must be numeric.");
                }
                input.x = *x;
                input.y = *y;
                input.z = *z;
            }
            if(const toml::array* variables = (*probe_table)["variables"].as_array()) {
                std::vector<LogVariable> parsed_variables;
                for(const toml::node& variable_node : *variables) {
                    const auto variable_name = variable_node.value<std::string>();
                    if(!variable_name) {
                        throw std::runtime_error("Every logger probe variable must be a string.");
                    }
                    parsed_variables.push_back(parse_log_variable(*variable_name));
                }
                input.variables = std::move(parsed_variables);
            }
            parsed_probes.push_back(std::move(input));
        }
        cfg.probes = std::move(parsed_probes);
    }

    void parse_logger(const toml::table& root, LoggerInput& cfg) {
        const toml::table* logger_table = root["logger"].as_table();

        if(logger_table == nullptr) {
            return; // logging config defaults are going to be used
        }

        if(auto value = (*logger_table)["template"].value<std::string>()) {
            cfg.template_file = *value;
            return;
        } else {
            cfg.template_file = "NULL";
        }
        
        if(auto value = (*logger_table)["output_directory"].value<std::string>()) {
            cfg.output_directory = *value;
        } 
        if(auto value = (*logger_table)["enable_summary_logging"].value<bool>()) {
            cfg.enable_summary_logging = *value;
        } 
        if(auto value = (*logger_table)["enable_field_logging"].value<bool>()) {
            cfg.enable_field_logging = *value;
        } 
        if(auto value = (*logger_table)["enable_probe_logging"].value<bool>()) {
            cfg.enable_probe_logging = *value;
        } 
        if(auto value = (*logger_table)["field_interval"].value<int>()) {
            cfg.field_interval = *value;
        } 
        if(auto value = (*logger_table)["summary_interval"].value<int>()) {
            cfg.summary_interval = *value;
        } 
        if(auto value = (*logger_table)["probe_interval"].value<int>()) {
            cfg.probe_interval = *value;
        } 
        if(const toml::array* variables = (*logger_table)["field_variables"].as_array()) {
            std::vector<LogVariable> parsed_variables;
            for(const toml::node& node : *variables) {
                const auto variable_name = node.value<std::string>();
                if(!variable_name) {
                    throw std::runtime_error("Every logger.field_variables entry must be a string");
                }
                parsed_variables.push_back(parse_log_variable(*variable_name));
            }
            cfg.field_variables = std::move(parsed_variables);
        }
        if(const toml::array* summaries = (*logger_table)["summary"].as_array()) {
            parse_logger_summary_requests(*summaries, cfg);
        }
        if(const toml::array* probes = (*logger_table)["probe"].as_array()) {
            parse_logger_probes(*probes, cfg);
        }

    }

    //--------------------make logger object from input struct-------------------
    inline LoggingConfig make_logging_config(const LoggerInput& input) {
        LoggingConfig config;
        if(input.output_directory) {
            config.output_directory = *input.output_directory;
        }
        if(input.enable_field_logging) {
            config.enable_field_logging = *input.enable_field_logging;
        }
        if(input.enable_summary_logging) {
            config.enable_summary_logging = *input.enable_summary_logging;
        }
        if(input.enable_probe_logging) {
            config.enable_probe_logging = *input.enable_probe_logging;
        }
        if(input.field_interval) {
            config.field_interval = *input.field_interval;
        }
        if(input.summary_interval) {
            config.summary_interval = *input.summary_interval;
        }
        if(input.probe_interval) {
            config.probe_interval = *input.probe_interval;
        }
        if(input.field_variables) {
            config.field_variables = *input.field_variables;
        }
        if(input.summary_requests) {
            config.summary_requests.clear();
            for(const LoggerSummaryInput& summary_input : *input.summary_requests) {
                SummaryRequest request;
                if(summary_input.name) {
                    request.name = *summary_input.name;
                }
                if(summary_input.variable) {
                    request.variable = *summary_input.variable;
                }
                if(summary_input.selection) {
                    request.selection = *summary_input.selection;
                }
                if(summary_input.log_min) {
                    request.log_min = *summary_input.log_min;
                }
                if(summary_input.log_max) {
                    request.log_max = *summary_input.log_max;
                }
                if(summary_input.log_average) {
                    request.log_average = *summary_input.log_average;
                }
                if(summary_input.log_rms) {
                    request.log_rms = *summary_input.log_rms;
                }
                if(summary_input.log_standard_deviation) {
                    request.log_standard_deviation = *summary_input.log_standard_deviation;
                }
                config.summary_requests.push_back(std::move(request));
            }
        }
        if(input.probes) {
            config.probes.clear();
            for(const LoggerProbeInput& probe_input : *input.probes) {
                Probe probe;
                if(probe_input.name) {
                    probe.name = *probe_input.name;
                }
                if(probe_input.x) {
                    probe.x = *probe_input.x;
                }
                if(probe_input.y) {
                    probe.y = *probe_input.y;
                }
                if(probe_input.z) {
                    probe.z = *probe_input.z;
                }
                if(probe_input.variables) {
                    probe.variables = *probe_input.variables;
                }
                config.probes.push_back(std::move(probe));
            }
        }
        return config;
    }
}

struct ModelLoader {
    ModelInput model;
    LoggerInput input;
    LoggingConfig config;
    std::unordered_map<std::string, FanCurveInput> fan_curve_library;
    std::unordered_map<std::string, ComponentInput> component_template_cache; 

    ModelLoader() = default;

    void load_fan_curves(const std::filesystem::path& library_path) {
        fan_curve_library = load_fan_curve_library(library_path);}

    void load_model(const std::filesystem::path& model_path) 
    {
        try {
            const toml::table root = toml::parse_file(model_path.string());

            model.name = root["name"].value<std::string>().value_or(model_path.stem().string());

            // ------------------------------------------Simulation------------------------------------------
            const toml::table& simulation = require_table(root["simulation"], "simulation");
            model.simulation.dt = require_value<double>(simulation["dt"], "simulation.dt");
            model.simulation.duration = require_value<double>(simulation["duration"], "simulation.duration");
            model.simulation.output_interval = simulation["output_interval"].value<int>().value_or(1);
            model.simulation.max_timesteps = require_value<double>(simulation["max_timesteps"], "simulation.max_timesteps");
            model.simulation.max_updates = require_value<double>(simulation["max_updates"], "simulation.max_updates");
            model.simulation.max_cell_count = require_value<double>(simulation["max_cell_count"], "simulation.max_cell_count");
            model.simulation.max_megabyte_usage = require_value<double>(simulation["max_megabyte_usage"], "simulation.max_megabyte_usage");
            model.simulation.update_flow_interval = simulation["update_flow_interval"].value<int>().value_or(1);
            model.simulation.advection_subcycling =
                simulation["advection_subcycling"].value<bool>().value_or(false);
            model.simulation.advection_cfl_target =
                simulation["advection_cfl_target"].value<double>().value_or(0.8);
            model.simulation.max_advection_substeps =
                simulation["max_advection_substeps"].value<int>().value_or(10000);

            // ----------------------------------------Flow Solver-------------------------------------------
            const toml::table& flow_solver = require_table(root["flow_solver"], "simulation");
            model.flow_solver.enable_flow_solver = flow_solver["enable_flow_solver"].value<bool>().value_or(false);
            model.flow_solver.pressure_method =
                flow_solver["pressure_method"].value<std::string>().value_or("sor");
            model.flow_solver.resistivity = flow_solver["resistivity"].value<double>().value_or(4.5);
            model.flow_solver.tolerance = flow_solver["tolerance"].value<double>().value_or(1e-4);
            model.flow_solver.max_iterations = flow_solver["max_iterations"].value<int>().value_or(100);
            model.flow_solver.sor_omega = flow_solver["sor_omega"].value<double>().value_or(1.2);
            model.flow_solver.max_outer_iters = flow_solver["max_outer_iters"].value<int>().value_or(5);
            model.flow_solver.flow_tolerance = flow_solver["flow_tolerance"].value<double>().value_or(1e-2);

            // ----------------------------------------Environtment------------------------------------------
            const toml::table& environment = require_table(root["environment"], "environment");
            model.environment.humidity = require_value<double>(environment["humidity"], "environtment.humidity");
            model.environment.elevation = require_value<double>(environment["elevation"], "environtment.elevation");
            model.environment.T_ambient = require_value<double>(environment["T_ambient"], "environtment.T_ambient");
            model.environment.cp = require_value<double>(environment["cp"], "environtment.cp");
            model.environment.k = require_value<double>(environment["k"], "environtment.k");
            model.environment.mu = require_value<double>(environment["mu"], "environtment.mu");
            model.environment.pr = require_value<double>(environment["pr"], "environment.pr");
            model.environment.rho = require_value<double>(environment["rho"], "environtment.rho");

            // ------------------------------------------Mesh------------------------------------------------
            const toml::table& mesh = require_table(root["mesh"],"mesh");
            model.mesh.adaptive = mesh["adaptive"].value<bool>().value_or(false);
            if (model.mesh.adaptive) {
                model.mesh.fine_dx = require_value<double>(mesh["fine_dx"], "mesh.fine_dx");
                model.mesh.coarse_dx = require_value<double>(mesh["coarse_dx"], "mesh.coarse_dx");
                model.mesh.refinement_margin =
                    mesh["refinement_margin"].value<double>().value_or(0.0);
            } else {
                model.mesh.dx = require_value<double>(mesh["dx"], "mesh.dx");
                model.mesh.dy = require_value<double>(mesh["dy"], "mesh.dy");
                model.mesh.dz = require_value<double>(mesh["dz"], "mesh.dz");
            }

            // ----------------------------------------Multistage---------------------------------------------
            if (const toml::table* multistage = root["multistage"].as_table()) {
                model.multistage.enabled =
                    (*multistage)["enabled"].value<bool>().value_or(false);
                if (model.multistage.enabled) {
                    model.multistage.coarse_dt = require_value<double>(
                        (*multistage)["coarse_dt"], "multistage.coarse_dt");
                    model.multistage.coarse_duration = require_value<double>(
                        (*multistage)["coarse_duration"], "multistage.coarse_duration");
                    model.multistage.coarse_update_flow_interval =
                        (*multistage)["coarse_update_flow_interval"]
                            .value<int>().value_or(-1);

                    const toml::table& coarse_mesh = require_table(
                        (*multistage)["coarse_mesh"], "multistage.coarse_mesh");
                    model.multistage.coarse_mesh.adaptive = true;
                    model.multistage.coarse_mesh.fine_dx = require_value<double>(
                        coarse_mesh["fine_dx"], "multistage.coarse_mesh.fine_dx");
                    model.multistage.coarse_mesh.coarse_dx = require_value<double>(
                        coarse_mesh["coarse_dx"], "multistage.coarse_mesh.coarse_dx");
                    model.multistage.coarse_mesh.refinement_margin =
                        coarse_mesh["refinement_margin"].value<double>().value_or(0.0);
                }
            }
            // Optional backend. Its absence preserves every legacy TOML and
            // keeps the native solver authoritative.
            if(const toml::table* foam=root["openfoam_solver"].as_table()) {
                auto& cfg=model.openfoam_solver;
                toml::table template_root;
                const toml::table* template_cfg=nullptr;
                cfg.template_file=(*foam)["template"].value<std::string>();
                if(cfg.template_file) {
                    template_root=toml::parse_file(*cfg.template_file);
                    template_cfg=template_root["openfoam_solver"].as_table();
                    if(!template_cfg)
                        throw std::runtime_error(
                            "OpenFOAM configuration template '" +
                            *cfg.template_file +
                            "' must contain [openfoam_solver].");
                }
                auto value=[&](std::string_view key) {
                    const auto local=(*foam)[key];
                    if(local) return local;
                    return template_cfg
                        ? (*template_cfg)[key] : (*foam)[key];
                };
                // A fidelity profile may optionally carry mesh controls in
                // its top-level [mesh] table. Selecting such a profile is an
                // explicit request to replace the model's base mesh settings.
                // A nested [openfoam_solver.mesh] table remains the
                // highest-priority per-model override.
                const toml::table* profile_mesh=
                    cfg.template_file ? template_root["mesh"].as_table()
                                      : nullptr;
                const toml::table* local_profile_mesh=
                    (*foam)["mesh"].as_table();
                const toml::table* selected_mesh=
                    local_profile_mesh ? local_profile_mesh : profile_mesh;
                if(selected_mesh) {
                    model.mesh.adaptive=
                        (*selected_mesh)["adaptive"]
                            .value<bool>().value_or(false);
                    if(model.mesh.adaptive) {
                        model.mesh.fine_dx=require_value<double>(
                            (*selected_mesh)["fine_dx"],
                            "openfoam_solver.mesh.fine_dx");
                        model.mesh.coarse_dx=require_value<double>(
                            (*selected_mesh)["coarse_dx"],
                            "openfoam_solver.mesh.coarse_dx");
                        model.mesh.refinement_margin=
                            (*selected_mesh)["refinement_margin"]
                                .value<double>().value_or(0.0);
                    } else {
                        model.mesh.dx=require_value<double>(
                            (*selected_mesh)["dx"],
                            "openfoam_solver.mesh.dx");
                        model.mesh.dy=require_value<double>(
                            (*selected_mesh)["dy"],
                            "openfoam_solver.mesh.dy");
                        model.mesh.dz=require_value<double>(
                            (*selected_mesh)["dz"],
                            "openfoam_solver.mesh.dz");
                    }
                }
                cfg.enabled=value("enabled").value<bool>().value_or(false);
                cfg.case_directory=value("case_directory")
                    .value<std::string>().value_or(
                        ("openfoam_cases/"+model_path.stem().string()));
                cfg.overwrite=value("overwrite").value<bool>().value_or(false);
                cfg.parallel_processes=value("parallel_processes")
                    .value<int>().value_or(4);
                cfg.maximum_time_step=value("maximum_time_step")
                    .value<double>().value_or(1.0);
                cfg.maximum_courant_number=value("maximum_courant_number")
                    .value<double>().value_or(1.0);
                cfg.field_write_interval=value("field_write_interval")
                    .value<double>().value_or(60.0);
                cfg.saved_time_directories=value("saved_time_directories")
                    .value<int>().value_or(3);
                cfg.report_interval=value("report_interval")
                    .value<double>().value_or(10.0);
                cfg.use_k_omega_sst=value("use_k_omega_sst")
                    .value<bool>().value_or(true);
                cfg.inlet_turbulence_intensity=
                    value("inlet_turbulence_intensity")
                        .value<double>().value_or(0.05);
                cfg.turbulence_length_scale=
                    value("turbulence_length_scale")
                        .value<double>().value_or(0.01);
                cfg.turbulent_prandtl_number=
                    value("turbulent_prandtl_number")
                        .value<double>().value_or(0.85);
                cfg.temperature_dependent_air=
                    value("temperature_dependent_air")
                        .value<bool>().value_or(true);
                if(const toml::table* gravity=value("gravity").as_table())
                    cfg.gravity=parse_direction(
                        *gravity,"openfoam_solver.gravity");
                cfg.sutherland_temperature=
                    value("sutherland_temperature")
                        .value<double>().value_or(110.4);
                cfg.use_vent_pressure_loss=
                    value("use_vent_pressure_loss")
                        .value<bool>().value_or(true);
                cfg.use_fan_curves=value("use_fan_curves")
                    .value<bool>().value_or(true);
                cfg.fan_curve_extension_multiplier=
                    value("fan_curve_extension_multiplier")
                        .value<double>().value_or(2.0);
                cfg.use_multirate_thermal=
                    value("use_multirate_thermal")
                        .value<bool>().value_or(true);
                cfg.airflow_warmup_time=value("airflow_warmup_time")
                    .value<double>().value_or(5.0);
                cfg.use_fan_startup_ramp=value("use_fan_startup_ramp")
                    .value<bool>().value_or(true);
                cfg.fan_startup_ramp_time=value("fan_startup_ramp_time")
                    .value<double>().value_or(0.05);
                cfg.fan_startup_ramp_steps=value("fan_startup_ramp_steps")
                    .value<int>().value_or(5);
                cfg.initial_airflow_check_interval=
                    value("initial_airflow_check_interval")
                        .value<double>().value_or(0.01);
                cfg.minimum_initial_airflow_duration=
                    value("minimum_initial_airflow_duration")
                        .value<double>().value_or(0.02);
                cfg.thermal_only_maximum_time_step=
                    value("thermal_only_maximum_time_step")
                        .value<double>().value_or(1.0);
                cfg.thermal_only_maximum_courant_number=
                    value("thermal_only_maximum_courant_number")
                        .value<double>().value_or(1000.0);
                cfg.airflow_refresh_interval=
                    value("airflow_refresh_interval")
                        .value<double>().value_or(300.0);
                cfg.airflow_refresh_duration=
                    value("airflow_refresh_duration")
                        .value<double>().value_or(1.0);
                cfg.use_adaptive_airflow_refresh=
                    value("use_adaptive_airflow_refresh")
                        .value<bool>().value_or(true);
                cfg.airflow_refresh_maximum_courant_number=
                    value("airflow_refresh_maximum_courant_number")
                        .value<double>().value_or(10.0);
                cfg.airflow_refresh_check_interval=
                    value("airflow_refresh_check_interval")
                        .value<double>().value_or(0.01);
                cfg.maximum_airflow_refresh_duration=
                    value("maximum_airflow_refresh_duration")
                        .value<double>().value_or(0.2);
                cfg.maximum_mass_imbalance_fraction=
                    value("maximum_mass_imbalance_fraction")
                        .value<double>().value_or(0.01);
                cfg.maximum_device_flow_change_fraction=
                    value("maximum_device_flow_change_fraction")
                        .value<double>().value_or(0.02);
                cfg.stop_when_thermally_converged=
                    value("stop_when_thermally_converged")
                        .value<bool>().value_or(true);
                cfg.minimum_thermal_convergence_time=
                    value("minimum_thermal_convergence_time")
                        .value<double>().value_or(3600.0);
                cfg.thermal_convergence_reference_interval=
                    value("thermal_convergence_reference_interval")
                        .value<double>().value_or(300.0);
                cfg.maximum_temperature_change=
                    value("maximum_temperature_change")
                        .value<double>().value_or(0.1);
                cfg.maximum_component_average_temperature_change=
                    value("maximum_component_average_temperature_change")
                        .value<double>().value_or(0.05);
                cfg.thermal_convergence_required_checkpoints=
                    value("thermal_convergence_required_checkpoints")
                        .value<int>().value_or(2);
            }
            // ------------------------------------------Rack------------------------------------------------
            const toml::table& rack = require_table(root["rack"], "rack");
            model.rack.name = rack["name"].value<std::string>().value_or("Unnamed rack");
            model.rack.size = parse_size(require_table(rack["size"], "rack.size"), "rack.size");
            const toml::table& ambient = require_table(rack["ambient"], "rack.ambient");
            model.rack.ambient.pressure = ambient["pressure"].value<double>().value_or(101325.0);
            model.rack.ambient.h = ambient["h"].value<double>().value_or(0.0);
            model.rack.ambient.temperature = require_value<double>(ambient["temperature"], "rack.ambient.temperature");
            model.rack.ambient.k = require_value<double>(ambient["k"], "rack.ambient.k");
            model.rack.ambient.rho = require_value<double>(ambient["rho"], "rack.ambient.rho");
            model.rack.ambient.cp = require_value<double>(ambient["cp"], "rack.ambient.cp");

            // ------------------------------------------Global Objects---------------------------------------
            parse_global_components(root, model);
            parse_global_fans(root, model);
            parse_global_vents(root, model);

            // ------------------------------------------Global Objects---------------------------------------
            parse_logger(root, input);
            if(input.template_file && *input.template_file != "NULL") {
                const toml::table template_root =
                    toml::parse_file(*input.template_file);
                parse_logger(template_root, input);
            }
            config = std::move(make_logging_config(input));

        } catch(const toml::parse_error& error) {
            throw std::runtime_error("Failed to parse model file '" + model_path.string() + "': " + std::string(error.description()));
        }
    }

    const ComponentInput& get_component_template(const std::string& path, PositionInput pos) {
        auto it = component_template_cache.find(path);
        if (it != component_template_cache.end()) return it->second;
        auto [inserted, ok] = component_template_cache.emplace(path, load_component_template(path, pos));
        return inserted->second;
    }

    void run() 
    {
        Workload load = Workload(model.simulation.max_timesteps, model.simulation.max_updates, model.simulation.max_cell_count, model.simulation.max_megabyte_usage);
        Environment env(model.environment.humidity, model.environment.elevation, model.environment.T_ambient, 
                        model.environment.cp, model.environment.k, model.environment.mu, model.environment.pr, model.environment.rho);
        Rack rack;
        const std::string rack_units = model.rack.size.units.value_or("u");

        if(rack_units == "u") {
            rack = Rack::from_rack_units(model.rack.size.width, model.rack.size.depth, model.rack.size.height, model.rack.name);
        } else if(rack_units == "m") {
            rack = Rack::from_meters(model.rack.size.width, model.rack.size.depth, model.rack.size.height, model.rack.name);
        } else if(rack_units == "in") {
            rack = Rack::from_inches(model.rack.size.width, model.rack.size.depth, model.rack.size.height, model.rack.name);
        } else if(rack_units == "mm") {
            rack = Rack::from_mm(model.rack.size.width, model.rack.size.depth, model.rack.size.height, model.rack.name);
        } else {
            throw std::runtime_error("Invalid rack.size units: '" + rack_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
        }
        rack.set_cp(model.rack.ambient.cp);
        rack.set_k(model.rack.ambient.k);
        rack.set_h(model.rack.ambient.h);
        rack.set_rho(model.rack.ambient.rho);
        rack.set_t(model.rack.ambient.temperature);

        // Built up-front, geometry-checked as a whole, then stamped. Keeping the
        // "build" and "stamp" phases separate is what lets CollisionChecker see
        // every component/fan/vent before the mesh has any say in the matter.
        std::vector<Component> components;
        std::vector<Fan> fans;
        std::vector<Vent> vents;

        for(const ComponentInput& c_in : model.components) {
            Component component;
            ComponentInput c = c_in;
            if (c.template_path.has_value()) {
                const ComponentInput& tmpl = get_component_template(*c.template_path, c.position);
                c.name = tmpl.name;
                c.size = tmpl.size;
                c.material = tmpl.material;
                c.watts = tmpl.watts;
                c.internal_regions = tmpl.internal_regions;
            }
            const std::string comp_units = c.size.units.value_or("u");
            if(comp_units == "u") {
                component = Component::from_rack_units(c.size.width, c.size.depth, c.size.height, c.name);
            } else if(comp_units == "m") {
                component = Component::from_meters(c.size.width, c.size.depth, c.size.height, c.name);
            } else if(comp_units == "in") {
                component = Component::from_inches(c.size.width, c.size.depth, c.size.height, c.name);
            } else if(comp_units == "mm") {
                component = Component::from_mm(c.size.width, c.size.depth, c.size.height, c.name);
            } else {
                throw std::runtime_error("Invalid component.size units: '" + comp_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
            }
            const std::string comp_pos_units = c.position.units.value_or("u");
            if(comp_pos_units == "u") {
                component.set_coords_rack_units(c.position.x, c.position.y, c.position.z);
            } else if(comp_pos_units == "m") {
                component.set_coords_m(c.position.x, c.position.y, c.position.z);
            } else if(comp_pos_units == "in") {
                component.set_coords_in(c.position.x, c.position.y, c.position.z);
            } else if(comp_pos_units == "mm") {
                component.set_coords_mm(c.position.x, c.position.y, c.position.z);
            } else {
                throw std::runtime_error("Invalid component.position units: '" + comp_pos_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
            }
            component.set_name(c.name);
            component.set_cp(c.material.cp);
            component.set_rho_solid(c.material.density);
            component.set_k_solid(c.material.k);
            component.set_watts(c.watts);
            for(const InternalRegionInput& i : c.internal_regions) {
                component.add_region(build_internal_region(i, &fan_curve_library));
            }
            component.order_internal_regions();
            components.push_back(component);
        }

        for(const FanInput& f : model.fans) {
            Fan fan = Fan();
            const std::string fan_units = f.position.units.value_or("u");
            if(fan_units == "u") {
                fan.set_center_rack_units(f.position.x, f.position.y, f.position.z);
            } else if(fan_units == "m") {
                fan.set_center_meters(f.position.x, f.position.y, f.position.z);
            } else if(fan_units == "in") {
                fan.set_center_inches(f.position.x, f.position.y, f.position.z);
            } else if(fan_units == "mm") {
                fan.set_center_mm(f.position.x, f.position.y, f.position.z);
            } else {
                throw std::runtime_error("Invalid fan.position units: '" + fan_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
            }
            if(f.shape == FanShape::Circular) {
                FlowType flow = FlowType::Exhaust;
                if(f.flow_type == FanFlowType::Intake) flow = FlowType::Intake;
                const std::string fan_d_units = f.diameter_units.value_or("u");
                if(fan_d_units == "u") {
                    fan.set_diameter_rack_units(*f.diameter);
                } else if(fan_d_units == "m") {
                    fan.set_diameter_meters(*f.diameter);
                } else if(fan_d_units == "in") {
                    fan.set_diameter_inches(*f.diameter);
                } else if(fan_d_units == "mm") {
                    fan.set_diameter_mm(*f.diameter);
                } else {
                    throw std::runtime_error("Invalid fan diameter_units: '" + fan_d_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
                }
                fan.set_name(f.name);
                fan.set_cfm(f.cfm);
                fan.set_velocity_dir({f.direction.x, f.direction.y, f.direction.z});
                fan.set_type(flow);
                fan.set_shape(ShapeType::Circular);
            }
            if(f.shape == FanShape::Rectangular) {
            FlowType flow = FlowType::Exhaust;
            if(f.flow_type == FanFlowType::Intake) flow = FlowType::Intake;
                fan.set_name(f.name);
                const std::string fan_pos_units = f.size->units.value_or("u");
                if(fan_pos_units == "u") {
                    fan.set_size_rack_units(f.size->width, f.size->depth, f.size->height);
                } else if(fan_pos_units == "m") {
                    fan.set_size_meters(f.size->width, f.size->depth, f.size->height);
                } else if(fan_pos_units == "in") {
                    fan.set_size_inches(f.size->width, f.size->depth, f.size->height);
                } else if(fan_pos_units == "mm") {
                    fan.set_size_mm(f.size->width, f.size->depth, f.size->height);
                } else {
                    throw std::runtime_error("Invalid fan.size units: '" + fan_pos_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
                }
                fan.set_cfm(f.cfm);
                fan.set_velocity_dir({f.direction.x, f.direction.y, f.direction.z});
                fan.set_type(flow);
                fan.set_shape(ShapeType::Rectangular);
            }
            if (f.curve_name.has_value()) {
                auto it = fan_curve_library.find(*f.curve_name);
                if (it == fan_curve_library.end()) {
                    throw std::runtime_error(
                        "Fan '" + f.name + "' references unknown curve '" + *f.curve_name + "'");
                }
                const FanCurveInput& curve = it->second;
                fan.set_curve(curve.a, curve.b, curve.c, curve.rho_rated);
            }
            fans.push_back(fan);
        }

        for(const VentInput& v : model.vents) {
            Vent vent = Vent();
            const std::string vent_units = v.position.units.value_or("u");
            if(vent_units == "u") {
                vent.set_center_rack_units(v.position.x, v.position.y, v.position.z);
            } else if(vent_units == "m") {
                vent.set_center_meters(v.position.x, v.position.y, v.position.z);
            } else if(vent_units == "in") {
                vent.set_center_inches(v.position.x, v.position.y, v.position.z);
            } else if(vent_units == "mm") {
                vent.set_center_mm(v.position.x, v.position.y, v.position.z);
            } else {
                throw std::runtime_error("Invalid vent.position units: '" + vent_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
            }
            if(v.shape == VentShape::Circular) {
                const std::string vent_d_units = v.diameter_units.value_or("u");
                if(vent_d_units == "u") {
                    vent.set_diameter_rack_units(*v.diameter);
                } else if(vent_d_units == "m") {
                    vent.set_diameter_meters(*v.diameter);
                } else if(vent_d_units == "in") {
                    vent.set_diameter_inches(*v.diameter);
                } else if(vent_d_units == "mm") {
                    vent.set_diameter_mm(*v.diameter);
                } else {
                    throw std::runtime_error("Invalid vent diameter_units: '" + vent_d_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
                }
                vent.set_cd(v.cd);
                vent.set_direction(v.direction.x, v.direction.y, v.direction.z);
                vent.set_name(v.name);
                vent.set_free_area_ratio(v.free_area_ratio);
                vent.set_shape(VentShapeType::Circular);
            }
            if(v.shape == VentShape::Rectangular) {
                const std::string vent_s_units = v.size->units.value_or("u");
                if(vent_s_units == "u") {
                    vent.set_size_rack_units(v.size->width, v.size->depth, v.size->height);
                } else if(vent_s_units == "m") {
                    vent.set_size_meters(v.size->width, v.size->depth, v.size->height);
                } else if(vent_s_units == "in") {
                    vent.set_size_inches(v.size->width, v.size->depth, v.size->height);
                } else if(vent_s_units == "mm") {
                    vent.set_size_mm(v.size->width, v.size->depth, v.size->height);
                } else {
                    throw std::runtime_error("Invalid vent.size units: '" + vent_s_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
                }
                vent.set_cd(v.cd);
                vent.set_name(v.name);
                vent.set_free_area_ratio(v.free_area_ratio);
                vent.set_direction(v.direction.x, v.direction.y, v.direction.z);
                // vent.set_shape(ShapeType::Rectangular);
            }
            vents.push_back(vent);
        }

        // Single geometry-level validation gate, run once before anything is
        // stamped into the mesh or populated into the grapher: first, does
        // everything fit inside the rack; second, does anything collide with
        // anything else. Both are resolution-independent - no dx/dy/dz involved.
        RackBoundsChecker::check_all(rack, components, fans, vents);
        CollisionChecker::check_all(components, fans, vents);
        
        std::optional<Mesh> coarse_warm_start;
        std::optional<ThermalTimeEstimate> coarse_thermal_estimate;
        std::size_t coarse_timestep_count = 0;
        if (model.multistage.enabled && !model.openfoam_solver.enabled) {
            std::cout << "----- Coarse warm-start stage -----\n";
            const MeshInput& coarse_cfg = model.multistage.coarse_mesh;
            const MeshRefinementPlan coarse_plan = MeshRefinementPlanner::plan(
                rack, components, fans, vents,
                coarse_cfg.fine_dx, coarse_cfg.coarse_dx,
                coarse_cfg.refinement_margin,
                false); // omit thin internal cuts on the warm-start grid

            Mesh coarse_mesh = Mesh().build_adaptive_mesh(
                rack, coarse_plan.dxs, coarse_plan.dys, coarse_plan.dzs,
                env, load);

            for (const Component& component : components)
                coarse_mesh.stamp_component_face_walls_adaptive(component);
            for (const Fan& fan : fans)
                coarse_mesh.stamp_fan_adaptive(fan);
            for (const Vent& vent : vents)
                coarse_mesh.stamp_vent_adaptive(vent);

            if (model.flow_solver.enable_flow_solver) {
                FlowSolver coarse_flow(
                    coarse_mesh,
                    *model.flow_solver.resistivity,
                    *model.flow_solver.tolerance,
                    *model.flow_solver.max_iterations,
                    *model.flow_solver.sor_omega,
                    *model.flow_solver.max_outer_iters,
                    *model.flow_solver.flow_tolerance,
                    model.flow_solver.pressure_method);
                coarse_flow.solve();
                const double flow_scale = std::max({
                    std::abs(coarse_flow.total_source_m3s()),
                    std::abs(coarse_flow.total_vent_flow_m3s()),
                    1e-9});
                const double relative_imbalance =
                    std::abs(coarse_flow.mass_imbalance_m3s()) / flow_scale;
                if(relative_imbalance > 0.05) {
                    throw std::runtime_error(
                        "Coarse flow solve is not usable: relative physical "
                        "source/vent imbalance = " +
                        std::to_string(relative_imbalance) +
                        ". Correct fan/vent connectivity or flow settings before "
                        "running the coarse transient.");
                }
                if(!coarse_flow.converged()) {
                    std::cerr
                        << "FlowSolver: coarse nonlinear iteration did not meet "
                        << "its relative-change target, but physical mass "
                        << "imbalance is " << 100.0*relative_imbalance
                        << "% (within the 5% coarse acceptance limit).\n";
                }
            }

            std::cout << "\n===== Coarse-stage thermal estimate =====\n";
            coarse_thermal_estimate =
                ThermalTimeEstimator::estimate(coarse_mesh);
            coarse_thermal_estimate->print();
            coarse_timestep_count = static_cast<std::size_t>(std::ceil(
                model.multistage.coarse_duration /
                model.multistage.coarse_dt));
            std::cout << "Configured coarse dt:       "
                      << model.multistage.coarse_dt << " s\n";
            std::cout << "Configured coarse duration: "
                      << model.multistage.coarse_duration << " s\n";
            std::cout << "Planned coarse timesteps:   "
                      << coarse_timestep_count << "\n";
            const double coarse_dt_limit =
                model.simulation.advection_subcycling
                    ? 0.8*coarse_thermal_estimate
                        ->max_stable_dt_conduction_s
                    : coarse_thermal_estimate->recommended_dt_s;
            if(model.simulation.advection_subcycling &&
               std::isfinite(
                   coarse_thermal_estimate->max_stable_dt_advection_s)) {
                const int estimated_substeps=std::max(1,
                    static_cast<int>(std::ceil(
                        model.multistage.coarse_dt /
                        (model.simulation.advection_cfl_target *
                         coarse_thermal_estimate
                            ->max_stable_dt_advection_s))));
                std::cout << "Estimated coarse advection substeps: "
                          << estimated_substeps << "\n";
            }
            if(std::isfinite(coarse_dt_limit) &&
               model.multistage.coarse_dt > coarse_dt_limit) {
                throw std::runtime_error(
                    "Configured coarse_dt = " +
                    std::to_string(model.multistage.coarse_dt) +
                    " s exceeds the non-subcycled stability limit of " +
                    std::to_string(coarse_dt_limit) +
                    " s. The coarse transient was not started.");
            }

            const int coarse_flow_interval =
                model.flow_solver.enable_flow_solver
                    ? model.multistage.coarse_update_flow_interval
                    : -1;
            Solver coarse_solver(
                coarse_mesh,
                model.multistage.coarse_dt,
                model.multistage.coarse_duration,
                false,
                std::max(1, static_cast<int>(std::ceil(
                    model.multistage.coarse_duration /
                    model.multistage.coarse_dt))),
                coarse_flow_interval,
                *model.flow_solver.resistivity,
                *model.flow_solver.tolerance,
                *model.flow_solver.max_iterations,
                *model.flow_solver.sor_omega,
                *model.flow_solver.max_outer_iters,
                *model.flow_solver.flow_tolerance,
                model.simulation.advection_subcycling,
                model.simulation.advection_cfl_target,
                model.simulation.max_advection_substeps,
                "coarse_simulation.csv",
                model.flow_solver.pressure_method);
            coarse_solver.solve();
            coarse_warm_start = coarse_solver.get_mesh();
            double coarse_min_T=std::numeric_limits<double>::infinity();
            double coarse_max_T=-std::numeric_limits<double>::infinity();
            double coarse_total_watts=0.0;
            std::size_t coarse_heated_cells=0;
            for(const Cell& cell : coarse_warm_start->get_cells()) {
                coarse_min_T=std::min(coarse_min_T,cell.get_T());
                coarse_max_T=std::max(coarse_max_T,cell.get_T());
                coarse_total_watts += cell.get_qdot()*cell.volume();
                if(cell.get_qdot()>0.0) ++coarse_heated_cells;
            }
            std::cout << "Coarse thermal completion: Tmin = "
                      << coarse_min_T << " C, Tmax = "
                      << coarse_max_T << " C, heat-source cells = "
                      << coarse_heated_cells << ", integrated power = "
                      << coarse_total_watts << " W\n";
            std::cout << "Coarse CSV: coarse_simulation.csv\n";
            std::cout << "----- Fine production stage -----\n";
        }
        Mesh mesh;
        double graph_dx = model.mesh.dx;
        double graph_dy = model.mesh.dy;
        double graph_dz = model.mesh.dz;
        if (model.mesh.adaptive) {
            const MeshRefinementPlan plan = MeshRefinementPlanner::plan(
                rack, components, fans, vents,
                model.mesh.fine_dx, model.mesh.coarse_dx,
                model.mesh.refinement_margin);
            mesh = Mesh().build_adaptive_mesh(
                rack, plan.dxs, plan.dys, plan.dzs, env, load);
            // Grapher remains a lightweight uniform ASCII geometry preview.
            // Use the fine spacing so it does not hide resolved features.
            graph_dx = graph_dy = graph_dz = model.mesh.fine_dx;
        } else {
            mesh = Mesh().build_mesh(
                rack, model.mesh.dx, model.mesh.dy, model.mesh.dz, env, load);
        }
        Grapher grapher(rack, graph_dx, graph_dy, graph_dz);

        for(const Component& component : components) {
            if(model.openfoam_solver.enabled) {
                mesh.stamp_component_for_openfoam(component);
            } else if(model.mesh.adaptive) {
                mesh.stamp_component_adaptive(component);
            } else {
                mesh.stamp_component(component);
            }
            grapher.add_component(component);
        }
        for(const Fan& fan : fans) {
            if(model.openfoam_solver.enabled) {
                mesh.stamp_fan_for_openfoam(fan);
            } else if(model.mesh.adaptive) {
                mesh.stamp_fan_adaptive(fan);
            } else {
                mesh.stamp_fan(fan);
            }
            grapher.add_fan(fan);
        }
        for(const Vent& vent : vents) {
            if(model.openfoam_solver.enabled) {
                mesh.stamp_vent_for_openfoam(vent);
            } else if(model.mesh.adaptive) {
                mesh.stamp_vent_adaptive(vent);
            } else {
                mesh.stamp_vent(vent);
            }
            grapher.add_vent(vent);
        }

        if(model.openfoam_solver.enabled) {
            const auto& cfg=model.openfoam_solver;
            OpenFoamExportOptions options{
                .case_directory=cfg.case_directory,
                .overwrite=cfg.overwrite,
                .parallel_processes=cfg.parallel_processes,
                .end_time=model.simulation.duration,
                .initial_time_step=model.simulation.dt,
                .maximum_time_step=cfg.maximum_time_step,
                .maximum_courant_number=cfg.maximum_courant_number,
                .field_write_interval=cfg.field_write_interval,
                .saved_time_directories=cfg.saved_time_directories,
                .report_interval=cfg.report_interval,
                .use_k_omega_sst=cfg.use_k_omega_sst,
                .inlet_turbulence_intensity=
                    cfg.inlet_turbulence_intensity,
                .turbulence_length_scale=cfg.turbulence_length_scale,
                .turbulent_prandtl_number=cfg.turbulent_prandtl_number,
                .temperature_dependent_air=cfg.temperature_dependent_air,
                .gravity={cfg.gravity.x,cfg.gravity.y,cfg.gravity.z},
                .sutherland_temperature=cfg.sutherland_temperature,
                .use_vent_pressure_loss=cfg.use_vent_pressure_loss,
                .use_fan_curves=cfg.use_fan_curves,
                .fan_curve_extension_multiplier=
                    cfg.fan_curve_extension_multiplier,
                .use_multirate_thermal=cfg.use_multirate_thermal,
                .airflow_warmup_time=cfg.airflow_warmup_time,
                .use_fan_startup_ramp=cfg.use_fan_startup_ramp,
                .fan_startup_ramp_time=cfg.fan_startup_ramp_time,
                .fan_startup_ramp_steps=cfg.fan_startup_ramp_steps,
                .initial_airflow_check_interval=
                    cfg.initial_airflow_check_interval,
                .minimum_initial_airflow_duration=
                    cfg.minimum_initial_airflow_duration,
                .frozen_flow_maximum_time_step=
                    cfg.thermal_only_maximum_time_step,
                .frozen_flow_maximum_courant_number=
                    cfg.thermal_only_maximum_courant_number,
                .airflow_refresh_interval=cfg.airflow_refresh_interval,
                .airflow_refresh_duration=cfg.airflow_refresh_duration,
                .use_adaptive_airflow_refresh=
                    cfg.use_adaptive_airflow_refresh,
                .airflow_refresh_maximum_courant_number=
                    cfg.airflow_refresh_maximum_courant_number,
                .airflow_refresh_check_interval=
                    cfg.airflow_refresh_check_interval,
                .maximum_airflow_refresh_duration=
                    cfg.maximum_airflow_refresh_duration,
                .maximum_mass_imbalance_fraction=
                    cfg.maximum_mass_imbalance_fraction,
                .maximum_device_flow_change_fraction=
                    cfg.maximum_device_flow_change_fraction,
                .stop_when_thermally_converged=
                    cfg.stop_when_thermally_converged,
                .minimum_thermal_convergence_time=
                    cfg.minimum_thermal_convergence_time,
                .thermal_convergence_reference_interval=
                    cfg.thermal_convergence_reference_interval,
                .maximum_temperature_change=
                    cfg.maximum_temperature_change,
                .maximum_component_average_temperature_change=
                    cfg.maximum_component_average_temperature_change,
                .thermal_convergence_required_checkpoints=
                    cfg.thermal_convergence_required_checkpoints
            };
            const std::filesystem::path absolute_case_directory=
                std::filesystem::absolute(options.case_directory);
            const std::string native_case_directory=
                absolute_case_directory.string();
            if(std::any_of(
                   native_case_directory.begin(),native_case_directory.end(),
                   [](const unsigned char character) {
                       return std::isspace(character)!=0;
                   }))
                throw std::runtime_error(
                    "OpenFOAM case_directory resolves to a path containing "
                    "whitespace, which OpenFOAM MPI does not support: '" +
                    native_case_directory +
                    "'. Set openfoam_solver.case_directory to an absolute "
                    "path without spaces (for example "
                    "\"C:/OpenFOAM/my_model\").");
            OpenFoamExporter::export_mesh(mesh,options);
            // Keep the source geometry beside the exact OpenFOAM mesh. The
            // Python visualizer uses this for component names, internal-region
            // boxes, and fan/vent arrows without relying on a stale output.txt
            // from another model run.
            grapher.export_to_file(
                (absolute_case_directory/"geometry.txt").string());
            std::string launch_directory=absolute_case_directory.string();
#ifdef _WIN32
            if(launch_directory.size()>=3 &&
               std::isalpha(
                   static_cast<unsigned char>(launch_directory[0])) &&
               launch_directory[1]==':') {
                const char drive=static_cast<char>(std::tolower(
                    static_cast<unsigned char>(launch_directory[0])));
                launch_directory="/mnt/" + std::string(1,drive) +
                    launch_directory.substr(2);
            }
            std::replace(
                launch_directory.begin(),launch_directory.end(),'\\','/');
#endif
            std::cout
                << "OpenFOAM backend selected; case exported to "
                << absolute_case_directory
                << "\nNative transient solver was not run.\n"
                << "Run from a WSL terminal with:\n  cd '"
                << launch_directory << "' && ./run_parallel.sh "
                << cfg.parallel_processes
                << (cfg.use_multirate_thermal ? " --multirate " : " ")
                << model.simulation.duration << '\n';
            return;
        }

        if (coarse_warm_start.has_value()) {
            const Mesh& coarse = *coarse_warm_start;
            std::size_t direct_transfers = 0;
            std::size_t neighbor_transfers = 0;
            std::size_t wall_face_transfers = 0;
            std::size_t ambient_fallbacks = 0;
            std::vector<std::vector<const Mesh::WallFace*>>
                component_wall_faces(components.size());
            for(const Mesh::WallFace& wall : coarse.get_wall_faces()) {
                if(wall.active && wall.component_group >= 0 &&
                   static_cast<size_t>(wall.component_group) <
                       component_wall_faces.size()) {
                    component_wall_faces[wall.component_group].push_back(&wall);
                }
            }

            for (int i = 0; i < mesh.get_nx(); ++i) {
                for (int j = 0; j < mesh.get_ny(); ++j) {
                    for (int k = 0; k < mesh.get_nz(); ++k) {
                        Cell& fine_cell = mesh.at(i, j, k);
                        const double x = mesh.cell_center_x(i);
                        const double y = mesh.cell_center_y(j);
                        const double z = mesh.cell_center_z(k);
                        const int ci = coarse.index_x(x);
                        const int cj = coarse.index_y(y);
                        const int ck = coarse.index_z(z);

                        // Resolved fine enclosure walls have no solid-cell
                        // counterpart in a face-wall coarse mesh. Transfer
                        // from the nearest wall face belonging to the same
                        // component instead of borrowing an electronics
                        // heat-source temperature.
                        const bool resolved_enclosure_wall =
                            fine_cell.get_state() == Cell::State::Component &&
                            std::abs(fine_cell.get_qdot()) <= 1e-15;
                        if(resolved_enclosure_wall) {
                            int owner = -1;
                            constexpr double eps = 1e-9;
                            for(size_t component_index=0;
                                component_index<components.size();
                                ++component_index) {
                                const Component& component =
                                    components[component_index];
                                const auto corner=component.get_coords();
                                if(x >= corner[0]-eps &&
                                   x <= corner[0]+component.get_width_m()+eps &&
                                   y >= corner[1]-eps &&
                                   y <= corner[1]+component.get_depth_m()+eps &&
                                   z >= corner[2]-eps &&
                                   z <= corner[2]+component.get_height_m()+eps) {
                                    owner=static_cast<int>(component_index);
                                    break;
                                }
                            }
                            if(owner >= 0 &&
                               !component_wall_faces[owner].empty()) {
                                const Mesh::WallFace* nearest=nullptr;
                                double nearest_distance2=
                                    std::numeric_limits<double>::infinity();
                                for(const Mesh::WallFace* wall :
                                    component_wall_faces[owner]) {
                                    const auto center=
                                        coarse.wall_face_center(*wall);
                                    const double dx=center[0]-x;
                                    const double dy=center[1]-y;
                                    const double dz=center[2]-z;
                                    const double distance2=
                                        dx*dx+dy*dy+dz*dz;
                                    if(distance2 < nearest_distance2) {
                                        nearest_distance2=distance2;
                                        nearest=wall;
                                    }
                                }
                                fine_cell.set_T(nearest->temperature);
                                ++wall_face_transfers;
                                continue;
                            }
                        }

                        const Cell* source = nullptr;
                        if (coarse.in_bounds(ci, cj, ck)) {
                            const Cell& candidate = coarse.at(ci, cj, ck);
                            if (candidate.is_solid() == fine_cell.is_solid()) {
                                source = &candidate;
                                ++direct_transfers;
                            }
                        }

                        if (source == nullptr) {
                            for (int radius = 1; radius <= 2 && source == nullptr; ++radius) {
                                for (int di = -radius; di <= radius && source == nullptr; ++di) {
                                    for (int dj = -radius; dj <= radius && source == nullptr; ++dj) {
                                        for (int dk = -radius; dk <= radius; ++dk) {
                                            const int ni = ci + di;
                                            const int nj = cj + dj;
                                            const int nk = ck + dk;
                                            if (!coarse.in_bounds(ni, nj, nk)) continue;
                                            const Cell& candidate = coarse.at(ni, nj, nk);
                                            if (candidate.is_solid() == fine_cell.is_solid()) {
                                                source = &candidate;
                                                ++neighbor_transfers;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (source != nullptr) {
                            fine_cell.set_T(source->get_T());
                        } else {
                            fine_cell.set_T(env.get_T_ambient());
                            ++ambient_fallbacks;
                        }
                    }
                }
            }

            std::cout << "Warm-start temperature transfer: "
                      << direct_transfers << " direct, "
                      << neighbor_transfers << " nearby-phase, "
                      << wall_face_transfers << " wall-face, "
                      << ambient_fallbacks << " ambient fallbacks.\n";
        }
        grapher.stamp_components();
        grapher.stamp_fans();
        grapher.stamp_vents();
        grapher.export_to_file("output.txt");
        if(model.flow_solver.enable_flow_solver) {
            FlowSolver flow_solver(mesh, *model.flow_solver.resistivity, *model.flow_solver.tolerance, *model.flow_solver.max_iterations, 
                                *model.flow_solver.sor_omega, *model.flow_solver.max_outer_iters, *model.flow_solver.flow_tolerance,
                                model.flow_solver.pressure_method);
            flow_solver.solve(); // pre populate all velocity cells
        }
        int update_flow_interval = model.flow_solver.enable_flow_solver
            ? model.simulation.update_flow_interval.value_or(1)
            : -1;

        // Estimate from the fully stamped, geometry-aligned mesh. Run this
        // after the initial flow solve so the advection limit sees populated
        // velocities as well as the exact wall/region cell dimensions.
        std::cout << "\n===== Fine-stage thermal estimate =====\n";
        const ThermalTimeEstimate thermal_estimate =
            ThermalTimeEstimator::estimate(mesh);
        thermal_estimate.print();
        const std::size_t fine_timestep_count =
            static_cast<std::size_t>(std::ceil(
                model.simulation.duration / model.simulation.dt));
        std::cout << "Configured fine dt:         "
                  << model.simulation.dt << " s\n";
        std::cout << "Configured fine duration:   "
                  << model.simulation.duration << " s\n";
        std::cout << "Planned fine timesteps:     "
                  << fine_timestep_count << "\n";
        const double fine_dt_limit =
            model.simulation.advection_subcycling
                ? 0.8*thermal_estimate.max_stable_dt_conduction_s
                : thermal_estimate.recommended_dt_s;
        if(model.simulation.advection_subcycling &&
           std::isfinite(thermal_estimate.max_stable_dt_advection_s)) {
            const int estimated_substeps=std::max(1,
                static_cast<int>(std::ceil(
                    model.simulation.dt /
                    (model.simulation.advection_cfl_target *
                     thermal_estimate.max_stable_dt_advection_s))));
            std::cout << "Estimated fine advection substeps: "
                      << estimated_substeps << "\n";
        }
        if(std::isfinite(fine_dt_limit) &&
           model.simulation.dt > fine_dt_limit) {
            std::cerr
                << "WARNING: configured fine dt = "
                << model.simulation.dt
                << " s exceeds the non-subcycled stability limit of "
                << fine_dt_limit << " s.\n";
        }

        if(coarse_thermal_estimate.has_value()) {
            const std::size_t coarse_updates =
                coarse_thermal_estimate->mesh_cell_count *
                coarse_timestep_count;
            const std::size_t fine_updates =
                thermal_estimate.mesh_cell_count *
                fine_timestep_count;
            const std::size_t peak_mesh_bytes = 2u * std::max(
                coarse_thermal_estimate->mesh_memory_bytes,
                thermal_estimate.mesh_memory_bytes);
            std::cout << "\n===== Combined multistage estimate =====\n";
            std::cout << "Coarse cell updates:        "
                      << coarse_updates << "\n";
            std::cout << "Fine cell updates:          "
                      << fine_updates << "\n";
            std::cout << "Total cell updates:         "
                      << coarse_updates + fine_updates << "\n";
            std::cout << "Approx. peak two-mesh memory: "
                      << peak_mesh_bytes << " bytes ("
                      << peak_mesh_bytes/(1024.0*1024.0)
                      << " MiB)\n";
            std::cout << "========================================\n";
        }

        Solver solver(mesh, model.simulation.dt, model.simulation.duration, false,
                    model.simulation.output_interval,
                    update_flow_interval,
                    *model.flow_solver.resistivity, *model.flow_solver.tolerance,
                    *model.flow_solver.max_iterations, *model.flow_solver.sor_omega,
                    *model.flow_solver.max_outer_iters, *model.flow_solver.flow_tolerance,
                    model.simulation.advection_subcycling,
                    model.simulation.advection_cfl_target,
                    model.simulation.max_advection_substeps,
                    "simulation.csv",
                    model.flow_solver.pressure_method);


        SimulationLogger logger(config);
        logger.initialize(mesh);
        solver.set_logger(logger);
        solver.solve();

        mesh.check_stamps();
    }
};



struct ComponentLoader {
    ComponentInput model;
    std::unordered_map<std::string, FanCurveInput> fan_curve_library;
    std::unordered_map<std::string, ComponentInput> component_template_cache; 

    ComponentLoader() = default;

    void load_fan_curves(const std::filesystem::path& library_path) {
        fan_curve_library = load_fan_curve_library(library_path);
    }


    void load_component(const std::filesystem::path& component_path) 
    {
        try {
            const toml::table root = toml::parse_file(component_path.string());

            model.name = root["name"].value<std::string>().value_or(component_path.stem().string());
            PositionInput pos;
            model = parse_component(root, "component laoder", true, pos);
    
        } catch(const toml::parse_error& error) {
            throw std::runtime_error("Failed to parse model file '" + component_path.string() + "': " + std::string(error.description()));
        }
    }

    const ComponentInput& get_component_template(const std::string& path, PositionInput pos) {
        auto it = component_template_cache.find(path);
        if (it != component_template_cache.end()) return it->second;
        auto [inserted, ok] = component_template_cache.emplace(path, load_component_template(path, pos));
        return inserted->second;
    }

    void run() 
    {
        
        ComponentGrapher grapher = ComponentGrapher();

        Component component;
        if (model.template_path.has_value()) {
            const ComponentInput& tmpl = get_component_template(*model.template_path, model.position);
            model.name = tmpl.name;
            model.size = tmpl.size;
            model.material = tmpl.material;
            model.watts = tmpl.watts;
            model.internal_regions = tmpl.internal_regions;
        }
        const std::string comp_units = model.size.units.value_or("u");
        if(comp_units == "u") {
            component = Component::from_rack_units(model.size.width, model.size.depth, model.size.height, model.name);
        } else if(comp_units == "m") {
            component = Component::from_meters(model.size.width, model.size.depth, model.size.height, model.name);
        } else if(comp_units == "in") {
            component = Component::from_inches(model.size.width, model.size.depth, model.size.height, model.name);
        } else if(comp_units == "mm") {
            component = Component::from_mm(model.size.width, model.size.depth, model.size.height, model.name);
        } else {
            throw std::runtime_error("Invalid component.size units: '" + comp_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
        }
        const std::string comp_pos_units = model.position.units.value_or("u");
        if(comp_pos_units == "u") {
            component.set_coords_rack_units(model.position.x, model.position.y, model.position.z);
        } else if(comp_pos_units == "m") {
            component.set_coords_m(model.position.x, model.position.y, model.position.z);
        } else if(comp_pos_units == "in") {
            component.set_coords_in(model.position.x, model.position.y, model.position.z);
        } else if(comp_pos_units == "mm") {
            component.set_coords_mm(model.position.x, model.position.y, model.position.z);
        } else {
            throw std::runtime_error("Invalid component.position units: '" + comp_pos_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
        }
        component.set_name(model.name);
        component.set_cp(model.material.cp);
        component.set_rho_solid(model.material.density);
        component.set_k_solid(model.material.k);
        component.set_watts(model.watts);
        component.set_coords_m(0.0, 0.0, 0.0);
        for(const InternalRegionInput& i : model.internal_regions) {
            component.add_region(build_internal_region(i, &fan_curve_library));
        }
        component.order_internal_regions();
        grapher.add_component(component);
        grapher.export_to_file("output.txt");
    }
};

#endif
